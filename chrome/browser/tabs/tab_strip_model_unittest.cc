// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app/system_monitor.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/scoped_ptr.h"
#include "base/string_util.h"
#include "base/stl_util-inl.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/dom_ui/new_tab_ui.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/profile_manager.h"
#include "chrome/browser/renderer_host/test/test_render_view_host.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "chrome/browser/tab_contents/navigation_entry.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/tabs/tab_strip_model_order_controller.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/notification_observer_mock.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/property_bag.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/testing_profile.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;

namespace {

// Class used to delete a TabContents when another TabContents is destroyed.
class DeleteTabContentsOnDestroyedObserver : public NotificationObserver {
 public:
  DeleteTabContentsOnDestroyedObserver(TabContents* source,
                                       TabContents* tab_to_delete)
      : source_(source),
        tab_to_delete_(tab_to_delete) {
    registrar_.Add(this,
                   NotificationType::TAB_CONTENTS_DESTROYED,
                   Source<TabContents>(source));
  }

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) {
    TabContents* tab_to_delete = tab_to_delete_;
    tab_to_delete_ = NULL;
    delete tab_to_delete;
  }

 private:
  TabContents* source_;
  TabContents* tab_to_delete_;
  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(DeleteTabContentsOnDestroyedObserver);
};

}  // namespace

class TabStripDummyDelegate : public TabStripModelDelegate {
 public:
  explicit TabStripDummyDelegate(TabContents* dummy)
      : dummy_contents_(dummy), can_close_(true), run_unload_(false) {}
  virtual ~TabStripDummyDelegate() {}

  void set_can_close(bool value) { can_close_ = value; }
  void set_run_unload_listener(bool value) { run_unload_ = value; }

  // Overridden from TabStripModelDelegate:
  virtual TabContents* AddBlankTab(bool foreground) { return NULL; }
  virtual TabContents* AddBlankTabAt(int index, bool foreground) {
    return NULL;
  }
  virtual Browser* CreateNewStripWithContents(TabContents* contents,
                                              const gfx::Rect& window_bounds,
                                              const DockInfo& dock_info,
                                              bool maximize) {
    return NULL;
  }
  virtual void ContinueDraggingDetachedTab(TabContents* contents,
                                           const gfx::Rect& window_bounds,
                                           const gfx::Rect& tab_bounds) {
  }
  virtual int GetDragActions() const { return 0; }
  virtual TabContents* CreateTabContentsForURL(
      const GURL& url,
      const GURL& referrer,
      Profile* profile,
      PageTransition::Type transition,
      bool defer_load,
      SiteInstance* instance) const {
    if (url == GURL(chrome::kChromeUINewTabURL))
      return dummy_contents_;
    return NULL;
  }
  virtual bool CanDuplicateContentsAt(int index) { return false; }
  virtual void DuplicateContentsAt(int index) {}
  virtual void CloseFrameAfterDragSession() {}
  virtual void CreateHistoricalTab(TabContents* contents) {}
  virtual bool RunUnloadListenerBeforeClosing(TabContents* contents) {
    return run_unload_;
  }
  virtual bool CanRestoreTab() { return false; }
  virtual void RestoreTab() {}
  virtual bool CanCloseContentsAt(int index) { return can_close_ ; }
  virtual bool CanBookmarkAllTabs() const { return false; }
  virtual void BookmarkAllTabs() {}
  virtual bool CanCloseTab() const { return true; }
  virtual bool UseVerticalTabs() const { return false; }
  virtual void ToggleUseVerticalTabs() {}
  virtual bool LargeIconsPermitted() const { return true; }

 private:
  // A dummy TabContents we give to callers that expect us to actually build a
  // Destinations tab for them.
  TabContents* dummy_contents_;

  // Whether tabs can be closed.
  bool can_close_;

  // Whether to report that we need to run an unload listener before closing.
  bool run_unload_;

  DISALLOW_COPY_AND_ASSIGN(TabStripDummyDelegate);
};

class TabStripModelTest : public RenderViewHostTestHarness {
 public:
  TabContents* CreateTabContents() {
    return new TabContents(profile(), NULL, 0, NULL, NULL);
  }

  TabContents* CreateTabContentsWithSharedRPH(TabContents* tab_contents) {
    TabContents* retval = new TabContents(profile(),
        tab_contents->render_view_host()->site_instance(), MSG_ROUTING_NONE,
        NULL, NULL);
    EXPECT_EQ(retval->GetRenderProcessHost(),
              tab_contents->GetRenderProcessHost());
    return retval;
  }

  // Forwards a URL "load" request through to our dummy TabContents
  // implementation.
  void LoadURL(TabContents* con, const std::wstring& url) {
    controller().LoadURL(GURL(WideToUTF16(url)), GURL(), PageTransition::LINK);
  }

  void GoBack(TabContents* contents) {
    controller().GoBack();
  }

  void GoForward(TabContents* contents) {
    controller().GoForward();
  }

  void SwitchTabTo(TabContents* contents) {
    // contents()->DidBecomeSelected();
  }

  // Sets the id of the specified contents.
  void SetID(TabContents* contents, int id) {
    GetIDAccessor()->SetProperty(contents->property_bag(), id);
  }

  // Returns the id of the specified contents.
  int GetID(TabContents* contents) {
    return *GetIDAccessor()->GetProperty(contents->property_bag());
  }

  // Returns the state of the given tab strip as a string. The state consists
  // of the ID of each tab contents followed by a 'p' if pinned. For example,
  // if the model consists of two tabs with ids 2 and 1, with the first
  // tab pinned, this returns "2p 1".
  std::string GetPinnedState(const TabStripModel& model) {
    std::string actual;
    for (int i = 0; i < model.count(); ++i) {
      if (i > 0)
        actual += " ";

      actual += base::IntToString(GetID(model.GetTabContentsAt(i)));

      if (model.IsAppTab(i))
        actual += "a";

      if (model.IsTabPinned(i))
        actual += "p";
    }
    return actual;
  }

  std::string GetIndicesClosedByCommandAsString(
      const TabStripModel& model,
      int index,
      TabStripModel::ContextMenuCommand id) const {
    std::vector<int> indices = model.GetIndicesClosedByCommand(index, id);
    std::string result;
    for (size_t i = 0; i < indices.size(); ++i) {
      if (i != 0)
        result += " ";
      result += base::IntToString(indices[i]);
    }
    return result;
  }

 private:
  PropertyAccessor<int>* GetIDAccessor() {
    static PropertyAccessor<int> accessor;
    return &accessor;
  }

  std::wstring test_dir_;
  std::wstring profile_path_;
  std::map<TabContents*, int> foo_;

  // ProfileManager requires a SystemMonitor.
  SystemMonitor system_monitor;

  ProfileManager pm_;
};

class MockTabStripModelObserver : public TabStripModelObserver {
 public:
  MockTabStripModelObserver() : empty_(true) {}
  ~MockTabStripModelObserver() {
    STLDeleteContainerPointers(states_.begin(), states_.end());
  }

  enum TabStripModelObserverAction {
    INSERT,
    CLOSE,
    DETACH,
    SELECT,
    MOVE,
    CHANGE,
    PINNED,
    REPLACED
  };

  struct State {
    State(TabContents* a_dst_contents,
          int a_dst_index,
          TabStripModelObserverAction a_action)
        : src_contents(NULL),
          dst_contents(a_dst_contents),
          src_index(-1),
          dst_index(a_dst_index),
          user_gesture(false),
          foreground(false),
          action(a_action) {
    }

    TabContents* src_contents;
    TabContents* dst_contents;
    int src_index;
    int dst_index;
    bool user_gesture;
    bool foreground;
    TabStripModelObserverAction action;
  };

  int GetStateCount() const {
    return static_cast<int>(states_.size());
  }

  State* GetStateAt(int index) const {
    DCHECK(index >= 0 && index < GetStateCount());
    return states_.at(index);
  }

  bool StateEquals(int index, const State& state) {
    State* s = GetStateAt(index);
    EXPECT_EQ(state.src_contents, s->src_contents);
    EXPECT_EQ(state.dst_contents, s->dst_contents);
    EXPECT_EQ(state.src_index, s->src_index);
    EXPECT_EQ(state.dst_index, s->dst_index);
    EXPECT_EQ(state.user_gesture, s->user_gesture);
    EXPECT_EQ(state.foreground, s->foreground);
    EXPECT_EQ(state.action, s->action);
    return (s->src_contents == state.src_contents &&
            s->dst_contents == state.dst_contents &&
            s->src_index == state.src_index &&
            s->dst_index == state.dst_index &&
            s->user_gesture == state.user_gesture &&
            s->foreground == state.foreground &&
            s->action == state.action);
  }

  // TabStripModelObserver implementation:
  virtual void TabInsertedAt(TabContents* contents,
                             int index,
                             bool foreground) {
    empty_ = false;
    State* s = new State(contents, index, INSERT);
    s->foreground = foreground;
    states_.push_back(s);
  }
  virtual void TabSelectedAt(TabContents* old_contents,
                             TabContents* new_contents,
                             int index,
                             bool user_gesture) {
    State* s = new State(new_contents, index, SELECT);
    s->src_contents = old_contents;
    s->user_gesture = user_gesture;
    states_.push_back(s);
  }
  virtual void TabMoved(
      TabContents* contents, int from_index, int to_index) {
    State* s = new State(contents, to_index, MOVE);
    s->src_index = from_index;
    states_.push_back(s);
  }

  virtual void TabClosingAt(TabStripModel* tab_strip_model,
                            TabContents* contents,
                            int index) {
    states_.push_back(new State(contents, index, CLOSE));
  }
  virtual void TabDetachedAt(TabContents* contents, int index) {
    states_.push_back(new State(contents, index, DETACH));
  }
  virtual void TabChangedAt(TabContents* contents, int index,
                            TabChangeType change_type) {
    states_.push_back(new State(contents, index, CHANGE));
  }
  virtual void TabReplacedAt(TabContents* old_contents,
                             TabContents* new_contents, int index) {
    State* s = new State(new_contents, index, REPLACED);
    s ->src_contents = old_contents;
    states_.push_back(s);
  }
  virtual void TabPinnedStateChanged(TabContents* contents, int index) {
    states_.push_back(new State(contents, index, PINNED));
  }
  virtual void TabStripEmpty() {
    empty_ = true;
  }

  void ClearStates() {
    STLDeleteContainerPointers(states_.begin(), states_.end());
    states_.clear();
  }

  bool empty() const { return empty_; }

 private:
  std::vector<State*> states_;

  bool empty_;

  DISALLOW_COPY_AND_ASSIGN(MockTabStripModelObserver);
};

TEST_F(TabStripModelTest, TestBasicAPI) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  MockTabStripModelObserver observer;
  tabstrip.AddObserver(&observer);

  EXPECT_TRUE(tabstrip.empty());

  typedef MockTabStripModelObserver::State State;

  TabContents* contents1 = CreateTabContents();

  // Note! The ordering of these tests is important, each subsequent test
  // builds on the state established in the previous. This is important if you
  // ever insert tests rather than append.

  // Test AppendTabContents, ContainsIndex
  {
    EXPECT_FALSE(tabstrip.ContainsIndex(0));
    tabstrip.AppendTabContents(contents1, true);
    EXPECT_TRUE(tabstrip.ContainsIndex(0));
    EXPECT_EQ(1, tabstrip.count());
    EXPECT_EQ(2, observer.GetStateCount());
    State s1(contents1, 0, MockTabStripModelObserver::INSERT);
    s1.foreground = true;
    EXPECT_TRUE(observer.StateEquals(0, s1));
    State s2(contents1, 0, MockTabStripModelObserver::SELECT);
    s2.src_contents = NULL;
    EXPECT_TRUE(observer.StateEquals(1, s2));
    observer.ClearStates();
  }

  // Test InsertTabContentsAt, foreground tab.
  TabContents* contents2 = CreateTabContents();
  {
    tabstrip.InsertTabContentsAt(1, contents2, TabStripModel::ADD_SELECTED);

    EXPECT_EQ(2, tabstrip.count());
    EXPECT_EQ(2, observer.GetStateCount());
    State s1(contents2, 1, MockTabStripModelObserver::INSERT);
    s1.foreground = true;
    EXPECT_TRUE(observer.StateEquals(0, s1));
    State s2(contents2, 1, MockTabStripModelObserver::SELECT);
    s2.src_contents = contents1;
    EXPECT_TRUE(observer.StateEquals(1, s2));
    observer.ClearStates();
  }

  // Test InsertTabContentsAt, background tab.
  TabContents* contents3 = CreateTabContents();
  {
    tabstrip.InsertTabContentsAt(2, contents3, TabStripModel::ADD_NONE);

    EXPECT_EQ(3, tabstrip.count());
    EXPECT_EQ(1, observer.GetStateCount());
    State s1(contents3, 2, MockTabStripModelObserver::INSERT);
    s1.foreground = false;
    EXPECT_TRUE(observer.StateEquals(0, s1));
    observer.ClearStates();
  }

  // Test SelectTabContentsAt
  {
    tabstrip.SelectTabContentsAt(2, true);
    EXPECT_EQ(1, observer.GetStateCount());
    State s1(contents3, 2, MockTabStripModelObserver::SELECT);
    s1.src_contents = contents2;
    s1.user_gesture = true;
    EXPECT_TRUE(observer.StateEquals(0, s1));
    observer.ClearStates();
  }

  // Test DetachTabContentsAt
  {
    // Detach
    TabContents* detached = tabstrip.DetachTabContentsAt(2);
    // ... and append again because we want this for later.
    tabstrip.AppendTabContents(detached, true);
    EXPECT_EQ(4, observer.GetStateCount());
    State s1(detached, 2, MockTabStripModelObserver::DETACH);
    EXPECT_TRUE(observer.StateEquals(0, s1));
    State s2(contents2, 1, MockTabStripModelObserver::SELECT);
    s2.src_contents = contents3;
    s2.user_gesture = false;
    EXPECT_TRUE(observer.StateEquals(1, s2));
    State s3(detached, 2, MockTabStripModelObserver::INSERT);
    s3.foreground = true;
    EXPECT_TRUE(observer.StateEquals(2, s3));
    State s4(detached, 2, MockTabStripModelObserver::SELECT);
    s4.src_contents = contents2;
    s4.user_gesture = false;
    EXPECT_TRUE(observer.StateEquals(3, s4));
    observer.ClearStates();
  }

  // Test CloseTabContentsAt
  {
    // Let's test nothing happens when the delegate veto the close.
    delegate.set_can_close(false);
    EXPECT_FALSE(tabstrip.CloseTabContentsAt(2, TabStripModel::CLOSE_NONE));
    EXPECT_EQ(3, tabstrip.count());
    EXPECT_EQ(0, observer.GetStateCount());

    // Now let's close for real.
    delegate.set_can_close(true);
    EXPECT_TRUE(tabstrip.CloseTabContentsAt(2, TabStripModel::CLOSE_NONE));
    EXPECT_EQ(2, tabstrip.count());

    EXPECT_EQ(3, observer.GetStateCount());
    State s1(contents3, 2, MockTabStripModelObserver::CLOSE);
    EXPECT_TRUE(observer.StateEquals(0, s1));
    State s2(contents3, 2, MockTabStripModelObserver::DETACH);
    EXPECT_TRUE(observer.StateEquals(1, s2));
    State s3(contents2, 1, MockTabStripModelObserver::SELECT);
    s3.src_contents = contents3;
    s3.user_gesture = false;
    EXPECT_TRUE(observer.StateEquals(2, s3));
    observer.ClearStates();
  }

  // Test MoveTabContentsAt, select_after_move == true
  {
    tabstrip.MoveTabContentsAt(1, 0, true);

    EXPECT_EQ(1, observer.GetStateCount());
    State s1(contents2, 0, MockTabStripModelObserver::MOVE);
    s1.src_index = 1;
    EXPECT_TRUE(observer.StateEquals(0, s1));
    EXPECT_EQ(0, tabstrip.selected_index());
    observer.ClearStates();
  }

  // Test MoveTabContentsAt, select_after_move == false
  {
    tabstrip.MoveTabContentsAt(1, 0, false);
    EXPECT_EQ(1, observer.GetStateCount());
    State s1(contents1, 0, MockTabStripModelObserver::MOVE);
    s1.src_index = 1;
    EXPECT_TRUE(observer.StateEquals(0, s1));
    EXPECT_EQ(1, tabstrip.selected_index());

    tabstrip.MoveTabContentsAt(0, 1, false);
    observer.ClearStates();
  }

  // Test Getters
  {
    EXPECT_EQ(contents2, tabstrip.GetSelectedTabContents());
    EXPECT_EQ(contents2, tabstrip.GetTabContentsAt(0));
    EXPECT_EQ(contents1, tabstrip.GetTabContentsAt(1));
    EXPECT_EQ(0, tabstrip.GetIndexOfTabContents(contents2));
    EXPECT_EQ(1, tabstrip.GetIndexOfTabContents(contents1));
    EXPECT_EQ(0, tabstrip.GetIndexOfController(&contents2->controller()));
    EXPECT_EQ(1, tabstrip.GetIndexOfController(&contents1->controller()));
  }

  // Test UpdateTabContentsStateAt
  {
    tabstrip.UpdateTabContentsStateAt(0, TabStripModelObserver::ALL);
    EXPECT_EQ(1, observer.GetStateCount());
    State s1(contents2, 0, MockTabStripModelObserver::CHANGE);
    EXPECT_TRUE(observer.StateEquals(0, s1));
    observer.ClearStates();
  }

  // Test SelectNextTab, SelectPreviousTab, SelectLastTab
  {
    // Make sure the second of the two tabs is selected first...
    tabstrip.SelectTabContentsAt(1, true);
    tabstrip.SelectPreviousTab();
    EXPECT_EQ(0, tabstrip.selected_index());
    tabstrip.SelectLastTab();
    EXPECT_EQ(1, tabstrip.selected_index());
    tabstrip.SelectNextTab();
    EXPECT_EQ(0, tabstrip.selected_index());
  }

  // Test CloseSelectedTab
  {
    tabstrip.CloseSelectedTab();
    // |CloseSelectedTab| calls CloseTabContentsAt, we already tested that, now
    // just verify that the count and selected index have changed
    // appropriately...
    EXPECT_EQ(1, tabstrip.count());
    EXPECT_EQ(0, tabstrip.selected_index());
  }

  tabstrip.CloseAllTabs();
  // TabStripModel should now be empty.
  EXPECT_TRUE(tabstrip.empty());

  // Opener methods are tested below...

  tabstrip.RemoveObserver(&observer);
}

TEST_F(TabStripModelTest, TestBasicOpenerAPI) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  // This is a basic test of opener functionality. opener_contents is created
  // as the first tab in the strip and then we create 5 other tabs in the
  // background with opener_contents set as their opener.

  TabContents* opener_contents = CreateTabContents();
  NavigationController* opener = &opener_contents->controller();
  tabstrip.AppendTabContents(opener_contents, true);
  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();
  TabContents* contents4 = CreateTabContents();
  TabContents* contents5 = CreateTabContents();

  // We use |InsertTabContentsAt| here instead of AppendTabContents so that
  // openership relationships are preserved.
  tabstrip.InsertTabContentsAt(tabstrip.count(), contents1,
                               TabStripModel::ADD_INHERIT_GROUP);
  tabstrip.InsertTabContentsAt(tabstrip.count(), contents2,
                               TabStripModel::ADD_INHERIT_GROUP);
  tabstrip.InsertTabContentsAt(tabstrip.count(), contents3,
                               TabStripModel::ADD_INHERIT_GROUP);
  tabstrip.InsertTabContentsAt(tabstrip.count(), contents4,
                               TabStripModel::ADD_INHERIT_GROUP);
  tabstrip.InsertTabContentsAt(tabstrip.count(), contents5,
                               TabStripModel::ADD_INHERIT_GROUP);

  // All the tabs should have the same opener.
  for (int i = 1; i < tabstrip.count(); ++i)
    EXPECT_EQ(opener, tabstrip.GetOpenerOfTabContentsAt(i));

  // If there is a next adjacent item, then the index should be of that item.
  EXPECT_EQ(2, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 1, false));
  // If the last tab in the group is closed, the preceding tab in the same
  // group should be selected.
  EXPECT_EQ(4, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 5, false));

  // Tests the method that finds the last tab opened by the same opener in the
  // strip (this is the insertion index for the next background tab for the
  // specified opener).
  EXPECT_EQ(5, tabstrip.GetIndexOfLastTabContentsOpenedBy(opener, 1));

  // For a tab that has opened no other tabs, the return value should always be
  // -1...
  NavigationController* o1 = &contents1->controller();
  EXPECT_EQ(-1, tabstrip.GetIndexOfNextTabContentsOpenedBy(o1, 3, false));
  EXPECT_EQ(-1, tabstrip.GetIndexOfLastTabContentsOpenedBy(o1, 3));

  // ForgetAllOpeners should destroy all opener relationships.
  tabstrip.ForgetAllOpeners();
  EXPECT_EQ(-1, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 1, false));
  EXPECT_EQ(-1, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 5, false));
  EXPECT_EQ(-1, tabstrip.GetIndexOfLastTabContentsOpenedBy(opener, 1));

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

static int GetInsertionIndex(TabStripModel* tabstrip, TabContents* contents) {
  return tabstrip->order_controller()->DetermineInsertionIndex(
      contents, PageTransition::LINK, false);
}

static void InsertTabContentses(TabStripModel* tabstrip,
                                TabContents* contents1,
                                TabContents* contents2,
                                TabContents* contents3) {
  tabstrip->InsertTabContentsAt(GetInsertionIndex(tabstrip, contents1),
                                contents1, TabStripModel::ADD_INHERIT_GROUP);
  tabstrip->InsertTabContentsAt(GetInsertionIndex(tabstrip, contents2),
                                contents2, TabStripModel::ADD_INHERIT_GROUP);
  tabstrip->InsertTabContentsAt(GetInsertionIndex(tabstrip, contents3),
                                contents3, TabStripModel::ADD_INHERIT_GROUP);
}

// Tests opening background tabs.
TEST_F(TabStripModelTest, TestLTRInsertionOptions) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  TabContents* opener_contents = CreateTabContents();
  tabstrip.AppendTabContents(opener_contents, true);

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();

  // Test LTR
  InsertTabContentses(&tabstrip, contents1, contents2, contents3);
  EXPECT_EQ(contents1, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(contents2, tabstrip.GetTabContentsAt(2));
  EXPECT_EQ(contents3, tabstrip.GetTabContentsAt(3));

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests inserting tabs with InsertAfter set to false.
TEST_F(TabStripModelTest, InsertBefore) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  tabstrip.SetInsertionPolicy(TabStripModel::INSERT_BEFORE);
  EXPECT_TRUE(tabstrip.empty());

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();

  InsertTabContentses(&tabstrip, contents1, contents2, contents3);

  // The order should be reversed.
  EXPECT_EQ(contents3, tabstrip.GetTabContentsAt(0));
  EXPECT_EQ(contents2, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(contents1, tabstrip.GetTabContentsAt(2));

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests opening background tabs with InsertAfter set to false.
TEST_F(TabStripModelTest, InsertBeforeOpeners) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  tabstrip.SetInsertionPolicy(TabStripModel::INSERT_BEFORE);
  EXPECT_TRUE(tabstrip.empty());
  TabContents* opener_contents = CreateTabContents();
  tabstrip.AppendTabContents(opener_contents, true);

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();

  InsertTabContentses(&tabstrip, contents1, contents2, contents3);

  // The order should be reversed.
  EXPECT_EQ(contents3, tabstrip.GetTabContentsAt(0));
  EXPECT_EQ(contents2, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(contents1, tabstrip.GetTabContentsAt(2));

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// This test constructs a tabstrip, and then simulates loading several tabs in
// the background from link clicks on the first tab. Then it simulates opening
// a new tab from the first tab in the foreground via a link click, verifies
// that this tab is opened adjacent to the opener, then closes it.
// Finally it tests that a tab opened for some non-link purpose openes at the
// end of the strip, not bundled to any existing context.
TEST_F(TabStripModelTest, TestInsertionIndexDetermination) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  TabContents* opener_contents = CreateTabContents();
  NavigationController* opener = &opener_contents->controller();
  tabstrip.AppendTabContents(opener_contents, true);

  // Open some other random unrelated tab in the background to monkey with our
  // insertion index.
  TabContents* other_contents = CreateTabContents();
  tabstrip.AppendTabContents(other_contents, false);

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();

  // Start by testing LTR
  InsertTabContentses(&tabstrip, contents1, contents2, contents3);
  EXPECT_EQ(opener_contents, tabstrip.GetTabContentsAt(0));
  EXPECT_EQ(contents1, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(contents2, tabstrip.GetTabContentsAt(2));
  EXPECT_EQ(contents3, tabstrip.GetTabContentsAt(3));
  EXPECT_EQ(other_contents, tabstrip.GetTabContentsAt(4));

  // The opener API should work...
  EXPECT_EQ(3, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 2, false));
  EXPECT_EQ(2, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 3, false));
  EXPECT_EQ(3, tabstrip.GetIndexOfLastTabContentsOpenedBy(opener, 1));

  // Now open a foreground tab from a link. It should be opened adjacent to the
  // opener tab.
  TabContents* fg_link_contents = CreateTabContents();
  int insert_index = tabstrip.order_controller()->DetermineInsertionIndex(
      fg_link_contents, PageTransition::LINK, true);
  EXPECT_EQ(1, insert_index);
  tabstrip.InsertTabContentsAt(insert_index, fg_link_contents,
                               TabStripModel::ADD_SELECTED |
                               TabStripModel::ADD_INHERIT_GROUP);
  EXPECT_EQ(1, tabstrip.selected_index());
  EXPECT_EQ(fg_link_contents, tabstrip.GetSelectedTabContents());

  // Now close this contents. The selection should move to the opener contents.
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(0, tabstrip.selected_index());

  // Now open a new empty tab. It should open at the end of the strip.
  TabContents* fg_nonlink_contents = CreateTabContents();
  insert_index = tabstrip.order_controller()->DetermineInsertionIndex(
      fg_nonlink_contents, PageTransition::AUTO_BOOKMARK, true);
  EXPECT_EQ(tabstrip.count(), insert_index);
  // We break the opener relationship...
  tabstrip.InsertTabContentsAt(insert_index, fg_nonlink_contents,
                               TabStripModel::ADD_NONE);
  // Now select it, so that user_gesture == true causes the opener relationship
  // to be forgotten...
  tabstrip.SelectTabContentsAt(tabstrip.count() - 1, true);
  EXPECT_EQ(tabstrip.count() - 1, tabstrip.selected_index());
  EXPECT_EQ(fg_nonlink_contents, tabstrip.GetSelectedTabContents());

  // Verify that all opener relationships are forgotten.
  EXPECT_EQ(-1, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 2, false));
  EXPECT_EQ(-1, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 3, false));
  EXPECT_EQ(-1, tabstrip.GetIndexOfNextTabContentsOpenedBy(opener, 3, false));
  EXPECT_EQ(-1, tabstrip.GetIndexOfLastTabContentsOpenedBy(opener, 1));

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests that selection is shifted to the correct tab when a tab is closed.
// If a tab is in the background when it is closed, the selection does not
// change.
// If a tab is in the foreground (selected),
//   If that tab does not have an opener, selection shifts to the right.
//   If the tab has an opener,
//     The next tab (scanning LTR) in the entire strip that has the same opener
//     is selected
//     If there are no other tabs that have the same opener,
//       The opener is selected
//
TEST_F(TabStripModelTest, TestSelectOnClose) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  TabContents* opener_contents = CreateTabContents();
  tabstrip.AppendTabContents(opener_contents, true);

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();

  // Note that we use Detach instead of Close throughout this test to avoid
  // having to keep reconstructing these TabContentses.

  // First test that closing tabs that are in the background doesn't adjust the
  // current selection.
  InsertTabContentses(&tabstrip, contents1, contents2, contents3);
  EXPECT_EQ(0, tabstrip.selected_index());

  tabstrip.DetachTabContentsAt(1);
  EXPECT_EQ(0, tabstrip.selected_index());

  for (int i = tabstrip.count() - 1; i >= 1; --i)
    tabstrip.DetachTabContentsAt(i);

  // Now test that when a tab doesn't have an opener, selection shifts to the
  // right when the tab is closed.
  InsertTabContentses(&tabstrip, contents1, contents2, contents3);
  EXPECT_EQ(0, tabstrip.selected_index());

  tabstrip.ForgetAllOpeners();
  tabstrip.SelectTabContentsAt(1, true);
  EXPECT_EQ(1, tabstrip.selected_index());
  tabstrip.DetachTabContentsAt(1);
  EXPECT_EQ(1, tabstrip.selected_index());
  tabstrip.DetachTabContentsAt(1);
  EXPECT_EQ(1, tabstrip.selected_index());
  tabstrip.DetachTabContentsAt(1);
  EXPECT_EQ(0, tabstrip.selected_index());

  for (int i = tabstrip.count() - 1; i >= 1; --i)
    tabstrip.DetachTabContentsAt(i);

  // Now test that when a tab does have an opener, it selects the next tab
  // opened by the same opener scanning LTR when it is closed.
  InsertTabContentses(&tabstrip, contents1, contents2, contents3);
  EXPECT_EQ(0, tabstrip.selected_index());
  tabstrip.SelectTabContentsAt(2, false);
  EXPECT_EQ(2, tabstrip.selected_index());
  tabstrip.CloseTabContentsAt(2, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(2, tabstrip.selected_index());
  tabstrip.CloseTabContentsAt(2, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(1, tabstrip.selected_index());
  tabstrip.CloseTabContentsAt(1, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(0, tabstrip.selected_index());
  // Finally test that when a tab has no "siblings" that the opener is
  // selected.
  TabContents* other_contents = CreateTabContents();
  tabstrip.InsertTabContentsAt(1, other_contents, TabStripModel::ADD_NONE);
  EXPECT_EQ(2, tabstrip.count());
  TabContents* opened_contents = CreateTabContents();
  tabstrip.InsertTabContentsAt(2, opened_contents,
                               TabStripModel::ADD_SELECTED |
                               TabStripModel::ADD_INHERIT_GROUP);
  EXPECT_EQ(2, tabstrip.selected_index());
  tabstrip.CloseTabContentsAt(2, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(0, tabstrip.selected_index());

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests the following context menu commands:
//  - Close Tab
//  - Close Other Tabs
//  - Close Tabs To Right
TEST_F(TabStripModelTest, TestContextMenuCloseCommands) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  TabContents* opener_contents = CreateTabContents();
  tabstrip.AppendTabContents(opener_contents, true);

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();

  InsertTabContentses(&tabstrip, contents1, contents2, contents3);
  EXPECT_EQ(0, tabstrip.selected_index());

  tabstrip.ExecuteContextMenuCommand(2, TabStripModel::CommandCloseTab);
  EXPECT_EQ(3, tabstrip.count());

  tabstrip.ExecuteContextMenuCommand(0, TabStripModel::CommandCloseTabsToRight);
  EXPECT_EQ(1, tabstrip.count());
  EXPECT_EQ(opener_contents, tabstrip.GetSelectedTabContents());

  TabContents* dummy_contents = CreateTabContents();
  tabstrip.AppendTabContents(dummy_contents, false);

  contents1 = CreateTabContents();
  contents2 = CreateTabContents();
  contents3 = CreateTabContents();
  InsertTabContentses(&tabstrip, contents1, contents2, contents3);
  EXPECT_EQ(5, tabstrip.count());

  int dummy_index = tabstrip.count() - 1;
  tabstrip.SelectTabContentsAt(dummy_index, true);
  EXPECT_EQ(dummy_contents, tabstrip.GetSelectedTabContents());

  tabstrip.ExecuteContextMenuCommand(dummy_index,
                                     TabStripModel::CommandCloseOtherTabs);
  EXPECT_EQ(1, tabstrip.count());
  EXPECT_EQ(dummy_contents, tabstrip.GetSelectedTabContents());

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests GetIndicesClosedByCommand.
TEST_F(TabStripModelTest, GetIndicesClosedByCommand) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();
  TabContents* contents4 = CreateTabContents();
  TabContents* contents5 = CreateTabContents();

  tabstrip.AppendTabContents(contents1, true);
  tabstrip.AppendTabContents(contents2, true);
  tabstrip.AppendTabContents(contents3, true);
  tabstrip.AppendTabContents(contents4, true);
  tabstrip.AppendTabContents(contents5, true);

  EXPECT_EQ("4 3 2 1", GetIndicesClosedByCommandAsString(
                tabstrip, 0, TabStripModel::CommandCloseTabsToRight));
  EXPECT_EQ("4 3 2", GetIndicesClosedByCommandAsString(
                tabstrip, 1, TabStripModel::CommandCloseTabsToRight));

  EXPECT_EQ("4 3 2 1", GetIndicesClosedByCommandAsString(
                tabstrip, 0, TabStripModel::CommandCloseOtherTabs));
  EXPECT_EQ("4 3 2 0", GetIndicesClosedByCommandAsString(
                tabstrip, 1, TabStripModel::CommandCloseOtherTabs));

  // Pin the first two tabs. Pinned tabs shouldn't be closed by the close other
  // commands.
  tabstrip.SetTabPinned(0, true);
  tabstrip.SetTabPinned(1, true);

  EXPECT_EQ("4 3 2", GetIndicesClosedByCommandAsString(
                tabstrip, 0, TabStripModel::CommandCloseTabsToRight));
  EXPECT_EQ("4 3", GetIndicesClosedByCommandAsString(
                tabstrip, 2, TabStripModel::CommandCloseTabsToRight));

  EXPECT_EQ("4 3 2", GetIndicesClosedByCommandAsString(
                tabstrip, 0, TabStripModel::CommandCloseOtherTabs));
  EXPECT_EQ("4 3", GetIndicesClosedByCommandAsString(
                tabstrip, 2, TabStripModel::CommandCloseOtherTabs));

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests whether or not TabContentses are inserted in the correct position
// using this "smart" function with a simulated middle click action on a series
// of links on the home page.
TEST_F(TabStripModelTest, AddTabContents_MiddleClickLinksAndClose) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  // Open the Home Page
  TabContents* homepage_contents = CreateTabContents();
  tabstrip.AddTabContents(
      homepage_contents, -1, PageTransition::AUTO_BOOKMARK,
      TabStripModel::ADD_SELECTED);

  // Open some other tab, by user typing.
  TabContents* typed_page_contents = CreateTabContents();
  tabstrip.AddTabContents(
      typed_page_contents, -1, PageTransition::TYPED,
      TabStripModel::ADD_SELECTED);

  EXPECT_EQ(2, tabstrip.count());

  // Re-select the home page.
  tabstrip.SelectTabContentsAt(0, true);

  // Open a bunch of tabs by simulating middle clicking on links on the home
  // page.
  TabContents* middle_click_contents1 = CreateTabContents();
  tabstrip.AddTabContents(
      middle_click_contents1, -1, PageTransition::LINK,
      TabStripModel::ADD_NONE);
  TabContents* middle_click_contents2 = CreateTabContents();
  tabstrip.AddTabContents(
      middle_click_contents2, -1, PageTransition::LINK,
      TabStripModel::ADD_NONE);
  TabContents* middle_click_contents3 = CreateTabContents();
  tabstrip.AddTabContents(
      middle_click_contents3, -1, PageTransition::LINK,
      TabStripModel::ADD_NONE);

  EXPECT_EQ(5, tabstrip.count());

  EXPECT_EQ(homepage_contents, tabstrip.GetTabContentsAt(0));
  EXPECT_EQ(middle_click_contents1, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(middle_click_contents2, tabstrip.GetTabContentsAt(2));
  EXPECT_EQ(middle_click_contents3, tabstrip.GetTabContentsAt(3));
  EXPECT_EQ(typed_page_contents, tabstrip.GetTabContentsAt(4));

  // Now simulate seleting a tab in the middle of the group of tabs opened from
  // the home page and start closing them. Each TabContents in the group should
  // be closed, right to left. This test is constructed to start at the middle
  // TabContents in the group to make sure the cursor wraps around to the first
  // TabContents in the group before closing the opener or any other
  // TabContents.
  tabstrip.SelectTabContentsAt(2, true);
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(middle_click_contents3, tabstrip.GetSelectedTabContents());
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(middle_click_contents1, tabstrip.GetSelectedTabContents());
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(homepage_contents, tabstrip.GetSelectedTabContents());
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(typed_page_contents, tabstrip.GetSelectedTabContents());

  EXPECT_EQ(1, tabstrip.count());

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests whether or not a TabContents created by a left click on a link that
// opens a new tab is inserted correctly adjacent to the tab that spawned it.
TEST_F(TabStripModelTest, AddTabContents_LeftClickPopup) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  // Open the Home Page
  TabContents* homepage_contents = CreateTabContents();
  tabstrip.AddTabContents(
      homepage_contents, -1, PageTransition::AUTO_BOOKMARK,
      TabStripModel::ADD_SELECTED);

  // Open some other tab, by user typing.
  TabContents* typed_page_contents = CreateTabContents();
  tabstrip.AddTabContents(
      typed_page_contents, -1, PageTransition::TYPED,
      TabStripModel::ADD_SELECTED);

  EXPECT_EQ(2, tabstrip.count());

  // Re-select the home page.
  tabstrip.SelectTabContentsAt(0, true);

  // Open a tab by simulating a left click on a link that opens in a new tab.
  TabContents* left_click_contents = CreateTabContents();
  tabstrip.AddTabContents(left_click_contents, -1, PageTransition::LINK,
                          TabStripModel::ADD_SELECTED);

  // Verify the state meets our expectations.
  EXPECT_EQ(3, tabstrip.count());
  EXPECT_EQ(homepage_contents, tabstrip.GetTabContentsAt(0));
  EXPECT_EQ(left_click_contents, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(typed_page_contents, tabstrip.GetTabContentsAt(2));

  // The newly created tab should be selected.
  EXPECT_EQ(left_click_contents, tabstrip.GetSelectedTabContents());

  // After closing the selected tab, the selection should move to the left, to
  // the opener.
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(homepage_contents, tabstrip.GetSelectedTabContents());

  EXPECT_EQ(2, tabstrip.count());

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests whether or not new tabs that should split context (typed pages,
// generated urls, also blank tabs) open at the end of the tabstrip instead of
// in the middle.
TEST_F(TabStripModelTest, AddTabContents_CreateNewBlankTab) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  // Open the Home Page
  TabContents* homepage_contents = CreateTabContents();
  tabstrip.AddTabContents(
      homepage_contents, -1, PageTransition::AUTO_BOOKMARK,
      TabStripModel::ADD_SELECTED);

  // Open some other tab, by user typing.
  TabContents* typed_page_contents = CreateTabContents();
  tabstrip.AddTabContents(
      typed_page_contents, -1, PageTransition::TYPED,
      TabStripModel::ADD_SELECTED);

  EXPECT_EQ(2, tabstrip.count());

  // Re-select the home page.
  tabstrip.SelectTabContentsAt(0, true);

  // Open a new blank tab in the foreground.
  TabContents* new_blank_contents = CreateTabContents();
  tabstrip.AddTabContents(new_blank_contents, -1, PageTransition::TYPED,
                          TabStripModel::ADD_SELECTED);

  // Verify the state of the tabstrip.
  EXPECT_EQ(3, tabstrip.count());
  EXPECT_EQ(homepage_contents, tabstrip.GetTabContentsAt(0));
  EXPECT_EQ(typed_page_contents, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(new_blank_contents, tabstrip.GetTabContentsAt(2));

  // Now open a couple more blank tabs in the background.
  TabContents* background_blank_contents1 = CreateTabContents();
  tabstrip.AddTabContents(
      background_blank_contents1, -1, PageTransition::TYPED,
      TabStripModel::ADD_NONE);
  TabContents* background_blank_contents2 = CreateTabContents();
  tabstrip.AddTabContents(
      background_blank_contents2, -1, PageTransition::GENERATED,
      TabStripModel::ADD_NONE);
  EXPECT_EQ(5, tabstrip.count());
  EXPECT_EQ(homepage_contents, tabstrip.GetTabContentsAt(0));
  EXPECT_EQ(typed_page_contents, tabstrip.GetTabContentsAt(1));
  EXPECT_EQ(new_blank_contents, tabstrip.GetTabContentsAt(2));
  EXPECT_EQ(background_blank_contents1, tabstrip.GetTabContentsAt(3));
  EXPECT_EQ(background_blank_contents2, tabstrip.GetTabContentsAt(4));

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Tests whether opener state is correctly forgotten when the user switches
// context.
TEST_F(TabStripModelTest, AddTabContents_ForgetOpeners) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  // Open the Home Page
  TabContents* homepage_contents = CreateTabContents();
  tabstrip.AddTabContents(
      homepage_contents, -1, PageTransition::AUTO_BOOKMARK,
      TabStripModel::ADD_SELECTED);

  // Open some other tab, by user typing.
  TabContents* typed_page_contents = CreateTabContents();
  tabstrip.AddTabContents(
      typed_page_contents, -1, PageTransition::TYPED,
      TabStripModel::ADD_SELECTED);

  EXPECT_EQ(2, tabstrip.count());

  // Re-select the home page.
  tabstrip.SelectTabContentsAt(0, true);

  // Open a bunch of tabs by simulating middle clicking on links on the home
  // page.
  TabContents* middle_click_contents1 = CreateTabContents();
  tabstrip.AddTabContents(
      middle_click_contents1, -1, PageTransition::LINK,
      TabStripModel::ADD_NONE);
  TabContents* middle_click_contents2 = CreateTabContents();
  tabstrip.AddTabContents(
      middle_click_contents2, -1, PageTransition::LINK,
      TabStripModel::ADD_NONE);
  TabContents* middle_click_contents3 = CreateTabContents();
  tabstrip.AddTabContents(
      middle_click_contents3, -1, PageTransition::LINK,
      TabStripModel::ADD_NONE);

  // Break out of the context by selecting a tab in a different context.
  EXPECT_EQ(typed_page_contents, tabstrip.GetTabContentsAt(4));
  tabstrip.SelectLastTab();
  EXPECT_EQ(typed_page_contents, tabstrip.GetSelectedTabContents());

  // Step back into the context by selecting a tab inside it.
  tabstrip.SelectTabContentsAt(2, true);
  EXPECT_EQ(middle_click_contents2, tabstrip.GetSelectedTabContents());

  // Now test that closing tabs selects to the right until there are no more,
  // then to the left, as if there were no context (context has been
  // successfully forgotten).
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(middle_click_contents3, tabstrip.GetSelectedTabContents());
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(typed_page_contents, tabstrip.GetSelectedTabContents());
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(middle_click_contents1, tabstrip.GetSelectedTabContents());
  tabstrip.CloseSelectedTab();
  EXPECT_EQ(homepage_contents, tabstrip.GetSelectedTabContents());

  EXPECT_EQ(1, tabstrip.count());

  tabstrip.CloseAllTabs();
  EXPECT_TRUE(tabstrip.empty());
}

// Added for http://b/issue?id=958960
TEST_F(TabStripModelTest, AppendContentsReselectionTest) {
  TabContents fake_destinations_tab(profile(), NULL, 0, NULL, NULL);
  TabStripDummyDelegate delegate(&fake_destinations_tab);
  TabStripModel tabstrip(&delegate, profile());
  EXPECT_TRUE(tabstrip.empty());

  // Open the Home Page
  TabContents* homepage_contents = CreateTabContents();
  tabstrip.AddTabContents(
      homepage_contents, -1, PageTransition::AUTO_BOOKMARK,
      TabStripModel::ADD_SELECTED);

  // Open some other tab, by user typing.
  TabContents* typed_page_contents = CreateTabContents();
  tabstrip.AddTabContents(
      typed_page_contents, -1, PageTransition::TYPED,
      TabStripModel::ADD_NONE);

  // The selected tab should still be the first.
  EXPECT_EQ(0, tabstrip.selected_index());

  // Now simulate a link click that opens a new tab (by virtue of target=_blank)
  // and make sure the right tab gets selected when the new tab is closed.
  TabContents* target_blank_contents = CreateTabContents();
  tabstrip.AppendTabContents(target_blank_contents, true);
  EXPECT_EQ(2, tabstrip.selected_index());
  tabstrip.CloseTabContentsAt(2, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(0, tabstrip.selected_index());

  // clean up after ourselves
  tabstrip.CloseAllTabs();
}

// Added for http://b/issue?id=1027661
TEST_F(TabStripModelTest, ReselectionConsidersChildrenTest) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel strip(&delegate, profile());

  // Open page A
  TabContents* page_a_contents = CreateTabContents();
  strip.AddTabContents(
      page_a_contents, -1, PageTransition::AUTO_BOOKMARK,
      TabStripModel::ADD_SELECTED);

  // Simulate middle click to open page A.A and A.B
  TabContents* page_a_a_contents = CreateTabContents();
  strip.AddTabContents(page_a_a_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);
  TabContents* page_a_b_contents = CreateTabContents();
  strip.AddTabContents(page_a_b_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);

  // Select page A.A
  strip.SelectTabContentsAt(1, true);
  EXPECT_EQ(page_a_a_contents, strip.GetSelectedTabContents());

  // Simulate a middle click to open page A.A.A
  TabContents* page_a_a_a_contents = CreateTabContents();
  strip.AddTabContents(page_a_a_a_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);

  EXPECT_EQ(page_a_a_a_contents, strip.GetTabContentsAt(2));

  // Close page A.A
  strip.CloseTabContentsAt(strip.selected_index(), TabStripModel::CLOSE_NONE);

  // Page A.A.A should be selected, NOT A.B
  EXPECT_EQ(page_a_a_a_contents, strip.GetSelectedTabContents());

  // Close page A.A.A
  strip.CloseTabContentsAt(strip.selected_index(), TabStripModel::CLOSE_NONE);

  // Page A.B should be selected
  EXPECT_EQ(page_a_b_contents, strip.GetSelectedTabContents());

  // Close page A.B
  strip.CloseTabContentsAt(strip.selected_index(), TabStripModel::CLOSE_NONE);

  // Page A should be selected
  EXPECT_EQ(page_a_contents, strip.GetSelectedTabContents());

  // Clean up.
  strip.CloseAllTabs();
}

TEST_F(TabStripModelTest, AddTabContents_NewTabAtEndOfStripInheritsGroup) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel strip(&delegate, profile());

  // Open page A
  TabContents* page_a_contents = CreateTabContents();
  strip.AddTabContents(page_a_contents, -1, PageTransition::START_PAGE,
                       TabStripModel::ADD_SELECTED);

  // Open pages B, C and D in the background from links on page A...
  TabContents* page_b_contents = CreateTabContents();
  TabContents* page_c_contents = CreateTabContents();
  TabContents* page_d_contents = CreateTabContents();
  strip.AddTabContents(page_b_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);
  strip.AddTabContents(page_c_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);
  strip.AddTabContents(page_d_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);

  // Switch to page B's tab.
  strip.SelectTabContentsAt(1, true);

  // Open a New Tab at the end of the strip (simulate Ctrl+T)
  TabContents* new_tab_contents = CreateTabContents();
  strip.AddTabContents(new_tab_contents, -1, PageTransition::TYPED,
                       TabStripModel::ADD_SELECTED);

  EXPECT_EQ(4, strip.GetIndexOfTabContents(new_tab_contents));
  EXPECT_EQ(4, strip.selected_index());

  // Close the New Tab that was just opened. We should be returned to page B's
  // Tab...
  strip.CloseTabContentsAt(4, TabStripModel::CLOSE_NONE);

  EXPECT_EQ(1, strip.selected_index());

  // Open a non-New Tab tab at the end of the strip, with a TYPED transition.
  // This is like typing a URL in the address bar and pressing Alt+Enter. The
  // behavior should be the same as above.
  TabContents* page_e_contents = CreateTabContents();
  strip.AddTabContents(page_e_contents, -1, PageTransition::TYPED,
                       TabStripModel::ADD_SELECTED);

  EXPECT_EQ(4, strip.GetIndexOfTabContents(page_e_contents));
  EXPECT_EQ(4, strip.selected_index());

  // Close the Tab. Selection should shift back to page B's Tab.
  strip.CloseTabContentsAt(4, TabStripModel::CLOSE_NONE);

  EXPECT_EQ(1, strip.selected_index());

  // Open a non-New Tab tab at the end of the strip, with some other
  // transition. This is like right clicking on a bookmark and choosing "Open
  // in New Tab". No opener relationship should be preserved between this Tab
  // and the one that was active when the gesture was performed.
  TabContents* page_f_contents = CreateTabContents();
  strip.AddTabContents(page_f_contents, -1, PageTransition::AUTO_BOOKMARK,
                       TabStripModel::ADD_SELECTED);

  EXPECT_EQ(4, strip.GetIndexOfTabContents(page_f_contents));
  EXPECT_EQ(4, strip.selected_index());

  // Close the Tab. The next-adjacent should be selected.
  strip.CloseTabContentsAt(4, TabStripModel::CLOSE_NONE);

  EXPECT_EQ(3, strip.selected_index());

  // Clean up.
  strip.CloseAllTabs();
}

// A test of navigations in a tab that is part of a group of opened from some
// parent tab. If the navigations are link clicks, the group relationship of
// the tab to its parent are preserved. If they are of any other type, they are
// not preserved.
TEST_F(TabStripModelTest, NavigationForgetsOpeners) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel strip(&delegate, profile());

  // Open page A
  TabContents* page_a_contents = CreateTabContents();
  strip.AddTabContents(page_a_contents, -1, PageTransition::START_PAGE,
                       TabStripModel::ADD_SELECTED);

  // Open pages B, C and D in the background from links on page A...
  TabContents* page_b_contents = CreateTabContents();
  TabContents* page_c_contents = CreateTabContents();
  TabContents* page_d_contents = CreateTabContents();
  strip.AddTabContents(page_b_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);
  strip.AddTabContents(page_c_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);
  strip.AddTabContents(page_d_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);

  // Open page E in a different opener group from page A.
  TabContents* page_e_contents = CreateTabContents();
  strip.AddTabContents(page_e_contents, -1, PageTransition::START_PAGE,
                       TabStripModel::ADD_NONE);

  // Tell the TabStripModel that we are navigating page D via a link click.
  strip.SelectTabContentsAt(3, true);
  strip.TabNavigating(page_d_contents, PageTransition::LINK);

  // Close page D, page C should be selected. (part of same group).
  strip.CloseTabContentsAt(3, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(2, strip.selected_index());

  // Tell the TabStripModel that we are navigating in page C via a bookmark.
  strip.TabNavigating(page_c_contents, PageTransition::AUTO_BOOKMARK);

  // Close page C, page E should be selected. (C is no longer part of the
  // A-B-C-D group, selection moves to the right).
  strip.CloseTabContentsAt(2, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(page_e_contents, strip.GetTabContentsAt(strip.selected_index()));

  strip.CloseAllTabs();
}

// A test that the forgetting behavior tested in NavigationForgetsOpeners above
// doesn't cause the opener relationship for a New Tab opened at the end of the
// TabStrip to be reset (Test 1 below), unless another any other tab is
// seelcted (Test 2 below).
TEST_F(TabStripModelTest, NavigationForgettingDoesntAffectNewTab) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel strip(&delegate, profile());

  // Open a tab and several tabs from it, then select one of the tabs that was
  // opened.
  TabContents* page_a_contents = CreateTabContents();
  strip.AddTabContents(page_a_contents, -1, PageTransition::START_PAGE,
                       TabStripModel::ADD_SELECTED);

  TabContents* page_b_contents = CreateTabContents();
  TabContents* page_c_contents = CreateTabContents();
  TabContents* page_d_contents = CreateTabContents();
  strip.AddTabContents(page_b_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);
  strip.AddTabContents(page_c_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);
  strip.AddTabContents(page_d_contents, -1, PageTransition::LINK,
                       TabStripModel::ADD_NONE);

  strip.SelectTabContentsAt(2, true);

  // TEST 1: If the user is in a group of tabs and opens a new tab at the end
  // of the strip, closing that new tab will select the tab that they were
  // last on.

  // Now simulate opening a new tab at the end of the TabStrip.
  TabContents* new_tab_contents1 = CreateTabContents();
  strip.AddTabContents(new_tab_contents1, -1, PageTransition::TYPED,
                       TabStripModel::ADD_SELECTED);

  // At this point, if we close this tab the last selected one should be
  // re-selected.
  strip.CloseTabContentsAt(strip.count() - 1, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(page_c_contents, strip.GetTabContentsAt(strip.selected_index()));

  // TEST 2: If the user is in a group of tabs and opens a new tab at the end
  // of the strip, selecting any other tab in the strip will cause that new
  // tab's opener relationship to be forgotten.

  // Open a new tab again.
  TabContents* new_tab_contents2 = CreateTabContents();
  strip.AddTabContents(new_tab_contents2, -1, PageTransition::TYPED,
                       TabStripModel::ADD_SELECTED);

  // Now select the first tab.
  strip.SelectTabContentsAt(0, true);

  // Now select the last tab.
  strip.SelectTabContentsAt(strip.count() - 1, true);

  // Now close the last tab. The next adjacent should be selected.
  strip.CloseTabContentsAt(strip.count() - 1, TabStripModel::CLOSE_NONE);
  EXPECT_EQ(page_d_contents, strip.GetTabContentsAt(strip.selected_index()));

  strip.CloseAllTabs();
}

// Tests that fast shutdown is attempted appropriately.
TEST_F(TabStripModelTest, FastShutdown) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  MockTabStripModelObserver observer;
  tabstrip.AddObserver(&observer);

  EXPECT_TRUE(tabstrip.empty());

  // Make sure fast shutdown is attempted when tabs that share a RPH are shut
  // down.
  {
    TabContents* contents1 = CreateTabContents();
    TabContents* contents2 = CreateTabContentsWithSharedRPH(contents1);

    SetID(contents1, 1);
    SetID(contents2, 2);

    tabstrip.AppendTabContents(contents1, true);
    tabstrip.AppendTabContents(contents2, true);

    // Turn on the fake unload listener so the tabs don't actually get shut
    // down when we call CloseAllTabs()---we need to be able to check that
    // fast shutdown was attempted.
    delegate.set_run_unload_listener(true);
    tabstrip.CloseAllTabs();
    // On a mock RPH this checks whether we *attempted* fast shutdown.
    // A real RPH would reject our attempt since there is an unload handler.
    EXPECT_TRUE(contents1->GetRenderProcessHost()->fast_shutdown_started());
    EXPECT_EQ(2, tabstrip.count());

    delegate.set_run_unload_listener(false);
    tabstrip.CloseAllTabs();
    EXPECT_TRUE(tabstrip.empty());
  }

  // Make sure fast shutdown is not attempted when only some tabs that share a
  // RPH are shut down.
  {
    TabContents* contents1 = CreateTabContents();
    TabContents* contents2 = CreateTabContentsWithSharedRPH(contents1);

    SetID(contents1, 1);
    SetID(contents2, 2);

    tabstrip.AppendTabContents(contents1, true);
    tabstrip.AppendTabContents(contents2, true);

    tabstrip.CloseTabContentsAt(1, TabStripModel::CLOSE_NONE);
    EXPECT_FALSE(contents1->GetRenderProcessHost()->fast_shutdown_started());
    EXPECT_EQ(1, tabstrip.count());

    tabstrip.CloseAllTabs();
    EXPECT_TRUE(tabstrip.empty());
  }
}

// Tests various permutations of apps.
TEST_F(TabStripModelTest, Apps) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  MockTabStripModelObserver observer;
  tabstrip.AddObserver(&observer);

  EXPECT_TRUE(tabstrip.empty());

  typedef MockTabStripModelObserver::State State;

#if defined(OS_WIN)
  FilePath path(FILE_PATH_LITERAL("c:\\foo"));
#elif defined(OS_POSIX)
  FilePath path(FILE_PATH_LITERAL("/foo"));
#endif
  scoped_refptr<Extension> extension_app(new Extension(path,
                                                       Extension::INVALID));
  extension_app->launch_web_url_ = "http://www.google.com";
  TabContents* contents1 = CreateTabContents();
  contents1->SetExtensionApp(extension_app);
  TabContents* contents2 = CreateTabContents();
  contents2->SetExtensionApp(extension_app);
  TabContents* contents3 = CreateTabContents();

  SetID(contents1, 1);
  SetID(contents2, 2);
  SetID(contents3, 3);

  // Note! The ordering of these tests is important, each subsequent test
  // builds on the state established in the previous. This is important if you
  // ever insert tests rather than append.

  // Initial state, tab3 only and selected.
  tabstrip.AppendTabContents(contents3, true);

  observer.ClearStates();

  // Attempt to insert tab1 (an app tab) at position 1. This isn't a legal
  // position and tab1 should end up at position 0.
  {
    tabstrip.InsertTabContentsAt(1, contents1, TabStripModel::ADD_NONE);

    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents1, 0, MockTabStripModelObserver::INSERT);
    EXPECT_TRUE(observer.StateEquals(0, state));

    // And verify the state.
    EXPECT_EQ("1ap 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Insert tab 2 at position 1.
  {
    tabstrip.InsertTabContentsAt(1, contents2, TabStripModel::ADD_NONE);

    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents2, 1, MockTabStripModelObserver::INSERT);
    EXPECT_TRUE(observer.StateEquals(0, state));

    // And verify the state.
    EXPECT_EQ("1ap 2ap 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Try to move tab 3 to position 0. This isn't legal and should be ignored.
  {
    tabstrip.MoveTabContentsAt(2, 0, false);

    ASSERT_EQ(0, observer.GetStateCount());

    // And verify the state didn't change.
    EXPECT_EQ("1ap 2ap 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Try to move tab 0 to position 3. This isn't legal and should be ignored.
  {
    tabstrip.MoveTabContentsAt(0, 2, false);

    ASSERT_EQ(0, observer.GetStateCount());

    // And verify the state didn't change.
    EXPECT_EQ("1ap 2ap 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Try to move tab 0 to position 1. This is a legal move.
  {
    tabstrip.MoveTabContentsAt(0, 1, false);

    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents1, 1, MockTabStripModelObserver::MOVE);
    state.src_index = 0;
    EXPECT_TRUE(observer.StateEquals(0, state));

    // And verify the state didn't change.
    EXPECT_EQ("2ap 1ap 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Remove tab3 and insert at position 0. It should be forced to position 2.
  {
    tabstrip.DetachTabContentsAt(2);
    observer.ClearStates();

    tabstrip.InsertTabContentsAt(0, contents3, TabStripModel::ADD_NONE);

    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents3, 2, MockTabStripModelObserver::INSERT);
    EXPECT_TRUE(observer.StateEquals(0, state));

    // And verify the state didn't change.
    EXPECT_EQ("2ap 1ap 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  tabstrip.CloseAllTabs();
}

// Tests various permutations of pinning tabs.
TEST_F(TabStripModelTest, Pinning) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel tabstrip(&delegate, profile());
  MockTabStripModelObserver observer;
  tabstrip.AddObserver(&observer);

  EXPECT_TRUE(tabstrip.empty());

  typedef MockTabStripModelObserver::State State;

  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  TabContents* contents3 = CreateTabContents();

  SetID(contents1, 1);
  SetID(contents2, 2);
  SetID(contents3, 3);

  // Note! The ordering of these tests is important, each subsequent test
  // builds on the state established in the previous. This is important if you
  // ever insert tests rather than append.

  // Initial state, three tabs, first selected.
  tabstrip.AppendTabContents(contents1, true);
  tabstrip.AppendTabContents(contents2, false);
  tabstrip.AppendTabContents(contents3, false);

  observer.ClearStates();

  // Pin the first tab, this shouldn't visually reorder anything.
  {
    tabstrip.SetTabPinned(0, true);

    // As the order didn't change, we should get a pinned notification.
    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents1, 0, MockTabStripModelObserver::PINNED);
    EXPECT_TRUE(observer.StateEquals(0, state));

    // And verify the state.
    EXPECT_EQ("1p 2 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Unpin the first tab.
  {
    tabstrip.SetTabPinned(0, false);

    // As the order didn't change, we should get a pinned notification.
    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents1, 0, MockTabStripModelObserver::PINNED);
    EXPECT_TRUE(observer.StateEquals(0, state));

    // And verify the state.
    EXPECT_EQ("1 2 3", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Pin the 3rd tab, which should move it to the front.
  {
    tabstrip.SetTabPinned(2, true);

    // The pinning should have resulted in a move and a pinned notification.
    ASSERT_EQ(2, observer.GetStateCount());
    State state(contents3, 0, MockTabStripModelObserver::MOVE);
    state.src_index = 2;
    EXPECT_TRUE(observer.StateEquals(0, state));

    state = State(contents3, 0, MockTabStripModelObserver::PINNED);
    EXPECT_TRUE(observer.StateEquals(1, state));

    // And verify the state.
    EXPECT_EQ("3p 1 2", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Pin the tab "1", which shouldn't move anything.
  {
    tabstrip.SetTabPinned(1, true);

    // As the order didn't change, we should get a pinned notification.
    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents1, 1, MockTabStripModelObserver::PINNED);
    EXPECT_TRUE(observer.StateEquals(0, state));

    // And verify the state.
    EXPECT_EQ("3p 1p 2", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Try to move tab "2" to the front, it should be ignored.
  {
    tabstrip.MoveTabContentsAt(2, 0, false);

    // As the order didn't change, we should get a pinned notification.
    ASSERT_EQ(0, observer.GetStateCount());

    // And verify the state.
    EXPECT_EQ("3p 1p 2", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Unpin tab "3", which implicitly moves it to the end.
  {
    tabstrip.SetTabPinned(0, false);

    ASSERT_EQ(2, observer.GetStateCount());
    State state(contents3, 1, MockTabStripModelObserver::MOVE);
    state.src_index = 0;
    EXPECT_TRUE(observer.StateEquals(0, state));

    state = State(contents3, 1, MockTabStripModelObserver::PINNED);
    EXPECT_TRUE(observer.StateEquals(1, state));

    // And verify the state.
    EXPECT_EQ("1p 3 2", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Unpin tab "3", nothing should happen.
  {
    tabstrip.SetTabPinned(1, false);

    ASSERT_EQ(0, observer.GetStateCount());

    EXPECT_EQ("1p 3 2", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  // Pin "3" and "1".
  {
    tabstrip.SetTabPinned(0, true);
    tabstrip.SetTabPinned(1, true);

    EXPECT_EQ("1p 3p 2", GetPinnedState(tabstrip));

    observer.ClearStates();
  }

  TabContents* contents4 = CreateTabContents();
  SetID(contents4, 4);

  // Insert "4" between "1" and "3". As "1" and "4" are pinned, "4" should end
  // up after them.
  {
    tabstrip.InsertTabContentsAt(1, contents4, TabStripModel::ADD_NONE);

    ASSERT_EQ(1, observer.GetStateCount());
    State state(contents4, 2, MockTabStripModelObserver::INSERT);
    EXPECT_TRUE(observer.StateEquals(0, state));

    EXPECT_EQ("1p 3p 4 2", GetPinnedState(tabstrip));
  }

  tabstrip.CloseAllTabs();
}

// Makes sure the TabStripModel calls the right observer methods during a
// replace.
TEST_F(TabStripModelTest, ReplaceSendsSelected) {
  typedef MockTabStripModelObserver::State State;

  TabStripDummyDelegate delegate(NULL);
  TabStripModel strip(&delegate, profile());

  TabContents* first_contents = CreateTabContents();
  strip.AddTabContents(first_contents, -1, PageTransition::TYPED,
                       TabStripModel::ADD_SELECTED);

  MockTabStripModelObserver tabstrip_observer;
  strip.AddObserver(&tabstrip_observer);

  TabContents* new_contents = CreateTabContents();
  strip.ReplaceTabContentsAt(0, new_contents);

  ASSERT_EQ(2, tabstrip_observer.GetStateCount());

  // First event should be for replaced.
  State state(new_contents, 0, MockTabStripModelObserver::REPLACED);
  state.src_contents = first_contents;
  EXPECT_TRUE(tabstrip_observer.StateEquals(0, state));

  // And the second for selected.
  state = State(new_contents, 0, MockTabStripModelObserver::SELECT);
  state.src_contents = first_contents;
  EXPECT_TRUE(tabstrip_observer.StateEquals(1, state));

  // Now add another tab and replace it, making sure we don't get a selected
  // event this time.
  TabContents* third_contents = CreateTabContents();
  strip.AddTabContents(third_contents, 1, PageTransition::TYPED,
                       TabStripModel::ADD_NONE);

  tabstrip_observer.ClearStates();

  // And replace it.
  new_contents = CreateTabContents();
  strip.ReplaceTabContentsAt(1, new_contents);

  ASSERT_EQ(1, tabstrip_observer.GetStateCount());

  state = State(new_contents, 1, MockTabStripModelObserver::REPLACED);
  state.src_contents = third_contents;
  EXPECT_TRUE(tabstrip_observer.StateEquals(0, state));

  strip.CloseAllTabs();
}

// Makes sure TabStripModel handles the case of deleting a tab while removing
// another tab.
TEST_F(TabStripModelTest, DeleteFromDestroy) {
  TabStripDummyDelegate delegate(NULL);
  TabStripModel strip(&delegate, profile());
  TabContents* contents1 = CreateTabContents();
  TabContents* contents2 = CreateTabContents();
  strip.AppendTabContents(contents1, true);
  strip.AppendTabContents(contents2, true);
  // DeleteTabContentsOnDestroyedObserver deletes contents1 when contents2 sends
  // out notification that it is being destroyed.
  DeleteTabContentsOnDestroyedObserver observer(contents2, contents1);
  strip.CloseAllTabs();
}
