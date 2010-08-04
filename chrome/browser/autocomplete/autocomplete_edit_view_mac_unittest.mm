// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/autocomplete/autocomplete_edit_view_mac.h"

#include "app/clipboard/clipboard.h"
#include "app/clipboard/scoped_clipboard_writer.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "gfx/size.h"
#include "testing/platform_test.h"

namespace {

TEST(AutocompleteEditViewMacTest, GetClipboardText) {
  Clipboard clipboard;
  std::wstring text;

  // Does an empty clipboard get empty text?
  clipboard.WriteObjects(Clipboard::ObjectMap());
  text = AutocompleteEditViewMac::GetClipboardText(&clipboard);
  EXPECT_EQ(std::wstring(), text);

  const string16 plainText(ASCIIToUTF16("test text"));
  const std::string url("http://www.example.com/");
  const string16 title(ASCIIToUTF16("The Example Company")), title_result;

  // Can we pull straight text off the clipboard?
  {
    ScopedClipboardWriter clipboard_writer(&clipboard);
    clipboard_writer.WriteText(plainText);
  }

  text = AutocompleteEditViewMac::GetClipboardText(&clipboard);
  EXPECT_EQ(UTF16ToWide(plainText), text);

  // Can we pull a bookmark off the clipboard?
  {
    ScopedClipboardWriter clipboard_writer(&clipboard);
    clipboard_writer.WriteBookmark(title, url);
  }

  text = AutocompleteEditViewMac::GetClipboardText(&clipboard);
  EXPECT_EQ(ASCIIToWide(url), text);

  // Do we pull text in preference to a bookmark?
  {
    ScopedClipboardWriter clipboard_writer(&clipboard);
    clipboard_writer.WriteText(plainText);
    clipboard_writer.WriteBookmark(title, url);
  }

  text = AutocompleteEditViewMac::GetClipboardText(&clipboard);
  EXPECT_EQ(UTF16ToWide(plainText), text);

  // Do we get nothing if there is neither text nor a bookmark?
  {
    const string16 markup(ASCIIToUTF16("<strong>Hi!</string>"));
    ScopedClipboardWriter clipboard_writer(&clipboard);
    clipboard_writer.WriteHTML(markup, url);
  }

  text = AutocompleteEditViewMac::GetClipboardText(&clipboard);
  EXPECT_TRUE(text.empty());
}

TEST(AutocompleteEditViewMacTest, GetFieldFont) {
  EXPECT_TRUE(AutocompleteEditViewMac::GetFieldFont());
}

}  // namespace
