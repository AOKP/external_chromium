// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/autofill_manager.h"

#include <limits>
#include <string>

#include "app/l10n_util.h"
#include "base/basictypes.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#ifndef ANDROID
#include "chrome/browser/autofill/autofill_cc_infobar_delegate.h"
#endif
#include "chrome/browser/autofill/autofill_dialog.h"
#include "chrome/browser/autofill/form_structure.h"
#include "chrome/browser/autofill/select_control_handler.h"
#include "chrome/browser/guid.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#ifndef ANDROID
#include "chrome/browser/renderer_host/render_view_host.h"
#endif
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#ifndef ANDROID
#include "grit/generated_resources.h"
#endif
#include "webkit/glue/form_data.h"
#ifdef ANDROID
#include <WebCoreSupport/autofill/FormFieldAndroid.h>
#else
#include "webkit/glue/form_field.h"
#endif

using webkit_glue::FormData;
using webkit_glue::FormField;

namespace {

// We only send a fraction of the forms to upload server.
// The rate for positive/negative matches potentially could be different.
const double kAutoFillPositiveUploadRateDefaultValue = 0.01;
const double kAutoFillNegativeUploadRateDefaultValue = 0.01;

// Size and offset of the prefix and suffix portions of phone numbers.
const int kAutoFillPhoneNumberPrefixOffset = 0;
const int kAutoFillPhoneNumberPrefixCount = 3;
const int kAutoFillPhoneNumberSuffixOffset = 3;
const int kAutoFillPhoneNumberSuffixCount = 4;

const string16::value_type kCreditCardPrefix[] = {'*',0};
const string16::value_type kLabelSeparator[] = {';',' ','*',0};

// Removes duplicate elements whilst preserving original order of |elements| and
// |unique_ids|.
void RemoveDuplicateElements(
    std::vector<string16>* elements, std::vector<int>* unique_ids) {
  DCHECK_EQ(elements->size(), unique_ids->size());

  std::vector<string16> elements_copy;
  std::vector<int> unique_ids_copy;
  for (size_t i = 0; i < elements->size(); ++i) {
    const string16& element = (*elements)[i];

    bool unique = true;
    for (std::vector<string16>::const_iterator copy_iter
             = elements_copy.begin();
         copy_iter != elements_copy.end(); ++copy_iter) {
      if (element == *copy_iter) {
        unique = false;
        break;
      }
    }

    if (unique) {
      elements_copy.push_back(element);
      unique_ids_copy.push_back((*unique_ids)[i]);
    }
  }

  elements->assign(elements_copy.begin(), elements_copy.end());
  unique_ids->assign(unique_ids_copy.begin(), unique_ids_copy.end());
}

bool FormIsHTTPS(FormStructure* form) {
  return form->source_url().SchemeIs(chrome::kHttpsScheme);
}

}  // namespace

AutoFillManager::AutoFillManager(TabContents* tab_contents)
    : tab_contents_(tab_contents),
      personal_data_(NULL),
      download_manager_(tab_contents_->profile()),
      disable_download_manager_requests_(false),
      cc_infobar_(NULL) {
  DCHECK(tab_contents);

  // |personal_data_| is NULL when using TestTabContents.
  personal_data_ =
      tab_contents_->profile()->GetOriginalProfile()->GetPersonalDataManager();
  download_manager_.SetObserver(this);
}

AutoFillManager::~AutoFillManager() {
  download_manager_.SetObserver(NULL);
}

// static
void AutoFillManager::RegisterBrowserPrefs(PrefService* prefs) {
  prefs->RegisterDictionaryPref(prefs::kAutoFillDialogPlacement);
}

// static
void AutoFillManager::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterBooleanPref(prefs::kAutoFillEnabled, true);
#if defined(OS_MACOSX)
  prefs->RegisterBooleanPref(prefs::kAutoFillAuxiliaryProfilesEnabled, true);
#else
  prefs->RegisterBooleanPref(prefs::kAutoFillAuxiliaryProfilesEnabled, false);
#endif
  prefs->RegisterRealPref(prefs::kAutoFillPositiveUploadRate,
                          kAutoFillPositiveUploadRateDefaultValue);
  prefs->RegisterRealPref(prefs::kAutoFillNegativeUploadRate,
                          kAutoFillNegativeUploadRateDefaultValue);
}

void AutoFillManager::FormSubmitted(const FormData& form) {
  if (!IsAutoFillEnabled())
    return;

  if (tab_contents_->profile()->IsOffTheRecord())
    return;

  // Don't save data that was submitted through JavaScript.
  if (!form.user_submitted)
    return;

  // Grab a copy of the form data.
  upload_form_structure_.reset(new FormStructure(form));

  if (!upload_form_structure_->IsAutoFillable(true))
    return;

  // Determine the possible field types and upload the form structure to the
  // PersonalDataManager.
  DeterminePossibleFieldTypes(upload_form_structure_.get());
  HandleSubmit();
}

void AutoFillManager::FormsSeen(const std::vector<FormData>& forms) {
  if (!IsAutoFillEnabled())
    return;

  ParseForms(forms);
}

bool AutoFillManager::GetAutoFillSuggestions(bool field_autofilled,
                                             const FormField& field) {
  if (!IsAutoFillEnabled())
    return false;

#ifdef ANDROID
  // TODO: Refactor the Autofill methods for Chrome too so that
  // they do not neet to get the render view host here
  AutoFillHost* host = tab_contents_->autofill_host();
#else
  RenderViewHost* host = tab_contents_->render_view_host();
#endif
  if (!host)
    return false;

  if (personal_data_->profiles().empty() &&
      personal_data_->credit_cards().empty())
    return false;

  // Loops through the cached FormStructures looking for the FormStructure that
  // contains |field| and the associated AutoFillFieldType.
  FormStructure* form = NULL;
  AutoFillField* autofill_field = NULL;
  for (std::vector<FormStructure*>::iterator form_iter =
           form_structures_.begin();
       form_iter != form_structures_.end() && !autofill_field; ++form_iter) {
    form = *form_iter;

    // Don't send suggestions for forms that aren't auto-fillable.
    if (!form->IsAutoFillable(false))
      continue;

    for (std::vector<AutoFillField*>::const_iterator iter = form->begin();
         iter != form->end(); ++iter) {
      // The field list is terminated with a NULL AutoFillField, so don't try to
      // dereference it.
      if (!*iter)
        break;

      if ((**iter) == field) {
        autofill_field = *iter;
        break;
      }
    }
  }

  if (!autofill_field)
    return false;

  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  AutoFillType type(autofill_field->type());

  if (type.group() == AutoFillType::CREDIT_CARD) {
    GetCreditCardSuggestions(
        form, field, type, &values, &labels, &icons, &unique_ids);
  } else {
    GetProfileSuggestions(
        form, field, type, &values, &labels, &icons, &unique_ids);
  }

  DCHECK_EQ(values.size(), labels.size());
  DCHECK_EQ(values.size(), icons.size());
  DCHECK_EQ(values.size(), unique_ids.size());

  // No suggestions.
  if (values.empty())
    return false;

#ifndef ANDROID
  // Don't provide AutoFill suggestions when AutoFill is disabled, but provide a
  // warning to the user.
  if (!form->IsAutoFillable(true)) {
    values.assign(
        1, l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_FORM_DISABLED));
    labels.assign(1, string16());
    icons.assign(1, string16());
    unique_ids.assign(1, -1);
    host->AutoFillSuggestionsReturned(values, labels, icons, unique_ids);
    return true;
  }
#endif

#ifndef ANDROID
  // Don't provide credit card suggestions for non-HTTPS pages, but provide a
  // warning to the user.
  if (!FormIsHTTPS(form) && type.group() == AutoFillType::CREDIT_CARD) {
    values.assign(
        1, l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_INSECURE_CONNECTION));
    labels.assign(1, string16());
    icons.assign(1, string16());
    unique_ids.assign(1, -1);
    host->AutoFillSuggestionsReturned(values, labels, icons, unique_ids);
    return true;
  }
#endif

  // If the form is auto-filled and the renderer is querying for suggestions,
  // then the user is editing the value of a field.  In this case, mimick
  // autocomplete.  In particular, don't display labels, as that information is
  // redundant. In addition, remove duplicate values.
  if (field_autofilled) {
    RemoveDuplicateElements(&values, &unique_ids);
    labels.resize(values.size());
    icons.resize(values.size());
    unique_ids.resize(values.size());

    for (size_t i = 0; i < labels.size(); ++i) {
      labels[i] = string16();
      icons[i] = string16();
      unique_ids[i] = 0;
    }
  }

  host->AutoFillSuggestionsReturned(values, labels, icons, unique_ids);
  return true;
}

bool AutoFillManager::FillAutoFillFormData(int query_id,
                                           const FormData& form,
                                           int unique_id) {
  if (!IsAutoFillEnabled())
    return false;

#ifdef ANDROID
  AutoFillHost* host = tab_contents_->autofill_host();
#else
  RenderViewHost* host = tab_contents_->render_view_host();
#endif
  if (!host)
    return false;

  const std::vector<AutoFillProfile*>& profiles = personal_data_->profiles();
  const std::vector<CreditCard*>& credit_cards = personal_data_->credit_cards();

  // No data to return if the profiles are empty.
  if (profiles.empty() && credit_cards.empty())
    return false;

  // Find the FormStructure that corresponds to |form|.
  FormData result = form;
  FormStructure* form_structure = NULL;
  for (std::vector<FormStructure*>::const_iterator iter =
           form_structures_.begin();
       iter != form_structures_.end(); ++iter) {
    if (**iter == form) {
      form_structure = *iter;
      break;
    }
  }

  if (!form_structure)
    return false;

  // No data to return if there are no auto-fillable fields.
  if (!form_structure->autofill_count())
    return false;

  // Unpack the |unique_id| into component parts.
  std::string cc_guid;
  std::string profile_guid;
  UnpackGUIDs(unique_id, &cc_guid, &profile_guid);
  DCHECK(!guid::IsValidGUID(cc_guid) || !guid::IsValidGUID(profile_guid));

  // Find the profile that matches the |profile_id|, if one is specified.
  const AutoFillProfile* profile = NULL;
  if (guid::IsValidGUID(profile_guid)) {
    for (std::vector<AutoFillProfile*>::const_iterator iter = profiles.begin();
         iter != profiles.end(); ++iter) {
      if ((*iter)->guid() == profile_guid) {
        profile = *iter;
        break;
      }
    }
    DCHECK(profile);
  }

  // Find the credit card that matches the |cc_id|, if one is specified.
  const CreditCard* credit_card = NULL;
  if (guid::IsValidGUID(cc_guid)) {
    for (std::vector<CreditCard*>::const_iterator iter = credit_cards.begin();
         iter != credit_cards.end(); ++iter) {
      if ((*iter)->guid() == cc_guid) {
        credit_card = *iter;
        break;
      }
    }
    DCHECK(credit_card);
  }

  if (!profile && !credit_card)
    return false;

  // The list of fields in |form_structure| and |result.fields| often match
  // directly and we can fill these corresponding fields; however, when the
  // |form_structure| and |result.fields| do not match directly we search
  // ahead in the |form_structure| for the matching field.
  // See unit tests: AutoFillManagerTest.FormChangesRemoveField and
  // AutoFillManagerTest.FormChangesAddField for usage.
  for (size_t i = 0, j = 0;
       i < form_structure->field_count() && j < result.fields.size();
       j++) {
    size_t k = i;

    // Search forward in the |form_structure| for a corresponding field.
    while (k < form_structure->field_count() &&
           *form_structure->field(k) != result.fields[j]) {
      k++;
    }

    // If we've found a match then fill the |result| field with the found
    // field in the |form_structure|.
    if (k >= form_structure->field_count())
      continue;

    const AutoFillField* field = form_structure->field(k);
    AutoFillType autofill_type(field->type());
    if (credit_card &&
        autofill_type.group() == AutoFillType::CREDIT_CARD) {
      FillCreditCardFormField(credit_card, autofill_type, &result.fields[j]);
    } else if (profile) {
      FillFormField(profile, autofill_type, &result.fields[j]);
    }

    // We found a matching field in the |form_structure| so we
    // proceed to the next |result| field, and the next |form_structure|.
    ++i;
  }
  autofilled_forms_signatures_.push_front(form_structure->FormSignature());

  host->AutoFillFormDataFilled(query_id, result);
  return true;
}

void AutoFillManager::ShowAutoFillDialog() {
#ifndef ANDROID
  ::ShowAutoFillDialog(tab_contents_->GetContentNativeView(),
                       personal_data_,
                       tab_contents_->profile()->GetOriginalProfile());
#endif
}

void AutoFillManager::Reset() {
  upload_form_structure_.reset();
  form_structures_.reset();
}

void AutoFillManager::OnLoadedAutoFillHeuristics(
    const std::string& heuristic_xml) {
  // TODO(jhawkins): Store |upload_required| in the AutoFillManager.
  UploadRequired upload_required;
  FormStructure::ParseQueryResponse(heuristic_xml,
                                    form_structures_.get(),
                                    &upload_required);
}

void AutoFillManager::OnUploadedAutoFillHeuristics(
    const std::string& form_signature) {
}

void AutoFillManager::OnHeuristicsRequestError(
    const std::string& form_signature,
    AutoFillDownloadManager::AutoFillRequestType request_type,
    int http_error) {
}

bool AutoFillManager::IsAutoFillEnabled() const {
#ifdef ANDROID
  // TODO: This should be a setting in the android UI.
  return true;
#endif
  PrefService* prefs = tab_contents_->profile()->GetPrefs();

  // Migrate obsolete AutoFill pref.
  if (prefs->FindPreference(prefs::kFormAutofillEnabled)) {
    bool enabled = prefs->GetBoolean(prefs::kFormAutofillEnabled);
    prefs->ClearPref(prefs::kFormAutofillEnabled);
    prefs->SetBoolean(prefs::kAutoFillEnabled, enabled);
    return enabled;
  }

  return prefs->GetBoolean(prefs::kAutoFillEnabled);
}

void AutoFillManager::DeterminePossibleFieldTypes(
    FormStructure* form_structure) {
  for (size_t i = 0; i < form_structure->field_count(); i++) {
    const AutoFillField* field = form_structure->field(i);
    FieldTypeSet field_types;
    personal_data_->GetPossibleFieldTypes(field->value(), &field_types);
    form_structure->set_possible_types(i, field_types);
  }
}

void AutoFillManager::HandleSubmit() {
  // If there wasn't enough data to import then we don't want to send an upload
  // to the server.
  // TODO(jhawkins): Import form data from |form_structures_|.  That will
  // require querying the FormManager for updated field values.
  std::vector<FormStructure*> import;
  import.push_back(upload_form_structure_.get());
  if (!personal_data_->ImportFormData(import))
    return;

  // Did we get credit card info?
  AutoFillProfile* profile;
  CreditCard* credit_card;
  personal_data_->GetImportedFormData(&profile, &credit_card);

  if (!credit_card) {
    UploadFormData();
    return;
  }

#ifndef ANDROID
  // Show an infobar to offer to save the credit card info.
  if (tab_contents_) {
    tab_contents_->AddInfoBar(new AutoFillCCInfoBarDelegate(tab_contents_,
                                                            this));
  }
#endif
}

void AutoFillManager::UploadFormData() {
  if (!disable_download_manager_requests_ && upload_form_structure_.get()) {
    bool was_autofilled = false;
    // Check if the form among last 3 forms that were auto-filled.
    // Clear older signatures.
    std::list<std::string>::iterator it;
    int total_form_checked = 0;
    for (it = autofilled_forms_signatures_.begin();
         it != autofilled_forms_signatures_.end() && total_form_checked < 3;
         ++it, ++total_form_checked) {
      if (*it == upload_form_structure_->FormSignature())
        was_autofilled = true;
    }
    // Remove outdated form signatures.
    if (total_form_checked == 3 && it != autofilled_forms_signatures_.end()) {
      autofilled_forms_signatures_.erase(it,
                                         autofilled_forms_signatures_.end());
    }
    download_manager_.StartUploadRequest(*(upload_form_structure_.get()),
                                         was_autofilled);
  }
}

void AutoFillManager::OnInfoBarClosed(bool should_save) {
  if (should_save)
    personal_data_->SaveImportedCreditCard();
  UploadFormData();
}

AutoFillManager::AutoFillManager()
    : tab_contents_(NULL),
      personal_data_(NULL),
      download_manager_(NULL),
      disable_download_manager_requests_(false),
      cc_infobar_(NULL) {
}

AutoFillManager::AutoFillManager(TabContents* tab_contents,
                                 PersonalDataManager* personal_data)
    : tab_contents_(tab_contents),
      personal_data_(personal_data),
      download_manager_(NULL),
      disable_download_manager_requests_(false),
      cc_infobar_(NULL) {
  DCHECK(tab_contents);
}

void AutoFillManager::GetProfileSuggestions(FormStructure* form,
                                            const FormField& field,
                                            AutoFillType type,
                                            std::vector<string16>* values,
                                            std::vector<string16>* labels,
                                            std::vector<string16>* icons,
                                            std::vector<int>* unique_ids) {
  const std::vector<AutoFillProfile*>& profiles = personal_data_->profiles();
  std::vector<AutoFillProfile*> matched_profiles;
  for (std::vector<AutoFillProfile*>::const_iterator iter = profiles.begin();
       iter != profiles.end(); ++iter) {
    AutoFillProfile* profile = *iter;

    // The value of the stored data for this field type in the |profile|.
    string16 profile_field_value = profile->GetFieldText(type);

    if (!profile_field_value.empty() &&
        StartsWith(profile_field_value, field.value(), false)) {
      matched_profiles.push_back(profile);
      values->push_back(profile_field_value);
      unique_ids->push_back(PackGUIDs(std::string(), profile->guid()));
    }
  }

  std::vector<AutoFillFieldType> form_fields;
  form_fields.reserve(form->field_count());
  for (std::vector<AutoFillField*>::const_iterator iter = form->begin();
       iter != form->end(); ++iter) {
    // The field list is terminated with a NULL AutoFillField, so don't try to
    // dereference it.
    if (!*iter)
      break;
    form_fields.push_back((*iter)->type());
  }

  AutoFillProfile::CreateInferredLabels(&matched_profiles, labels, 0,
                                        type.field_type(), &form_fields);

  // No icons for profile suggestions.
  icons->resize(values->size());
}

void AutoFillManager::GetCreditCardSuggestions(FormStructure* form,
                                               const FormField& field,
                                               AutoFillType type,
                                               std::vector<string16>* values,
                                               std::vector<string16>* labels,
                                               std::vector<string16>* icons,
                                               std::vector<int>* unique_ids) {
  for (std::vector<CreditCard*>::const_iterator iter =
           personal_data_->credit_cards().begin();
       iter != personal_data_->credit_cards().end(); ++iter) {
    CreditCard* credit_card = *iter;

    // The value of the stored data for this field type in the |credit_card|.
    string16 creditcard_field_value = credit_card->GetFieldText(type);
    if (!creditcard_field_value.empty() &&
        StartsWith(creditcard_field_value, field.value(), false)) {
      if (type.field_type() == CREDIT_CARD_NUMBER)
        creditcard_field_value = credit_card->ObfuscatedNumber();

      values->push_back(creditcard_field_value);
      labels->push_back(kCreditCardPrefix + credit_card->LastFourDigits());
      icons->push_back(credit_card->type());
      unique_ids->push_back(PackGUIDs(credit_card->guid(), std::string()));
    }
  }
}

void AutoFillManager::FillCreditCardFormField(const CreditCard* credit_card,
                                              AutoFillType type,
                                              webkit_glue::FormField* field) {
  DCHECK(credit_card);
  DCHECK(field);

  if (field->form_control_type() == ASCIIToUTF16("select-one"))
    autofill::FillSelectControl(credit_card, type, field);
  else
    field->set_value(credit_card->GetFieldText(type));
}

void AutoFillManager::FillFormField(const AutoFillProfile* profile,
                                    AutoFillType type,
                                    webkit_glue::FormField* field) {
  DCHECK(profile);
  DCHECK(field);

  if (type.subgroup() == AutoFillType::PHONE_NUMBER) {
    FillPhoneNumberField(profile, field);
  } else {
    if (field->form_control_type() == ASCIIToUTF16("select-one"))
      autofill::FillSelectControl(profile, type, field);
    else
      field->set_value(profile->GetFieldText(type));
  }
}

void AutoFillManager::FillPhoneNumberField(const AutoFillProfile* profile,
                                           webkit_glue::FormField* field) {
  // If we are filling a phone number, check to see if the size field
  // matches the "prefix" or "suffix" sizes and fill accordingly.
  string16 number = profile->GetFieldText(AutoFillType(PHONE_HOME_NUMBER));
  bool has_valid_suffix_and_prefix = (number.length() ==
      (kAutoFillPhoneNumberPrefixCount + kAutoFillPhoneNumberSuffixCount));
  if (has_valid_suffix_and_prefix &&
      field->size() == kAutoFillPhoneNumberPrefixCount) {
    number = number.substr(kAutoFillPhoneNumberPrefixOffset,
                           kAutoFillPhoneNumberPrefixCount);
    field->set_value(number);
  } else if (has_valid_suffix_and_prefix &&
             field->size() == kAutoFillPhoneNumberSuffixCount) {
    number = number.substr(kAutoFillPhoneNumberSuffixOffset,
                           kAutoFillPhoneNumberSuffixCount);
    field->set_value(number);
  } else {
    field->set_value(number);
  }
}

void AutoFillManager::ParseForms(const std::vector<FormData>& forms) {
  std::vector<FormStructure *> non_queryable_forms;
  for (std::vector<FormData>::const_iterator iter = forms.begin();
       iter != forms.end(); ++iter) {
    scoped_ptr<FormStructure> form_structure(new FormStructure(*iter));
    if (!form_structure->ShouldBeParsed(false))
      continue;

    DeterminePossibleFieldTypes(form_structure.get());

    // Set aside forms with method GET so that they are not included in the
    // query to the server.
    if (form_structure->ShouldBeParsed(true))
      form_structures_.push_back(form_structure.release());
    else
      non_queryable_forms.push_back(form_structure.release());
  }

  // If none of the forms were parsed, no use querying the server.
  if (!form_structures_.empty() && !disable_download_manager_requests_)
    download_manager_.StartQueryRequest(form_structures_);

  for (std::vector<FormStructure *>::const_iterator iter =
           non_queryable_forms.begin();
       iter != non_queryable_forms.end(); ++iter) {
    form_structures_.push_back(*iter);
  }
}

// When sending IDs (across processes) to the renderer we pack credit card and
// profile IDs into a single integer.  Credit card IDs are sent in the high
// word and profile IDs are sent in the low word.
int AutoFillManager::PackGUIDs(const std::string& cc_guid,
                               const std::string& profile_guid) {
  int cc_id = GUIDToID(cc_guid);
  int profile_id = GUIDToID(profile_guid);

  DCHECK(cc_id <= std::numeric_limits<unsigned short>::max());
  DCHECK(profile_id <= std::numeric_limits<unsigned short>::max());

  return cc_id << std::numeric_limits<unsigned short>::digits | profile_id;
}

// When receiving IDs (across processes) from the renderer we unpack credit card
// and profile IDs from a single integer.  Credit card IDs are stored in the
// high word and profile IDs are stored in the low word.
void AutoFillManager::UnpackGUIDs(int id,
                                  std::string* cc_guid,
                                  std::string* profile_guid) {
  int cc_id = id >> std::numeric_limits<unsigned short>::digits &
      std::numeric_limits<unsigned short>::max();
  int profile_id = id & std::numeric_limits<unsigned short>::max();

  *cc_guid = IDToGUID(cc_id);
  *profile_guid = IDToGUID(profile_id);
}

int AutoFillManager::GUIDToID(const std::string& guid) {
  static int last_id = 1;

  if (!guid::IsValidGUID(guid))
    return 0;

  std::map<std::string, int>::const_iterator iter = guid_id_map_.find(guid);
  if (iter == guid_id_map_.end()) {
    guid_id_map_[guid] = last_id;
    id_guid_map_[last_id] = guid;
    return last_id++;
  } else {
    return iter->second;
  }
}

const std::string AutoFillManager::IDToGUID(int id) {
  if (id == 0)
    return std::string();

  std::map<int, std::string>::const_iterator iter = id_guid_map_.find(id);
  if (iter == id_guid_map_.end()) {
    NOTREACHED();
    return std::string();
  }

  return iter->second;
}
