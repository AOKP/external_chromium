// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for the SyncApi. Note that a lot of the underlying
// functionality is provided by the Syncable layer, which has its own
// unit tests. We'll test SyncApi specific things in this harness.

#include "base/basictypes.h"
#include "base/message_loop.h"
#include "base/scoped_ptr.h"
#include "base/scoped_temp_dir.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/sync/engine/syncapi.h"
#include "chrome/browser/sync/js_arg_list.h"
#include "chrome/browser/sync/js_backend.h"
#include "chrome/browser/sync/js_event_handler.h"
#include "chrome/browser/sync/js_event_router.h"
#include "chrome/browser/sync/js_test_util.h"
#include "chrome/browser/sync/protocol/password_specifics.pb.h"
#include "chrome/browser/sync/protocol/proto_value_conversions.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/browser/sync/syncable/syncable.h"
#include "chrome/test/sync/engine/test_directory_setter_upper.h"
#include "chrome/test/values_test_util.h"
#include "jingle/notifier/base/notifier_options.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using browser_sync::HasArgsAsList;
using browser_sync::KeyParams;
using browser_sync::JsArgList;
using browser_sync::MockJsEventHandler;
using browser_sync::MockJsEventRouter;
using test::ExpectDictionaryValue;
using test::ExpectStringValue;
using testing::_;
using testing::Invoke;
using testing::SaveArg;
using testing::StrictMock;

namespace sync_api {

namespace {

void ExpectInt64Value(int64 expected_value,
                      const DictionaryValue& value, const std::string& key) {
  std::string int64_str;
  EXPECT_TRUE(value.GetString(key, &int64_str));
  int64 val = 0;
  EXPECT_TRUE(base::StringToInt64(int64_str, &val));
  EXPECT_EQ(expected_value, val);
}

// Makes a non-folder child of the root node.  Returns the id of the
// newly-created node.
int64 MakeNode(UserShare* share,
               syncable::ModelType model_type,
               const std::string& client_tag) {
  WriteTransaction trans(share);
  ReadNode root_node(&trans);
  root_node.InitByRootLookup();
  WriteNode node(&trans);
  EXPECT_TRUE(node.InitUniqueByCreation(model_type, root_node, client_tag));
  node.SetIsFolder(false);
  return node.GetId();
}

}  // namespace

class SyncApiTest : public testing::Test {
 public:
  virtual void SetUp() {
    setter_upper_.SetUp();
    share_.dir_manager.reset(setter_upper_.manager());
    share_.name = setter_upper_.name();
  }

  virtual void TearDown() {
    // |share_.dir_manager| does not actually own its value.
    ignore_result(share_.dir_manager.release());
    setter_upper_.TearDown();
  }

 protected:
  UserShare share_;
  browser_sync::TestDirectorySetterUpper setter_upper_;
};

TEST_F(SyncApiTest, SanityCheckTest) {
  {
    ReadTransaction trans(&share_);
    EXPECT_TRUE(trans.GetWrappedTrans() != NULL);
  }
  {
    WriteTransaction trans(&share_);
    EXPECT_TRUE(trans.GetWrappedTrans() != NULL);
  }
  {
    // No entries but root should exist
    ReadTransaction trans(&share_);
    ReadNode node(&trans);
    // Metahandle 1 can be root, sanity check 2
    EXPECT_FALSE(node.InitByIdLookup(2));
  }
}

TEST_F(SyncApiTest, BasicTagWrite) {
  {
    ReadTransaction trans(&share_);
    ReadNode root_node(&trans);
    root_node.InitByRootLookup();
    EXPECT_EQ(root_node.GetFirstChildId(), 0);
  }

  ignore_result(MakeNode(&share_, syncable::BOOKMARKS, "testtag"));

  {
    ReadTransaction trans(&share_);
    ReadNode node(&trans);
    EXPECT_TRUE(node.InitByClientTagLookup(syncable::BOOKMARKS,
        "testtag"));

    ReadNode root_node(&trans);
    root_node.InitByRootLookup();
    EXPECT_NE(node.GetId(), 0);
    EXPECT_EQ(node.GetId(), root_node.GetFirstChildId());
  }
}

TEST_F(SyncApiTest, GenerateSyncableHash) {
  EXPECT_EQ("OyaXV5mEzrPS4wbogmtKvRfekAI=",
      BaseNode::GenerateSyncableHash(syncable::BOOKMARKS, "tag1"));
  EXPECT_EQ("iNFQtRFQb+IZcn1kKUJEZDDkLs4=",
      BaseNode::GenerateSyncableHash(syncable::PREFERENCES, "tag1"));
  EXPECT_EQ("gO1cPZQXaM73sHOvSA+tKCKFs58=",
      BaseNode::GenerateSyncableHash(syncable::AUTOFILL, "tag1"));

  EXPECT_EQ("A0eYIHXM1/jVwKDDp12Up20IkKY=",
      BaseNode::GenerateSyncableHash(syncable::BOOKMARKS, "tag2"));
  EXPECT_EQ("XYxkF7bhS4eItStFgiOIAU23swI=",
      BaseNode::GenerateSyncableHash(syncable::PREFERENCES, "tag2"));
  EXPECT_EQ("GFiWzo5NGhjLlN+OyCfhy28DJTQ=",
      BaseNode::GenerateSyncableHash(syncable::AUTOFILL, "tag2"));
}

TEST_F(SyncApiTest, ModelTypesSiloed) {
  {
    WriteTransaction trans(&share_);
    ReadNode root_node(&trans);
    root_node.InitByRootLookup();
    EXPECT_EQ(root_node.GetFirstChildId(), 0);
  }

  ignore_result(MakeNode(&share_, syncable::BOOKMARKS, "collideme"));
  ignore_result(MakeNode(&share_, syncable::PREFERENCES, "collideme"));
  ignore_result(MakeNode(&share_, syncable::AUTOFILL, "collideme"));

  {
    ReadTransaction trans(&share_);

    ReadNode bookmarknode(&trans);
    EXPECT_TRUE(bookmarknode.InitByClientTagLookup(syncable::BOOKMARKS,
        "collideme"));

    ReadNode prefnode(&trans);
    EXPECT_TRUE(prefnode.InitByClientTagLookup(syncable::PREFERENCES,
        "collideme"));

    ReadNode autofillnode(&trans);
    EXPECT_TRUE(autofillnode.InitByClientTagLookup(syncable::AUTOFILL,
        "collideme"));

    EXPECT_NE(bookmarknode.GetId(), prefnode.GetId());
    EXPECT_NE(autofillnode.GetId(), prefnode.GetId());
    EXPECT_NE(bookmarknode.GetId(), autofillnode.GetId());
  }
}

TEST_F(SyncApiTest, ReadMissingTagsFails) {
  {
    ReadTransaction trans(&share_);
    ReadNode node(&trans);
    EXPECT_FALSE(node.InitByClientTagLookup(syncable::BOOKMARKS,
        "testtag"));
  }
  {
    WriteTransaction trans(&share_);
    WriteNode node(&trans);
    EXPECT_FALSE(node.InitByClientTagLookup(syncable::BOOKMARKS,
        "testtag"));
  }
}

// TODO(chron): Hook this all up to the server and write full integration tests
//              for update->undelete behavior.
TEST_F(SyncApiTest, TestDeleteBehavior) {

  int64 node_id;
  int64 folder_id;
  std::wstring test_title(L"test1");

  {
    WriteTransaction trans(&share_);
    ReadNode root_node(&trans);
    root_node.InitByRootLookup();

    // we'll use this spare folder later
    WriteNode folder_node(&trans);
    EXPECT_TRUE(folder_node.InitByCreation(syncable::BOOKMARKS,
        root_node, NULL));
    folder_id = folder_node.GetId();

    WriteNode wnode(&trans);
    EXPECT_TRUE(wnode.InitUniqueByCreation(syncable::BOOKMARKS,
        root_node, "testtag"));
    wnode.SetIsFolder(false);
    wnode.SetTitle(test_title);

    node_id = wnode.GetId();
  }

  // Ensure we can delete something with a tag.
  {
    WriteTransaction trans(&share_);
    WriteNode wnode(&trans);
    EXPECT_TRUE(wnode.InitByClientTagLookup(syncable::BOOKMARKS,
        "testtag"));
    EXPECT_FALSE(wnode.GetIsFolder());
    EXPECT_EQ(wnode.GetTitle(), test_title);

    wnode.Remove();
  }

  // Lookup of a node which was deleted should return failure,
  // but have found some data about the node.
  {
    ReadTransaction trans(&share_);
    ReadNode node(&trans);
    EXPECT_FALSE(node.InitByClientTagLookup(syncable::BOOKMARKS,
        "testtag"));
    // Note that for proper function of this API this doesn't need to be
    // filled, we're checking just to make sure the DB worked in this test.
    EXPECT_EQ(node.GetTitle(), test_title);
  }

  {
    WriteTransaction trans(&share_);
    ReadNode folder_node(&trans);
    EXPECT_TRUE(folder_node.InitByIdLookup(folder_id));

    WriteNode wnode(&trans);
    // This will undelete the tag.
    EXPECT_TRUE(wnode.InitUniqueByCreation(syncable::BOOKMARKS,
        folder_node, "testtag"));
    EXPECT_EQ(wnode.GetIsFolder(), false);
    EXPECT_EQ(wnode.GetParentId(), folder_node.GetId());
    EXPECT_EQ(wnode.GetId(), node_id);
    EXPECT_NE(wnode.GetTitle(), test_title);  // Title should be cleared
    wnode.SetTitle(test_title);
  }

  // Now look up should work.
  {
    ReadTransaction trans(&share_);
    ReadNode node(&trans);
    EXPECT_TRUE(node.InitByClientTagLookup(syncable::BOOKMARKS,
        "testtag"));
    EXPECT_EQ(node.GetTitle(), test_title);
    EXPECT_EQ(node.GetModelType(), syncable::BOOKMARKS);
  }
}

TEST_F(SyncApiTest, WriteAndReadPassword) {
  KeyParams params = {"localhost", "username", "passphrase"};
  share_.dir_manager->cryptographer()->AddKey(params);
  {
    WriteTransaction trans(&share_);
    ReadNode root_node(&trans);
    root_node.InitByRootLookup();

    WriteNode password_node(&trans);
    EXPECT_TRUE(password_node.InitUniqueByCreation(syncable::PASSWORDS,
                                                   root_node, "foo"));
    sync_pb::PasswordSpecificsData data;
    data.set_password_value("secret");
    password_node.SetPasswordSpecifics(data);
  }
  {
    ReadTransaction trans(&share_);
    ReadNode root_node(&trans);
    root_node.InitByRootLookup();

    ReadNode password_node(&trans);
    EXPECT_TRUE(password_node.InitByClientTagLookup(syncable::PASSWORDS,
                                                    "foo"));
    const sync_pb::PasswordSpecificsData& data =
        password_node.GetPasswordSpecifics();
    EXPECT_EQ("secret", data.password_value());
  }
}

namespace {

void CheckNodeValue(const BaseNode& node, const DictionaryValue& value) {
  ExpectInt64Value(node.GetId(), value, "id");
  ExpectInt64Value(node.GetModificationTime(), value, "modificationTime");
  ExpectInt64Value(node.GetParentId(), value, "parentId");
  {
    bool is_folder = false;
    EXPECT_TRUE(value.GetBoolean("isFolder", &is_folder));
    EXPECT_EQ(node.GetIsFolder(), is_folder);
  }
  ExpectStringValue(WideToUTF8(node.GetTitle()), value, "title");
  {
    syncable::ModelType expected_model_type = node.GetModelType();
    std::string type_str;
    EXPECT_TRUE(value.GetString("type", &type_str));
    if (expected_model_type >= syncable::FIRST_REAL_MODEL_TYPE) {
      syncable::ModelType model_type =
          syncable::ModelTypeFromString(type_str);
      EXPECT_EQ(expected_model_type, model_type);
    } else if (expected_model_type == syncable::TOP_LEVEL_FOLDER) {
      EXPECT_EQ("Top-level folder", type_str);
    } else if (expected_model_type == syncable::UNSPECIFIED) {
      EXPECT_EQ("Unspecified", type_str);
    } else {
      ADD_FAILURE();
    }
  }
  {
    scoped_ptr<DictionaryValue> expected_specifics(
        browser_sync::EntitySpecificsToValue(
            node.GetEntry()->Get(syncable::SPECIFICS)));
    Value* specifics = NULL;
    EXPECT_TRUE(value.Get("specifics", &specifics));
    EXPECT_TRUE(Value::Equals(specifics, expected_specifics.get()));
  }
  ExpectInt64Value(node.GetExternalId(), value, "externalId");
  ExpectInt64Value(node.GetPredecessorId(), value, "predecessorId");
  ExpectInt64Value(node.GetSuccessorId(), value, "successorId");
  ExpectInt64Value(node.GetFirstChildId(), value, "firstChildId");
  EXPECT_EQ(11u, value.size());
}

}  // namespace

TEST_F(SyncApiTest, BaseNodeToValue) {
  ReadTransaction trans(&share_);
  ReadNode node(&trans);
  node.InitByRootLookup();
  scoped_ptr<DictionaryValue> value(node.ToValue());
  if (value.get()) {
    CheckNodeValue(node, *value);
  } else {
    ADD_FAILURE();
  }
}

namespace {

void ExpectChangeRecordActionValue(SyncManager::ChangeRecord::Action
                                       expected_value,
                                   const DictionaryValue& value,
                                   const std::string& key) {
  std::string str_value;
  EXPECT_TRUE(value.GetString(key, &str_value));
  switch (expected_value) {
    case SyncManager::ChangeRecord::ACTION_ADD:
      EXPECT_EQ("Add", str_value);
      break;
    case SyncManager::ChangeRecord::ACTION_UPDATE:
      EXPECT_EQ("Update", str_value);
      break;
    case SyncManager::ChangeRecord::ACTION_DELETE:
      EXPECT_EQ("Delete", str_value);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void CheckNonDeleteChangeRecordValue(const SyncManager::ChangeRecord& record,
                                     const DictionaryValue& value,
                                     BaseTransaction* trans) {
  EXPECT_NE(SyncManager::ChangeRecord::ACTION_DELETE, record.action);
  ExpectChangeRecordActionValue(record.action, value, "action");
  {
    ReadNode node(trans);
    EXPECT_TRUE(node.InitByIdLookup(record.id));
    scoped_ptr<DictionaryValue> expected_node_value(node.ToValue());
    ExpectDictionaryValue(*expected_node_value, value, "node");
  }
}

void CheckDeleteChangeRecordValue(const SyncManager::ChangeRecord& record,
                                  const DictionaryValue& value) {
  EXPECT_EQ(SyncManager::ChangeRecord::ACTION_DELETE, record.action);
  ExpectChangeRecordActionValue(record.action, value, "action");
  DictionaryValue* node_value = NULL;
  EXPECT_TRUE(value.GetDictionary("node", &node_value));
  if (node_value) {
    ExpectInt64Value(record.id, *node_value, "id");
    scoped_ptr<DictionaryValue> expected_specifics_value(
        browser_sync::EntitySpecificsToValue(record.specifics));
    ExpectDictionaryValue(*expected_specifics_value,
                          *node_value, "specifics");
    scoped_ptr<DictionaryValue> expected_extra_value;
    if (record.extra.get()) {
      expected_extra_value.reset(record.extra->ToValue());
    }
    Value* extra_value = NULL;
    EXPECT_EQ(record.extra.get() != NULL,
              node_value->Get("extra", &extra_value));
    EXPECT_TRUE(Value::Equals(extra_value, expected_extra_value.get()));
  }
}

class MockExtraChangeRecordData : public SyncManager::ExtraChangeRecordData {
 public:
  MOCK_CONST_METHOD0(ToValue, DictionaryValue*());
};

}  // namespace

TEST_F(SyncApiTest, ChangeRecordToValue) {
  int64 child_id = MakeNode(&share_, syncable::BOOKMARKS, "testtag");
  sync_pb::EntitySpecifics child_specifics;
  {
    ReadTransaction trans(&share_);
    ReadNode node(&trans);
    EXPECT_TRUE(node.InitByIdLookup(child_id));
    child_specifics = node.GetEntry()->Get(syncable::SPECIFICS);
  }

  // Add
  {
    ReadTransaction trans(&share_);
    SyncManager::ChangeRecord record;
    record.action = SyncManager::ChangeRecord::ACTION_ADD;
    record.id = 1;
    record.specifics = child_specifics;
    record.extra.reset(new StrictMock<MockExtraChangeRecordData>());
    scoped_ptr<DictionaryValue> value(record.ToValue(&trans));
    CheckNonDeleteChangeRecordValue(record, *value, &trans);
  }

  // Update
  {
    ReadTransaction trans(&share_);
    SyncManager::ChangeRecord record;
    record.action = SyncManager::ChangeRecord::ACTION_UPDATE;
    record.id = child_id;
    record.specifics = child_specifics;
    record.extra.reset(new StrictMock<MockExtraChangeRecordData>());
    scoped_ptr<DictionaryValue> value(record.ToValue(&trans));
    CheckNonDeleteChangeRecordValue(record, *value, &trans);
  }

  // Delete (no extra)
  {
    ReadTransaction trans(&share_);
    SyncManager::ChangeRecord record;
    record.action = SyncManager::ChangeRecord::ACTION_DELETE;
    record.id = child_id + 1;
    record.specifics = child_specifics;
    scoped_ptr<DictionaryValue> value(record.ToValue(&trans));
    CheckDeleteChangeRecordValue(record, *value);
  }

  // Delete (with extra)
  {
    ReadTransaction trans(&share_);
    SyncManager::ChangeRecord record;
    record.action = SyncManager::ChangeRecord::ACTION_DELETE;
    record.id = child_id + 1;
    record.specifics = child_specifics;

    DictionaryValue extra_value;
    extra_value.SetString("foo", "bar");
    scoped_ptr<StrictMock<MockExtraChangeRecordData> > extra(
        new StrictMock<MockExtraChangeRecordData>());
    EXPECT_CALL(*extra, ToValue()).Times(2).WillRepeatedly(
        Invoke(&extra_value, &DictionaryValue::DeepCopy));

    record.extra.reset(extra.release());
    scoped_ptr<DictionaryValue> value(record.ToValue(&trans));
    CheckDeleteChangeRecordValue(record, *value);
  }
}

namespace {

class TestHttpPostProviderFactory : public HttpPostProviderFactory {
 public:
  virtual ~TestHttpPostProviderFactory() {}
  virtual HttpPostProviderInterface* Create() {
    NOTREACHED();
    return NULL;
  }
  virtual void Destroy(HttpPostProviderInterface* http) {
    NOTREACHED();
  }
};

class SyncManagerTest : public testing::Test {
 protected:
  SyncManagerTest() : ui_thread_(BrowserThread::UI, &ui_loop_) {}

  void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    sync_manager_.Init(temp_dir_.path(), "bogus", 0, false,
                       new TestHttpPostProviderFactory(), NULL, "bogus",
                       SyncCredentials(), notifier::NotifierOptions(),
                       "", true /* setup_for_test_mode */);
  }

  void TearDown() {
    sync_manager_.Shutdown();
  }

 private:
  // Needed by |ui_thread_|.
  MessageLoopForUI ui_loop_;
  // Needed by |sync_manager_|.
  BrowserThread ui_thread_;
  // Needed by |sync_manager_|.
  ScopedTempDir temp_dir_;

 protected:
  SyncManager sync_manager_;
};

TEST_F(SyncManagerTest, ParentJsEventRouter) {
  StrictMock<MockJsEventRouter> event_router;
  browser_sync::JsBackend* js_backend = sync_manager_.GetJsBackend();
  EXPECT_EQ(NULL, js_backend->GetParentJsEventRouter());
  js_backend->SetParentJsEventRouter(&event_router);
  EXPECT_EQ(&event_router, js_backend->GetParentJsEventRouter());
  js_backend->RemoveParentJsEventRouter();
  EXPECT_EQ(NULL, js_backend->GetParentJsEventRouter());
}

TEST_F(SyncManagerTest, ProcessMessage) {
  const JsArgList kNoArgs;

  browser_sync::JsBackend* js_backend = sync_manager_.GetJsBackend();

  // Messages sent without any parent router should be dropped.
  {
    StrictMock<MockJsEventHandler> event_handler;
    js_backend->ProcessMessage("unknownMessage",
                               kNoArgs, &event_handler);
    js_backend->ProcessMessage("getNotificationState",
                               kNoArgs, &event_handler);
  }

  {
    StrictMock<MockJsEventHandler> event_handler;
    StrictMock<MockJsEventRouter> event_router;

    ListValue false_args;
    false_args.Append(Value::CreateBooleanValue(false));

    EXPECT_CALL(event_router,
                RouteJsEvent("onGetNotificationStateFinished",
                             HasArgsAsList(false_args), &event_handler));

    js_backend->SetParentJsEventRouter(&event_router);

    // This message should be dropped.
    js_backend->ProcessMessage("unknownMessage",
                                 kNoArgs, &event_handler);

    // This should trigger the reply.
    js_backend->ProcessMessage("getNotificationState",
                                 kNoArgs, &event_handler);

    js_backend->RemoveParentJsEventRouter();
  }

  // Messages sent after a parent router has been removed should be
  // dropped.
  {
    StrictMock<MockJsEventHandler> event_handler;
    js_backend->ProcessMessage("unknownMessage",
                                 kNoArgs, &event_handler);
    js_backend->ProcessMessage("getNotificationState",
                                 kNoArgs, &event_handler);
  }
}

TEST_F(SyncManagerTest, ProcessMessageGetRootNode) {
  const JsArgList kNoArgs;

  browser_sync::JsBackend* js_backend = sync_manager_.GetJsBackend();

  StrictMock<MockJsEventHandler> event_handler;
  StrictMock<MockJsEventRouter> event_router;

  JsArgList return_args;

  EXPECT_CALL(event_router,
              RouteJsEvent("onGetRootNodeFinished", _, &event_handler)).
      WillOnce(SaveArg<1>(&return_args));

  js_backend->SetParentJsEventRouter(&event_router);

  // Should trigger the reply.
  js_backend->ProcessMessage("getRootNode", kNoArgs, &event_handler);

  EXPECT_EQ(1u, return_args.Get().GetSize());
  DictionaryValue* node_info = NULL;
  EXPECT_TRUE(return_args.Get().GetDictionary(0, &node_info));
  if (node_info) {
    ReadTransaction trans(sync_manager_.GetUserShare());
    ReadNode node(&trans);
    node.InitByRootLookup();
    CheckNodeValue(node, *node_info);
  } else {
    ADD_FAILURE();
  }

  js_backend->RemoveParentJsEventRouter();
}

void CheckGetNodeByIdReturnArgs(const SyncManager& sync_manager,
                                const JsArgList& return_args,
                                int64 id) {
  EXPECT_EQ(1u, return_args.Get().GetSize());
  DictionaryValue* node_info = NULL;
  EXPECT_TRUE(return_args.Get().GetDictionary(0, &node_info));
  if (node_info) {
    ReadTransaction trans(sync_manager.GetUserShare());
    ReadNode node(&trans);
    node.InitByIdLookup(id);
    CheckNodeValue(node, *node_info);
  } else {
    ADD_FAILURE();
  }
}

TEST_F(SyncManagerTest, ProcessMessageGetNodeById) {
  int64 child_id =
      MakeNode(sync_manager_.GetUserShare(), syncable::BOOKMARKS, "testtag");

  browser_sync::JsBackend* js_backend = sync_manager_.GetJsBackend();

  StrictMock<MockJsEventHandler> event_handler;
  StrictMock<MockJsEventRouter> event_router;

  JsArgList return_args;

  EXPECT_CALL(event_router,
              RouteJsEvent("onGetNodeByIdFinished", _, &event_handler))
      .Times(2).WillRepeatedly(SaveArg<1>(&return_args));

  js_backend->SetParentJsEventRouter(&event_router);

  // Should trigger the reply.
  {
    ListValue args;
    args.Append(Value::CreateStringValue("1"));
    js_backend->ProcessMessage("getNodeById", JsArgList(args), &event_handler);
  }

  CheckGetNodeByIdReturnArgs(sync_manager_, return_args, 1);

  // Should trigger another reply.
  {
    ListValue args;
    args.Append(Value::CreateStringValue(base::Int64ToString(child_id)));
    js_backend->ProcessMessage("getNodeById", JsArgList(args), &event_handler);
  }

  CheckGetNodeByIdReturnArgs(sync_manager_, return_args, child_id);

  js_backend->RemoveParentJsEventRouter();
}

TEST_F(SyncManagerTest, ProcessMessageGetNodeByIdFailure) {
  browser_sync::JsBackend* js_backend = sync_manager_.GetJsBackend();

  StrictMock<MockJsEventHandler> event_handler;
  StrictMock<MockJsEventRouter> event_router;

  ListValue null_args;
  null_args.Append(Value::CreateNullValue());

  EXPECT_CALL(event_router,
              RouteJsEvent("onGetNodeByIdFinished",
                           HasArgsAsList(null_args), &event_handler))
      .Times(5);

  js_backend->SetParentJsEventRouter(&event_router);

  {
    ListValue args;
    js_backend->ProcessMessage("getNodeById", JsArgList(args), &event_handler);
  }

  {
    ListValue args;
    args.Append(Value::CreateStringValue(""));
    js_backend->ProcessMessage("getNodeById", JsArgList(args), &event_handler);
  }

  {
    ListValue args;
    args.Append(Value::CreateStringValue("nonsense"));
    js_backend->ProcessMessage("getNodeById", JsArgList(args), &event_handler);
  }

  {
    ListValue args;
    args.Append(Value::CreateStringValue("nonsense"));
    js_backend->ProcessMessage("getNodeById", JsArgList(args), &event_handler);
  }

  {
    ListValue args;
    args.Append(Value::CreateStringValue("0"));
    js_backend->ProcessMessage("getNodeById", JsArgList(args), &event_handler);
  }

  // TODO(akalin): Figure out how to test InitByIdLookup() failure.

  js_backend->RemoveParentJsEventRouter();
}

TEST_F(SyncManagerTest, OnNotificationStateChange) {
  StrictMock<MockJsEventRouter> event_router;

  ListValue true_args;
  true_args.Append(Value::CreateBooleanValue(true));
  ListValue false_args;
  false_args.Append(Value::CreateBooleanValue(false));

  EXPECT_CALL(event_router,
              RouteJsEvent("onSyncNotificationStateChange",
                           HasArgsAsList(true_args), NULL));
  EXPECT_CALL(event_router,
              RouteJsEvent("onSyncNotificationStateChange",
                           HasArgsAsList(false_args), NULL));

  browser_sync::JsBackend* js_backend = sync_manager_.GetJsBackend();

  sync_manager_.TriggerOnNotificationStateChangeForTest(true);
  sync_manager_.TriggerOnNotificationStateChangeForTest(false);

  js_backend->SetParentJsEventRouter(&event_router);
  sync_manager_.TriggerOnNotificationStateChangeForTest(true);
  sync_manager_.TriggerOnNotificationStateChangeForTest(false);
  js_backend->RemoveParentJsEventRouter();

  sync_manager_.TriggerOnNotificationStateChangeForTest(true);
  sync_manager_.TriggerOnNotificationStateChangeForTest(false);
}

TEST_F(SyncManagerTest, OnIncomingNotification) {
  StrictMock<MockJsEventRouter> event_router;

  const syncable::ModelTypeBitSet empty_model_types;
  syncable::ModelTypeBitSet model_types;
  model_types.set(syncable::BOOKMARKS);
  model_types.set(syncable::THEMES);

  // Build expected_args to have a single argument with the string
  // equivalents of model_types.
  ListValue expected_args;
  {
    ListValue* model_type_list = new ListValue();
    expected_args.Append(model_type_list);
    for (int i = syncable::FIRST_REAL_MODEL_TYPE;
         i < syncable::MODEL_TYPE_COUNT; ++i) {
      if (model_types[i]) {
        model_type_list->Append(
            Value::CreateStringValue(
                syncable::ModelTypeToString(
                    syncable::ModelTypeFromInt(i))));
      }
    }
  }

  EXPECT_CALL(event_router,
              RouteJsEvent("onSyncIncomingNotification",
                           HasArgsAsList(expected_args), NULL));

  browser_sync::JsBackend* js_backend = sync_manager_.GetJsBackend();

  sync_manager_.TriggerOnIncomingNotificationForTest(empty_model_types);
  sync_manager_.TriggerOnIncomingNotificationForTest(model_types);

  js_backend->SetParentJsEventRouter(&event_router);
  sync_manager_.TriggerOnIncomingNotificationForTest(model_types);
  js_backend->RemoveParentJsEventRouter();

  sync_manager_.TriggerOnIncomingNotificationForTest(empty_model_types);
  sync_manager_.TriggerOnIncomingNotificationForTest(model_types);
}

}  // namespace

}  // namespace browser_sync
