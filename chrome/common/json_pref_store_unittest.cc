// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/path_service.h"
#include "base/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/thread.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/json_pref_store.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "testing/gtest/include/gtest/gtest.h"

class JsonPrefStoreTest : public testing::Test {
 protected:
  virtual void SetUp() {
    message_loop_proxy_ = base::MessageLoopProxy::CreateForCurrentThread();
    // Name a subdirectory of the temp directory.
    ASSERT_TRUE(PathService::Get(base::DIR_TEMP, &test_dir_));
    test_dir_ = test_dir_.AppendASCII("JsonPrefStoreTest");

    // Create a fresh, empty copy of this directory.
    file_util::Delete(test_dir_, true);
    file_util::CreateDirectory(test_dir_);

    ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &data_dir_));
    data_dir_ = data_dir_.AppendASCII("pref_service");
    ASSERT_TRUE(file_util::PathExists(data_dir_));
  }

  virtual void TearDown() {
    // Clean up test directory
    ASSERT_TRUE(file_util::Delete(test_dir_, true));
    ASSERT_FALSE(file_util::PathExists(test_dir_));
  }

  // the path to temporary directory used to contain the test operations
  FilePath test_dir_;
  // the path to the directory where the test data is stored
  FilePath data_dir_;
  // A message loop that we can use as the file thread message loop.
  MessageLoop message_loop_;
  scoped_refptr<base::MessageLoopProxy> message_loop_proxy_;
};

// Test fallback behavior for a nonexistent file.
TEST_F(JsonPrefStoreTest, NonExistentFile) {
  FilePath bogus_input_file = data_dir_.AppendASCII("read.txt");
  ASSERT_FALSE(file_util::PathExists(bogus_input_file));
  JsonPrefStore pref_store(bogus_input_file, message_loop_proxy_.get());
  EXPECT_EQ(PrefStore::PREF_READ_ERROR_NO_FILE, pref_store.ReadPrefs());
  EXPECT_FALSE(pref_store.ReadOnly());
  EXPECT_TRUE(pref_store.prefs()->empty());
}

// Test fallback behavior for an invalid file.
TEST_F(JsonPrefStoreTest, InvalidFile) {
  FilePath invalid_file_original = data_dir_.AppendASCII("invalid.json");
  FilePath invalid_file = test_dir_.AppendASCII("invalid.json");
  ASSERT_TRUE(file_util::CopyFile(invalid_file_original, invalid_file));
  JsonPrefStore pref_store(invalid_file, message_loop_proxy_.get());
  EXPECT_EQ(PrefStore::PREF_READ_ERROR_JSON_PARSE, pref_store.ReadPrefs());
  EXPECT_FALSE(pref_store.ReadOnly());
  EXPECT_TRUE(pref_store.prefs()->empty());

  // The file should have been moved aside.
  EXPECT_FALSE(file_util::PathExists(invalid_file));
  FilePath moved_aside = test_dir_.AppendASCII("invalid.bad");
  EXPECT_TRUE(file_util::PathExists(moved_aside));
  EXPECT_TRUE(file_util::TextContentsEqual(invalid_file_original,
                                           moved_aside));
}

TEST_F(JsonPrefStoreTest, Basic) {
  ASSERT_TRUE(file_util::CopyFile(data_dir_.AppendASCII("read.json"),
                                  test_dir_.AppendASCII("write.json")));

  // Test that the persistent value can be loaded.
  FilePath input_file = test_dir_.AppendASCII("write.json");
  ASSERT_TRUE(file_util::PathExists(input_file));
  JsonPrefStore pref_store(input_file, message_loop_proxy_.get());
  ASSERT_EQ(PrefStore::PREF_READ_ERROR_NONE, pref_store.ReadPrefs());
  ASSERT_FALSE(pref_store.ReadOnly());
  DictionaryValue* prefs = pref_store.prefs();

  // The JSON file looks like this:
  // {
  //   "homepage": "http://www.cnn.com",
  //   "some_directory": "/usr/local/",
  //   "tabs": {
  //     "new_windows_in_tabs": true,
  //     "max_tabs": 20
  //   }
  // }

  const char kNewWindowsInTabs[] = "tabs.new_windows_in_tabs";
  const char kMaxTabs[] = "tabs.max_tabs";
  const char kLongIntPref[] = "long_int.pref";

  std::string cnn("http://www.cnn.com");

  std::string string_value;
  EXPECT_TRUE(prefs->GetString(prefs::kHomePage, &string_value));
  EXPECT_EQ(cnn, string_value);

  const char kSomeDirectory[] = "some_directory";

  FilePath::StringType path;
  EXPECT_TRUE(prefs->GetString(kSomeDirectory, &path));
  EXPECT_EQ(FilePath::StringType(FILE_PATH_LITERAL("/usr/local/")), path);
  FilePath some_path(FILE_PATH_LITERAL("/usr/sbin/"));
  prefs->SetString(kSomeDirectory, some_path.value());
  EXPECT_TRUE(prefs->GetString(kSomeDirectory, &path));
  EXPECT_EQ(some_path.value(), path);

  // Test reading some other data types from sub-dictionaries.
  bool boolean;
  EXPECT_TRUE(prefs->GetBoolean(kNewWindowsInTabs, &boolean));
  EXPECT_TRUE(boolean);

  prefs->SetBoolean(kNewWindowsInTabs, false);
  EXPECT_TRUE(prefs->GetBoolean(kNewWindowsInTabs, &boolean));
  EXPECT_FALSE(boolean);

  int integer;
  EXPECT_TRUE(prefs->GetInteger(kMaxTabs, &integer));
  EXPECT_EQ(20, integer);
  prefs->SetInteger(kMaxTabs, 10);
  EXPECT_TRUE(prefs->GetInteger(kMaxTabs, &integer));
  EXPECT_EQ(10, integer);

  prefs->SetString(kLongIntPref, base::Int64ToString(214748364842LL));
  EXPECT_TRUE(prefs->GetString(kLongIntPref, &string_value));
  int64 value;
  base::StringToInt64(string_value, &value);
  EXPECT_EQ(214748364842LL, value);

  // Serialize and compare to expected output.
  FilePath output_file = input_file;
  FilePath golden_output_file = data_dir_.AppendASCII("write.golden.json");
  ASSERT_TRUE(file_util::PathExists(golden_output_file));
  ASSERT_TRUE(pref_store.WritePrefs());
  MessageLoop::current()->RunAllPending();
  EXPECT_TRUE(file_util::TextContentsEqual(golden_output_file, output_file));
  ASSERT_TRUE(file_util::Delete(output_file, false));
}
