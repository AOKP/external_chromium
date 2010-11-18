// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete_edit_view_gtk.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <algorithm>

#include "app/l10n_util.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/autocomplete_edit.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_popup_model.h"
#include "chrome/browser/bookmarks/bookmark_drag_data.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/gtk/gtk_util.h"
#include "chrome/browser/gtk/view_id_util.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/toolbar_model.h"
#include "chrome/common/notification_service.h"
#include "gfx/font.h"
#include "gfx/gtk_util.h"
#include "gfx/skia_utils_gtk.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "third_party/undoview/undo_view.h"

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/views/autocomplete/autocomplete_popup_contents_view.h"
#include "chrome/browser/views/location_bar/location_bar_view.h"
#else
#include "chrome/browser/autocomplete/autocomplete_popup_view_gtk.h"
#include "chrome/browser/gtk/gtk_theme_provider.h"
#include "chrome/browser/gtk/location_bar_view_gtk.h"
#endif

namespace {

const gchar* kAutocompleteEditViewGtkKey = "__ACE_VIEW_GTK__";

const char kTextBaseColor[] = "#808080";
const char kSecureSchemeColor[] = "#079500";
const char kSecurityErrorSchemeColor[] = "#a20000";

const double kStrikethroughStrokeRed = 162.0 / 256.0;
const double kStrikethroughStrokeWidth = 2.0;

size_t GetUTF8Offset(const std::wstring& wide_text, size_t wide_text_offset) {
  return WideToUTF8(wide_text.substr(0, wide_text_offset)).size();
}

// Stores GTK+-specific state so it can be restored after switching tabs.
struct ViewState {
  explicit ViewState(const AutocompleteEditViewGtk::CharRange& selection_range)
      : selection_range(selection_range) {
  }

  // Range of selected text.
  AutocompleteEditViewGtk::CharRange selection_range;
};

struct AutocompleteEditState {
  AutocompleteEditState(const AutocompleteEditModel::State& model_state,
                        const ViewState& view_state)
      : model_state(model_state),
        view_state(view_state) {
  }

  const AutocompleteEditModel::State model_state;
  const ViewState view_state;
};

// Returns a lazily initialized property bag accessor for saving our state in a
// TabContents.
PropertyAccessor<AutocompleteEditState>* GetStateAccessor() {
  static PropertyAccessor<AutocompleteEditState> state;
  return &state;
}

// Set up style properties to override the default GtkTextView; if a theme has
// overridden some of these properties, an inner-line will be displayed inside
// the fake GtkTextEntry.
void SetEntryStyle() {
  static bool style_was_set = false;

  if (style_was_set)
    return;
  style_was_set = true;

  gtk_rc_parse_string(
      "style \"chrome-location-bar-entry\" {"
      "  xthickness = 0\n"
      "  ythickness = 0\n"
      "  GtkWidget::focus_padding = 0\n"
      "  GtkWidget::focus-line-width = 0\n"
      "  GtkWidget::interior_focus = 0\n"
      "  GtkWidget::internal-padding = 0\n"
      "  GtkContainer::border-width = 0\n"
      "}\n"
      "widget \"*chrome-location-bar-entry\" "
      "style \"chrome-location-bar-entry\"");
}

// Copied from GTK+. Called when we lose the primary selection. This will clear
// the selection in the text buffer.
void ClipboardSelectionCleared(GtkClipboard* clipboard,
                               gpointer data) {
  GtkTextIter insert;
  GtkTextIter selection_bound;
  GtkTextBuffer* buffer = GTK_TEXT_BUFFER(data);

  gtk_text_buffer_get_iter_at_mark(buffer, &insert,
                                   gtk_text_buffer_get_insert(buffer));
  gtk_text_buffer_get_iter_at_mark(buffer, &selection_bound,
                                   gtk_text_buffer_get_selection_bound(buffer));

  if (!gtk_text_iter_equal(&insert, &selection_bound)) {
    gtk_text_buffer_move_mark(buffer,
                              gtk_text_buffer_get_selection_bound(buffer),
                              &insert);
  }
}

}  // namespace

AutocompleteEditViewGtk::AutocompleteEditViewGtk(
    AutocompleteEditController* controller,
    ToolbarModel* toolbar_model,
    Profile* profile,
    CommandUpdater* command_updater,
    bool popup_window_mode,
#if defined(TOOLKIT_VIEWS)
    const views::View* location_bar)
#else
    GtkWidget* location_bar)
#endif
    : text_view_(NULL),
      tag_table_(NULL),
      text_buffer_(NULL),
      faded_text_tag_(NULL),
      secure_scheme_tag_(NULL),
      security_error_scheme_tag_(NULL),
      normal_text_tag_(NULL),
      instant_anchor_tag_(NULL),
      instant_view_(NULL),
      instant_mark_(NULL),
      model_(new AutocompleteEditModel(this, controller, profile)),
#if defined(TOOLKIT_VIEWS)
      popup_view_(new AutocompletePopupContentsView(
          gfx::Font(), this, model_.get(), profile, location_bar)),
#else
      popup_view_(new AutocompletePopupViewGtk(this, model_.get(), profile,
                                               location_bar)),
#endif
      controller_(controller),
      toolbar_model_(toolbar_model),
      command_updater_(command_updater),
      popup_window_mode_(popup_window_mode),
      security_level_(ToolbarModel::NONE),
      mark_set_handler_id_(0),
#if defined(OS_CHROMEOS)
      button_1_pressed_(false),
      text_selected_during_click_(false),
      text_view_focused_before_button_press_(false),
#endif
#if !defined(TOOLKIT_VIEWS)
      theme_provider_(GtkThemeProvider::GetFrom(profile)),
#endif
      enter_was_pressed_(false),
      tab_was_pressed_(false),
      paste_clipboard_requested_(false),
      enter_was_inserted_(false),
      enable_tab_to_search_(true),
      selection_suggested_(false),
      going_to_focus_(NULL) {
  model_->SetPopupModel(popup_view_->GetModel());
}

AutocompleteEditViewGtk::~AutocompleteEditViewGtk() {
  NotificationService::current()->Notify(
      NotificationType::AUTOCOMPLETE_EDIT_DESTROYED,
      Source<AutocompleteEditViewGtk>(this),
      NotificationService::NoDetails());

  // Explicitly teardown members which have a reference to us.  Just to be safe
  // we want them to be destroyed before destroying any other internal state.
  popup_view_.reset();
  model_.reset();

  // We own our widget and TextView related objects.
  if (alignment_.get()) {  // Init() has been called.
    alignment_.Destroy();
    g_object_unref(text_buffer_);
    g_object_unref(tag_table_);
    // The tags we created are owned by the tag_table, and should be destroyed
    // along with it.  We don't hold our own reference to them.
  }
}

void AutocompleteEditViewGtk::Init() {
  SetEntryStyle();

  // The height of the text view is going to change based on the font used.  We
  // don't want to stretch the height, and we want it vertically centered.
  alignment_.Own(gtk_alignment_new(0., 0.5, 1.0, 0.0));
  gtk_widget_set_name(alignment_.get(),
                      "chrome-autocomplete-edit-view");

  // The GtkTagTable and GtkTextBuffer are not initially unowned, so we have
  // our own reference when we create them, and we own them.  Adding them to
  // the other objects adds a reference; it doesn't adopt them.
  tag_table_ = gtk_text_tag_table_new();
  text_buffer_ = gtk_text_buffer_new(tag_table_);
  g_object_set_data(G_OBJECT(text_buffer_), kAutocompleteEditViewGtkKey, this);

  // We need to run this two handlers before undo manager's handlers, so that
  // text iterators modified by these handlers can be passed down to undo
  // manager's handlers.
  g_signal_connect(text_buffer_, "delete-range",
                   G_CALLBACK(&HandleDeleteRangeThunk), this);
  g_signal_connect(text_buffer_, "mark-set",
                   G_CALLBACK(&HandleMarkSetAlwaysThunk), this);

  text_view_ = gtk_undo_view_new(text_buffer_);
  if (popup_window_mode_)
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_), false);

  // One pixel left margin is necessary to make the cursor visible when UI
  // language direction is LTR but |text_buffer_|'s content direction is RTL.
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view_), 1);

  // See SetEntryStyle() comments.
  gtk_widget_set_name(text_view_, "chrome-location-bar-entry");

  // The text view was floating.  It will now be owned by the alignment.
  gtk_container_add(GTK_CONTAINER(alignment_.get()), text_view_);

  // Do not allow inserting tab characters when pressing Tab key, so that when
  // Tab key is pressed, |text_view_| will emit "move-focus" signal, which will
  // be intercepted by our own handler to trigger Tab to search feature when
  // necessary.
  gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(text_view_), FALSE);

  faded_text_tag_ = gtk_text_buffer_create_tag(text_buffer_,
      NULL, "foreground", kTextBaseColor, NULL);
  secure_scheme_tag_ = gtk_text_buffer_create_tag(text_buffer_,
      NULL, "foreground", kSecureSchemeColor, NULL);
  security_error_scheme_tag_ = gtk_text_buffer_create_tag(text_buffer_,
      NULL, "foreground", kSecurityErrorSchemeColor, NULL);
  normal_text_tag_ = gtk_text_buffer_create_tag(text_buffer_,
      NULL, "foreground", "#000000", NULL);

  // NOTE: This code used to connect to "changed", however this was fired too
  // often and during bad times (our own buffer changes?).  It works out much
  // better to listen to end-user-action, which should be fired whenever the
  // user makes some sort of change to the buffer.
  g_signal_connect(text_buffer_, "begin-user-action",
                   G_CALLBACK(&HandleBeginUserActionThunk), this);
  g_signal_connect(text_buffer_, "end-user-action",
                   G_CALLBACK(&HandleEndUserActionThunk), this);
  // We connect to key press and release for special handling of a few keys.
  g_signal_connect(text_view_, "key-press-event",
                   G_CALLBACK(&HandleKeyPressThunk), this);
  g_signal_connect(text_view_, "key-release-event",
                   G_CALLBACK(&HandleKeyReleaseThunk), this);
  g_signal_connect(text_view_, "button-press-event",
                   G_CALLBACK(&HandleViewButtonPressThunk), this);
  g_signal_connect(text_view_, "button-release-event",
                   G_CALLBACK(&HandleViewButtonReleaseThunk), this);
  g_signal_connect(text_view_, "focus-in-event",
                   G_CALLBACK(&HandleViewFocusInThunk), this);
  g_signal_connect(text_view_, "focus-out-event",
                   G_CALLBACK(&HandleViewFocusOutThunk), this);
  // NOTE: The GtkTextView documentation asks you not to connect to this
  // signal, but it is very convenient and clean for catching up/down.
  g_signal_connect(text_view_, "move-cursor",
                   G_CALLBACK(&HandleViewMoveCursorThunk), this);
  g_signal_connect(text_view_, "move-focus",
                   G_CALLBACK(&HandleViewMoveFocusThunk), this);
  // Override the size request.  We want to keep the original height request
  // from the widget, since that's font dependent.  We want to ignore the width
  // so we don't force a minimum width based on the text length.
  g_signal_connect(text_view_, "size-request",
                   G_CALLBACK(&HandleViewSizeRequestThunk), this);
  g_signal_connect(text_view_, "populate-popup",
                   G_CALLBACK(&HandlePopulatePopupThunk), this);
  mark_set_handler_id_ = g_signal_connect(
      text_buffer_, "mark-set", G_CALLBACK(&HandleMarkSetThunk), this);
  mark_set_handler_id2_ = g_signal_connect_after(
      text_buffer_, "mark-set", G_CALLBACK(&HandleMarkSetAfterThunk), this);
  g_signal_connect(text_view_, "drag-data-received",
                   G_CALLBACK(&HandleDragDataReceivedThunk), this);
  // Override the text_view_'s default drag-data-get handler by calling our own
  // version after the normal call has happened.
  g_signal_connect_after(text_view_, "drag-data-get",
                   G_CALLBACK(&HandleDragDataGetThunk), this);
  g_signal_connect(text_view_, "backspace",
                   G_CALLBACK(&HandleBackSpaceThunk), this);
  g_signal_connect(text_view_, "copy-clipboard",
                   G_CALLBACK(&HandleCopyClipboardThunk), this);
  g_signal_connect(text_view_, "cut-clipboard",
                   G_CALLBACK(&HandleCutClipboardThunk), this);
  g_signal_connect(text_view_, "paste-clipboard",
                   G_CALLBACK(&HandlePasteClipboardThunk), this);
  g_signal_connect_after(text_view_, "expose-event",
                         G_CALLBACK(&HandleExposeEventThunk), this);
  g_signal_connect(text_view_, "direction-changed",
                   G_CALLBACK(&HandleWidgetDirectionChangedThunk), this);
  g_signal_connect(text_view_, "delete-from-cursor",
                   G_CALLBACK(&HandleDeleteFromCursorThunk), this);
  g_signal_connect(text_view_, "hierarchy-changed",
                   G_CALLBACK(&HandleHierarchyChangedThunk), this);
#if GTK_CHECK_VERSION(2, 20, 0)
  g_signal_connect(text_view_, "preedit-changed",
                   G_CALLBACK(&HandlePreeditChangedThunk), this);
#endif
  g_signal_connect(text_view_, "undo", G_CALLBACK(&HandleUndoRedoThunk), this);
  g_signal_connect(text_view_, "redo", G_CALLBACK(&HandleUndoRedoThunk), this);
  g_signal_connect_after(text_view_, "undo",
                         G_CALLBACK(&HandleUndoRedoAfterThunk), this);
  g_signal_connect_after(text_view_, "redo",
                         G_CALLBACK(&HandleUndoRedoAfterThunk), this);

  // Setup for the Instant suggestion text view.
  // GtkLabel is used instead of GtkTextView to get transparent background.
  instant_view_ = gtk_label_new(NULL);

  GtkTextIter end_iter;
  gtk_text_buffer_get_end_iter(text_buffer_, &end_iter);

  // Insert a Zero Width Space character just before the instant anchor.
  // It's a hack to workaround a bug of GtkTextView which can not align the
  // preedit string and a child anchor correctly when there is no other content
  // around the preedit string.
  gtk_text_buffer_insert(text_buffer_, &end_iter, "\342\200\213", -1);
  GtkTextChildAnchor* instant_anchor =
      gtk_text_buffer_create_child_anchor(text_buffer_, &end_iter);

  gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(text_view_),
                                    instant_view_,
                                    instant_anchor);

  instant_anchor_tag_ = gtk_text_buffer_create_tag(text_buffer_, NULL, NULL);

  GtkTextIter anchor_iter;
  gtk_text_buffer_get_iter_at_child_anchor(text_buffer_, &anchor_iter,
                                           instant_anchor);
  gtk_text_buffer_apply_tag(text_buffer_, instant_anchor_tag_,
                            &anchor_iter, &end_iter);

  GtkTextIter start_iter;
  gtk_text_buffer_get_start_iter(text_buffer_, &start_iter);
  instant_mark_ =
      gtk_text_buffer_create_mark(text_buffer_, NULL, &start_iter, FALSE);

  // Hooking up this handler after setting up above hacks for Instant view, so
  // that we won't filter out the special ZWP mark itself.
  g_signal_connect(text_buffer_, "insert-text",
                   G_CALLBACK(&HandleInsertTextThunk), this);

  AdjustVerticalAlignmentOfInstantView();

#if !defined(TOOLKIT_VIEWS)
  registrar_.Add(this,
                 NotificationType::BROWSER_THEME_CHANGED,
                 NotificationService::AllSources());
  theme_provider_->InitThemesFor(this);
#else
  // Manually invoke SetBaseColor() because TOOLKIT_VIEWS doesn't observe
  // themes.
  SetBaseColor();
#endif

  ViewIDUtil::SetID(GetNativeView(), VIEW_ID_AUTOCOMPLETE);
}

void AutocompleteEditViewGtk::HandleHierarchyChanged(
    GtkWidget* sender, GtkWidget* old_toplevel) {
  GtkWindow* new_toplevel = platform_util::GetTopLevel(sender);
  if (!new_toplevel)
    return;

  // Use |signals_| to make sure we don't get called back after destruction.
  signals_.Connect(new_toplevel, "set-focus",
                   G_CALLBACK(&HandleWindowSetFocusThunk), this);
}

void AutocompleteEditViewGtk::SetFocus() {
  gtk_widget_grab_focus(text_view_);
}

int AutocompleteEditViewGtk::TextWidth() {
  int horizontal_border_size =
      gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(text_view_),
                                           GTK_TEXT_WINDOW_LEFT) +
      gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(text_view_),
                                           GTK_TEXT_WINDOW_RIGHT) +
      gtk_text_view_get_left_margin(GTK_TEXT_VIEW(text_view_)) +
      gtk_text_view_get_right_margin(GTK_TEXT_VIEW(text_view_));

  GtkTextIter start, end;
  GdkRectangle first_char_bounds, last_char_bounds;
  gtk_text_buffer_get_start_iter(text_buffer_, &start);

  // Use the real end iterator here to take the width of instant suggestion
  // text into account, so that location bar can layout its children correctly.
  gtk_text_buffer_get_end_iter(text_buffer_, &end);
  gtk_text_view_get_iter_location(GTK_TEXT_VIEW(text_view_),
                                  &start, &first_char_bounds);
  gtk_text_view_get_iter_location(GTK_TEXT_VIEW(text_view_),
                                  &end, &last_char_bounds);
  return ((last_char_bounds.x > first_char_bounds.x) ?
          (last_char_bounds.x + last_char_bounds.width - first_char_bounds.x) :
          (first_char_bounds.x - last_char_bounds.x + last_char_bounds.width)) +
      horizontal_border_size;
}

int AutocompleteEditViewGtk::WidthOfTextAfterCursor() {
  // Not used.
  return -1;
}

gfx::Font AutocompleteEditViewGtk::GetFont() {
  GtkRcStyle* rc_style = gtk_widget_get_modifier_style(text_view_);
  return gfx::Font((rc_style && rc_style->font_desc) ?
                   rc_style->font_desc : text_view_->style->font_desc);
}

void AutocompleteEditViewGtk::SaveStateToTab(TabContents* tab) {
  DCHECK(tab);
  // If any text has been selected, register it as the PRIMARY selection so it
  // can still be pasted via middle-click after the text view is cleared.
  if (!selected_text_.empty())
    SavePrimarySelection(selected_text_);
  // NOTE: GetStateForTabSwitch may affect GetSelection, so order is important.
  AutocompleteEditModel::State model_state = model_->GetStateForTabSwitch();
  GetStateAccessor()->SetProperty(
      tab->property_bag(),
      AutocompleteEditState(model_state, ViewState(GetSelection())));
}

void AutocompleteEditViewGtk::Update(const TabContents* contents) {
  // NOTE: We're getting the URL text here from the ToolbarModel.
  bool visibly_changed_permanent_text =
      model_->UpdatePermanentText(toolbar_model_->GetText());

  ToolbarModel::SecurityLevel security_level =
        toolbar_model_->GetSecurityLevel();
  bool changed_security_level = (security_level != security_level_);
  security_level_ = security_level;

  if (contents) {
    selected_text_.clear();
    RevertAll();
    const AutocompleteEditState* state =
        GetStateAccessor()->GetProperty(contents->property_bag());
    if (state) {
      model_->RestoreState(state->model_state);

      // Move the marks for the cursor and the other end of the selection to
      // the previously-saved offsets (but preserve PRIMARY).
      StartUpdatingHighlightedText();
      SetSelectedRange(state->view_state.selection_range);
      FinishUpdatingHighlightedText();
    }
  } else if (visibly_changed_permanent_text) {
    RevertAll();
    // TODO(deanm): There should be code to restore select all here.
  } else if (changed_security_level) {
    EmphasizeURLComponents();
  }
}

void AutocompleteEditViewGtk::OpenURL(const GURL& url,
                                      WindowOpenDisposition disposition,
                                      PageTransition::Type transition,
                                      const GURL& alternate_nav_url,
                                      size_t selected_line,
                                      const std::wstring& keyword) {
  if (!url.is_valid())
    return;

  model_->OpenURL(url, disposition, transition, alternate_nav_url,
                  selected_line, keyword);
}

std::wstring AutocompleteEditViewGtk::GetText() const {
  GtkTextIter start, end;
  GetTextBufferBounds(&start, &end);
  gchar* utf8 = gtk_text_buffer_get_text(text_buffer_, &start, &end, false);
  std::wstring out(UTF8ToWide(utf8));
  g_free(utf8);

#if GTK_CHECK_VERSION(2, 20, 0)
  // We need to treat the text currently being composed by the input method as
  // part of the text content, so that omnibox can work correctly in the middle
  // of composition.
  if (preedit_.size()) {
    GtkTextMark* mark = gtk_text_buffer_get_insert(text_buffer_);
    gtk_text_buffer_get_iter_at_mark(text_buffer_, &start, mark);
    out.insert(gtk_text_iter_get_offset(&start), preedit_);
  }
#endif
  return out;
}

bool AutocompleteEditViewGtk::IsEditingOrEmpty() const {
  return model_->user_input_in_progress() || (GetTextLength() == 0);
}

int AutocompleteEditViewGtk::GetIcon() const {
  return IsEditingOrEmpty() ?
      AutocompleteMatch::TypeToIcon(model_->CurrentTextType()) :
      toolbar_model_->GetIcon();
}

void AutocompleteEditViewGtk::SetUserText(const std::wstring& text) {
  SetUserText(text, text, true);
}

void AutocompleteEditViewGtk::SetUserText(const std::wstring& text,
                                          const std::wstring& display_text,
                                          bool update_popup) {
  model_->SetUserText(text);
  // TODO(deanm): something about selection / focus change here.
  SetWindowTextAndCaretPos(display_text, display_text.length());
  if (update_popup)
    UpdatePopup();
  TextChanged();
}

void AutocompleteEditViewGtk::SetWindowTextAndCaretPos(const std::wstring& text,
                                                       size_t caret_pos) {
  CharRange range(static_cast<int>(caret_pos), static_cast<int>(caret_pos));
  SetTextAndSelectedRange(text, range);
}

void AutocompleteEditViewGtk::SetForcedQuery() {
  const std::wstring current_text(GetText());
  const size_t start = current_text.find_first_not_of(kWhitespaceWide);
  if (start == std::wstring::npos || (current_text[start] != '?')) {
    SetUserText(L"?");
  } else {
    StartUpdatingHighlightedText();
    SetSelectedRange(CharRange(current_text.size(), start + 1));
    FinishUpdatingHighlightedText();
  }
}

bool AutocompleteEditViewGtk::IsSelectAll() {
  GtkTextIter sel_start, sel_end;
  gtk_text_buffer_get_selection_bounds(text_buffer_, &sel_start, &sel_end);

  GtkTextIter start, end;
  GetTextBufferBounds(&start, &end);

  // Returns true if the |text_buffer_| is empty.
  return gtk_text_iter_equal(&start, &sel_start) &&
      gtk_text_iter_equal(&end, &sel_end);
}

void AutocompleteEditViewGtk::GetSelectionBounds(std::wstring::size_type* start,
                                                 std::wstring::size_type* end) {
  CharRange selection = GetSelection();
  *start = static_cast<size_t>(selection.cp_min);
  *end = static_cast<size_t>(selection.cp_max);
}

void AutocompleteEditViewGtk::SelectAll(bool reversed) {
  // SelectAll() is invoked as a side effect of other actions (e.g.  switching
  // tabs or hitting Escape) in autocomplete_edit.cc, so we don't update the
  // PRIMARY selection here.
  SelectAllInternal(reversed, false);
}

void AutocompleteEditViewGtk::RevertAll() {
  ClosePopup();
  model_->Revert();
  TextChanged();
}

void AutocompleteEditViewGtk::UpdatePopup() {
  model_->SetInputInProgress(true);
  if (!model_->has_focus())
    return;

  // Don't inline autocomplete when the caret/selection isn't at the end of
  // the text, or in the middle of composition.
  CharRange sel = GetSelection();
  bool no_inline_autocomplete =
      std::max(sel.cp_max, sel.cp_min) < GetTextLength();
#if GTK_CHECK_VERSION(2, 20, 0)
  no_inline_autocomplete = no_inline_autocomplete || preedit_.size();
#endif
  model_->StartAutocomplete(sel.cp_min != sel.cp_max, no_inline_autocomplete);
}

void AutocompleteEditViewGtk::ClosePopup() {
  if (popup_view_->GetModel()->IsOpen())
    controller_->OnAutocompleteWillClosePopup();

  popup_view_->GetModel()->StopAutocomplete();
}

void AutocompleteEditViewGtk::OnTemporaryTextMaybeChanged(
    const std::wstring& display_text,
    bool save_original_selection) {
  if (save_original_selection)
    saved_temporary_selection_ = GetSelection();

  StartUpdatingHighlightedText();
  SetWindowTextAndCaretPos(display_text, display_text.length());
  FinishUpdatingHighlightedText();
  TextChanged();
}

bool AutocompleteEditViewGtk::OnInlineAutocompleteTextMaybeChanged(
    const std::wstring& display_text,
    size_t user_text_length) {
  if (display_text == GetText())
    return false;

  StartUpdatingHighlightedText();
  CharRange range(display_text.size(), user_text_length);
  SetTextAndSelectedRange(display_text, range);
  FinishUpdatingHighlightedText();
  TextChanged();
  return true;
}

void AutocompleteEditViewGtk::OnRevertTemporaryText() {
  StartUpdatingHighlightedText();
  SetSelectedRange(saved_temporary_selection_);
  FinishUpdatingHighlightedText();
  TextChanged();
}

void AutocompleteEditViewGtk::OnBeforePossibleChange() {
  // If this change is caused by a paste clipboard action and all text is
  // selected, then call model_->on_paste_replacing_all() to prevent inline
  // autocomplete.
  if (paste_clipboard_requested_) {
    paste_clipboard_requested_ = false;
    if (IsSelectAll())
      model_->on_paste_replacing_all();
  }

  // Record our state.
  text_before_change_ = GetText();
  sel_before_change_ = GetSelection();
}

// TODO(deanm): This is mostly stolen from Windows, and will need some work.
bool AutocompleteEditViewGtk::OnAfterPossibleChange() {
  // If the change is caused by an Enter key press event, and the event was not
  // handled by IME, then it's an unexpected change and shall be reverted here.
  // {Start|Finish}UpdatingHighlightedText() are called here to prevent the
  // PRIMARY selection from being changed.
  if (enter_was_pressed_ && enter_was_inserted_) {
    StartUpdatingHighlightedText();
    SetTextAndSelectedRange(text_before_change_, sel_before_change_);
    FinishUpdatingHighlightedText();
    return false;
  }

  CharRange new_sel = GetSelection();
  int length = GetTextLength();
  bool selection_differs = (new_sel.cp_min != sel_before_change_.cp_min) ||
                           (new_sel.cp_max != sel_before_change_.cp_max);
  bool at_end_of_edit = (new_sel.cp_min == length && new_sel.cp_max == length);

  // See if the text or selection have changed since OnBeforePossibleChange().
  std::wstring new_text(GetText());
  text_changed_ = (new_text != text_before_change_);

  if (text_changed_)
    AdjustTextJustification();

  // When the user has deleted text, we don't allow inline autocomplete.  Make
  // sure to not flag cases like selecting part of the text and then pasting
  // (or typing) the prefix of that selection.  (We detect these by making
  // sure the caret, which should be after any insertion, hasn't moved
  // forward of the old selection start.)
  bool just_deleted_text =
      (text_before_change_.length() > new_text.length()) &&
      (new_sel.cp_min <= std::min(sel_before_change_.cp_min,
                                 sel_before_change_.cp_max));

  bool something_changed = model_->OnAfterPossibleChange(new_text,
      selection_differs, text_changed_, just_deleted_text, at_end_of_edit);

  // If only selection was changed, we don't need to call |controller_|'s
  // OnChanged() method, which is called in TextChanged().
  // But we still need to call EmphasizeURLComponents() to make sure the text
  // attributes are updated correctly.
  if (something_changed && text_changed_)
    TextChanged();
  else if (selection_differs)
    EmphasizeURLComponents();

  return something_changed;
}

gfx::NativeView AutocompleteEditViewGtk::GetNativeView() const {
  return alignment_.get();
}

CommandUpdater* AutocompleteEditViewGtk::GetCommandUpdater() {
  return command_updater_;
}

void AutocompleteEditViewGtk::Observe(NotificationType type,
                                      const NotificationSource& source,
                                      const NotificationDetails& details) {
  DCHECK(type == NotificationType::BROWSER_THEME_CHANGED);

  SetBaseColor();
}

void AutocompleteEditViewGtk::SetBaseColor() {
#if defined(TOOLKIT_VIEWS)
  bool use_gtk = false;
#else
  bool use_gtk = theme_provider_->UseGtkTheme();
#endif

  if (use_gtk) {
    gtk_widget_modify_cursor(text_view_, NULL, NULL);
    gtk_widget_modify_base(text_view_, GTK_STATE_NORMAL, NULL);
    gtk_widget_modify_base(text_view_, GTK_STATE_SELECTED, NULL);
    gtk_widget_modify_text(text_view_, GTK_STATE_SELECTED, NULL);
    gtk_widget_modify_base(text_view_, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_text(text_view_, GTK_STATE_ACTIVE, NULL);

    gtk_util::UndoForceFontSize(text_view_);
    gtk_util::UndoForceFontSize(instant_view_);

    // Grab the text colors out of the style and set our tags to use them.
    GtkStyle* style = gtk_rc_get_style(text_view_);

    // style may be unrealized at this point, so calculate the halfway point
    // between text[] and base[] manually instead of just using text_aa[].
    GdkColor average_color = gtk_util::AverageColors(
        style->text[GTK_STATE_NORMAL], style->base[GTK_STATE_NORMAL]);

    g_object_set(faded_text_tag_, "foreground-gdk", &average_color, NULL);
    g_object_set(normal_text_tag_, "foreground-gdk",
                 &style->text[GTK_STATE_NORMAL], NULL);

    // GtkLabel uses fg color instead of text color.
    gtk_widget_modify_fg(instant_view_, GTK_STATE_NORMAL, &average_color);
  } else {
    const GdkColor* background_color_ptr;
#if defined(TOOLKIT_VIEWS)
    const GdkColor background_color = gfx::SkColorToGdkColor(
        LocationBarView::GetColor(ToolbarModel::NONE,
                                  LocationBarView::BACKGROUND));
    background_color_ptr = &background_color;
#else
    background_color_ptr = &LocationBarViewGtk::kBackgroundColor;
#endif
    gtk_widget_modify_cursor(
        text_view_, &gtk_util::kGdkBlack, &gtk_util::kGdkGray);
    gtk_widget_modify_base(text_view_, GTK_STATE_NORMAL, background_color_ptr);

    GdkColor c;
#if !defined(TOOLKIT_VIEWS)
    // Override the selected colors so we don't leak colors from the current
    // gtk theme into the chrome-theme.
    c = gfx::SkColorToGdkColor(
        theme_provider_->get_active_selection_bg_color());
    gtk_widget_modify_base(text_view_, GTK_STATE_SELECTED, &c);

    c = gfx::SkColorToGdkColor(
        theme_provider_->get_active_selection_fg_color());
    gtk_widget_modify_text(text_view_, GTK_STATE_SELECTED, &c);

    c = gfx::SkColorToGdkColor(
        theme_provider_->get_inactive_selection_bg_color());
    gtk_widget_modify_base(text_view_, GTK_STATE_ACTIVE, &c);

    c = gfx::SkColorToGdkColor(
        theme_provider_->get_inactive_selection_fg_color());
    gtk_widget_modify_text(text_view_, GTK_STATE_ACTIVE, &c);
#endif

    gdk_color_parse(kTextBaseColor, &c);
    gtk_widget_modify_fg(instant_view_, GTK_STATE_NORMAL, &c);

    // Until we switch to vector graphics, force the font size.
    gtk_util::ForceFontSizePixels(text_view_,
        popup_window_mode_ ?
        browser_defaults::kAutocompleteEditFontPixelSizeInPopup :
        browser_defaults::kAutocompleteEditFontPixelSize);

    gtk_util::ForceFontSizePixels(instant_view_,
        popup_window_mode_ ?
        browser_defaults::kAutocompleteEditFontPixelSizeInPopup :
        browser_defaults::kAutocompleteEditFontPixelSize);

    g_object_set(faded_text_tag_, "foreground", kTextBaseColor, NULL);
    g_object_set(normal_text_tag_, "foreground", "#000000", NULL);
  }

  AdjustVerticalAlignmentOfInstantView();
}

void AutocompleteEditViewGtk::HandleBeginUserAction(GtkTextBuffer* sender) {
  OnBeforePossibleChange();
}

void AutocompleteEditViewGtk::HandleEndUserAction(GtkTextBuffer* sender) {
  OnAfterPossibleChange();
}

gboolean AutocompleteEditViewGtk::HandleKeyPress(GtkWidget* widget,
                                                 GdkEventKey* event) {
  // Background of this piece of complicated code:
  // The omnibox supports several special behaviors which may be triggered by
  // certain key events:
  // Tab to search - triggered by Tab key
  // Accept input - triggered by Enter key
  // Revert input - triggered by Escape key
  //
  // Because we use a GtkTextView object |text_view_| for text input, we need
  // send all key events to |text_view_| before handling them, to make sure
  // IME works without any problem. So here, we intercept "key-press-event"
  // signal of |text_view_| object and call its default handler to handle the
  // key event first.
  //
  // Then if the key event is one of Tab, Enter and Escape, we need to trigger
  // the corresponding special behavior if IME did not handle it.
  // For Escape key, if the default signal handler returns FALSE, then we know
  // it's not handled by IME.
  //
  // For Tab key, as "accepts-tab" property of |text_view_| is set to FALSE,
  // if IME did not handle it then "move-focus" signal will be emitted by the
  // default signal handler of |text_view_|. So we can intercept "move-focus"
  // signal of |text_view_| to know if a Tab key press event was handled by IME,
  // and trigger Tab to search behavior when necessary in the signal handler.
  //
  // But for Enter key, if IME did not handle the key event, the default signal
  // handler will delete current selection range and insert '\n' and always
  // return TRUE. We need to prevent |text_view_| from performing this default
  // action if IME did not handle the key event, because we don't want the
  // content of omnibox to be changed before triggering our special behavior.
  // Otherwise our special behavior would not be performed correctly.
  //
  // But there is no way for us to prevent GtkTextView from handling the key
  // event and performing built-in operation. So in order to achieve our goal,
  // "insert-text" signal of |text_buffer_| object is intercepted, and
  // following actions are done in the signal handler:
  // - If there is only one character in inserted text, and it's '\n' or '\r',
  //   then set |enter_was_inserted_| to true.
  // - Filter out all new line and tab characters.
  //
  // So if |enter_was_inserted_| is true after calling |text_view_|'s default
  // signal handler against an Enter key press event, then we know that the
  // Enter key press event was handled by GtkTextView rather than IME, and can
  // perform the special behavior for Enter key safely.
  //
  // Now the last thing is to prevent the content of omnibox from being changed
  // by GtkTextView when Enter key is pressed. As OnBeforePossibleChange() and
  // OnAfterPossibleChange() will be called by GtkTextView before and after
  // changing the content, and the content is already saved in
  // OnBeforePossibleChange(), so if the Enter key press event was not handled
  // by IME, it's easy to restore the content in OnAfterPossibleChange(), as if
  // it's not changed at all.

  GtkWidgetClass* klass = GTK_WIDGET_GET_CLASS(widget);

  enter_was_pressed_ = (event->keyval == GDK_Return ||
                        event->keyval == GDK_ISO_Enter ||
                        event->keyval == GDK_KP_Enter);

  // Set |tab_was_pressed_| to true if it's a Tab key press event, so that our
  // handler of "move-focus" signal can trigger Tab to search behavior when
  // necessary.
  tab_was_pressed_ = ((event->keyval == GDK_Tab ||
                       event->keyval == GDK_ISO_Left_Tab ||
                       event->keyval == GDK_KP_Tab) &&
                      !(event->state & GDK_CONTROL_MASK));

  // Reset |enter_was_inserted_|, which may be set in the "insert-text" signal
  // handler, so that we'll know if an Enter key event was handled by IME.
  enter_was_inserted_ = false;

  // Reset |paste_clipboard_requested_| to make sure we won't misinterpret this
  // key input action as a paste action.
  paste_clipboard_requested_ = false;

  // Reset |text_changed_| before passing the key event on to the text view.
  text_changed_ = false;

  // Call the default handler, so that IME can work as normal.
  // New line characters will be filtered out by our "insert-text"
  // signal handler attached to |text_buffer_| object.
  gboolean result = klass->key_press_event(widget, event);

  // Set |tab_was_pressed_| to false, to make sure Tab to search behavior can
  // only be triggered by pressing Tab key.
  tab_was_pressed_ = false;

  if (enter_was_pressed_ && enter_was_inserted_) {
    bool alt_held = (event->state & GDK_MOD1_MASK);
    model_->AcceptInput(alt_held ? NEW_FOREGROUND_TAB : CURRENT_TAB, false);
    result = TRUE;
  } else if (!result && event->keyval == GDK_Escape &&
             (event->state & gtk_accelerator_get_default_mod_mask()) == 0) {
    // We can handle the Escape key if |text_view_| did not handle it.
    // If it's not handled by us, then we need to propagate it up to the parent
    // widgets, so that Escape accelerator can still work.
    result = model_->OnEscapeKeyPressed();
  } else if (event->keyval == GDK_Control_L || event->keyval == GDK_Control_R) {
    // Omnibox2 can switch its contents while pressing a control key. To switch
    // the contents of omnibox2, we notify the AutocompleteEditModel class when
    // the control-key state is changed.
    model_->OnControlKeyChanged(true);
  } else if (!text_changed_ && event->keyval == GDK_Delete &&
             event->state & GDK_SHIFT_MASK) {
    // If shift+del didn't change the text, we let this delete an entry from
    // the popup.  We can't check to see if the IME handled it because even if
    // nothing is selected, the IME or the TextView still report handling it.
    AutocompletePopupModel* popup_model = popup_view_->GetModel();
    if (popup_model->IsOpen())
      popup_model->TryDeletingCurrentItem();
  }

  // Set |enter_was_pressed_| to false, to make sure OnAfterPossibleChange() can
  // act as normal for changes made by other events.
  enter_was_pressed_ = false;

  // If the key event is not handled by |text_view_| or us, then we need to
  // propagate the key event up to parent widgets by returning FALSE.
  // In this case we need to stop the signal emission explicitly to prevent the
  // default "key-press-event" handler of |text_view_| from being called again.
  if (!result) {
    static guint signal_id =
        g_signal_lookup("key-press-event", GTK_TYPE_WIDGET);
    g_signal_stop_emission(widget, signal_id, 0);
  }

  return result;
}

gboolean AutocompleteEditViewGtk::HandleKeyRelease(GtkWidget* widget,
                                                   GdkEventKey* event) {
  // Omnibox2 can switch its contents while pressing a control key. To switch
  // the contents of omnibox2, we notify the AutocompleteEditModel class when
  // the control-key state is changed.
  if (event->keyval == GDK_Control_L || event->keyval == GDK_Control_R) {
    // Round trip to query the control state after the release.  This allows
    // you to release one control key while still holding another control key.
    GdkDisplay* display = gdk_drawable_get_display(event->window);
    GdkModifierType mod;
    gdk_display_get_pointer(display, NULL, NULL, NULL, &mod);
    if (!(mod & GDK_CONTROL_MASK))
      model_->OnControlKeyChanged(false);
  }

  // Even though we handled the press ourselves, let GtkTextView handle the
  // release.  It shouldn't do anything particularly interesting, but it will
  // handle the IME work for us.
  return FALSE;  // Propagate into GtkTextView.
}

gboolean AutocompleteEditViewGtk::HandleViewButtonPress(GtkWidget* sender,
                                                        GdkEventButton* event) {
  // We don't need to care about double and triple clicks.
  if (event->type != GDK_BUTTON_PRESS)
    return FALSE;

  if (event->button == 1) {
#if defined(OS_CHROMEOS)
    // When the first button is pressed, track some stuff that will help us
    // determine whether we should select all of the text when the button is
    // released.
    button_1_pressed_ = true;
    text_view_focused_before_button_press_ = GTK_WIDGET_HAS_FOCUS(text_view_);
    text_selected_during_click_ = false;
#endif

    // Button press event may change the selection, we need to record the change
    // and report it to |model_| later when button is released.
    OnBeforePossibleChange();
  } else if (event->button == 2) {
    // GtkTextView pastes PRIMARY selection with middle click.
    // We can't call model_->on_paste_replacing_all() here, because the actual
    // paste clipboard action may not be performed if the clipboard is empty.
    paste_clipboard_requested_ = true;
  }
  return FALSE;
}

gboolean AutocompleteEditViewGtk::HandleViewButtonRelease(
    GtkWidget* sender, GdkEventButton* event) {
  if (event->button != 1)
    return FALSE;

#if defined(OS_CHROMEOS)
  button_1_pressed_ = false;
#endif

  // Call the GtkTextView default handler, ignoring the fact that it will
  // likely have told us to stop propagating.  We want to handle selection.
  GtkWidgetClass* klass = GTK_WIDGET_GET_CLASS(text_view_);
  klass->button_release_event(text_view_, event);

#if defined(OS_CHROMEOS)
  if (!text_view_focused_before_button_press_ && !text_selected_during_click_) {
    // If this was a focusing click and the user didn't drag to highlight any
    // text, select the full input and update the PRIMARY selection.
    SelectAllInternal(false, true);

    // So we told the buffer where the cursor should be, but make sure to tell
    // the view so it can scroll it to be visible if needed.
    // NOTE: This function doesn't seem to like a count of 0, looking at the
    // code it will skip an important loop.  Use -1 to achieve the same.
    GtkTextIter start, end;
    GetTextBufferBounds(&start, &end);
    gtk_text_view_move_visually(GTK_TEXT_VIEW(text_view_), &start, -1);
  }
#endif

  // Inform |model_| about possible text selection change.
  OnAfterPossibleChange();

  return TRUE;  // Don't continue, we called the default handler already.
}

gboolean AutocompleteEditViewGtk::HandleViewFocusIn(GtkWidget* sender,
                                                    GdkEventFocus* event) {
  GdkModifierType modifiers;
  gdk_window_get_pointer(text_view_->window, NULL, NULL, &modifiers);
  model_->OnSetFocus((modifiers & GDK_CONTROL_MASK) != 0);
  controller_->OnSetFocus();
  // TODO(deanm): Some keyword hit business, etc here.

  g_signal_connect(
      gdk_keymap_get_for_display(gtk_widget_get_display(text_view_)),
      "direction-changed",
      G_CALLBACK(&HandleKeymapDirectionChangedThunk), this);

  AdjustTextJustification();

  return FALSE;  // Continue propagation.
}

gboolean AutocompleteEditViewGtk::HandleViewFocusOut(GtkWidget* sender,
                                                     GdkEventFocus* event) {
  GtkWidget* view_getting_focus = NULL;
  GtkWindow* toplevel = platform_util::GetTopLevel(sender);
  if (gtk_window_is_active(toplevel))
    view_getting_focus = going_to_focus_;

  // This must be invoked before ClosePopup.
  controller_->OnAutocompleteLosingFocus(view_getting_focus);

  // Close the popup.
  ClosePopup();
  // Tell the model to reset itself.
  model_->OnKillFocus();
  controller_->OnKillFocus();

  g_signal_handlers_disconnect_by_func(
      gdk_keymap_get_for_display(gtk_widget_get_display(text_view_)),
      reinterpret_cast<gpointer>(&HandleKeymapDirectionChangedThunk), this);

  return FALSE;  // Pass the event on to the GtkTextView.
}

void AutocompleteEditViewGtk::HandleViewMoveCursor(
    GtkWidget* sender,
    GtkMovementStep step,
    gint count,
    gboolean extend_selection) {
  GtkTextIter sel_start, sel_end;
  gboolean has_selection =
      gtk_text_buffer_get_selection_bounds(text_buffer_, &sel_start, &sel_end);

  bool handled = true;

  // We want the GtkEntry behavior when you move the cursor while you have a
  // selection.  GtkTextView just drops the selection and moves the cursor, but
  // instead we want to move the cursor to the appropiate end of the selection.
  if (step == GTK_MOVEMENT_VISUAL_POSITIONS && !extend_selection) {
    if ((count == 1 || count == -1) && has_selection) {
      // We have a selection and start / end are in ascending order.
      // Cursor placement will remove the selection, so we need inform |model_|
      // about this change by calling On{Before|After}PossibleChange() methods.
      OnBeforePossibleChange();
      gtk_text_buffer_place_cursor(text_buffer_,
                                   count == 1 ? &sel_end : &sel_start);
      OnAfterPossibleChange();
    } else if (count == 1 && !has_selection) {
      gint cursor_pos;
      g_object_get(G_OBJECT(text_buffer_), "cursor-position", &cursor_pos,
                   NULL);
      if (cursor_pos == GetTextLength())
        controller_->OnCommitSuggestedText(GetText());
      else
        handled = false;
    } else {
      handled = false;
    }
  } else if (step == GTK_MOVEMENT_PAGES) {  // Page up and down.
    // Multiply by count for the direction (if we move too much that's ok).
    model_->OnUpOrDownKeyPressed(model_->result().size() * count);
  } else if (step == GTK_MOVEMENT_DISPLAY_LINES) {  // Arrow up and down.
    model_->OnUpOrDownKeyPressed(count);
  } else {
    handled = false;
  }

  if (!handled) {
    // Cursor movement may change the selection, we need to record the change
    // and report it to |model_|.
    if (has_selection || extend_selection)
      OnBeforePossibleChange();

    // Propagate into GtkTextView
    GtkTextViewClass* klass = GTK_TEXT_VIEW_GET_CLASS(text_view_);
    klass->move_cursor(GTK_TEXT_VIEW(text_view_), step, count,
                       extend_selection);

    if (has_selection || extend_selection)
      OnAfterPossibleChange();
  }

  // move-cursor doesn't use a signal accumulator on the return value (it
  // just ignores then), so we have to stop the propagation.
  static guint signal_id = g_signal_lookup("move-cursor", GTK_TYPE_TEXT_VIEW);
  g_signal_stop_emission(text_view_, signal_id, 0);
}

void AutocompleteEditViewGtk::HandleViewSizeRequest(GtkWidget* sender,
                                                    GtkRequisition* req) {
  // Don't force a minimum width, but use the font-relative height.  This is a
  // run-first handler, so the default handler was already called.
  req->width = 1;
}

void AutocompleteEditViewGtk::HandlePopulatePopup(GtkWidget* sender,
                                                  GtkMenu* menu) {
  GtkWidget* separator = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
  gtk_widget_show(separator);

  // Search Engine menu item.
  GtkWidget* search_engine_menuitem = gtk_menu_item_new_with_mnemonic(
      gfx::ConvertAcceleratorsFromWindowsStyle(
          l10n_util::GetStringUTF8(IDS_EDIT_SEARCH_ENGINES)).c_str());
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), search_engine_menuitem);
  g_signal_connect(search_engine_menuitem, "activate",
                   G_CALLBACK(HandleEditSearchEnginesThunk), this);
  gtk_widget_set_sensitive(search_engine_menuitem,
      command_updater_->IsCommandEnabled(IDC_EDIT_SEARCH_ENGINES));
  gtk_widget_show(search_engine_menuitem);

  // We need to update the paste and go controller before we know what text
  // to show. We could do this all asynchronously, but it would be elaborate
  // because we'd have to account for multiple menus showing, getting called
  // back after shutdown, and similar issues.
  GtkClipboard* x_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gchar* text = gtk_clipboard_wait_for_text(x_clipboard);
  std::wstring text_wstr = UTF8ToWide(text);
  g_free(text);

  // Paste and Go menu item.
  GtkWidget* paste_go_menuitem = gtk_menu_item_new_with_mnemonic(
      gfx::ConvertAcceleratorsFromWindowsStyle(
          l10n_util::GetStringUTF8(model_->is_paste_and_search() ?
              IDS_PASTE_AND_SEARCH : IDS_PASTE_AND_GO)).c_str());
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste_go_menuitem);
  g_signal_connect(paste_go_menuitem, "activate",
                   G_CALLBACK(HandlePasteAndGoThunk), this);
  gtk_widget_set_sensitive(paste_go_menuitem,
                           model_->CanPasteAndGo(text_wstr));
  gtk_widget_show(paste_go_menuitem);
}

void AutocompleteEditViewGtk::HandleEditSearchEngines(GtkWidget* sender) {
  command_updater_->ExecuteCommand(IDC_EDIT_SEARCH_ENGINES);
}

void AutocompleteEditViewGtk::HandlePasteAndGo(GtkWidget* sender) {
  model_->PasteAndGo();
}

void AutocompleteEditViewGtk::HandleMarkSet(GtkTextBuffer* buffer,
                                            GtkTextIter* location,
                                            GtkTextMark* mark) {
  if (!text_buffer_ || buffer != text_buffer_)
    return;

  if (mark != gtk_text_buffer_get_insert(text_buffer_) &&
      mark != gtk_text_buffer_get_selection_bound(text_buffer_)) {
    return;
  }

  // If we are here, that means the user may be changing the selection
  selection_suggested_ = false;

  // Get the currently-selected text, if there is any.
  std::string new_selected_text = GetSelectedText();

#if defined(OS_CHROMEOS)
  // If the user just selected some text with the mouse (or at least while the
  // mouse button was down), make sure that we won't blow their selection away
  // later by selecting all of the text when the button is released.
  if (button_1_pressed_ && !new_selected_text.empty())
    text_selected_during_click_ = true;
#endif

  // If we had some text selected earlier but it's no longer highlighted, we
  // might need to save it now...
  if (!selected_text_.empty() && new_selected_text.empty()) {
    // ... but only if we currently own the selection.  We want to manually
    // update the selection when the text is unhighlighted because the user
    // clicked in a blank area of the text view, but not when it's unhighlighted
    // because another client or widget took the selection.  (This handler gets
    // called before the default handler, so as long as nobody else took the
    // selection, the text buffer still owns it even if GTK is about to take it
    // away in the default handler.)
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (gtk_clipboard_get_owner(clipboard) == G_OBJECT(text_buffer_))
      SavePrimarySelection(selected_text_);
  }

  selected_text_ = new_selected_text;
}

// Override the primary selection the text buffer has set. This has to happen
// after the default handler for the "mark-set" signal.
void AutocompleteEditViewGtk::HandleMarkSetAfter(GtkTextBuffer* buffer,
                                                 GtkTextIter* location,
                                                 GtkTextMark* mark) {
  UpdatePrimarySelectionIfValidURL();
}

// Just use the default behavior for DnD, except if the drop can be a PasteAndGo
// then override.
void AutocompleteEditViewGtk::HandleDragDataReceived(
    GtkWidget* sender, GdkDragContext* context, gint x, gint y,
    GtkSelectionData* selection_data, guint target_type, guint time) {
  // Reset |paste_clipboard_requested_| to make sure we won't misinterpret this
  // drop action as a paste action.
  paste_clipboard_requested_ = false;

  // Don't try to PasteAndGo on drops originating from this omnibox. However, do
  // allow default behavior for such drags.
  if (context->source_window == text_view_->window)
    return;

  guchar* text = gtk_selection_data_get_text(selection_data);
  if (!text)
    return;

  std::wstring possible_url = UTF8ToWide(reinterpret_cast<char*>(text));
  g_free(text);
  if (model_->CanPasteAndGo(CollapseWhitespace(possible_url, true))) {
    model_->PasteAndGo();
    gtk_drag_finish(context, TRUE, TRUE, time);

    static guint signal_id =
        g_signal_lookup("drag-data-received", GTK_TYPE_WIDGET);
    g_signal_stop_emission(text_view_, signal_id, 0);
  }
}

void AutocompleteEditViewGtk::HandleDragDataGet(
    GtkWidget* widget,
    GdkDragContext* context,
    GtkSelectionData* selection_data,
    guint target_type,
    guint time) {
  // If GTK put the normal textual version of the selection in our drag data,
  // put our doctored selection that might have the 'http://' prefix. Also, GTK
  // is confused about signedness of its datatypes, leading to the weird switch
  // statement (no set of casts fixes this).
  switch (target_type) {
    case GTK_TEXT_BUFFER_TARGET_INFO_TEXT: {
      gtk_selection_data_set_text(selection_data, selected_text_.c_str(), -1);
    }
  }
}

void AutocompleteEditViewGtk::HandleInsertText(
    GtkTextBuffer* buffer, GtkTextIter* location, const gchar* text, gint len) {
  std::string filtered_text;
  filtered_text.reserve(len);

  // Filter out new line and tab characters.
  // |text| is guaranteed to be a valid UTF-8 string, so we don't need to
  // validate it here.
  //
  // If there was only a single character, then it might be generated by a key
  // event. In this case, we save the single character to help our
  // "key-press-event" signal handler distinguish if an Enter key event is
  // handled by IME or not.
  if (len == 1 && (text[0] == '\n' || text[0] == '\r'))
    enter_was_inserted_ = true;

  const gchar* p = text;
  while(*p) {
    gunichar c = g_utf8_get_char(p);
    const gchar* next = g_utf8_next_char(p);

    // 0x200B is Zero Width Space, which is inserted just before the instant
    // anchor for working around the GtkTextView's misalignment bug.
    // This character might be captured and inserted into the content by undo
    // manager, so we need to filter it out here.
    if (c != L'\n' && c != L'\r' && c != L'\t' && c != 0x200B)
      filtered_text.append(p, next);

    p = next;
  }

  if (filtered_text.length()) {
    // Avoid inserting the text after the instant anchor.
    ValidateTextBufferIter(location);

    // Call the default handler to insert filtered text.
    GtkTextBufferClass* klass = GTK_TEXT_BUFFER_GET_CLASS(buffer);
    klass->insert_text(buffer, location, filtered_text.data(),
                       static_cast<gint>(filtered_text.length()));
  }

  // Stop propagating the signal emission to prevent the default handler from
  // being called again.
  static guint signal_id = g_signal_lookup("insert-text", GTK_TYPE_TEXT_BUFFER);
  g_signal_stop_emission(buffer, signal_id, 0);
}

void AutocompleteEditViewGtk::HandleBackSpace(GtkWidget* sender) {
  // Checks if it's currently in keyword search mode.
  if (model_->is_keyword_hint() || model_->keyword().empty())
    return;  // Propgate into GtkTextView.

  GtkTextIter sel_start, sel_end;
  // Checks if there is some text selected.
  if (gtk_text_buffer_get_selection_bounds(text_buffer_, &sel_start, &sel_end))
    return;  // Propgate into GtkTextView.

  GtkTextIter start;
  gtk_text_buffer_get_start_iter(text_buffer_, &start);

  if (!gtk_text_iter_equal(&start, &sel_start))
    return;  // Propgate into GtkTextView.

  // We're showing a keyword and the user pressed backspace at the beginning
  // of the text. Delete the selected keyword.
  model_->ClearKeyword(GetText());

  // Stop propagating the signal emission into GtkTextView.
  static guint signal_id = g_signal_lookup("backspace", GTK_TYPE_TEXT_VIEW);
  g_signal_stop_emission(text_view_, signal_id, 0);
}

void AutocompleteEditViewGtk::HandleViewMoveFocus(GtkWidget* widget,
                                                  GtkDirectionType direction) {
  // Trigger Tab to search behavior only when Tab key is pressed.
  if (tab_was_pressed_ && enable_tab_to_search_ &&
      model_->is_keyword_hint() && !model_->keyword().empty()) {
    model_->AcceptKeyword();

    // If Tab to search behavior is triggered, then stop the signal emission to
    // prevent the focus from being moved.
    static guint signal_id = g_signal_lookup("move-focus", GTK_TYPE_WIDGET);
    g_signal_stop_emission(widget, signal_id, 0);
  }

  // Propagate the signal so that focus can be moved as normal.
}

void AutocompleteEditViewGtk::HandleCopyClipboard(GtkWidget* sender) {
  HandleCopyOrCutClipboard(true);
}

void AutocompleteEditViewGtk::HandleCutClipboard(GtkWidget* sender) {
  HandleCopyOrCutClipboard(false);
}

void AutocompleteEditViewGtk::HandleCopyOrCutClipboard(bool copy) {
  // On copy or cut, we manually update the PRIMARY selection to contain the
  // highlighted text.  This matches Firefox -- we highlight the URL but don't
  // update PRIMARY on Ctrl-L, so Ctrl-L, Ctrl-C and then middle-click is a
  // convenient way to paste the current URL somewhere.
  if (!gtk_text_buffer_get_has_selection(text_buffer_))
    return;

  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
  DCHECK(clipboard);
  if (!clipboard)
    return;

  CharRange selection = GetSelection();
  GURL url;
  std::wstring text(UTF8ToWide(GetSelectedText()));
  bool write_url;
  model_->AdjustTextForCopy(selection.selection_min(), IsSelectAll(), &text,
                            &url, &write_url);

  if (write_url) {
    string16 text16(WideToUTF16(text));
    BookmarkDragData data;
    data.ReadFromTuple(url, text16);
    data.WriteToClipboard(NULL);

    // Stop propagating the signal.
    static guint copy_signal_id =
        g_signal_lookup("copy-clipboard", GTK_TYPE_TEXT_VIEW);
    static guint cut_signal_id =
        g_signal_lookup("cut-clipboard", GTK_TYPE_TEXT_VIEW);
    g_signal_stop_emission(text_view_,
                           copy ? copy_signal_id : cut_signal_id,
                           0);

    if (!copy)
      gtk_text_buffer_delete_selection(text_buffer_, true, true);
  }

  OwnPrimarySelection(WideToUTF8(text));
}

void AutocompleteEditViewGtk::OwnPrimarySelection(const std::string& text) {
  primary_selection_text_ = text;

  GtkTargetList* list = gtk_target_list_new(NULL, 0);
  gtk_target_list_add_text_targets(list, 0);
  gint len;
  GtkTargetEntry* entries = gtk_target_table_new_from_list(list, &len);

  // When |text_buffer_| is destroyed, it will clear the clipboard, hence
  // we needn't worry about calling gtk_clipboard_clear().
  gtk_clipboard_set_with_owner(gtk_clipboard_get(GDK_SELECTION_PRIMARY),
                               entries, len,
                               ClipboardGetSelectionThunk,
                               ClipboardSelectionCleared,
                               G_OBJECT(text_buffer_));

  gtk_target_list_unref(list);
  gtk_target_table_free(entries, len);
}

void AutocompleteEditViewGtk::HandlePasteClipboard(GtkWidget* sender) {
  // We can't call model_->on_paste_replacing_all() here, because the actual
  // paste clipboard action may not be performed if the clipboard is empty.
  paste_clipboard_requested_ = true;
}

gfx::Rect AutocompleteEditViewGtk::WindowBoundsFromIters(
    GtkTextIter* iter1, GtkTextIter* iter2) {
  GdkRectangle start_location, end_location;
  GtkTextView* text_view = GTK_TEXT_VIEW(text_view_);
  gtk_text_view_get_iter_location(text_view, iter1, &start_location);
  gtk_text_view_get_iter_location(text_view, iter2, &end_location);

  gint x1, x2, y1, y2;
  gtk_text_view_buffer_to_window_coords(text_view, GTK_TEXT_WINDOW_WIDGET,
                                        start_location.x, start_location.y,
                                        &x1, &y1);
  gtk_text_view_buffer_to_window_coords(text_view, GTK_TEXT_WINDOW_WIDGET,
                                        end_location.x + end_location.width,
                                        end_location.y + end_location.height,
                                        &x2, &y2);

  return gfx::Rect(x1, y1, x2 - x1, y2 - y1);
}

gboolean AutocompleteEditViewGtk::HandleExposeEvent(GtkWidget* sender,
                                                    GdkEventExpose* expose) {
  if (strikethrough_.cp_min >= strikethrough_.cp_max)
    return FALSE;

  gfx::Rect expose_rect(expose->area);

  GtkTextIter iter_min, iter_max;
  ItersFromCharRange(strikethrough_, &iter_min, &iter_max);
  gfx::Rect strikethrough_rect = WindowBoundsFromIters(&iter_min, &iter_max);

  if (!expose_rect.Intersects(strikethrough_rect))
    return FALSE;

  // Finally, draw.
  cairo_t* cr = gdk_cairo_create(GDK_DRAWABLE(expose->window));
  cairo_rectangle(cr, expose_rect.x(), expose_rect.y(),
                      expose_rect.width(), expose_rect.height());
  cairo_clip(cr);

  // TODO(estade): we probably shouldn't draw the strikethrough on selected
  // text. I started to do this, but it was way more effort than it seemed
  // worth.
  strikethrough_rect.Inset(kStrikethroughStrokeWidth,
                           kStrikethroughStrokeWidth);
  cairo_set_source_rgb(cr, kStrikethroughStrokeRed, 0.0, 0.0);
  cairo_set_line_width(cr, kStrikethroughStrokeWidth);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, strikethrough_rect.x(), strikethrough_rect.bottom());
  cairo_line_to(cr, strikethrough_rect.right(), strikethrough_rect.y());
  cairo_stroke(cr);
  cairo_destroy(cr);

  return FALSE;
}

void AutocompleteEditViewGtk::SelectAllInternal(bool reversed,
                                                bool update_primary_selection) {
  GtkTextIter start, end;
  if (reversed) {
    GetTextBufferBounds(&end, &start);
  } else {
    GetTextBufferBounds(&start, &end);
  }
  if (!update_primary_selection)
    StartUpdatingHighlightedText();
  gtk_text_buffer_select_range(text_buffer_, &start, &end);
  if (!update_primary_selection)
    FinishUpdatingHighlightedText();
}

void AutocompleteEditViewGtk::StartUpdatingHighlightedText() {
  if (GTK_WIDGET_REALIZED(text_view_)) {
    GtkClipboard* clipboard =
        gtk_widget_get_clipboard(text_view_, GDK_SELECTION_PRIMARY);
    DCHECK(clipboard);
    if (clipboard)
      gtk_text_buffer_remove_selection_clipboard(text_buffer_, clipboard);
  }
  g_signal_handler_block(text_buffer_, mark_set_handler_id_);
  g_signal_handler_block(text_buffer_, mark_set_handler_id2_);
}

void AutocompleteEditViewGtk::FinishUpdatingHighlightedText() {
  if (GTK_WIDGET_REALIZED(text_view_)) {
    GtkClipboard* clipboard =
        gtk_widget_get_clipboard(text_view_, GDK_SELECTION_PRIMARY);
    DCHECK(clipboard);
    if (clipboard)
      gtk_text_buffer_add_selection_clipboard(text_buffer_, clipboard);
  }
  g_signal_handler_unblock(text_buffer_, mark_set_handler_id_);
  g_signal_handler_unblock(text_buffer_, mark_set_handler_id2_);
}

AutocompleteEditViewGtk::CharRange AutocompleteEditViewGtk::GetSelection() {
  // You can not just use get_selection_bounds here, since the order will be
  // ascending, and you don't know where the user's start and end of the
  // selection was (if the selection was forwards or backwards).  Get the
  // actual marks so that we can preserve the selection direction.
  GtkTextIter start, insert;
  GtkTextMark* mark;

  mark = gtk_text_buffer_get_selection_bound(text_buffer_);
  gtk_text_buffer_get_iter_at_mark(text_buffer_, &start, mark);

  mark = gtk_text_buffer_get_insert(text_buffer_);
  gtk_text_buffer_get_iter_at_mark(text_buffer_, &insert, mark);

#if GTK_CHECK_VERSION(2, 20, 0)
  // Nothing should be selected when we are in the middle of composition.
  DCHECK(preedit_.empty() || gtk_text_iter_equal(&start, &insert));
#endif

  return CharRange(gtk_text_iter_get_offset(&start),
                   gtk_text_iter_get_offset(&insert));
}

void AutocompleteEditViewGtk::ItersFromCharRange(const CharRange& range,
                                                 GtkTextIter* iter_min,
                                                 GtkTextIter* iter_max) {
  gtk_text_buffer_get_iter_at_offset(text_buffer_, iter_min, range.cp_min);
  gtk_text_buffer_get_iter_at_offset(text_buffer_, iter_max, range.cp_max);
}

int AutocompleteEditViewGtk::GetTextLength() const {
  GtkTextIter end;
  gtk_text_buffer_get_iter_at_mark(text_buffer_, &end, instant_mark_);
#if GTK_CHECK_VERSION(2, 20, 0)
  // We need to count the length of the text being composed, because we treat
  // it as part of the content in GetText().
  return gtk_text_iter_get_offset(&end) + preedit_.size();
#else
  return gtk_text_iter_get_offset(&end);
#endif
}

void AutocompleteEditViewGtk::EmphasizeURLComponents() {
#if GTK_CHECK_VERSION(2, 20, 0)
  // We can't change the text style easily, if the preedit string (the text
  // being composed by the input method) is not empty, which is not treated as
  // a part of the text content inside GtkTextView. And it's ok to simply return
  // in this case, as this method will be called again when the preedit string
  // gets committed.
  if (preedit_.size())
    return;
#endif
  // See whether the contents are a URL with a non-empty host portion, which we
  // should emphasize.  To check for a URL, rather than using the type returned
  // by Parse(), ask the model, which will check the desired page transition for
  // this input.  This can tell us whether an UNKNOWN input string is going to
  // be treated as a search or a navigation, and is the same method the Paste
  // And Go system uses.
  url_parse::Component scheme, host;
  std::wstring text(GetText());
  AutocompleteInput::ParseForEmphasizeComponents(
      text, model_->GetDesiredTLD(), &scheme, &host);
  const bool emphasize = model_->CurrentTextIsURL() && (host.len > 0);

  // Set the baseline emphasis.
  GtkTextIter start, end;
  GetTextBufferBounds(&start, &end);
  gtk_text_buffer_remove_all_tags(text_buffer_, &start, &end);
  if (emphasize) {
    gtk_text_buffer_apply_tag(text_buffer_, faded_text_tag_, &start, &end);

    // We've found a host name, give it more emphasis.
    gtk_text_buffer_get_iter_at_line_index(text_buffer_, &start, 0,
                                           GetUTF8Offset(text,
                                                         host.begin));
    gtk_text_buffer_get_iter_at_line_index(text_buffer_, &end, 0,
                                           GetUTF8Offset(text,
                                                         host.end()));

    gtk_text_buffer_apply_tag(text_buffer_, normal_text_tag_, &start, &end);
  } else {
    gtk_text_buffer_apply_tag(text_buffer_, normal_text_tag_, &start, &end);
  }

  strikethrough_ = CharRange();
  // Emphasize the scheme for security UI display purposes (if necessary).
  if (!model_->user_input_in_progress() && scheme.is_nonempty() &&
      (security_level_ != ToolbarModel::NONE)) {
    CharRange scheme_range = CharRange(GetUTF8Offset(text, scheme.begin),
                                       GetUTF8Offset(text, scheme.end()));
    ItersFromCharRange(scheme_range, &start, &end);

    if (security_level_ == ToolbarModel::SECURITY_ERROR) {
      strikethrough_ = scheme_range;
      // When we draw the strikethrough, we don't want to include the ':' at the
      // end of the scheme.
      strikethrough_.cp_max--;

      gtk_text_buffer_apply_tag(text_buffer_, security_error_scheme_tag_,
                                &start, &end);
    } else if (security_level_ == ToolbarModel::SECURITY_WARNING) {
      gtk_text_buffer_apply_tag(text_buffer_, faded_text_tag_, &start, &end);
    } else {
      gtk_text_buffer_apply_tag(text_buffer_, secure_scheme_tag_, &start, &end);
    }
  }
}

void AutocompleteEditViewGtk::SetInstantSuggestion(
    const std::string& suggestion) {
  gtk_label_set_text(GTK_LABEL(instant_view_), suggestion.c_str());
  if (suggestion.empty()) {
    gtk_widget_hide(instant_view_);
  } else {
    gtk_widget_show(instant_view_);
    AdjustVerticalAlignmentOfInstantView();
  }
}

bool AutocompleteEditViewGtk::CommitInstantSuggestion() {
  const gchar* suggestion = gtk_label_get_text(GTK_LABEL(instant_view_));
  if (!suggestion || !*suggestion)
    return false;

  OnBeforePossibleChange();
  SetUserText(GetText() + UTF8ToWide(suggestion));
  OnAfterPossibleChange();
  return true;
}

void AutocompleteEditViewGtk::TextChanged() {
  EmphasizeURLComponents();
  controller_->OnChanged();
}

void AutocompleteEditViewGtk::SavePrimarySelection(
    const std::string& selected_text) {
  GtkClipboard* clipboard =
      gtk_widget_get_clipboard(text_view_, GDK_SELECTION_PRIMARY);
  DCHECK(clipboard);
  if (!clipboard)
    return;

  gtk_clipboard_set_text(
      clipboard, selected_text.data(), selected_text.size());
}

void AutocompleteEditViewGtk::SetTextAndSelectedRange(const std::wstring& text,
                                                      const CharRange& range) {
  std::string utf8 = WideToUTF8(text);
  gtk_text_buffer_set_text(text_buffer_, utf8.data(), utf8.length());
  SetSelectedRange(range);
  AdjustTextJustification();
}

void AutocompleteEditViewGtk::SetSelectedRange(const CharRange& range) {
  GtkTextIter insert, bound;
  ItersFromCharRange(range, &bound, &insert);
  gtk_text_buffer_select_range(text_buffer_, &insert, &bound);

  // This should be set *after* setting the selection range, in case setting the
  // selection triggers HandleMarkSet which sets |selection_suggested_| to
  // false.
  selection_suggested_ = true;
}

void AutocompleteEditViewGtk::AdjustTextJustification() {
  PangoDirection content_dir = GetContentDirection();

  // Use keymap direction if content does not have strong direction.
  // It matches the behavior of GtkTextView.
  if (content_dir == PANGO_DIRECTION_NEUTRAL) {
    content_dir = gdk_keymap_get_direction(
      gdk_keymap_get_for_display(gtk_widget_get_display(text_view_)));
  }

  GtkTextDirection widget_dir = gtk_widget_get_direction(text_view_);

  if ((widget_dir == GTK_TEXT_DIR_RTL && content_dir == PANGO_DIRECTION_LTR) ||
      (widget_dir == GTK_TEXT_DIR_LTR && content_dir == PANGO_DIRECTION_RTL)) {
    gtk_text_view_set_justification(GTK_TEXT_VIEW(text_view_),
                                    GTK_JUSTIFY_RIGHT);
  } else {
    gtk_text_view_set_justification(GTK_TEXT_VIEW(text_view_),
                                    GTK_JUSTIFY_LEFT);
  }
}

PangoDirection AutocompleteEditViewGtk::GetContentDirection() {
  GtkTextIter iter;
  gtk_text_buffer_get_start_iter(text_buffer_, &iter);

  PangoDirection dir = PANGO_DIRECTION_NEUTRAL;
  do {
    dir = pango_unichar_direction(gtk_text_iter_get_char(&iter));
    if (dir != PANGO_DIRECTION_NEUTRAL)
      break;
  } while (gtk_text_iter_forward_char(&iter));

  return dir;
}

void AutocompleteEditViewGtk::HandleWidgetDirectionChanged(
    GtkWidget* sender, GtkTextDirection previous_direction) {
  AdjustTextJustification();
}

void AutocompleteEditViewGtk::HandleDeleteFromCursor(GtkWidget *sender,
    GtkDeleteType type, gint count) {
  // If the selected text was suggested for autocompletion, then erase those
  // first and then let the default handler take over.
  if (selection_suggested_) {
    gtk_text_buffer_delete_selection(text_buffer_, true, true);
    selection_suggested_ = false;
  }
}

void AutocompleteEditViewGtk::HandleKeymapDirectionChanged(GdkKeymap* sender) {
  AdjustTextJustification();
}

void AutocompleteEditViewGtk::HandleDeleteRange(GtkTextBuffer* buffer,
                                                GtkTextIter* start,
                                                GtkTextIter* end) {
  // Prevent the user from deleting the instant anchor. We can't simply set the
  // instant anchor readonly by applying a tag with "editable" = FALSE, because
  // it'll prevent the insert caret from blinking.
  ValidateTextBufferIter(start);
  ValidateTextBufferIter(end);
  if (!gtk_text_iter_compare(start, end)) {
    static guint signal_id =
        g_signal_lookup("delete-range", GTK_TYPE_TEXT_BUFFER);
    g_signal_stop_emission(buffer, signal_id, 0);
  }
}

void AutocompleteEditViewGtk::HandleMarkSetAlways(GtkTextBuffer* buffer,
                                                  GtkTextIter* location,
                                                  GtkTextMark* mark) {
  if (mark == instant_mark_)
    return;

  GtkTextIter new_iter = *location;
  ValidateTextBufferIter(&new_iter);

  // "mark-set" signal is actually emitted after the mark's location is already
  // set, so if the location is beyond the instant anchor, we need to move the
  // mark again, which will emit the signal again. In order to prevent other
  // signal handlers from being called twice, we need to stop signal emission
  // before moving the mark again.
  if (gtk_text_iter_compare(&new_iter, location)) {
    static guint signal_id = g_signal_lookup("mark-set", GTK_TYPE_TEXT_BUFFER);
    g_signal_stop_emission(buffer, signal_id, 0);
    gtk_text_buffer_move_mark(buffer, mark, &new_iter);
  }
}

// static
void AutocompleteEditViewGtk::ClipboardGetSelectionThunk(
    GtkClipboard* clipboard,
    GtkSelectionData* selection_data,
    guint info,
    gpointer object) {
  AutocompleteEditViewGtk* edit_view =
      reinterpret_cast<AutocompleteEditViewGtk*>(
          g_object_get_data(G_OBJECT(object), kAutocompleteEditViewGtkKey));
  edit_view->ClipboardGetSelection(clipboard, selection_data, info);
}

void AutocompleteEditViewGtk::ClipboardGetSelection(
    GtkClipboard* clipboard,
    GtkSelectionData* selection_data,
    guint info) {
  gtk_selection_data_set_text(selection_data, primary_selection_text_.c_str(),
                              primary_selection_text_.size());
}

std::string AutocompleteEditViewGtk::GetSelectedText() const {
  GtkTextIter start, end;
  std::string result;
  if (gtk_text_buffer_get_selection_bounds(text_buffer_, &start, &end)) {
    gchar* text = gtk_text_iter_get_text(&start, &end);
    size_t text_len = strlen(text);
    if (text_len)
      result = std::string(text, text_len);
    g_free(text);
  }
  return result;
}

void AutocompleteEditViewGtk::UpdatePrimarySelectionIfValidURL() {
  std::wstring text = UTF8ToWide(GetSelectedText());

  if (text.empty())
    return;

  // Use AdjustTextForCopy to make sure we prefix the text with 'http://'.
  CharRange selection = GetSelection();
  GURL url;
  bool write_url;
  model_->AdjustTextForCopy(selection.selection_min(), IsSelectAll(), &text,
                            &url, &write_url);
  if (write_url) {
    selected_text_ = WideToUTF8(text);
    OwnPrimarySelection(selected_text_);
  }
}

#if GTK_CHECK_VERSION(2, 20, 0)
void AutocompleteEditViewGtk::HandlePreeditChanged(GtkWidget* sender,
                                                   const gchar* preedit) {
  // GtkTextView won't fire "begin-user-action" and "end-user-action" signals
  // when changing the preedit string, so we need to call
  // OnBeforePossibleChange() and OnAfterPossibleChange() by ourselves.
  OnBeforePossibleChange();
  if (preedit && *preedit) {
    // GtkTextView will only delete the selection range when committing the
    // preedit string, which will cause very strange behavior, so we need to
    // delete the selection range here explicitly. See http://crbug.com/18808.
    if (preedit_.empty())
      gtk_text_buffer_delete_selection(text_buffer_, false, true);
    preedit_ = UTF8ToWide(preedit);
  } else {
    preedit_.clear();
  }
  OnAfterPossibleChange();
}
#endif

void AutocompleteEditViewGtk::HandleWindowSetFocus(
    GtkWindow* sender, GtkWidget* focus) {
  // This is actually a guess. If the focused widget changes in "focus-out"
  // event handler, then the window will respect that and won't focus
  // |focus|. I doubt that is likely to happen however.
  going_to_focus_ = focus;
}

void AutocompleteEditViewGtk::HandleUndoRedo(GtkWidget* sender) {
  OnBeforePossibleChange();
}

void AutocompleteEditViewGtk::HandleUndoRedoAfter(GtkWidget* sender) {
  OnAfterPossibleChange();
}

void AutocompleteEditViewGtk::GetTextBufferBounds(GtkTextIter* start,
                                                  GtkTextIter* end) const {
  gtk_text_buffer_get_start_iter(text_buffer_, start);
  gtk_text_buffer_get_iter_at_mark(text_buffer_, end, instant_mark_);
}

void AutocompleteEditViewGtk::ValidateTextBufferIter(GtkTextIter* iter) const {
  if (!instant_mark_)
    return;

  GtkTextIter end;
  gtk_text_buffer_get_iter_at_mark(text_buffer_, &end, instant_mark_);
  if (gtk_text_iter_compare(iter, &end) > 0)
    *iter = end;
}

void AutocompleteEditViewGtk::AdjustVerticalAlignmentOfInstantView() {
  // By default, GtkTextView layouts an anchored child widget just above the
  // baseline, so we need to move the |instant_view_| down to make sure it
  // has the same baseline as the |text_view_|.
  PangoLayout* layout = gtk_label_get_layout(GTK_LABEL(instant_view_));
  int height;
  pango_layout_get_size(layout, NULL, &height);
  PangoLayoutIter* iter = pango_layout_get_iter(layout);
  int baseline = pango_layout_iter_get_baseline(iter);
  pango_layout_iter_free(iter);
  g_object_set(instant_anchor_tag_, "rise", baseline - height, NULL);
}
