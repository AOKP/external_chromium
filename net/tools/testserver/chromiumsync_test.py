#!/usr/bin/python2.4
# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests exercising chromiumsync and SyncDataModel."""

import unittest

from google.protobuf import text_format

import chromiumsync
import sync_pb2

class SyncDataModelTest(unittest.TestCase):
  def setUp(self):
    self.model = chromiumsync.SyncDataModel()

  def AddToModel(self, proto):
    self.model._entries[proto.id_string] = proto

  def testPermanentItemSpecs(self):
    SPECS = chromiumsync.SyncDataModel._PERMANENT_ITEM_SPECS
    # parent_tags must be declared before use.
    declared_specs = set(['0'])
    for spec in SPECS:
      self.assertTrue(spec.parent_tag in declared_specs)
      declared_specs.add(spec.tag)
    # Every sync datatype should have a permanent folder associated with it.
    unique_datatypes = set([x.sync_type for x in SPECS])
    self.assertEqual(unique_datatypes,
                     set(chromiumsync.ALL_TYPES))

  def testSaveEntry(self):
    proto = sync_pb2.SyncEntity()
    proto.id_string = 'abcd';
    proto.version = 0;
    self.assertFalse(self.model._ItemExists(proto.id_string))
    self.model._SaveEntry(proto)
    self.assertEqual(1, proto.version)
    self.assertTrue(self.model._ItemExists(proto.id_string))
    self.model._SaveEntry(proto)
    self.assertEqual(2, proto.version)
    proto.version = 0
    self.assertTrue(self.model._ItemExists(proto.id_string))
    self.assertEqual(2, self.model._entries[proto.id_string].version)

  def testWritePosition(self):
    def MakeProto(id_string, parent, position):
      proto = sync_pb2.SyncEntity()
      proto.id_string = id_string
      proto.position_in_parent = position
      proto.parent_id_string = parent
      self.AddToModel(proto)

    MakeProto('a', 'X', 1000)
    MakeProto('b', 'X', 1800)
    MakeProto('c', 'X', 2600)
    MakeProto('a1', 'Z', 1007)
    MakeProto('a2', 'Z', 1807)
    MakeProto('a3', 'Z', 2607)
    MakeProto('s', 'Y', 10000)

    def AssertPositionResult(my_id, parent_id, prev_id, expected_position):
      entry = sync_pb2.SyncEntity()
      entry.id_string = my_id
      self.model._WritePosition(entry, parent_id, prev_id)
      self.assertEqual(expected_position, entry.position_in_parent)
      self.assertEqual(parent_id, entry.parent_id_string)
      self.assertFalse(entry.HasField('insert_after_item_id'))

    AssertPositionResult('new', 'new_parent', '', 0)
    AssertPositionResult('new', 'Y', '', 10000 - (2 ** 20))
    AssertPositionResult('new', 'Y', 's', 10000 + (2 ** 20))
    AssertPositionResult('s', 'Y', '', 10000)
    AssertPositionResult('s', 'Y', 's', 10000)
    AssertPositionResult('a1', 'Z', '', 1007)

    AssertPositionResult('new', 'X', '', 1000 - (2 ** 20))
    AssertPositionResult('new', 'X', 'a', 1100)
    AssertPositionResult('new', 'X', 'b', 1900)
    AssertPositionResult('new', 'X', 'c', 2600 + (2 ** 20))

    AssertPositionResult('a1', 'X', '', 1000 - (2 ** 20))
    AssertPositionResult('a1', 'X', 'a', 1100)
    AssertPositionResult('a1', 'X', 'b', 1900)
    AssertPositionResult('a1', 'X', 'c', 2600 + (2 ** 20))

    AssertPositionResult('a', 'X', '', 1000)
    AssertPositionResult('a', 'X', 'b', 1900)
    AssertPositionResult('a', 'X', 'c', 2600 + (2 ** 20))

    AssertPositionResult('b', 'X', '', 1000 - (2 ** 20))
    AssertPositionResult('b', 'X', 'a', 1800)
    AssertPositionResult('b', 'X', 'c', 2600 + (2 ** 20))

    AssertPositionResult('c', 'X', '', 1000 - (2 ** 20))
    AssertPositionResult('c', 'X', 'a', 1100)
    AssertPositionResult('c', 'X', 'b', 2600)

  def testCreatePermanentItems(self):
    self.model._CreatePermanentItems(chromiumsync.ALL_TYPES)
    self.assertEqual(len(chromiumsync.ALL_TYPES) + 2,
                     len(self.model._entries))

  def ExpectedPermanentItemCount(self, sync_type):
    if sync_type == chromiumsync.BOOKMARK:
      return 4
    elif sync_type == chromiumsync.TOP_LEVEL:
      return 1
    else:
      return 2

  def testGetChangesFromTimestampZeroForEachType(self):
    for sync_type in chromiumsync.ALL_TYPES:
      self.model = chromiumsync.SyncDataModel()
      request_types = [sync_type, chromiumsync.TOP_LEVEL]

      version, changes = self.model.GetChangesFromTimestamp(request_types, 0)

      expected_count = self.ExpectedPermanentItemCount(sync_type)
      self.assertEqual(expected_count, version)
      self.assertEqual(expected_count, len(changes))
      self.assertEqual('google_chrome', changes[0].server_defined_unique_tag)
      for change in changes:
        self.assertTrue(change.HasField('server_defined_unique_tag'))
        self.assertEqual(change.version, change.sync_timestamp)
        self.assertTrue(change.version <= version)

      # Test idempotence: another GetUpdates from ts=0 shouldn't recreate.
      version, changes = self.model.GetChangesFromTimestamp(request_types, 0)
      self.assertEqual(expected_count, version)
      self.assertEqual(expected_count, len(changes))

      # Doing a wider GetUpdates from timestamp zero shouldn't recreate either.
      new_version, changes = self.model.GetChangesFromTimestamp(
          chromiumsync.ALL_TYPES, 0)
      self.assertEqual(len(chromiumsync.SyncDataModel._PERMANENT_ITEM_SPECS),
          new_version)
      self.assertEqual(new_version, len(changes))
      version, changes = self.model.GetChangesFromTimestamp(request_types, 0)
      self.assertEqual(new_version, version)
      self.assertEqual(expected_count, len(changes))

  def testBatchSize(self):
    for sync_type in chromiumsync.ALL_TYPES[1:]:
      specifics = chromiumsync.GetDefaultEntitySpecifics(sync_type)
      self.model = chromiumsync.SyncDataModel()
      request_types = [sync_type, chromiumsync.TOP_LEVEL]

      for i in range(self.model._BATCH_SIZE*3):
        entry = sync_pb2.SyncEntity()
        entry.id_string = 'batch test %d' % i
        entry.specifics.CopyFrom(specifics)
        self.model._SaveEntry(entry)
      version, changes = self.model.GetChangesFromTimestamp(request_types, 0)
      self.assertEqual(self.model._BATCH_SIZE, version)
      version, changes = self.model.GetChangesFromTimestamp(request_types,
          version)
      self.assertEqual(self.model._BATCH_SIZE*2, version)
      version, changes = self.model.GetChangesFromTimestamp(request_types,
          version)
      self.assertEqual(self.model._BATCH_SIZE*3, version)
      expected_dingleberry = self.ExpectedPermanentItemCount(sync_type)
      version, changes = self.model.GetChangesFromTimestamp(request_types,
          version)
      self.assertEqual(self.model._BATCH_SIZE*3 + expected_dingleberry,
          version)

      # Now delete a third of the items.
      for i in xrange(self.model._BATCH_SIZE*3 - 1, 0, -3):
        entry = sync_pb2.SyncEntity()
        entry.id_string = 'batch test %d' % i
        entry.deleted = True
        self.model._SaveEntry(entry)

      # The batch counts shouldn't change.
      version, changes = self.model.GetChangesFromTimestamp(request_types, 0)
      self.assertEqual(self.model._BATCH_SIZE, len(changes))
      version, changes = self.model.GetChangesFromTimestamp(request_types,
          version)
      self.assertEqual(self.model._BATCH_SIZE, len(changes))
      version, changes = self.model.GetChangesFromTimestamp(request_types,
          version)
      self.assertEqual(self.model._BATCH_SIZE, len(changes))
      expected_dingleberry = self.ExpectedPermanentItemCount(sync_type)
      version, changes = self.model.GetChangesFromTimestamp(request_types,
          version)
      self.assertEqual(expected_dingleberry, len(changes))
      self.assertEqual(self.model._BATCH_SIZE*4 + expected_dingleberry, version)

  def testCommitEachDataType(self):
    for sync_type in chromiumsync.ALL_TYPES[1:]:
      specifics = chromiumsync.GetDefaultEntitySpecifics(sync_type)
      self.model = chromiumsync.SyncDataModel()
      my_cache_guid = '112358132134'
      parent = 'foobar'
      commit_session = {}

      # Start with a GetUpdates from timestamp 0, to populate permanent items.
      original_version, original_changes = (
          self.model.GetChangesFromTimestamp([sync_type], 0))

      def DoCommit(original=None, id='', name=None, parent=None, prev=None):
        proto = sync_pb2.SyncEntity()
        if original is not None:
          proto.version = original.version
          proto.id_string = original.id_string
          proto.parent_id_string = original.parent_id_string
          proto.name = original.name
        else:
          proto.id_string = id
          proto.version = 0
        proto.specifics.CopyFrom(specifics)
        if name is not None:
          proto.name = name
        if parent:
          proto.parent_id_string = parent.id_string
        if prev:
          proto.insert_after_item_id = prev.id_string
        else:
          proto.insert_after_item_id = ''
        proto.folder = True
        proto.deleted = False
        result = self.model.CommitEntry(proto, my_cache_guid, commit_session)
        self.assertTrue(result)
        return (proto, result)

      # Commit a new item.
      proto1, result1 = DoCommit(name='namae', id='Foo',
                                 parent=original_changes[-1])
      # Commit an item whose parent is another item (referenced via the
      # pre-commit ID).
      proto2, result2 = DoCommit(name='Secondo', id='Bar',
                                 parent=proto1)
        # Commit a sibling of the second item.
      proto3, result3 = DoCommit(name='Third!', id='Baz',
                                 parent=proto1, prev=proto2)

      self.assertEqual(3, len(commit_session))
      for p, r in [(proto1, result1), (proto2, result2), (proto3, result3)]:
        self.assertNotEqual(r.id_string, p.id_string)
        self.assertEqual(r.originator_client_item_id, p.id_string)
        self.assertEqual(r.originator_cache_guid, my_cache_guid)
        self.assertTrue(r is not self.model._entries[r.id_string],
            "Commit result didn't make a defensive copy.")
        self.assertTrue(p is not self.model._entries[r.id_string],
            "Commit result didn't make a defensive copy.")
        self.assertEqual(commit_session.get(p.id_string), r.id_string)
        self.assertTrue(r.version > original_version)
      self.assertEqual(result1.parent_id_string, proto1.parent_id_string)
      self.assertEqual(result2.parent_id_string, result1.id_string)
      version, changes = self.model.GetChangesFromTimestamp([sync_type],
          original_version)
      self.assertEqual(3, len(changes))
      self.assertEqual(original_version + 3, version)
      self.assertEqual([result1, result2, result3], changes)
      for c in changes:
        self.assertTrue(c is not self.model._entries[c.id_string],
            "GetChanges didn't make a defensive copy.")
      self.assertTrue(result2.position_in_parent < result3.position_in_parent)
      self.assertEqual(0, result2.position_in_parent)

      # Now update the items so that the second item is the parent of the
      # first; with the first sandwiched between two new items (4 and 5).
      # Do this in a new commit session, meaning we'll reference items from
      # the first batch by their post-commit, server IDs.
      commit_session = {}
      old_cache_guid = my_cache_guid
      my_cache_guid = 'A different GUID'
      proto2b, result2b = DoCommit(original=result2,
                                   parent=original_changes[-1])
      proto4, result4 = DoCommit(id='ID4', name='Four',
                                 parent=result2, prev=None)
      proto1b, result1b = DoCommit(original=result1,
                                   parent=result2, prev=proto4)
      proto5, result5 = DoCommit(id='ID5', name='Five', parent=result2,
                                 prev=result1)

      self.assertEqual(2, len(commit_session),
          'Only new items in second batch should be in the session')
      for p, r, original in [(proto2b, result2b, proto2),
                             (proto4, result4, proto4),
                             (proto1b, result1b, proto1),
                             (proto5, result5, proto5)]:
        self.assertEqual(r.originator_client_item_id, original.id_string)
        if original is not p:
          self.assertEqual(r.id_string, p.id_string,
              'Ids should be stable after first commit')
          self.assertEqual(r.originator_cache_guid, old_cache_guid)
        else:
          self.assertNotEqual(r.id_string, p.id_string)
          self.assertEqual(r.originator_cache_guid, my_cache_guid)
          self.assertEqual(commit_session.get(p.id_string), r.id_string)
        self.assertTrue(r is not self.model._entries[r.id_string],
            "Commit result didn't make a defensive copy.")
        self.assertTrue(p is not self.model._entries[r.id_string],
            "Commit didn't make a defensive copy.")
        self.assertTrue(r.version > p.version)
      version, changes = self.model.GetChangesFromTimestamp([sync_type],
          original_version)
      self.assertEqual(5, len(changes))
      self.assertEqual(original_version + 7, version)
      self.assertEqual([result3, result2b, result4, result1b, result5], changes)
      for c in changes:
        self.assertTrue(c is not self.model._entries[c.id_string],
            "GetChanges didn't make a defensive copy.")
      self.assertTrue(result4.parent_id_string ==
                      result1b.parent_id_string ==
                      result5.parent_id_string ==
                      result2b.id_string)
      self.assertTrue(result4.position_in_parent <
                      result1b.position_in_parent <
                      result5.position_in_parent)

if __name__ == '__main__':
  unittest.main()