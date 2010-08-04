// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/js_modal_dialog.h"

#include <gtk/gtk.h>

#include "app/gtk_util.h"
#include "app/l10n_util.h"
#include "app/message_box_flags.h"
#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/gtk/gtk_util.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"

namespace {

// We stash pointers to widgets on the gtk_dialog so we can refer to them
// after dialog creation.
const char kPromptTextId[] = "chrome_prompt_text";
const char kSuppressCheckboxId[] = "chrome_suppress_checkbox";

// If there's a text entry in the dialog, get the text from the first one and
// return it.
std::wstring GetPromptText(GtkDialog* dialog) {
  GtkWidget* widget = static_cast<GtkWidget*>(
      g_object_get_data(G_OBJECT(dialog), kPromptTextId));
  if (widget)
    return UTF8ToWide(gtk_entry_get_text(GTK_ENTRY(widget)));
  return std::wstring();
}

// If there's a toggle button in the dialog, return the toggled state.
// Otherwise, return false.
bool ShouldSuppressJSDialogs(GtkDialog* dialog) {
  GtkWidget* widget = static_cast<GtkWidget*>(
      g_object_get_data(G_OBJECT(dialog), kSuppressCheckboxId));
  if (widget)
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  return false;
}

}  // namespace

void JavaScriptAppModalDialog::CreateAndShowDialog() {
  dialog_ = CreateNativeDialog();
  gtk_util::ShowModalDialogWithMinLocalizedWidth(GTK_WIDGET(dialog_),
      IDS_ALERT_DIALOG_WIDTH_CHARS);
}

void JavaScriptAppModalDialog::HandleDialogResponse(GtkDialog* dialog,
                                                    gint response_id) {
  switch (response_id) {
    case GTK_RESPONSE_OK:
      // The first arg is the prompt text and the second is true if we want to
      // suppress additional popups from the page.
      OnAccept(GetPromptText(dialog), ShouldSuppressJSDialogs(dialog));
      break;

    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_DELETE_EVENT:   // User hit the X on the dialog.
      OnCancel();
      break;

    default:
      NOTREACHED();
  }
  gtk_widget_destroy(GTK_WIDGET(dialog));

  // Now that the dialog is gone, we can put all the  windows into separate
  // window groups so other dialogs are no longer app modal.
  gtk_util::AppModalDismissedUngroupWindows();
  delete this;
}

int JavaScriptAppModalDialog::GetDialogButtons() {
  switch (dialog_flags_) {
    case MessageBoxFlags::kIsJavascriptAlert:
      return MessageBoxFlags::DIALOGBUTTON_OK;

    case MessageBoxFlags::kIsJavascriptConfirm:
      return MessageBoxFlags::DIALOGBUTTON_OK |
             MessageBoxFlags::DIALOGBUTTON_CANCEL;

    case MessageBoxFlags::kIsJavascriptPrompt:
      return MessageBoxFlags::DIALOGBUTTON_OK;

    default:
      NOTREACHED();
      return 0;
  }
}

void JavaScriptAppModalDialog::AcceptWindow() {
  HandleDialogResponse(GTK_DIALOG(dialog_), GTK_RESPONSE_OK);
}

void JavaScriptAppModalDialog::CancelWindow() {
  HandleDialogResponse(GTK_DIALOG(dialog_), GTK_RESPONSE_CANCEL);
}

NativeDialog JavaScriptAppModalDialog::CreateNativeDialog() {
  GtkButtonsType buttons = GTK_BUTTONS_NONE;
  GtkMessageType message_type = GTK_MESSAGE_OTHER;
  // We add in the OK button manually later because we want to focus it
  // explicitly.
  switch (dialog_flags_) {
    case MessageBoxFlags::kIsJavascriptAlert:
      buttons = GTK_BUTTONS_NONE;
      message_type = GTK_MESSAGE_WARNING;
      break;

    case MessageBoxFlags::kIsJavascriptConfirm:
      if (is_before_unload_dialog_) {
        // onbeforeunload also uses a confirm prompt, it just has custom
        // buttons.  We add the buttons using gtk_dialog_add_button below.
        buttons = GTK_BUTTONS_NONE;
      } else {
        buttons = GTK_BUTTONS_CANCEL;
      }
      message_type = GTK_MESSAGE_QUESTION;
      break;

    case MessageBoxFlags::kIsJavascriptPrompt:
      buttons = GTK_BUTTONS_CANCEL;
      message_type = GTK_MESSAGE_QUESTION;
      break;

    default:
      NOTREACHED();
  }

  // We want the alert to be app modal so put all the browser windows into the
  // same window group.
  gtk_util::MakeAppModalWindowGroup();

  gfx::NativeWindow window = client_->GetMessageBoxRootWindow();
  NativeDialog dialog = gtk_message_dialog_new(window, GTK_DIALOG_MODAL,
      message_type, buttons, "%s", WideToUTF8(message_text_).c_str());
  gtk_util::ApplyMessageDialogQuirks(dialog);
  gtk_window_set_title(GTK_WINDOW(dialog), WideToUTF8(title_).c_str());

  // Adjust content area as needed.  Set up the prompt text entry or
  // suppression check box.
  if (MessageBoxFlags::kIsJavascriptPrompt == dialog_flags_) {
    // TODO(tc): Replace with gtk_dialog_get_content_area() when using GTK 2.14+
    GtkWidget* contents_vbox = GTK_DIALOG(dialog)->vbox;
    GtkWidget* text_box = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(text_box),
                       WideToUTF8(default_prompt_text_).c_str());
    gtk_box_pack_start(GTK_BOX(contents_vbox), text_box, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(dialog), kPromptTextId, text_box);
    gtk_entry_set_activates_default(GTK_ENTRY(text_box), TRUE);
  }

  if (display_suppress_checkbox_) {
    GtkWidget* contents_vbox = GTK_DIALOG(dialog)->vbox;
    GtkWidget* check_box = gtk_check_button_new_with_label(
        l10n_util::GetStringUTF8(
            IDS_JAVASCRIPT_MESSAGEBOX_SUPPRESS_OPTION).c_str());
    gtk_box_pack_start(GTK_BOX(contents_vbox), check_box, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(dialog), kSuppressCheckboxId, check_box);
  }

  // Adjust buttons/action area as needed.
  if (is_before_unload_dialog_) {
    std::string button_text = l10n_util::GetStringUTF8(
        IDS_BEFOREUNLOAD_MESSAGEBOX_OK_BUTTON_LABEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), button_text.c_str(),
                          GTK_RESPONSE_OK);

    button_text = l10n_util::GetStringUTF8(
        IDS_BEFOREUNLOAD_MESSAGEBOX_CANCEL_BUTTON_LABEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), button_text.c_str(),
                          GTK_RESPONSE_CANCEL);
  } else {
    // Add the OK button and focus it.
    GtkWidget* ok_button = gtk_dialog_add_button(GTK_DIALOG(dialog),
        GTK_STOCK_OK, GTK_RESPONSE_OK);
    if (MessageBoxFlags::kIsJavascriptPrompt != dialog_flags_)
      gtk_widget_grab_focus(ok_button);
  }

  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
  g_signal_connect(dialog, "response",
                   G_CALLBACK(AppModalDialog::OnDialogResponse),
                   reinterpret_cast<AppModalDialog*>(this));
  return dialog;
}

