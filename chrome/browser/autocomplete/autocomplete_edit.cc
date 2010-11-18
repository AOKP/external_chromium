// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete_edit.h"

#include <string>

#include "base/basictypes.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/autocomplete_classifier.h"
#include "chrome/browser/autocomplete/autocomplete_edit_view.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_popup_model.h"
#include "chrome/browser/autocomplete/keyword_provider.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/extensions/extension_omnibox_api.h"
#include "chrome/browser/google/google_url_tracker.h"
#include "chrome/browser/instant/instant_controller.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/net/predictor_api.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "googleurl/src/url_util.h"
#include "third_party/skia/include/core/SkBitmap.h"

///////////////////////////////////////////////////////////////////////////////
// AutocompleteEditController

AutocompleteEditController::~AutocompleteEditController() {
}

///////////////////////////////////////////////////////////////////////////////
// AutocompleteEditModel::State

AutocompleteEditModel::State::State(bool user_input_in_progress,
                                    const std::wstring& user_text,
                                    const std::wstring& keyword,
                                    bool is_keyword_hint,
                                    KeywordUIState keyword_ui_state)
    : user_input_in_progress(user_input_in_progress),
      user_text(user_text),
      keyword(keyword),
      is_keyword_hint(is_keyword_hint),
      keyword_ui_state(keyword_ui_state) {
}

AutocompleteEditModel::State::~State() {
}

///////////////////////////////////////////////////////////////////////////////
// AutocompleteEditModel

AutocompleteEditModel::AutocompleteEditModel(
    AutocompleteEditView* view,
    AutocompleteEditController* controller,
    Profile* profile)
    : view_(view),
      popup_(NULL),
      controller_(controller),
      has_focus_(false),
      user_input_in_progress_(false),
      just_deleted_text_(false),
      has_temporary_text_(false),
      original_keyword_ui_state_(NORMAL),
      paste_state_(NONE),
      control_key_state_(UP),
      is_keyword_hint_(false),
      keyword_ui_state_(NORMAL),
      paste_and_go_transition_(PageTransition::TYPED),
      profile_(profile) {
}

AutocompleteEditModel::~AutocompleteEditModel() {
}

void AutocompleteEditModel::SetPopupModel(AutocompletePopupModel* popup_model) {
  popup_ = popup_model;
  registrar_.Add(this,
      NotificationType::AUTOCOMPLETE_CONTROLLER_DEFAULT_MATCH_UPDATED,
      Source<AutocompleteController>(popup_->autocomplete_controller()));
}

void AutocompleteEditModel::SetProfile(Profile* profile) {
  DCHECK(profile);
  profile_ = profile;
  popup_->SetProfile(profile);
}

const AutocompleteEditModel::State
    AutocompleteEditModel::GetStateForTabSwitch() {
  // Like typing, switching tabs "accepts" the temporary text as the user
  // text, because it makes little sense to have temporary text when the
  // popup is closed.
  if (user_input_in_progress_) {
    // Weird edge case to match other browsers: if the edit is empty, revert to
    // the permanent text (so the user can get it back easily) but select it (so
    // on switching back, typing will "just work").
    const std::wstring user_text(UserTextFromDisplayText(view_->GetText()));
    if (user_text.empty()) {
      view_->RevertAll();
      view_->SelectAll(true);
    } else {
      InternalSetUserText(user_text);
    }
  }

  return State(user_input_in_progress_, user_text_, keyword_, is_keyword_hint_,
               keyword_ui_state_);
}

void AutocompleteEditModel::RestoreState(const State& state) {
  // Restore any user editing.
  if (state.user_input_in_progress) {
    // NOTE: Be sure and set keyword-related state BEFORE invoking
    // DisplayTextFromUserText(), as its result depends upon this state.
    keyword_ = state.keyword;
    is_keyword_hint_ = state.is_keyword_hint;
    keyword_ui_state_ = state.keyword_ui_state;
    view_->SetUserText(state.user_text,
        DisplayTextFromUserText(state.user_text), false);
  }
}

AutocompleteMatch AutocompleteEditModel::CurrentMatch() {
  AutocompleteMatch match;
  GetInfoForCurrentText(&match, NULL);
  return match;
}

bool AutocompleteEditModel::UpdatePermanentText(
    const std::wstring& new_permanent_text) {
  // When there's a new URL, and the user is not editing anything or the edit
  // doesn't have focus, we want to revert the edit to show the new URL.  (The
  // common case where the edit doesn't have focus is when the user has started
  // an edit and then abandoned it and clicked a link on the page.)
  const bool visibly_changed_permanent_text =
      (permanent_text_ != new_permanent_text) &&
      (!user_input_in_progress_ || !has_focus_);

  permanent_text_ = new_permanent_text;
  return visibly_changed_permanent_text;
}

void AutocompleteEditModel::SetUserText(const std::wstring& text) {
  SetInputInProgress(true);
  InternalSetUserText(text);
  paste_state_ = NONE;
  has_temporary_text_ = false;
}

void AutocompleteEditModel::GetDataForURLExport(GURL* url,
                                                std::wstring* title,
                                                SkBitmap* favicon) {
  AutocompleteMatch match;
  GetInfoForCurrentText(&match, NULL);
  *url = match.destination_url;
  if (*url == URLFixerUpper::FixupURL(WideToUTF8(permanent_text_),
                                      std::string())) {
    *title = controller_->GetTitle();
    *favicon = controller_->GetFavIcon();
  }
}

std::wstring AutocompleteEditModel::GetDesiredTLD() const {
  // Tricky corner case: The user has typed "foo" and currently sees an inline
  // autocomplete suggestion of "foo.net".  He now presses ctrl-a (e.g. to
  // select all, on Windows).  If we treat the ctrl press as potentially for the
  // sake of ctrl-enter, then we risk "www.foo.com" being promoted as the best
  // match.  This would make the autocompleted text disappear, leaving our user
  // feeling very confused when the wrong text gets highlighted.
  //
  // Thus, we only treat the user as pressing ctrl-enter when the user presses
  // ctrl without any fragile state built up in the omnibox:
  // * the contents of the omnibox have not changed since the keypress,
  // * there is no autocompleted text visible, and
  // * the user is not typing a keyword query.
  return (control_key_state_ == DOWN_WITHOUT_CHANGE &&
          inline_autocomplete_text_.empty() && !KeywordIsSelected())?
    std::wstring(L"com") : std::wstring();
}

bool AutocompleteEditModel::CurrentTextIsURL() const {
  // If !user_input_in_progress_, the permanent text is showing, which should
  // always be a URL, so no further checking is needed.  By avoiding checking in
  // this case, we avoid calling into the autocomplete providers, and thus
  // initializing the history system, as long as possible, which speeds startup.
  if (!user_input_in_progress_)
    return true;

  AutocompleteMatch match;
  GetInfoForCurrentText(&match, NULL);
  return match.transition == PageTransition::TYPED;
}

AutocompleteMatch::Type AutocompleteEditModel::CurrentTextType() const {
  AutocompleteMatch match;
  GetInfoForCurrentText(&match, NULL);
  return match.type;
}

bool AutocompleteEditModel::GetURLForText(const std::wstring& text,
                                          GURL* url) const {
  const AutocompleteInput::Type type = AutocompleteInput::Parse(
      UserTextFromDisplayText(text), std::wstring(), NULL, NULL);
  if (type != AutocompleteInput::URL)
    return false;

  *url = URLFixerUpper::FixupURL(WideToUTF8(text), std::string());
  return true;
}

void AutocompleteEditModel::AdjustTextForCopy(int sel_min,
                                              bool is_all_selected,
                                              std::wstring* text,
                                              GURL* url,
                                              bool* write_url) {
  *write_url = false;

  if (sel_min != 0)
    return;

  // We can't use CurrentTextIsURL() or GetDataForURLExport() because right now
  // the user is probably holding down control to cause the copy, which will
  // screw up our calculation of the desired_tld.
  if (!GetURLForText(*text, url))
    return;  // Can't be parsed as a url, no need to adjust text.

  if (!user_input_in_progress() && is_all_selected) {
    // The user selected all the text and has not edited it. Use the url as the
    // text so that if the scheme was stripped it's added back, and the url
    // is unescaped (we escape parts of the url for display).
    *text = UTF8ToWide(url->spec());
    *write_url = true;
    return;
  }

  // Prefix the text with 'http://' if the text doesn't start with 'http://',
  // the text parses as a url with a scheme of http, the user selected the
  // entire host, and the user hasn't edited the host or manually removed the
  // scheme.
  GURL perm_url;
  if (GetURLForText(permanent_text_, &perm_url) &&
      perm_url.SchemeIs(chrome::kHttpScheme) &&
      url->SchemeIs(chrome::kHttpScheme) &&
      perm_url.host() == url->host()) {
    *write_url = true;

    std::wstring http = ASCIIToWide(chrome::kHttpScheme) +
        ASCIIToWide(chrome::kStandardSchemeSeparator);
    if (text->compare(0, http.length(), http) != 0)
      *text = http + *text;
  }
}

void AutocompleteEditModel::SetInputInProgress(bool in_progress) {
  if (user_input_in_progress_ == in_progress)
    return;

  user_input_in_progress_ = in_progress;
  controller_->OnInputInProgress(in_progress);
}

void AutocompleteEditModel::Revert() {
  SetInputInProgress(false);
  paste_state_ = NONE;
  InternalSetUserText(std::wstring());
  keyword_.clear();
  is_keyword_hint_ = false;
  keyword_ui_state_ = NORMAL;
  has_temporary_text_ = false;
  view_->SetWindowTextAndCaretPos(permanent_text_,
                                  has_focus_ ? permanent_text_.length() : 0);
}

void AutocompleteEditModel::StartAutocomplete(
    bool has_selected_text,
    bool prevent_inline_autocomplete) const {
  popup_->StartAutocomplete(user_text_, GetDesiredTLD(),
      prevent_inline_autocomplete || just_deleted_text_ ||
      (has_selected_text && inline_autocomplete_text_.empty()) ||
      (paste_state_ != NONE), keyword_ui_state_ == KEYWORD);
}

bool AutocompleteEditModel::CanPasteAndGo(const std::wstring& text) const {
  if (!view_->GetCommandUpdater()->IsCommandEnabled(IDC_OPEN_CURRENT_URL))
    return false;

  AutocompleteMatch match;
  profile_->GetAutocompleteClassifier()->Classify(text, std::wstring(),
      &match, &paste_and_go_alternate_nav_url_);
  paste_and_go_url_ = match.destination_url;
  paste_and_go_transition_ = match.transition;
  return paste_and_go_url_.is_valid();
}

void AutocompleteEditModel::PasteAndGo() {
  // The final parameter to OpenURL, keyword, is not quite correct here: it's
  // possible to "paste and go" a string that contains a keyword.  This is
  // enough of an edge case that we ignore this possibility.
  view_->RevertAll();
  view_->OpenURL(paste_and_go_url_, CURRENT_TAB, paste_and_go_transition_,
      paste_and_go_alternate_nav_url_, AutocompletePopupModel::kNoMatch,
      std::wstring());
}

void AutocompleteEditModel::AcceptInput(WindowOpenDisposition disposition,
                                        bool for_drop) {
  // Get the URL and transition type for the selected entry.
  AutocompleteMatch match;
  GURL alternate_nav_url;
  GetInfoForCurrentText(&match, &alternate_nav_url);

  if (!match.destination_url.is_valid())
    return;

  if ((match.transition == PageTransition::TYPED) && (match.destination_url ==
      URLFixerUpper::FixupURL(WideToUTF8(permanent_text_), std::string()))) {
    // When the user hit enter on the existing permanent URL, treat it like a
    // reload for scoring purposes.  We could detect this by just checking
    // user_input_in_progress_, but it seems better to treat "edits" that end
    // up leaving the URL unchanged (e.g. deleting the last character and then
    // retyping it) as reloads too.  We exclude non-TYPED transitions because if
    // the transition is GENERATED, the user input something that looked
    // different from the current URL, even if it wound up at the same place
    // (e.g. manually retyping the same search query), and it seems wrong to
    // treat this as a reload.
    match.transition = PageTransition::RELOAD;
  } else if (for_drop || ((paste_state_ != NONE) &&
                          match.is_history_what_you_typed_match)) {
    // When the user pasted in a URL and hit enter, score it like a link click
    // rather than a normal typed URL, so it doesn't get inline autocompleted
    // as aggressively later.
    match.transition = PageTransition::LINK;
  }

  if (match.type == AutocompleteMatch::SEARCH_WHAT_YOU_TYPED ||
      match.type == AutocompleteMatch::SEARCH_HISTORY ||
      match.type == AutocompleteMatch::SEARCH_SUGGEST) {
    const TemplateURL* default_provider =
        profile_->GetTemplateURLModel()->GetDefaultSearchProvider();
    if (default_provider && default_provider->url() &&
        default_provider->url()->HasGoogleBaseURLs())
      GoogleURLTracker::GoogleURLSearchCommitted();
  }
  view_->OpenURL(match.destination_url, disposition, match.transition,
                 alternate_nav_url, AutocompletePopupModel::kNoMatch,
                 is_keyword_hint_ ? std::wstring() : keyword_);
}

void AutocompleteEditModel::OpenURL(const GURL& url,
                                    WindowOpenDisposition disposition,
                                    PageTransition::Type transition,
                                    const GURL& alternate_nav_url,
                                    size_t index,
                                    const std::wstring& keyword) {
  // We only care about cases where there is a selection (i.e. the popup is
  // open).
  if (popup_->IsOpen()) {
    scoped_ptr<AutocompleteLog> log(popup_->GetAutocompleteLog());
    if (index != AutocompletePopupModel::kNoMatch)
      log->selected_index = index;
    else if (!has_temporary_text_)
      log->inline_autocompleted_length = inline_autocomplete_text_.length();
    NotificationService::current()->Notify(
        NotificationType::OMNIBOX_OPENED_URL, Source<Profile>(profile_),
        Details<AutocompleteLog>(log.get()));
  }

  TemplateURLModel* template_url_model = profile_->GetTemplateURLModel();
  if (template_url_model && !keyword.empty()) {
    const TemplateURL* const template_url =
        template_url_model->GetTemplateURLForKeyword(keyword);

    // Special case for extension keywords. Don't increment usage count for
    // these.
    if (template_url && template_url->IsExtensionKeyword()) {
      AutocompleteMatch current_match;
      GetInfoForCurrentText(&current_match, NULL);

      const AutocompleteMatch& match =
          index == AutocompletePopupModel::kNoMatch ?
              current_match : result().match_at(index);

      // Strip the keyword + leading space off the input.
      size_t prefix_length = match.template_url->keyword().size() + 1;
      ExtensionOmniboxEventRouter::OnInputEntered(
          profile_, match.template_url->GetExtensionId(),
          WideToUTF8(match.fill_into_edit.substr(prefix_length)));
      view_->RevertAll();
      return;
    }

    if (template_url) {
      UserMetrics::RecordAction(UserMetricsAction("AcceptedKeyword"), profile_);
      template_url_model->IncrementUsageCount(template_url);
    }

    // NOTE: We purposefully don't increment the usage count of the default
    // search engine, if applicable; see comments in template_url.h.
  }

  controller_->OnAutocompleteWillAccept();

  if (disposition != NEW_BACKGROUND_TAB)
    view_->RevertAll();  // Revert the box to its unedited state
  controller_->OnAutocompleteAccept(url, disposition, transition,
                                    alternate_nav_url);
}

void AutocompleteEditModel::AcceptKeyword() {
  view_->OnBeforePossibleChange();
  view_->SetWindowTextAndCaretPos(std::wstring(), 0);
  is_keyword_hint_ = false;
  keyword_ui_state_ = KEYWORD;
  view_->OnAfterPossibleChange();
  just_deleted_text_ = false;  // OnAfterPossibleChange() erroneously sets this
                               // since the edit contents have disappeared.  It
                               // doesn't really matter, but we clear it to be
                               // consistent.
  UserMetrics::RecordAction(UserMetricsAction("AcceptedKeywordHint"), profile_);
}

void AutocompleteEditModel::ClearKeyword(const std::wstring& visible_text) {
  view_->OnBeforePossibleChange();
  const std::wstring window_text(keyword_ + visible_text);
  view_->SetWindowTextAndCaretPos(window_text.c_str(), keyword_.length());
  keyword_.clear();
  keyword_ui_state_ = NORMAL;
  view_->OnAfterPossibleChange();
  just_deleted_text_ = true;  // OnAfterPossibleChange() fails to clear this
                              // since the edit contents have actually grown
                              // longer.
}

bool AutocompleteEditModel::query_in_progress() const {
  return !popup_->autocomplete_controller()->done();
}

const AutocompleteResult& AutocompleteEditModel::result() const {
  return popup_->autocomplete_controller()->result();
}

void AutocompleteEditModel::OnSetFocus(bool control_down) {
  has_focus_ = true;
  control_key_state_ = control_down ? DOWN_WITHOUT_CHANGE : UP;
  NotificationService::current()->Notify(
      NotificationType::AUTOCOMPLETE_EDIT_FOCUSED,
      Source<AutocompleteEditModel>(this),
      NotificationService::NoDetails());
}

void AutocompleteEditModel::OnKillFocus() {
  has_focus_ = false;
  control_key_state_ = UP;
  paste_state_ = NONE;

  // Like typing, killing focus "accepts" the temporary text as the user
  // text, because it makes little sense to have temporary text when the
  // popup is closed.
  InternalSetUserText(UserTextFromDisplayText(view_->GetText()));
  has_temporary_text_ = false;
}

bool AutocompleteEditModel::OnEscapeKeyPressed() {
  if (has_temporary_text_) {
    AutocompleteMatch match;
    popup_->InfoForCurrentSelection(&match, NULL);
    if (match.destination_url != original_url_) {
      // The user typed something, then selected a different item.  Restore the
      // text they typed and change back to the default item.
      // NOTE: This purposefully does not reset paste_state_.
      just_deleted_text_ = false;
      has_temporary_text_ = false;
      keyword_ui_state_ = original_keyword_ui_state_;
      popup_->ResetToDefaultMatch();
      view_->OnRevertTemporaryText();
      return true;
    }
  }

  // If the user wasn't editing, but merely had focus in the edit, allow <esc>
  // to be processed as an accelerator, so it can still be used to stop a load.
  // When the permanent text isn't all selected we still fall through to the
  // SelectAll() call below so users can arrow around in the text and then hit
  // <esc> to quickly replace all the text; this matches IE.
  if (!user_input_in_progress_ && view_->IsSelectAll())
    return false;

  view_->RevertAll();
  view_->SelectAll(true);
  return true;
}

void AutocompleteEditModel::OnControlKeyChanged(bool pressed) {
  // Don't change anything unless the key state is actually toggling.
  if (pressed == (control_key_state_ == UP)) {
    ControlKeyState old_state = control_key_state_;
    control_key_state_ = pressed ? DOWN_WITHOUT_CHANGE : UP;
    if ((control_key_state_ == DOWN_WITHOUT_CHANGE) && has_temporary_text_) {
      // Arrowing down and then hitting control accepts the temporary text as
      // the input text.
      InternalSetUserText(UserTextFromDisplayText(view_->GetText()));
      has_temporary_text_ = false;
      if (KeywordIsSelected())
        AcceptKeyword();
    }
    if ((old_state != DOWN_WITH_CHANGE) && popup_->IsOpen()) {
      // Autocomplete history provider results may change, so refresh the
      // popup.  This will force user_input_in_progress_ to true, but if the
      // popup is open, that should have already been the case.
      view_->UpdatePopup();
    }
  }
}

void AutocompleteEditModel::OnUpOrDownKeyPressed(int count) {
  // NOTE: This purposefully don't trigger any code that resets paste_state_.

  if (!popup_->IsOpen()) {
    if (!query_in_progress()) {
      // The popup is neither open nor working on a query already.  So, start an
      // autocomplete query for the current text.  This also sets
      // user_input_in_progress_ to true, which we want: if the user has started
      // to interact with the popup, changing the permanent_text_ shouldn't
      // change the displayed text.
      // Note: This does not force the popup to open immediately.
      // TODO(pkasting): We should, in fact, force this particular query to open
      // the popup immediately.
      if (!user_input_in_progress_)
        InternalSetUserText(permanent_text_);
      view_->UpdatePopup();
    } else {
      // TODO(pkasting): The popup is working on a query but is not open.  We
      // should force it to open immediately.
    }
  } else {
    // The popup is open, so the user should be able to interact with it
    // normally.
    popup_->Move(count);
  }

  // NOTE: We need to reset the keyword_ui_state_ after the popup updates, since
  // Move() will eventually call back to OnPopupDataChanged(), which needs to
  // save off the current keyword_ui_state_.
  keyword_ui_state_ = NORMAL;
}

void AutocompleteEditModel::OnPopupDataChanged(
    const std::wstring& text,
    GURL* destination_for_temporary_text_change,
    const std::wstring& keyword,
    bool is_keyword_hint) {
  // Update keyword/hint-related local state.
  bool keyword_state_changed = (keyword_ != keyword) ||
      ((is_keyword_hint_ != is_keyword_hint) && !keyword.empty());
  if (keyword_state_changed) {
    keyword_ = keyword;
    is_keyword_hint_ = is_keyword_hint;
  }

  // Handle changes to temporary text.
  if (destination_for_temporary_text_change != NULL) {
    const bool save_original_selection = !has_temporary_text_;
    if (save_original_selection) {
      // Save the original selection and URL so it can be reverted later.
      has_temporary_text_ = true;
      original_url_ = *destination_for_temporary_text_change;
      original_keyword_ui_state_ = keyword_ui_state_;
    }
    if (control_key_state_ == DOWN_WITHOUT_CHANGE) {
      // Arrowing around the popup cancels control-enter.
      control_key_state_ = DOWN_WITH_CHANGE;
      // Now things are a bit screwy: the desired_tld has changed, but if we
      // update the popup, the new order of entries won't match the old, so the
      // user's selection gets screwy; and if we don't update the popup, and the
      // user reverts, then the selected item will be as if control is still
      // pressed, even though maybe it isn't any more.  There is no obvious
      // right answer here :(
    }
    view_->OnTemporaryTextMaybeChanged(DisplayTextFromUserText(text),
                                       save_original_selection);
    return;
  }

  // TODO(suzhe): Instead of messing with |inline_autocomplete_text_| here,
  // we should probably do it inside Observe(), and save/restore it around
  // changes to the temporary text.  This will let us remove knowledge of
  // inline autocompletions from the popup code.
  //
  // Handle changes to inline autocomplete text.  Don't make changes if the user
  // is showing temporary text.  Making display changes would be obviously
  // wrong; making changes to the inline_autocomplete_text_ itself turns out to
  // be more subtlely wrong, because it means hitting esc will no longer revert
  // to the original state before arrowing.
  if (!has_temporary_text_) {
    inline_autocomplete_text_ = text;
    if (view_->OnInlineAutocompleteTextMaybeChanged(
        DisplayTextFromUserText(user_text_ + inline_autocomplete_text_),
        DisplayTextFromUserText(user_text_).length()))
      return;
  }

  // If the above changes didn't warrant a text update but we did change keyword
  // state, we have yet to notify the controller about it.
  if (keyword_state_changed)
    controller_->OnChanged();
}

bool AutocompleteEditModel::OnAfterPossibleChange(const std::wstring& new_text,
                                                  bool selection_differs,
                                                  bool text_differs,
                                                  bool just_deleted_text,
                                                  bool at_end_of_edit) {
  // Update the paste state as appropriate: if we're just finishing a paste
  // that replaced all the text, preserve that information; otherwise, if we've
  // made some other edit, clear paste tracking.
  if (paste_state_ == REPLACING_ALL)
    paste_state_ = REPLACED_ALL;
  else if (text_differs)
    paste_state_ = NONE;

  // Modifying the selection counts as accepting the autocompleted text.
  const bool user_text_changed =
      text_differs || (selection_differs && !inline_autocomplete_text_.empty());

  // If something has changed while the control key is down, prevent
  // "ctrl-enter" until the control key is released.  When we do this, we need
  // to update the popup if it's open, since the desired_tld will have changed.
  if ((text_differs || selection_differs) &&
      (control_key_state_ == DOWN_WITHOUT_CHANGE)) {
    control_key_state_ = DOWN_WITH_CHANGE;
    if (!text_differs && !popup_->IsOpen())
      return false;  // Don't open the popup for no reason.
  } else if (!user_text_changed) {
    return false;
  }

  const bool had_keyword = KeywordIsSelected();

  // If the user text has not changed, we do not want to change the model's
  // state associated with the text.  Otherwise, we can get surprising behavior
  // where the autocompleted text unexpectedly reappears, e.g. crbug.com/55983
  if (user_text_changed) {
    InternalSetUserText(UserTextFromDisplayText(new_text));
    has_temporary_text_ = false;

    // Track when the user has deleted text so we won't allow inline
    // autocomplete.
    just_deleted_text_ = just_deleted_text;
  }

  // Disable the fancy keyword UI if the user didn't already have a visible
  // keyword and is not at the end of the edit.  This prevents us from showing
  // the fancy UI (and interrupting the user's editing) if the user happens to
  // have a keyword for 'a', types 'ab' then puts a space between the 'a' and
  // the 'b'.
  if (!had_keyword)
    keyword_ui_state_ = at_end_of_edit ? NORMAL : NO_KEYWORD;

  view_->UpdatePopup();

  if (had_keyword) {
    if (is_keyword_hint_ || keyword_.empty())
      keyword_ui_state_ = NORMAL;
  } else if ((keyword_ui_state_ != NO_KEYWORD) && !is_keyword_hint_ &&
             !keyword_.empty()) {
    // Went from no selected keyword to a selected keyword.
    keyword_ui_state_ = KEYWORD;
  }

  return true;
}

void AutocompleteEditModel::PopupBoundsChangedTo(const gfx::Rect& bounds) {
  controller_->OnPopupBoundsChanged(bounds);
}

void AutocompleteEditModel::ResultsUpdated() {
  UpdateSuggestedSearchText();
}

// Return true if the suggestion type warrants a TCP/IP preconnection.
// i.e., it is now highly likely that the user will select the related domain.
static bool IsPreconnectable(AutocompleteMatch::Type type) {
  UMA_HISTOGRAM_ENUMERATION("Autocomplete.MatchType", type,
                            AutocompleteMatch::NUM_TYPES);
  switch (type) {
    // Matches using the user's default search engine.
    case AutocompleteMatch::SEARCH_WHAT_YOU_TYPED:
    case AutocompleteMatch::SEARCH_HISTORY:
    case AutocompleteMatch::SEARCH_SUGGEST:
    // A match that uses a non-default search engine (e.g. for tab-to-search).
    case AutocompleteMatch::SEARCH_OTHER_ENGINE:
      return true;

    default:
      return false;
  }
}

void AutocompleteEditModel::Observe(NotificationType type,
                                    const NotificationSource& source,
                                    const NotificationDetails& details) {
  DCHECK_EQ(NotificationType::AUTOCOMPLETE_CONTROLLER_DEFAULT_MATCH_UPDATED,
            type.value);

  std::wstring inline_autocomplete_text;
  std::wstring keyword;
  bool is_keyword_hint = false;
  const AutocompleteResult* result =
      Details<const AutocompleteResult>(details).ptr();
  const AutocompleteResult::const_iterator match(result->default_match());
  if (match != result->end()) {
    if ((match->inline_autocomplete_offset != std::wstring::npos) &&
        (match->inline_autocomplete_offset < match->fill_into_edit.length())) {
      inline_autocomplete_text =
          match->fill_into_edit.substr(match->inline_autocomplete_offset);
    }

    if (!match->destination_url.SchemeIs(chrome::kExtensionScheme)) {
      // Warm up DNS Prefetch cache, or preconnect to a search service.
      chrome_browser_net::AnticipateOmniboxUrl(match->destination_url,
                                               IsPreconnectable(match->type));
    }

    // We could prefetch the alternate nav URL, if any, but because there
    // can be many of these as a user types an initial series of characters,
    // the OS DNS cache could suffer eviction problems for minimal gain.

    is_keyword_hint = popup_->GetKeywordForMatch(*match, &keyword);
  }

  OnPopupDataChanged(inline_autocomplete_text, NULL, keyword, is_keyword_hint);
}

void AutocompleteEditModel::InternalSetUserText(const std::wstring& text) {
  user_text_ = text;
  just_deleted_text_ = false;
  inline_autocomplete_text_.clear();
}

bool AutocompleteEditModel::KeywordIsSelected() const {
  return ((keyword_ui_state_ != NO_KEYWORD) && !is_keyword_hint_ &&
          !keyword_.empty());
}

std::wstring AutocompleteEditModel::DisplayTextFromUserText(
    const std::wstring& text) const {
  return KeywordIsSelected() ?
      KeywordProvider::SplitReplacementStringFromInput(text) : text;
}

std::wstring AutocompleteEditModel::UserTextFromDisplayText(
    const std::wstring& text) const {
  return KeywordIsSelected() ? (keyword_ + L" " + text) : text;
}

void AutocompleteEditModel::GetInfoForCurrentText(
    AutocompleteMatch* match,
    GURL* alternate_nav_url) const {
  if (popup_->IsOpen() || query_in_progress()) {
    popup_->InfoForCurrentSelection(match, alternate_nav_url);
  } else {
    profile_->GetAutocompleteClassifier()->Classify(
        UserTextFromDisplayText(view_->GetText()), GetDesiredTLD(), match,
        alternate_nav_url);
  }
}

// Returns true if suggested search text should be shown for the specified match
// type.
static bool ShouldShowSuggestSearchTextFor(AutocompleteMatch::Type type) {
  // TODO: add support for other engines when in keyword mode.
  return ((type == AutocompleteMatch::SEARCH_HISTORY) ||
          (type == AutocompleteMatch::SEARCH_SUGGEST));
}

void AutocompleteEditModel::UpdateSuggestedSearchText() {
  if (!InstantController::IsEnabled(profile_, InstantController::VERBATIM_TYPE))
    return;

  string16 suggested_text;
  // The suggested text comes from the first search result.
  if (popup_->IsOpen()) {
    const AutocompleteResult& result = popup_->result();
    if ((result.size() > 1) && (popup_->selected_line() == 0) &&
        ((result.begin()->inline_autocomplete_offset == std::wstring::npos) ||
         (result.begin()->inline_autocomplete_offset ==
          result.begin()->fill_into_edit.size()))) {
      for (AutocompleteResult::const_iterator i = result.begin() + 1;
           i != result.end(); ++i) {
        // TODO: add support for other engines when in keyword mode.
        if (ShouldShowSuggestSearchTextFor(i->type) &&
            i->inline_autocomplete_offset != std::wstring::npos) {
          suggested_text = WideToUTF16(i->fill_into_edit.substr(
                                           i->inline_autocomplete_offset));
          break;
        }
      }
    }
  }
  controller_->OnSetSuggestedSearchText(suggested_text);
}
