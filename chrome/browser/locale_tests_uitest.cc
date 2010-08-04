// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/ui_test.h"

#include "build/build_config.h"

class LocaleTestsBase : public UITest {
 public:
  LocaleTestsBase() : UITest(), old_lc_all_(NULL) {
  }

 protected:
  void RestoreLcAllEnvironment() {
#if defined(OS_LINUX)
    if (old_lc_all_) {
      setenv("LC_ALL", old_lc_all_, 1);
    } else {
      unsetenv("LC_ALL");
    }
#endif
  };

  const char* old_lc_all_;
};


class LocaleTestsDa : public LocaleTestsBase {
 public:
  LocaleTestsDa() : LocaleTestsBase() {
    launch_arguments_.AppendSwitchWithValue("lang", "da");

    // Linux doesn't use --lang, it only uses environment variables to set the
    // language.
#if defined(OS_LINUX)
    old_lc_all_ = getenv("LC_ALL");
    setenv("LC_ALL", "da_DK.UTF-8", 1);
#endif
  }
};

class LocaleTestsHe : public LocaleTestsBase {
 public:
  LocaleTestsHe() : LocaleTestsBase() {
    launch_arguments_.AppendSwitchWithValue("lang", "he");
#if defined(OS_LINUX)
    old_lc_all_ = getenv("LC_ALL");
    setenv("LC_ALL", "he_IL.UTF-8", 1);
#endif
  }
};

class LocaleTestsZhTw : public LocaleTestsBase {
 public:
  LocaleTestsZhTw() : LocaleTestsBase() {
    launch_arguments_.AppendSwitchWithValue("lang", "zh-TW");
#if defined(OS_LINUX)
    old_lc_all_ = getenv("LC_ALL");
    setenv("LC_ALL", "zh_TW.UTF-8", 1);
#endif
  }
};

TEST_F(LocaleTestsDa, TestStart) {
  // Just making sure we can start/shutdown cleanly.
  RestoreLcAllEnvironment();
}

TEST_F(LocaleTestsHe, TestStart) {
  // Just making sure we can start/shutdown cleanly.
  RestoreLcAllEnvironment();
}

TEST_F(LocaleTestsZhTw, TestStart) {
  // Just making sure we can start/shutdown cleanly.
  RestoreLcAllEnvironment();
}
