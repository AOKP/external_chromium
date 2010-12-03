// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/personal_data_manager.h"

#include <algorithm>
#include <iterator>

#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_field.h"
#include "chrome/browser/autofill/form_structure.h"
#include "chrome/browser/autofill/phone_number.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/pref_names.h"

namespace {

// The minimum number of fields that must contain user data and have known types
// before AutoFill will attempt to import the data into a profile.
const int kMinImportSize = 3;

template<typename T>
class FormGroupGUIDMatchesFunctor {
 public:
  explicit FormGroupGUIDMatchesFunctor(const std::string& guid) : guid_(guid) {}

  bool operator()(const T& form_group) {
    return form_group.guid() == guid_;
  }

 private:
  std::string guid_;
};

template<typename T>
class DereferenceFunctor {
 public:
  template<typename T_Iterator>
  const T& operator()(const T_Iterator& iterator) {
    return *iterator;
  }
};

template<typename T>
T* address_of(T& v) {
  return &v;
}

bool FindInProfilesByGUID(const std::vector<AutoFillProfile>& profiles,
                          const std::string& guid) {
  for (std::vector<AutoFillProfile>::const_iterator iter = profiles.begin();
       iter != profiles.end();
       ++iter) {
    if (iter->guid() == guid)
      return true;
  }
  return false;
}

bool FindInScopedProfilesByGUID(const ScopedVector<AutoFillProfile>& profiles,
                                const std::string& guid) {
  for (std::vector<AutoFillProfile*>::const_iterator iter = profiles.begin();
       iter != profiles.end();
       ++iter) {
    if ((*iter)->guid() == guid)
      return true;
  }
  return false;
}

bool FindInCreditCardsByGUID(const std::vector<CreditCard>& credit_cards,
                             const std::string& guid) {
  for (std::vector<CreditCard>::const_iterator iter = credit_cards.begin();
       iter != credit_cards.end();
       ++iter) {
    if (iter->guid() == guid)
      return true;
  }
  return false;
}

bool FindInScopedCreditCardsByGUID(
    const ScopedVector<CreditCard>& credit_cards, const std::string& guid) {
  for (std::vector<CreditCard*>::const_iterator iter =
          credit_cards.begin();
       iter != credit_cards.end();
       ++iter) {
    if ((*iter)->guid() == guid)
      return true;
  }
  return false;
}

}  // namespace

PersonalDataManager::~PersonalDataManager() {
  CancelPendingQuery(&pending_profiles_query_);
  CancelPendingQuery(&pending_creditcards_query_);
}

void PersonalDataManager::OnWebDataServiceRequestDone(
    WebDataService::Handle h,
    const WDTypedResult* result) {
  // Error from the web database.
  if (!result)
    return;

  DCHECK(pending_profiles_query_ || pending_creditcards_query_);
  DCHECK(result->GetType() == AUTOFILL_PROFILES_RESULT ||
         result->GetType() == AUTOFILL_CREDITCARDS_RESULT);

  switch (result->GetType()) {
    case AUTOFILL_PROFILES_RESULT:
      ReceiveLoadedProfiles(h, result);
      break;
    case AUTOFILL_CREDITCARDS_RESULT:
      ReceiveLoadedCreditCards(h, result);
      break;
    default:
      NOTREACHED();
  }

  // If both requests have responded, then all personal data is loaded.
  if (pending_profiles_query_ == 0 && pending_creditcards_query_ == 0) {
    is_data_loaded_ = true;
    std::vector<AutoFillProfile*> profile_pointers(web_profiles_.size());
    std::copy(web_profiles_.begin(), web_profiles_.end(),
              profile_pointers.begin());
    AutoFillProfile::AdjustInferredLabels(&profile_pointers);
    FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataLoaded());
  }
}

/////////////////////////////////////////////////////////////////////////////
// PersonalDataManager,
// views::ButtonListener implementations
void PersonalDataManager::OnAutoFillDialogApply(
    std::vector<AutoFillProfile>* profiles,
    std::vector<CreditCard>* credit_cards) {
  // |profiles| may be NULL.
  // |credit_cards| may be NULL.
  if (profiles) {
    CancelPendingQuery(&pending_profiles_query_);
    SetProfiles(profiles);
  }
  if (credit_cards) {
    CancelPendingQuery(&pending_creditcards_query_);
    SetCreditCards(credit_cards);
  }
}

void PersonalDataManager::SetObserver(PersonalDataManager::Observer* observer) {
  // TODO: RemoveObserver is for compatibility with old code, it should be
  // nuked.
  observers_.RemoveObserver(observer);
  observers_.AddObserver(observer);
}

void PersonalDataManager::RemoveObserver(
    PersonalDataManager::Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool PersonalDataManager::ImportFormData(
<<<<<<< HEAD
    const std::vector<FormStructure*>& form_structures,
    AutoFillManager* autofill_manager) {
#ifdef ANDROID
  // TODO: Is this the funcionality that tries to create a profile for the user
  // based on what they've entered into forms?
  return false;
#else
=======
    const std::vector<FormStructure*>& form_structures) {
>>>>>>> chromium.org at r66597
  // Parse the form and construct a profile based on the information that is
  // possible to import.
  int importable_fields = 0;
  int importable_credit_card_fields = 0;
  imported_profile_.reset(new AutoFillProfile);
  // TODO(jhawkins): Use a hash of the CC# instead of a list of unique IDs?
  imported_credit_card_.reset(new CreditCard);

  std::vector<FormStructure*>::const_iterator iter;
  for (iter = form_structures.begin(); iter != form_structures.end(); ++iter) {
    const FormStructure* form = *iter;
    for (size_t i = 0; i < form->field_count(); ++i) {
      const AutoFillField* field = form->field(i);
      string16 value = CollapseWhitespace(field->value(), false);

      // If we don't know the type of the field, or the user hasn't entered any
      // information into the field, then skip it.
      if (!field->IsFieldFillable() || value.empty())
        continue;

      AutoFillType field_type(field->type());
      FieldTypeGroup group(field_type.group());

      if (group == AutoFillType::CREDIT_CARD) {
        // If the user has a password set, we have no way of setting credit
        // card numbers.
        if (!HasPassword()) {
          imported_credit_card_->SetInfo(AutoFillType(field_type.field_type()),
                                         value);
          ++importable_credit_card_fields;
        }
      } else {
        // In the case of a phone number, if the whole phone number was entered
        // into a single field, then parse it and set the sub components.
        if (field_type.subgroup() == AutoFillType::PHONE_WHOLE_NUMBER) {
          string16 number;
          string16 city_code;
          string16 country_code;
          if (group == AutoFillType::PHONE_HOME) {
            PhoneNumber::ParsePhoneNumber(
                value, &number, &city_code, &country_code);
            imported_profile_->SetInfo(
                AutoFillType(PHONE_HOME_COUNTRY_CODE), country_code);
            imported_profile_->SetInfo(
                AutoFillType(PHONE_HOME_CITY_CODE), city_code);
            imported_profile_->SetInfo(
                AutoFillType(PHONE_HOME_NUMBER), number);
          } else if (group == AutoFillType::PHONE_FAX) {
            PhoneNumber::ParsePhoneNumber(
                value, &number, &city_code, &country_code);
            imported_profile_->SetInfo(
                AutoFillType(PHONE_FAX_COUNTRY_CODE), country_code);
            imported_profile_->SetInfo(
                AutoFillType(PHONE_FAX_CITY_CODE), city_code);
            imported_profile_->SetInfo(
                AutoFillType(PHONE_FAX_NUMBER), number);
          }
          continue;
        }

        imported_profile_->SetInfo(AutoFillType(field_type.field_type()),
                                   value);
        ++importable_fields;
      }
    }
  }

  // If the user did not enter enough information on the page then don't bother
  // importing the data.
  if (importable_fields + importable_credit_card_fields < kMinImportSize)
    return false;

  if (importable_fields == 0)
    imported_profile_.reset();

  if (importable_credit_card_fields == 0)
    imported_credit_card_.reset();

  if (imported_credit_card_.get()) {
    if (!CreditCard::IsCreditCardNumber(imported_credit_card_->GetFieldText(
          AutoFillType(CREDIT_CARD_NUMBER)))) {
      imported_credit_card_.reset();
    }
  }

  // Don't import if we already have this info.
  if (imported_credit_card_.get()) {
    for (std::vector<CreditCard*>::const_iterator iter = credit_cards_.begin();
         iter != credit_cards_.end();
         ++iter) {
      if (imported_credit_card_->IsSubsetOf(**iter)) {
        imported_credit_card_.reset();
        break;
      }
    }
  }

  // We always save imported profiles.
  SaveImportedProfile();

  return true;
#endif
}

void PersonalDataManager::GetImportedFormData(AutoFillProfile** profile,
                                              CreditCard** credit_card) {
  DCHECK(profile);
  DCHECK(credit_card);

  *profile = imported_profile_.get();
  *credit_card = imported_credit_card_.get();
}

void PersonalDataManager::SetProfiles(std::vector<AutoFillProfile>* profiles) {
  if (profile_->IsOffTheRecord())
    return;

  // Remove empty profiles from input.
  profiles->erase(
      std::remove_if(profiles->begin(), profiles->end(),
                     std::mem_fun_ref(&AutoFillProfile::IsEmpty)),
      profiles->end());

#ifndef ANDROID
  // Ensure that profile labels are up to date.  Currently, sync relies on
  // labels to identify a profile.
  // TODO(dhollowa): We need to deprecate labels and update the way sync
  // identifies profiles.
  std::vector<AutoFillProfile*> profile_pointers(profiles->size());
  std::transform(profiles->begin(), profiles->end(), profile_pointers.begin(),
      address_of<AutoFillProfile>);
  AutoFillProfile::AdjustInferredLabels(&profile_pointers);

  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Any profiles that are not in the new profile list should be removed from
  // the web database.
  for (std::vector<AutoFillProfile*>::const_iterator iter =
           web_profiles_.begin();
       iter != web_profiles_.end(); ++iter) {
    if (!FindInProfilesByGUID(*profiles, (*iter)->guid()))
      wds->RemoveAutoFillProfileGUID((*iter)->guid());
  }

  // Update the web database with the existing profiles.
  for (std::vector<AutoFillProfile>::iterator iter = profiles->begin();
       iter != profiles->end(); ++iter) {
    if (FindInScopedProfilesByGUID(web_profiles_, iter->guid()))
      wds->UpdateAutoFillProfileGUID(*iter);
  }

  // Add the new profiles to the web database.
  for (std::vector<AutoFillProfile>::iterator iter = profiles->begin();
       iter != profiles->end(); ++iter) {
    if (!FindInScopedProfilesByGUID(web_profiles_, iter->guid()))
      wds->AddAutoFillProfileGUID(*iter);
  }
#endif

  // Copy in the new profiles.
  web_profiles_.reset();
  for (std::vector<AutoFillProfile>::iterator iter = profiles->begin();
       iter != profiles->end(); ++iter) {
    web_profiles_.push_back(new AutoFillProfile(*iter));
  }

  // Read our writes to ensure consistency with the database.
  Refresh();

  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
}

void PersonalDataManager::SetCreditCards(
    std::vector<CreditCard>* credit_cards) {
#ifndef ANDROID
  // Android does not do credit cards and does not have a WebDataService.
  if (profile_->IsOffTheRecord())
    return;

  // Remove empty credit cards from input.
  credit_cards->erase(
      std::remove_if(
          credit_cards->begin(), credit_cards->end(),
          std::mem_fun_ref(&CreditCard::IsEmpty)),
      credit_cards->end());

  SetUniqueCreditCardLabels(credit_cards);

  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Any credit cards that are not in the new credit card list should be
  // removed.
  for (std::vector<CreditCard*>::const_iterator iter = credit_cards_.begin();
       iter != credit_cards_.end(); ++iter) {
    if (!FindInCreditCardsByGUID(*credit_cards, (*iter)->guid()))
      wds->RemoveCreditCardGUID((*iter)->guid());
  }

  // Update the web database with the existing credit cards.
  for (std::vector<CreditCard>::iterator iter = credit_cards->begin();
       iter != credit_cards->end(); ++iter) {
    if (FindInScopedCreditCardsByGUID(credit_cards_, iter->guid()))
      wds->UpdateCreditCardGUID(*iter);
  }

  // Add the new credit cards to the web database.
  for (std::vector<CreditCard>::iterator iter = credit_cards->begin();
       iter != credit_cards->end(); ++iter) {
    if (!FindInScopedCreditCardsByGUID(credit_cards_, iter->guid()))
      wds->AddCreditCardGUID(*iter);
  }

  // Copy in the new credit cards.
  credit_cards_.reset();
  for (std::vector<CreditCard>::iterator iter = credit_cards->begin();
       iter != credit_cards->end(); ++iter) {
    credit_cards_.push_back(new CreditCard(*iter));
  }

  // Read our writes to ensure consistency with the database.
  Refresh();

  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
#endif
}

// TODO(jhawkins): Refactor SetProfiles so this isn't so hacky.
void PersonalDataManager::AddProfile(const AutoFillProfile& profile) {
  // Don't save a web profile if the data in the profile is a subset of an
  // auxiliary profile.
  for (std::vector<AutoFillProfile*>::const_iterator iter =
           auxiliary_profiles_.begin();
       iter != auxiliary_profiles_.end(); ++iter) {
    if (profile.IsSubsetOf(**iter))
      return;
  }

  // Set to true if |profile| is merged into the profile list.
  bool merged = false;

  // First preference is to add missing values to an existing profile.
  // Only merge with the first match.
  std::vector<AutoFillProfile> profiles;
  for (std::vector<AutoFillProfile*>::const_iterator iter =
           web_profiles_.begin();
       iter != web_profiles_.end(); ++iter) {
    if (!merged) {
      if (profile.IsSubsetOf(**iter)) {
        // In this case, the existing profile already contains all of the data
        // in |profile|, so consider the profiles already merged.
        merged = true;
      } else if ((*iter)->IntersectionOfTypesHasEqualValues(profile)) {
        // |profile| contains all of the data in this profile, plus more.
        merged = true;
        (*iter)->MergeWith(profile);
      }
    }
    profiles.push_back(**iter);
  }

  // The second preference, if not merged above, is to alter non-primary values
  // where the primary values match.
  // Again, only merge with the first match.
  if (!merged) {
    profiles.clear();
    for (std::vector<AutoFillProfile*>::const_iterator iter =
             web_profiles_.begin();
         iter != web_profiles_.end(); ++iter) {
      if (!merged) {
        if (!profile.PrimaryValue().empty() &&
            (*iter)->PrimaryValue() == profile.PrimaryValue()) {
          merged = true;
          (*iter)->OverwriteWith(profile);
        }
      }
      profiles.push_back(**iter);
    }
  }

  // Finally, if the new profile was not merged with an existing profile then
  // add the new profile to the list.
  if (!merged)
    profiles.push_back(profile);

  SetProfiles(&profiles);
}

void PersonalDataManager::UpdateProfile(const AutoFillProfile& profile) {
#ifndef ANDROID
  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Update the cached profile.
  for (std::vector<AutoFillProfile*>::iterator iter = web_profiles_->begin();
       iter != web_profiles_->end(); ++iter) {
    if ((*iter)->guid() == profile.guid()) {
      delete *iter;
      *iter = new AutoFillProfile(profile);
      break;
    }
  }

  // Ensure that profile labels are up to date.
  AutoFillProfile::AdjustInferredLabels(&web_profiles_.get());

  wds->UpdateAutoFillProfileGUID(profile);
  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
#endif
}

void PersonalDataManager::RemoveProfile(const std::string& guid) {
  // TODO(jhawkins): Refactor SetProfiles so this isn't so hacky.
  std::vector<AutoFillProfile> profiles(web_profiles_.size());
  std::transform(web_profiles_.begin(), web_profiles_.end(),
                 profiles.begin(),
                 DereferenceFunctor<AutoFillProfile>());

  // Remove the profile that matches |guid|.
  profiles.erase(
      std::remove_if(profiles.begin(), profiles.end(),
                     FormGroupGUIDMatchesFunctor<AutoFillProfile>(guid)),
      profiles.end());

  SetProfiles(&profiles);
}

AutoFillProfile* PersonalDataManager::GetProfileByGUID(
    const std::string& guid) {
  for (std::vector<AutoFillProfile*>::iterator iter = web_profiles_->begin();
       iter != web_profiles_->end(); ++iter) {
    if ((*iter)->guid() == guid)
      return *iter;
  }
  return NULL;
}

// TODO(jhawkins): Refactor SetCreditCards so this isn't so hacky.
void PersonalDataManager::AddCreditCard(const CreditCard& credit_card) {
  std::vector<CreditCard> credit_cards(credit_cards_.size());
  std::transform(credit_cards_.begin(), credit_cards_.end(),
                 credit_cards.begin(),
                 DereferenceFunctor<CreditCard>());

  credit_cards.push_back(credit_card);
  SetCreditCards(&credit_cards);
}

void PersonalDataManager::UpdateCreditCard(const CreditCard& credit_card) {
#ifndef ANDROID
  WebDataService* wds = profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!wds)
    return;

  // Update the cached credit card.
  for (std::vector<CreditCard*>::iterator iter = credit_cards_->begin();
       iter != credit_cards_->end(); ++iter) {
    if ((*iter)->guid() == credit_card.guid()) {
      delete *iter;
      *iter = new CreditCard(credit_card);
      break;
    }
  }

  wds->UpdateCreditCardGUID(credit_card);
  FOR_EACH_OBSERVER(Observer, observers_, OnPersonalDataChanged());
#endif
}

void PersonalDataManager::RemoveCreditCard(const std::string& guid) {
  // TODO(jhawkins): Refactor SetCreditCards so this isn't so hacky.
  std::vector<CreditCard> credit_cards(credit_cards_.size());
  std::transform(credit_cards_.begin(), credit_cards_.end(),
                 credit_cards.begin(),
                 DereferenceFunctor<CreditCard>());

  // Remove the credit card that matches |guid|.
  credit_cards.erase(
      std::remove_if(credit_cards.begin(), credit_cards.end(),
                     FormGroupGUIDMatchesFunctor<CreditCard>(guid)),
      credit_cards.end());

  SetCreditCards(&credit_cards);
}

CreditCard* PersonalDataManager::GetCreditCardByGUID(const std::string& guid) {
  for (std::vector<CreditCard*>::iterator iter = credit_cards_.begin();
       iter != credit_cards_.end(); ++iter) {
    if ((*iter)->guid() == guid)
      return *iter;
  }
  return NULL;
}

void PersonalDataManager::GetPossibleFieldTypes(const string16& text,
                                                FieldTypeSet* possible_types) {
  string16 clean_info = StringToLowerASCII(CollapseWhitespace(text, false));
  if (clean_info.empty()) {
    possible_types->insert(EMPTY_TYPE);
    return;
  }

  for (ScopedVector<AutoFillProfile>::iterator iter = web_profiles_.begin();
       iter != web_profiles_.end(); ++iter) {
    const FormGroup* profile = *iter;
    if (!profile) {
      DLOG(ERROR) << "NULL information in profiles list";
      continue;
    }

    profile->GetPossibleFieldTypes(clean_info, possible_types);
  }

  for (ScopedVector<CreditCard>::iterator iter = credit_cards_.begin();
       iter != credit_cards_.end(); ++iter) {
    const FormGroup* credit_card = *iter;
    if (!credit_card) {
      DLOG(ERROR) << "NULL information in credit cards list";
      continue;
    }

    credit_card->GetPossibleFieldTypes(clean_info, possible_types);
  }

  if (possible_types->size() == 0)
    possible_types->insert(UNKNOWN_TYPE);
}

bool PersonalDataManager::HasPassword() {
  return !password_hash_.empty();
}

const std::vector<AutoFillProfile*>& PersonalDataManager::profiles() {
  // |profile_| is NULL in AutoFillManagerTest.
  if (!profile_)
    return web_profiles_.get();

  bool auxiliary_profiles_enabled = profile_->GetPrefs()->GetBoolean(
      prefs::kAutoFillAuxiliaryProfilesEnabled);

#if !defined(OS_MACOSX)
  DCHECK(!auxiliary_profiles_enabled)
      << "Auxiliary profiles supported on Mac only";
#endif

  if (auxiliary_profiles_enabled) {
    profiles_.clear();

    // Populates |auxiliary_profiles_|.
    LoadAuxiliaryProfiles();

    profiles_.insert(profiles_.end(),
        web_profiles_.begin(), web_profiles_.end());
    profiles_.insert(profiles_.end(),
        auxiliary_profiles_.begin(), auxiliary_profiles_.end());
    return profiles_;
  } else {
    return web_profiles_.get();
  }
}

const std::vector<AutoFillProfile*>& PersonalDataManager::web_profiles() {
  return web_profiles_.get();
}

AutoFillProfile* PersonalDataManager::CreateNewEmptyAutoFillProfileForDBThread(
    const string16& label) {
#ifdef ANDROID
  NOTREACHED();
  return 0;
#else
  // See comment in header for thread details.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  AutoFillProfile* p = new AutoFillProfile;
  p->set_label(label);
  return p;
#endif
}

void PersonalDataManager::Refresh() {
  LoadProfiles();
  LoadCreditCards();
}

PersonalDataManager::PersonalDataManager()
    : profile_(NULL),
      is_data_loaded_(false),
      pending_profiles_query_(0),
      pending_creditcards_query_(0) {
}

void PersonalDataManager::Init(Profile* profile) {
  profile_ = profile;
  LoadProfiles();
  LoadCreditCards();
}

void PersonalDataManager::LoadProfiles() {
#ifdef ANDROID
  // This shoud request the profile(s) from java land on Android.
  // Call to a java class that would read/write the data in a database.
  // WebAutoFillClientAndroid will inject a profile while we're testing.
#else
  WebDataService* web_data_service =
      profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!web_data_service) {
    NOTREACHED();
    return;
  }

  CancelPendingQuery(&pending_profiles_query_);

  pending_profiles_query_ = web_data_service->GetAutoFillProfiles(this);
#endif
}

// Win and Linux implementations do nothing.  Mac implementation fills in the
// contents of |auxiliary_profiles_|.
#if !defined(OS_MACOSX)
void PersonalDataManager::LoadAuxiliaryProfiles() {
}
#endif

void PersonalDataManager::LoadCreditCards() {
#ifndef ANDROID
  // Need a web database service on Android
  WebDataService* web_data_service =
      profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
  if (!web_data_service) {
    NOTREACHED();
    return;
  }

  CancelPendingQuery(&pending_creditcards_query_);

  pending_creditcards_query_ = web_data_service->GetCreditCards(this);
#endif
}

void PersonalDataManager::ReceiveLoadedProfiles(WebDataService::Handle h,
                                                const WDTypedResult* result) {
  DCHECK_EQ(pending_profiles_query_, h);

  pending_profiles_query_ = 0;
  web_profiles_.reset();

  const WDResult<std::vector<AutoFillProfile*> >* r =
      static_cast<const WDResult<std::vector<AutoFillProfile*> >*>(result);

  std::vector<AutoFillProfile*> profiles = r->GetValue();
  for (std::vector<AutoFillProfile*>::iterator iter = profiles.begin();
       iter != profiles.end(); ++iter) {
    web_profiles_.push_back(*iter);
  }
}

void PersonalDataManager::ReceiveLoadedCreditCards(
    WebDataService::Handle h, const WDTypedResult* result) {
  DCHECK_EQ(pending_creditcards_query_, h);

  pending_creditcards_query_ = 0;
  credit_cards_.reset();

  const WDResult<std::vector<CreditCard*> >* r =
      static_cast<const WDResult<std::vector<CreditCard*> >*>(result);

  std::vector<CreditCard*> credit_cards = r->GetValue();
  for (std::vector<CreditCard*>::iterator iter = credit_cards.begin();
       iter != credit_cards.end(); ++iter) {
    credit_cards_.push_back(*iter);
  }
}

void PersonalDataManager::CancelPendingQuery(WebDataService::Handle* handle) {
#ifndef ANDROID
  // TODO: We need to come up with a web data service class for Android
  if (*handle) {
    WebDataService* web_data_service =
        profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);
    if (!web_data_service) {
      NOTREACHED();
      return;
    }
    web_data_service->CancelRequest(*handle);
  }
  *handle = 0;
#endif
}

void PersonalDataManager::SetUniqueCreditCardLabels(
    std::vector<CreditCard>* credit_cards) {
  std::map<string16, std::vector<CreditCard*> > label_map;
  for (std::vector<CreditCard>::iterator iter = credit_cards->begin();
       iter != credit_cards->end(); ++iter) {
    label_map[iter->Label()].push_back(&(*iter));
  }

  for (std::map<string16, std::vector<CreditCard*> >::iterator iter =
           label_map.begin();
       iter != label_map.end(); ++iter) {
    // Start at the second element because the first label should not be
    // renamed.  The appended label number starts at 2, because the first label
    // has an implicit index of 1.
    for (size_t i = 1; i < iter->second.size(); ++i) {
      string16 newlabel = iter->second[i]->Label() +
          base::UintToString16(static_cast<unsigned int>(i + 1));
      iter->second[i]->set_label(newlabel);
    }
  }
}

void PersonalDataManager::SaveImportedProfile() {
#ifdef ANDROID
  // TODO: This should update the profile in Java land.
  return;
#else
  if (profile_->IsOffTheRecord())
    return;

  if (!imported_profile_.get())
    return;

  AddProfile(*imported_profile_);
#endif
}

// TODO(jhawkins): Refactor and merge this with SaveImportedProfile.
void PersonalDataManager::SaveImportedCreditCard() {
  if (profile_->IsOffTheRecord())
    return;

  if (!imported_credit_card_.get())
    return;

  // Set to true if |imported_credit_card_| is merged into the profile list.
  bool merged = false;

  std::vector<CreditCard> creditcards;
  for (std::vector<CreditCard*>::const_iterator iter =
           credit_cards_.begin();
       iter != credit_cards_.end(); ++iter) {
    if (imported_credit_card_->IsSubsetOf(**iter)) {
      // In this case, the existing credit card already contains all of the data
      // in |imported_credit_card_|, so consider the credit cards already
      // merged.
      merged = true;
    } else if ((*iter)->IntersectionOfTypesHasEqualValues(
        *imported_credit_card_)) {
      // |imported_profile| contains all of the data in this profile, plus more.
      merged = true;
      (*iter)->MergeWith(*imported_credit_card_);
    } else if (!imported_credit_card_->number().empty() &&
               (*iter)->number() == imported_credit_card_->number()) {
      merged = true;
      (*iter)->OverwriteWith(*imported_credit_card_);
    }

    creditcards.push_back(**iter);
  }

  if (!merged)
    creditcards.push_back(*imported_credit_card_);

  SetCreditCards(&creditcards);
}
