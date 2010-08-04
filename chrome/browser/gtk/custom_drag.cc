// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/gtk/custom_drag.h"

#include "app/gtk_dnd_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/gtk/bookmark_utils_gtk.h"
#include "gfx/gtk_util.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_util.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace {

const int kDownloadItemCodeMask = gtk_dnd_util::TEXT_URI_LIST |
                                  gtk_dnd_util::CHROME_NAMED_URL;
const GdkDragAction kDownloadItemDragAction = GDK_ACTION_COPY;
const GdkDragAction kBookmarkDragAction =
    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE);

void OnDragDataGetForDownloadItem(GtkSelectionData* selection_data,
                                  guint target_type,
                                  const DownloadItem* download_item) {
  GURL url = net::FilePathToFileURL(download_item->full_path());
  gtk_dnd_util::WriteURLWithName(selection_data, url,
      UTF8ToUTF16(download_item->GetFileName().value()), target_type);
}

void OnDragDataGetStandalone(GtkWidget* widget, GdkDragContext* context,
                             GtkSelectionData* selection_data,
                             guint target_type, guint time,
                             const DownloadItem* item) {
  OnDragDataGetForDownloadItem(selection_data, target_type, item);
}

}  // namespace

// CustomDrag ------------------------------------------------------------------

CustomDrag::CustomDrag(SkBitmap* icon, int code_mask, GdkDragAction action)
    : drag_widget_(gtk_invisible_new()),
      pixbuf_(icon ? gfx::GdkPixbufFromSkBitmap(icon) : NULL) {
  g_object_ref_sink(drag_widget_);
  g_signal_connect(drag_widget_, "drag-data-get",
                   G_CALLBACK(OnDragDataGetThunk), this);
  g_signal_connect(drag_widget_, "drag-begin",
                   G_CALLBACK(OnDragBeginThunk), this);
  g_signal_connect(drag_widget_, "drag-end",
                   G_CALLBACK(OnDragEndThunk), this);

  GtkTargetList* list = gtk_dnd_util::GetTargetListFromCodeMask(code_mask);
  GdkEvent* event = gtk_get_current_event();
  gtk_drag_begin(drag_widget_, list, action, 1, event);
  if (event)
    gdk_event_free(event);
  gtk_target_list_unref(list);
}

CustomDrag::~CustomDrag() {
  if (pixbuf_)
    g_object_unref(pixbuf_);
  g_object_unref(drag_widget_);
}

void CustomDrag::OnDragBegin(GtkWidget* widget, GdkDragContext* drag_context) {
  if (pixbuf_)
    gtk_drag_set_icon_pixbuf(drag_context, pixbuf_, 0, 0);
}

void CustomDrag::OnDragEnd(GtkWidget* widget, GdkDragContext* drag_context) {
  delete this;
}

// DownloadItemDrag ------------------------------------------------------------

DownloadItemDrag::DownloadItemDrag(const DownloadItem* item,
                                   SkBitmap* icon)
    : CustomDrag(icon, kDownloadItemCodeMask, kDownloadItemDragAction),
      download_item_(item) {
}

DownloadItemDrag::~DownloadItemDrag() {
}

void DownloadItemDrag::OnDragDataGet(
    GtkWidget* widget, GdkDragContext* context,
    GtkSelectionData* selection_data,
    guint target_type, guint time) {
  OnDragDataGetForDownloadItem(selection_data, target_type, download_item_);
}

// static
void DownloadItemDrag::SetSource(GtkWidget* widget,
                                 DownloadItem* item,
                                 SkBitmap* icon) {
  gtk_drag_source_set(widget, GDK_BUTTON1_MASK, NULL, 0,
                      kDownloadItemDragAction);
  gtk_dnd_util::SetSourceTargetListFromCodeMask(widget, kDownloadItemCodeMask);

  // Disconnect previous signal handlers, if any.
  g_signal_handlers_disconnect_by_func(
      widget,
      reinterpret_cast<gpointer>(OnDragDataGetStandalone),
      item);
  // Connect new signal handlers.
  g_signal_connect(widget, "drag-data-get",
                   G_CALLBACK(OnDragDataGetStandalone), item);

  GdkPixbuf* pixbuf = icon ? gfx::GdkPixbufFromSkBitmap(icon) : NULL;
  if (pixbuf) {
    gtk_drag_source_set_icon_pixbuf(widget, pixbuf);
    g_object_unref(pixbuf);
  }
}

// static
void DownloadItemDrag::BeginDrag(const DownloadItem* item, SkBitmap* icon) {
  new DownloadItemDrag(item, icon);
}

// BookmarkDrag ----------------------------------------------------------------

BookmarkDrag::BookmarkDrag(Profile* profile,
                           const std::vector<const BookmarkNode*>& nodes)
    : CustomDrag(NULL,
                 bookmark_utils::GetCodeMask(false),
                 kBookmarkDragAction),
      profile_(profile),
      nodes_(nodes) {
}

BookmarkDrag::~BookmarkDrag() {
}

void BookmarkDrag::OnDragDataGet(GtkWidget* widget, GdkDragContext* context,
                                 GtkSelectionData* selection_data,
                                 guint target_type, guint time) {
  bookmark_utils::WriteBookmarksToSelection(nodes_, selection_data,
                                            target_type, profile_);
}

// static
void BookmarkDrag::BeginDrag(Profile* profile,
                             const std::vector<const BookmarkNode*>& nodes) {
  new BookmarkDrag(profile, nodes);
}

