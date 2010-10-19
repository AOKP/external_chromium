// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/reload_button.h"

#include "app/l10n_util.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/views/event_utils.h"
#include "chrome/browser/views/location_bar/location_bar_view.h"
#include "grit/generated_resources.h"

////////////////////////////////////////////////////////////////////////////////
// ReloadButton, public:

ReloadButton::ReloadButton(LocationBarView* location_bar, Browser* browser)
    : ALLOW_THIS_IN_INITIALIZER_LIST(ToggleImageButton(this)),
      location_bar_(location_bar),
      browser_(browser),
      intended_mode_(MODE_RELOAD),
      visible_mode_(MODE_RELOAD) {
  DCHECK(location_bar_);
}

ReloadButton::~ReloadButton() {
}

void ReloadButton::ChangeMode(Mode mode, bool force) {
  intended_mode_ = mode;

  // If the change is forced, or the user isn't hovering the icon, or it's safe
  // to change it to the other image type, make the change immediately;
  // otherwise we'll let it happen later.
  if (force || !IsMouseHovered() || ((mode == MODE_STOP) ?
      !timer_.IsRunning() : (visible_mode_ != MODE_STOP))) {
    timer_.Stop();
    SetToggled(mode == MODE_STOP);
    visible_mode_ = mode;
    SetEnabled(true);

  // We want to disable the button if we're preventing a change from stop to
  // reload due to hovering, but not if we're preventing a change from reload to
  // stop due to the timer running.  (There is no disabled reload state.)
  } else if (visible_mode_ != MODE_RELOAD) {
    SetEnabled(false);
  }
}

////////////////////////////////////////////////////////////////////////////////
// ReloadButton, views::ButtonListener implementation:

void ReloadButton::ButtonPressed(views::Button* button,
                                 const views::Event& event) {
  if (visible_mode_ == MODE_STOP) {
    browser_->Stop();
    // The user has clicked, so we can feel free to update the button,
    // even if the mouse is still hovering.
    ChangeMode(MODE_RELOAD, true);
  } else if (!timer_.IsRunning()) {
    // Shift-clicking or ctrl-clicking the reload button means we should ignore
    // any cached content.
    // TODO(avayvod): eliminate duplication of this logic in
    // CompactLocationBarView.
    int command;
    int flags = mouse_event_flags();
    if (event.IsShiftDown() || event.IsControlDown()) {
      command = IDC_RELOAD_IGNORING_CACHE;
      // Mask off Shift and Control so they don't affect the disposition below.
      flags &= ~(views::Event::EF_SHIFT_DOWN | views::Event::EF_CONTROL_DOWN);
    } else {
      command = IDC_RELOAD;
    }

    WindowOpenDisposition disposition =
        event_utils::DispositionFromEventFlags(flags);
    if (disposition == CURRENT_TAB) {
      // Forcibly reset the location bar, since otherwise it won't discard any
      // ongoing user edits, since it doesn't realize this is a user-initiated
      // action.
      location_bar_->Revert();
    }

    // Start a timer - while this timer is running, the reload button cannot be
    // changed to a stop button.  We do not set |intended_mode_| to MODE_STOP
    // here as the browser will do that when it actually starts loading (which
    // may happen synchronously, thus the need to do this before telling the
    // browser to execute the reload command).
    timer_.Stop();
    timer_.Start(base::TimeDelta::FromMilliseconds(GetDoubleClickTimeMS()),
                 this, &ReloadButton::OnButtonTimer);

    browser_->ExecuteCommandWithDisposition(command, disposition);
  }
}

////////////////////////////////////////////////////////////////////////////////
// ReloadButton, View overrides:

void ReloadButton::OnMouseExited(const views::MouseEvent& e) {
  ChangeMode(intended_mode_, true);
  if (state() != BS_DISABLED)
    SetState(BS_NORMAL);
}

bool ReloadButton::GetTooltipText(const gfx::Point& p, std::wstring* tooltip) {
  tooltip->assign(l10n_util::GetString((visible_mode_ == MODE_RELOAD) ?
      IDS_TOOLTIP_RELOAD : IDS_TOOLTIP_STOP));
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// ReloadButton, private:

void ReloadButton::OnButtonTimer() {
  ChangeMode(intended_mode_, false);
}
