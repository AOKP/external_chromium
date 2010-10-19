// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/context_menu.h"

ContextMenuParams::ContextMenuParams() {
}

ContextMenuParams::ContextMenuParams(const WebKit::WebContextMenuData& data)
    : media_type(data.mediaType),
      x(data.mousePosition.x),
      y(data.mousePosition.y),
      link_url(data.linkURL),
      unfiltered_link_url(data.linkURL),
      src_url(data.srcURL),
      is_image_blocked(data.isImageBlocked),
      page_url(data.pageURL),
      frame_url(data.frameURL),
      media_flags(data.mediaFlags),
      selection_text(UTF16ToWideHack(data.selectedText)),
      misspelled_word(data.misspelledWord),
      spellcheck_enabled(data.isSpellCheckingEnabled),
      is_editable(data.isEditable),
#if defined(OS_MACOSX)
      writing_direction_default(data.writingDirectionDefault),
      writing_direction_left_to_right(data.writingDirectionLeftToRight),
      writing_direction_right_to_left(data.writingDirectionRightToLeft),
#endif  // OS_MACOSX
      edit_flags(data.editFlags),
      security_info(data.securityInfo),
      frame_charset(data.frameEncoding.utf8()) {
  for (size_t i = 0; i < data.customItems.size(); ++i)
    custom_items.push_back(WebMenuItem(data.customItems[i]));
}

ContextMenuParams::~ContextMenuParams() {
}
