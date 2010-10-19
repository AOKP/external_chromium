// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/infobars/infobar_container.h"

#include "app/l10n_util.h"
#include "chrome/browser/tab_contents/infobar_delegate.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/view_ids.h"
#include "chrome/browser/views/infobars/infobars.h"
#include "chrome/common/notification_service.h"
#include "grit/generated_resources.h"

// InfoBarContainer, public: ---------------------------------------------------

InfoBarContainer::InfoBarContainer(Delegate* delegate)
    : delegate_(delegate),
      tab_contents_(NULL) {
  SetID(VIEW_ID_INFO_BAR_CONTAINER);
  SetAccessibleName(l10n_util::GetString(IDS_ACCNAME_INFOBAR_CONTAINER));
}

InfoBarContainer::~InfoBarContainer() {
  // We NULL this pointer before resetting the TabContents to prevent view
  // hierarchy modifications from attempting to resize the delegate which
  // could be in the process of shutting down.
  delegate_ = NULL;
  ChangeTabContents(NULL);
}

void InfoBarContainer::ChangeTabContents(TabContents* contents) {
  registrar_.RemoveAll();
  // No need to delete the child views here, their removal from the view
  // hierarchy does this automatically (see InfoBar::InfoBarRemoved).
  RemoveAllChildViews(false);
  tab_contents_ = contents;
  if (tab_contents_) {
    UpdateInfoBars();
    Source<TabContents> tc_source(tab_contents_);
    registrar_.Add(this, NotificationType::TAB_CONTENTS_INFOBAR_ADDED,
                   tc_source);
    registrar_.Add(this, NotificationType::TAB_CONTENTS_INFOBAR_REMOVED,
                   tc_source);
    registrar_.Add(this, NotificationType::TAB_CONTENTS_INFOBAR_REPLACED,
                   tc_source);
  }
}

void InfoBarContainer::InfoBarAnimated(bool completed) {
  if (delegate_)
    delegate_->InfoBarSizeChanged(!completed);
}

void InfoBarContainer::RemoveDelegate(InfoBarDelegate* delegate) {
  tab_contents_->RemoveInfoBar(delegate);
}

// InfoBarContainer, views::View overrides: ------------------------------------

gfx::Size InfoBarContainer::GetPreferredSize() {
  // We do not have a preferred width (we will expand to fit the available width
  // of the delegate). Our preferred height is the sum of the preferred
  // heights of the InfoBars contained within us.
  int height = 0;
  for (int i = 0; i < GetChildViewCount(); ++i)
    height += GetChildViewAt(i)->GetPreferredSize().height();
  return gfx::Size(0, height);
}

void InfoBarContainer::Layout() {
  int top = 0;
  for (int i = 0; i < GetChildViewCount(); ++i) {
    views::View* child = GetChildViewAt(i);
    gfx::Size ps = child->GetPreferredSize();
    child->SetBounds(0, top, width(), ps.height());
    top += ps.height();
  }
}

AccessibilityTypes::Role InfoBarContainer::GetAccessibleRole() {
  return AccessibilityTypes::ROLE_GROUPING;
}

void InfoBarContainer::ViewHierarchyChanged(bool is_add,
                                            views::View* parent,
                                            views::View* child) {
  if (parent == this && child->GetParent() == this) {
    if (delegate_) {
      // An InfoBar child was added or removed. Tell the delegate it needs to
      // re-layout since our preferred size will have changed.
      delegate_->InfoBarSizeChanged(false);
    }
  }
}

// InfoBarContainer, NotificationObserver implementation: ----------------------

void InfoBarContainer::Observe(NotificationType type,
                               const NotificationSource& source,
                               const NotificationDetails& details) {
  if (type == NotificationType::TAB_CONTENTS_INFOBAR_ADDED) {
    AddInfoBar(Details<InfoBarDelegate>(details).ptr(), true);  // animated
  } else if (type == NotificationType::TAB_CONTENTS_INFOBAR_REMOVED) {
    RemoveInfoBar(Details<InfoBarDelegate>(details).ptr(), true);  // animated
  } else if (type == NotificationType::TAB_CONTENTS_INFOBAR_REPLACED) {
    std::pair<InfoBarDelegate*, InfoBarDelegate*>* delegates =
        Details<std::pair<InfoBarDelegate*, InfoBarDelegate*> >(details).ptr();
    ReplaceInfoBar(delegates->first, delegates->second);
  } else {
    NOTREACHED();
  }
}

// InfoBarContainer, private: --------------------------------------------------

void InfoBarContainer::UpdateInfoBars() {
  for (int i = 0; i < tab_contents_->infobar_delegate_count(); ++i) {
    InfoBarDelegate* delegate = tab_contents_->GetInfoBarDelegateAt(i);
    InfoBar* infobar = delegate->CreateInfoBar();
    infobar->set_container(this);
    AddChildView(infobar);
    infobar->Open();
  }
}

void InfoBarContainer::AddInfoBar(InfoBarDelegate* delegate,
                                  bool use_animation) {
  InfoBar* infobar = delegate->CreateInfoBar();
  infobar->set_container(this);
  AddChildView(infobar);

  if (use_animation)
    infobar->AnimateOpen();
  else
    infobar->Open();
}

void InfoBarContainer::RemoveInfoBar(InfoBarDelegate* delegate,
                                     bool use_animation) {
  // Search for infobar associated with |delegate| among child views.
  // We cannot search for |delegate| in tab_contents, because an infobar remains
  // a child view until its close animation completes, which can result in
  // different number of infobars in container and infobar delegates in tab
  // contents.
  for (int i = 0; i < GetChildViewCount(); ++i) {
    InfoBar* infobar = static_cast<InfoBar*>(GetChildViewAt(i));
    if (infobar->delegate() == delegate) {
      if (use_animation) {
        // The View will be removed once the Close animation completes.
        infobar->AnimateClose();
      } else {
        infobar->Close();
      }
      break;
    }
  }
}

void InfoBarContainer::ReplaceInfoBar(InfoBarDelegate* old_delegate,
                                      InfoBarDelegate* new_delegate) {
  RemoveInfoBar(old_delegate, false);  // no animation
  AddInfoBar(new_delegate, false);  // no animation
}
