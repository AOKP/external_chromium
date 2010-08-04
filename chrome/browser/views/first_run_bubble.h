// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_FIRST_RUN_BUBBLE_H_
#define CHROME_BROWSER_VIEWS_FIRST_RUN_BUBBLE_H_

#include "base/compiler_specific.h"
#include "base/task.h"
#include "chrome/browser/first_run.h"
#include "chrome/browser/views/info_bubble.h"

class FirstRunBubbleViewBase;
class Profile;

class FirstRunBubble : public InfoBubble,
                       public InfoBubbleDelegate {
 public:
  static FirstRunBubble* Show(Profile* profile, views::Widget* parent,
                              const gfx::Rect& position_relative_to,
                              BubbleBorder::ArrowLocation arrow_location,
                              FirstRun::BubbleType bubble_type);

 private:
  FirstRunBubble();
  virtual ~FirstRunBubble();

  void set_view(FirstRunBubbleViewBase* view) { view_ = view; }

  // Re-enable the parent window.
  void EnableParent();

#if defined(OS_WIN)
  // Overridden from InfoBubble:
  virtual void OnActivate(UINT action, BOOL minimized, HWND window);
#endif

  // InfoBubbleDelegate.
  virtual void InfoBubbleClosing(InfoBubble* info_bubble,
                                 bool closed_by_escape);
  virtual bool CloseOnEscape() { return true; }
  virtual bool FadeInOnShow() { return true; }

  // Whether we have already been activated.
  bool has_been_activated_;

  ScopedRunnableMethodFactory<FirstRunBubble> enable_window_method_factory_;

  // The view inside the FirstRunBubble.
  FirstRunBubbleViewBase* view_;

  DISALLOW_COPY_AND_ASSIGN(FirstRunBubble);
};

#endif  // CHROME_BROWSER_VIEWS_FIRST_RUN_BUBBLE_H_
