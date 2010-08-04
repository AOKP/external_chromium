// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/tabs/browser_tab_strip_controller.h"

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_menu_model.h"
#include "chrome/browser/views/app_launcher.h"
#include "chrome/browser/views/tabs/base_tab_strip.h"
#include "chrome/browser/views/tabs/tab_renderer_data.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/url_constants.h"
#include "views/controls/menu/menu_2.h"
#include "views/widget/widget.h"

static TabRendererData::NetworkState TabContentsNetworkState(
    TabContents* contents) {
  if (!contents || !contents->is_loading())
    return TabRendererData::NETWORK_STATE_NONE;
  if (contents->waiting_for_response())
    return TabRendererData::NETWORK_STATE_WAITING;
  return TabRendererData::NETWORK_STATE_LOADING;
}

class BrowserTabStripController::TabContextMenuContents
    : public menus::SimpleMenuModel::Delegate {
 public:
  TabContextMenuContents(BaseTab* tab,
                         BrowserTabStripController* controller)
      : ALLOW_THIS_IN_INITIALIZER_LIST(
          model_(this, controller->IsTabPinned(tab))),
        tab_(tab),
        controller_(controller),
        last_command_(TabStripModel::CommandFirst) {
    Build();
  }
  virtual ~TabContextMenuContents() {
    menu_->CancelMenu();
    if (controller_)
      controller_->tabstrip_->StopAllHighlighting();
  }

  void Cancel() {
    controller_ = NULL;
  }

  void RunMenuAt(const gfx::Point& point) {
    BrowserTabStripController* controller = controller_;
    menu_->RunMenuAt(point, views::Menu2::ALIGN_TOPLEFT);
    // We could be gone now. Assume |this| is junk!
    if (controller)
      controller->tabstrip_->StopAllHighlighting();
  }

  // Overridden from menus::SimpleMenuModel::Delegate:
  virtual bool IsCommandIdChecked(int command_id) const {
    return controller_->IsCommandCheckedForTab(
        static_cast<TabStripModel::ContextMenuCommand>(command_id),
        tab_);
  }
  virtual bool IsCommandIdEnabled(int command_id) const {
    return controller_->IsCommandEnabledForTab(
        static_cast<TabStripModel::ContextMenuCommand>(command_id),
        tab_);
  }
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      menus::Accelerator* accelerator) {
    return controller_->tabstrip_->GetWidget()->GetAccelerator(command_id,
                                                               accelerator);
  }
  virtual void CommandIdHighlighted(int command_id) {
    controller_->StopHighlightTabsForCommand(last_command_, tab_);
    last_command_ = static_cast<TabStripModel::ContextMenuCommand>(command_id);
    controller_->StartHighlightTabsForCommand(last_command_, tab_);
  }
  virtual void ExecuteCommand(int command_id) {
    controller_->ExecuteCommandForTab(
        static_cast<TabStripModel::ContextMenuCommand>(command_id),
        tab_);
  }

 private:
  void Build() {
    menu_.reset(new views::Menu2(&model_));
  }

  TabMenuModel model_;
  scoped_ptr<views::Menu2> menu_;

  // The tab we're showing a menu for.
  BaseTab* tab_;

  // A pointer back to our hosting controller, for command state information.
  BrowserTabStripController* controller_;

  // The last command that was selected, so that we can start/stop highlighting
  // appropriately as the user moves through the menu.
  TabStripModel::ContextMenuCommand last_command_;

  DISALLOW_COPY_AND_ASSIGN(TabContextMenuContents);
};

////////////////////////////////////////////////////////////////////////////////
// BrowserTabStripController, public:

BrowserTabStripController::BrowserTabStripController(TabStripModel* model)
    : model_(model),
      tabstrip_(NULL) {
  model_->AddObserver(this);

  notification_registrar_.Add(this,
      NotificationType::TAB_CLOSEABLE_STATE_CHANGED,
      NotificationService::AllSources());
}

BrowserTabStripController::~BrowserTabStripController() {
  // When we get here the TabStrip is being deleted. We need to explicitly
  // cancel the menu, otherwise it may try to invoke something on the tabstrip
  // from it's destructor.
  if (context_menu_contents_.get())
    context_menu_contents_->Cancel();

  model_->RemoveObserver(this);
}

void BrowserTabStripController::InitFromModel(BaseTabStrip* tabstrip) {
  tabstrip_ = tabstrip;
  // Walk the model, calling our insertion observer method for each item within
  // it.
  for (int i = 0; i < model_->count(); ++i) {
    TabInsertedAt(model_->GetTabContentsAt(i), i,
                  i == model_->selected_index());
  }
}

bool BrowserTabStripController::IsCommandEnabledForTab(
    TabStripModel::ContextMenuCommand command_id,
    BaseTab* tab) const {
  int model_index = tabstrip_->GetModelIndexOfBaseTab(tab);
  return model_->ContainsIndex(model_index) ?
      model_->IsContextMenuCommandEnabled(model_index, command_id) : false;
}

bool BrowserTabStripController::IsCommandCheckedForTab(
    TabStripModel::ContextMenuCommand command_id,
    BaseTab* tab) const {
  int model_index = tabstrip_->GetModelIndexOfBaseTab(tab);
  return model_->ContainsIndex(model_index) ?
      model_->IsContextMenuCommandChecked(model_index, command_id) : false;
}

void BrowserTabStripController::ExecuteCommandForTab(
    TabStripModel::ContextMenuCommand command_id,
    BaseTab* tab) {
  int model_index = tabstrip_->GetModelIndexOfBaseTab(tab);
  if (model_->ContainsIndex(model_index))
    model_->ExecuteContextMenuCommand(model_index, command_id);
}

bool BrowserTabStripController::IsTabPinned(BaseTab* tab) {
  return IsTabPinned(tabstrip_->GetModelIndexOfBaseTab(tab));
}

int BrowserTabStripController::GetCount() const {
  return model_->count();
}

bool BrowserTabStripController::IsValidIndex(int index) const {
  return model_->ContainsIndex(index);
}

int BrowserTabStripController::GetSelectedIndex() const {
  return model_->selected_index();
}

bool BrowserTabStripController::IsTabSelected(int model_index) const {
  return model_->selected_index() == model_index;
}

bool BrowserTabStripController::IsTabPinned(int model_index) const {
  return model_->ContainsIndex(model_index) && model_->IsTabPinned(model_index);
}

bool BrowserTabStripController::IsTabCloseable(int model_index) const {
  return !model_->ContainsIndex(model_index) ||
      model_->delegate()->CanCloseTab();
}

bool BrowserTabStripController::IsNewTabPage(int model_index) const {
  return model_->ContainsIndex(model_index) &&
      model_->GetTabContentsAt(model_index)->GetURL() ==
      GURL(chrome::kChromeUINewTabURL);
}

void BrowserTabStripController::SelectTab(int model_index) {
  model_->SelectTabContentsAt(model_index, true);
}

void BrowserTabStripController::CloseTab(int model_index) {
  tabstrip_->PrepareForCloseAt(model_index);
  model_->CloseTabContentsAt(model_index,
                             TabStripModel::CLOSE_USER_GESTURE |
                             TabStripModel::CLOSE_CREATE_HISTORICAL_TAB);
}

void BrowserTabStripController::ShowContextMenu(BaseTab* tab,
                                                const gfx::Point& p) {
  context_menu_contents_.reset(new TabContextMenuContents(tab, this));
  context_menu_contents_->RunMenuAt(p);
}

void BrowserTabStripController::UpdateLoadingAnimations() {
  // Don't use the model count here as it's possible for this to be invoked
  // before we've applied an update from the model (Browser::TabInsertedAt may
  // be processed before us and invokes this).
  for (int tab_index = 0, tab_count = tabstrip_->tab_count();
       tab_index < tab_count; ++tab_index) {
    BaseTab* tab = tabstrip_->base_tab_at_tab_index(tab_index);
    int model_index = tabstrip_->GetModelIndexOfBaseTab(tab);
    if (model_->ContainsIndex(model_index)) {
      TabContents* contents = model_->GetTabContentsAt(model_index);
      tab->UpdateLoadingAnimation(TabContentsNetworkState(contents));
    }
  }
}

int BrowserTabStripController::HasAvailableDragActions() const {
  return model_->delegate()->GetDragActions();
}

void BrowserTabStripController::PerformDrop(bool drop_before,
                                            int index,
                                            const GURL& url) {
  if (drop_before) {
    UserMetrics::RecordAction(UserMetricsAction("Tab_DropURLBetweenTabs"),
                              model_->profile());

    // Insert a new tab.
    TabContents* contents = model_->delegate()->CreateTabContentsForURL(
        url, GURL(), model_->profile(), PageTransition::TYPED, false, NULL);
    model_->AddTabContents(contents, index, PageTransition::GENERATED,
                           TabStripModel::ADD_SELECTED);
  } else {
    UserMetrics::RecordAction(UserMetricsAction("Tab_DropURLOnTab"),
                              model_->profile());

    model_->GetTabContentsAt(index)->controller().LoadURL(
        url, GURL(), PageTransition::GENERATED);
    model_->SelectTabContentsAt(index, true);
  }
}

bool BrowserTabStripController::IsCompatibleWith(BaseTabStrip* other) const {
  Profile* other_profile =
      static_cast<BrowserTabStripController*>(other->controller())->profile();
  return other_profile == profile();
}

void BrowserTabStripController::CreateNewTab() {
  UserMetrics::RecordAction(UserMetricsAction("NewTab_Button"),
                            model_->profile());

  TabContents* selected_tab = model_->GetSelectedTabContents();
  if (!selected_tab)
    return;

  Browser* browser = selected_tab->delegate()->GetBrowser();
  if (browser->OpenAppsPanelAsNewTab())
    return;

  model_->delegate()->AddBlankTab(true);
}

////////////////////////////////////////////////////////////////////////////////
// BrowserTabStripController, TabStripModelObserver implementation:

void BrowserTabStripController::TabInsertedAt(TabContents* contents,
                                              int model_index,
                                              bool foreground) {
  DCHECK(contents);
  DCHECK(model_index == TabStripModel::kNoTab ||
         model_->ContainsIndex(model_index));
  // This tab may be attached to another browser window, we should notify
  // renderer.
  contents->render_view_host()->UpdateBrowserWindowId(
      contents->controller().window_id().id());

  TabRendererData data;
  SetTabRendererDataFromModel(contents, model_index, &data);
  tabstrip_->AddTabAt(model_index, foreground, data);
}

void BrowserTabStripController::TabDetachedAt(TabContents* contents,
                                              int model_index) {
  tabstrip_->RemoveTabAt(model_index);
}

void BrowserTabStripController::TabSelectedAt(TabContents* old_contents,
                                              TabContents* contents,
                                              int model_index,
                                              bool user_gesture) {
  tabstrip_->SelectTabAt(model_->GetIndexOfTabContents(old_contents),
                         model_index);
}

void BrowserTabStripController::TabMoved(TabContents* contents,
                                         int from_model_index,
                                         int to_model_index) {
  // Update the data first as the pinned state may have changed.
  TabRendererData data;
  SetTabRendererDataFromModel(contents, to_model_index, &data);
  tabstrip_->SetTabData(from_model_index, data);

  tabstrip_->MoveTab(from_model_index, to_model_index);
}

void BrowserTabStripController::TabChangedAt(TabContents* contents,
                                             int model_index,
                                             TabChangeType change_type) {
  if (change_type == TITLE_NOT_LOADING) {
    tabstrip_->TabTitleChangedNotLoading(model_index);
    // We'll receive another notification of the change asynchronously.
    return;
  }

  SetTabDataAt(contents, model_index);
}

void BrowserTabStripController::TabReplacedAt(TabContents* old_contents,
                                              TabContents* new_contents,
                                              int model_index) {
  SetTabDataAt(new_contents, model_index);
}

void BrowserTabStripController::TabPinnedStateChanged(TabContents* contents,
                                                      int model_index) {
  // Currently none of the renderers render pinned state differently.
}

void BrowserTabStripController::TabMiniStateChanged(
    TabContents* contents,
    int model_index) {
  SetTabDataAt(contents, model_index);
}

void BrowserTabStripController::TabBlockedStateChanged(TabContents* contents,
                                                       int model_index) {
  SetTabDataAt(contents, model_index);
}

void BrowserTabStripController::SetTabDataAt(TabContents* contents,
                                             int model_index) {
  TabRendererData data;
  SetTabRendererDataFromModel(contents, model_index, &data);
  tabstrip_->SetTabData(model_index, data);
}

void BrowserTabStripController::SetTabRendererDataFromModel(
    TabContents* contents,
    int model_index,
    TabRendererData* data) {
  SkBitmap* app_icon = contents->GetExtensionAppIcon();
  if (app_icon)
    data->favicon = *app_icon;
  else
    data->favicon = contents->GetFavIcon();
  data->network_state = TabContentsNetworkState(contents);
  data->title = contents->GetTitle();
  data->loading = contents->is_loading();
  data->crashed = contents->is_crashed();
  data->off_the_record = contents->profile()->IsOffTheRecord();
  data->show_icon = contents->ShouldDisplayFavIcon();
  data->mini = model_->IsMiniTab(model_index);
  data->blocked = model_->IsTabBlocked(model_index);
  data->phantom = model_->IsPhantomTab(model_index);
  data->app = contents->is_app();
}

void BrowserTabStripController::StartHighlightTabsForCommand(
    TabStripModel::ContextMenuCommand command_id,
    BaseTab* tab) {
  if (command_id == TabStripModel::CommandCloseOtherTabs ||
      command_id == TabStripModel::CommandCloseTabsToRight) {
    int model_index = tabstrip_->GetModelIndexOfBaseTab(tab);
    if (IsValidIndex(model_index)) {
      std::vector<int> indices =
          model_->GetIndicesClosedByCommand(model_index, command_id);
      for (std::vector<int>::const_iterator i = indices.begin();
           i != indices.end(); ++i) {
        tabstrip_->StartHighlight(*i);
      }
    }
  }
}

void BrowserTabStripController::StopHighlightTabsForCommand(
    TabStripModel::ContextMenuCommand command_id,
    BaseTab* tab) {
  if (command_id == TabStripModel::CommandCloseTabsToRight ||
      command_id == TabStripModel::CommandCloseOtherTabs) {
    // Just tell all Tabs to stop pulsing - it's safe.
    tabstrip_->StopAllHighlighting();
  }
}

////////////////////////////////////////////////////////////////////////////////
// BrowserTabStripController, NotificationObserver implementation:

void BrowserTabStripController::Observe(NotificationType type,
    const NotificationSource& source, const NotificationDetails& details) {
  DCHECK(type.value == NotificationType::TAB_CLOSEABLE_STATE_CHANGED);
  // Note that this notification may be fired during a model mutation and
  // possibly before the tabstrip has processed the change.
  // Here, we just re-layout each existing tab to reflect the change in its
  // closeable state, and then schedule paint for entire tabstrip.
  for (int i = 0; i < tabstrip_->tab_count(); ++i) {
    tabstrip_->base_tab_at_tab_index(i)->Layout();
  }
  tabstrip_->SchedulePaint();
}
