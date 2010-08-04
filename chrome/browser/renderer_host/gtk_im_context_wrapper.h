// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_GTK_IM_CONTEXT_WRAPPER_H_
#define CHROME_BROWSER_RENDERER_HOST_GTK_IM_CONTEXT_WRAPPER_H_

#include <gdk/gdk.h>
#include <pango/pango-attributes.h>
#include <vector>

#include "base/basictypes.h"
#include "base/string16.h"
#include "testing/gtest/include/gtest/gtest_prod.h"
#include "third_party/WebKit/WebKit/chromium/public/WebCompositionUnderline.h"
#include "third_party/WebKit/WebKit/chromium/public/WebTextInputType.h"

namespace gfx {
class Rect;
}

class MenuGtk;
class RenderWidgetHostViewGtk;
struct NativeWebKeyboardEvent;
typedef struct _GtkIMContext GtkIMContext;
typedef struct _GtkWidget GtkWidget;

// This class is a convenience wrapper for GtkIMContext.
// It creates and manages two GtkIMContext instances, one is GtkIMMulticontext,
// for plain text input box, another is GtkIMContextSimple, for password input
// box.
//
// This class is in charge of dispatching key events to these two GtkIMContext
// instances and handling signals emitted by them. Key events then will be
// forwarded to renderer along with input method results via corresponding host
// view.
//
// This class is used solely by RenderWidgetHostViewGtk.
class GtkIMContextWrapper {
 public:
  explicit GtkIMContextWrapper(RenderWidgetHostViewGtk* host_view);
  ~GtkIMContextWrapper();

  // Processes a gdk key event received by |host_view|.
  void ProcessKeyEvent(GdkEventKey* event);

  void UpdateInputMethodState(WebKit::WebTextInputType type,
                              const gfx::Rect& caret_rect);
  void OnFocusIn();
  void OnFocusOut();

  void AppendInputMethodsContextMenu(MenuGtk* menu);

  void CancelComposition();

  void ConfirmComposition();

 private:
  // For unit tests.
  class GtkIMContextWrapperTest;
  FRIEND_TEST(GtkIMContextWrapperTest, ExtractCompositionInfo);

  // Check if a text needs commit by forwarding a char event instead of
  // by confirming as a composition text.
  bool NeedCommitByForwardingCharEvent();

  void ProcessFilteredKeyPressEvent(NativeWebKeyboardEvent* wke);
  void ProcessUnfilteredKeyPressEvent(NativeWebKeyboardEvent* wke);

  // Processes result returned from input method after filtering a key event.
  // |filtered| indicates if the key event was filtered by the input method.
  void ProcessInputMethodResult(const GdkEventKey* event, bool filtered);

  // Real code of "commit" signal handler.
  void HandleCommit(const string16& text);

  // Real code of "preedit-start" signal handler.
  void HandlePreeditStart();

  // Real code of "preedit-changed" signal handler.
  void HandlePreeditChanged(const gchar* text,
                            PangoAttrList* attrs,
                            int cursor_position);

  // Real code of "preedit-end" signal handler.
  void HandlePreeditEnd();

  // Real code of "realize" signal handler, used for setting im context's client
  // window.
  void HandleHostViewRealize(GtkWidget* widget);

  // Real code of "unrealize" signal handler, used for unsetting im context's
  // client window.
  void HandleHostViewUnrealize();

  // Signal handlers of GtkIMContext object.
  static void HandleCommitThunk(GtkIMContext* context, gchar* text,
                                GtkIMContextWrapper* self);
  static void HandlePreeditStartThunk(GtkIMContext* context,
                                      GtkIMContextWrapper* self);
  static void HandlePreeditChangedThunk(GtkIMContext* context,
                                        GtkIMContextWrapper* self);
  static void HandlePreeditEndThunk(GtkIMContext* context,
                                    GtkIMContextWrapper* self);

  // Signal handlers connecting to |host_view_|'s native view widget.
  static void HandleHostViewRealizeThunk(GtkWidget* widget,
                                         GtkIMContextWrapper* self);
  static void HandleHostViewUnrealizeThunk(GtkWidget* widget,
                                           GtkIMContextWrapper* self);

  // Extracts composition underlines, selection range and utf-16 text from given
  // utf-8 text, pango attributes and cursor position.
  static void ExtractCompositionInfo(
      const gchar* utf8_text,
      PangoAttrList* attrs,
      int cursor_position,
      string16* utf16_text,
      std::vector<WebKit::WebCompositionUnderline>* underlines,
      int* selection_start,
      int* selection_end);

  // The parent object.
  RenderWidgetHostViewGtk* host_view_;

  // The GtkIMContext object.
  // In terms of the DOM event specification Appendix A
  //   <http://www.w3.org/TR/DOM-Level-3-Events/keyset.html>,
  // GTK uses a GtkIMContext object for the following two purposes:
  //  1. Composing Latin characters (A.1.2), and;
  //  2. Composing CJK characters with an IME (A.1.3).
  // Many JavaScript pages assume composed Latin characters are dispatched to
  // their onkeypress() handlers but not dispatched CJK characters composed
  // with an IME. To emulate this behavior, we should monitor the status of
  // this GtkIMContext object and prevent sending Char events when a
  // GtkIMContext object sends a "commit" signal with the CJK characters
  // composed by an IME.
  GtkIMContext* context_;

  // A GtkIMContextSimple object, for supporting dead/compose keys when input
  // method is disabled, eg. in password input box.
  GtkIMContext* context_simple_;

  // Whether or not this widget is focused.
  bool is_focused_;

  // Whether or not the above GtkIMContext is composing a text with an IME.
  // This flag is used in "commit" signal handler of the GtkIMContext object,
  // which determines how to submit the result text to WebKit according to this
  // flag.
  // If this flag is true or there are more than one characters in the result,
  // then the result text will be committed to WebKit as a confirmed
  // composition. Otherwise, it'll be forwarded as a key event.
  //
  // The GtkIMContext object sends a "preedit_start" before it starts composing
  // a text and a "preedit_end" signal after it finishes composing it.
  // "preedit_start" signal is monitored to turn it on.
  // We don't monitor "preedit_end" signal to turn it off, because an input
  // method may fire "preedit_end" signal before "commit" signal.
  // A buggy input method may not fire "preedit_start" and/or "preedit_end"
  // at all, so this flag will also be set to true when "preedit_changed" signal
  // is fired with non-empty preedit text.
  bool is_composing_text_;

  // Whether or not the IME is enabled.
  bool is_enabled_;

  // Whether or not it's currently running inside key event handler.
  // If it's true, then preedit-changed and commit handler will backup the
  // preedit or commit text instead of sending them down to webkit.
  // key event handler will send them later.
  bool is_in_key_event_handler_;

  // Stores a copy of the most recent preedit text retrieved from context_.
  string16 preedit_text_;

  // Stores the selection range in the stored preedit text.
  int preedit_selection_start_;
  int preedit_selection_end_;

  // Stores composition underlines computed from the pango attributes of the
  // most recent preedit text.
  std::vector<WebKit::WebCompositionUnderline> preedit_underlines_;

  // Whether or not the preedit has been changed since last key event.
  bool is_preedit_changed_;

  // Stores a copy of the most recent commit text received by commit signal
  // handler.
  string16 commit_text_;

  DISALLOW_COPY_AND_ASSIGN(GtkIMContextWrapper);
};

#endif  // CHROME_BROWSER_RENDERER_HOST_GTK_IM_CONTEXT_WRAPPER_H_
