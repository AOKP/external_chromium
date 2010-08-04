// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GTK_BOOKMARK_BAR_INSTRUCTIONS_GTK_CC_
#define CHROME_BROWSER_GTK_BOOKMARK_BAR_INSTRUCTIONS_GTK_CC_

#include "chrome/browser/gtk/bookmark_bar_instructions_gtk.h"

#include "app/l10n_util.h"
#include "base/observer_list.h"
#include "chrome/browser/gtk/gtk_chrome_link_button.h"
#include "chrome/browser/gtk/gtk_chrome_shrinkable_hbox.h"
#include "chrome/browser/gtk/gtk_theme_provider.h"
#include "chrome/browser/gtk/gtk_util.h"
#include "chrome/common/notification_service.h"
#include "grit/generated_resources.h"

namespace {

// Calculates the real size request of a label and set its ellipsize mode to
// PANGO_ELLIPSIZE_END.
// It must be done when the label is mapped (become visible on the screen),
// to make sure the pango can get correct font information for the calculation.
void InitLabelSizeRequestAndEllipsizeMode(GtkWidget* label) {
  GtkRequisition size;
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_NONE);
  gtk_widget_set_size_request(label, -1, -1);
  gtk_widget_size_request(label, &size);
  gtk_widget_set_size_request(label, size.width, size.height);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
}

}  // namespace

BookmarkBarInstructionsGtk::BookmarkBarInstructionsGtk(Delegate* delegate,
                                                       Profile* profile)
    : delegate_(delegate),
      profile_(profile),
      theme_provider_(GtkThemeProvider::GetFrom(profile_)) {
  instructions_hbox_ = gtk_chrome_shrinkable_hbox_new(FALSE, FALSE, 0);
  gtk_widget_set_size_request(instructions_hbox_, 0, -1);

  instructions_label_ = gtk_label_new(
      l10n_util::GetStringUTF8(IDS_BOOKMARKS_NO_ITEMS).c_str());
  gtk_misc_set_alignment(GTK_MISC(instructions_label_), 0, 0.5);
  gtk_util::CenterWidgetInHBox(instructions_hbox_, instructions_label_,
                               false, 1);
  g_signal_connect(instructions_label_, "map",
                   G_CALLBACK(InitLabelSizeRequestAndEllipsizeMode),
                   NULL);

  instructions_link_ = gtk_chrome_link_button_new(
      l10n_util::GetStringUTF8(IDS_BOOKMARK_BAR_IMPORT_LINK).c_str());
  gtk_misc_set_alignment(
      GTK_MISC(GTK_CHROME_LINK_BUTTON(instructions_link_)->label), 0, 0.5);
  g_signal_connect(instructions_link_, "clicked",
                   G_CALLBACK(OnButtonClickThunk), this);
  gtk_util::SetButtonTriggersNavigation(instructions_link_);
  // Until we switch to vector graphics, force the font size.
  // 13.4px == 10pt @ 96dpi
  gtk_util::ForceFontSizePixels(
      GTK_CHROME_LINK_BUTTON(instructions_link_)->label, 13.4);
  gtk_util::CenterWidgetInHBox(instructions_hbox_, instructions_link_,
                               false, 6);
  g_signal_connect(GTK_CHROME_LINK_BUTTON(instructions_link_)->label, "map",
                   G_CALLBACK(InitLabelSizeRequestAndEllipsizeMode),
                   NULL);

  registrar_.Add(this, NotificationType::BROWSER_THEME_CHANGED,
                 NotificationService::AllSources());
  theme_provider_->InitThemesFor(this);
}

void BookmarkBarInstructionsGtk::Observe(NotificationType type,
                                         const NotificationSource& source,
                                         const NotificationDetails& details) {
  if (type == NotificationType::BROWSER_THEME_CHANGED)
    UpdateColors();
}

void BookmarkBarInstructionsGtk::OnButtonClick(GtkWidget* button) {
  delegate_->ShowImportDialog();
}

void BookmarkBarInstructionsGtk::UpdateColors() {
  gtk_chrome_link_button_set_use_gtk_theme(
      GTK_CHROME_LINK_BUTTON(instructions_link_),
      theme_provider_->UseGtkTheme());

  GdkColor bookmark_color = theme_provider_->GetGdkColor(
      BrowserThemeProvider::COLOR_BOOKMARK_TEXT);
  if (theme_provider_->UseGtkTheme()) {
    gtk_util::SetLabelColor(instructions_label_, NULL);
    gtk_chrome_link_button_set_normal_color(
        GTK_CHROME_LINK_BUTTON(instructions_link_), NULL);
  } else {
    gtk_util::SetLabelColor(instructions_label_, &bookmark_color);

    // When using a non-standard, non-gtk theme, we make the link color match
    // the bookmark text color. Otherwise, standard link blue can look very
    // bad for some dark themes.
    if (theme_provider_->GetColor(BrowserThemeProvider::COLOR_BOOKMARK_TEXT) ==
        BrowserThemeProvider::GetDefaultColor(
            BrowserThemeProvider::COLOR_BOOKMARK_TEXT)) {
      gtk_chrome_link_button_set_normal_color(
          GTK_CHROME_LINK_BUTTON(instructions_link_), NULL);
    } else {
      gtk_chrome_link_button_set_normal_color(
          GTK_CHROME_LINK_BUTTON(instructions_link_), &bookmark_color);
    }
  }
}

#endif  // CHROME_BROWSER_GTK_BOOKMARK_BAR_INSTRUCTIONS_GTK_CC_
