// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_PERSONAL_DATA_MANAGER_H_
#define CHROME_BROWSER_AUTOFILL_PERSONAL_DATA_MANAGER_H_

#include <set>
#include <vector>

#include "base/observer_list.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/scoped_vector.h"
#include "base/string16.h"
#include "chrome/browser/autofill/autofill_dialog.h"
#include "chrome/browser/autofill/autofill_profile.h"
#include "chrome/browser/autofill/credit_card.h"
#include "chrome/browser/autofill/field_types.h"
#include "chrome/browser/webdata/web_data_service.h"

class AutoFillManager;
class FormStructure;
class Profile;

// Handles loading and saving AutoFill profile information to the web database.
// This class also stores the profiles loaded from the database for use during
// AutoFill.
class PersonalDataManager
    : public WebDataServiceConsumer,
      public AutoFillDialogObserver,
      public base::RefCountedThreadSafe<PersonalDataManager> {
 public:
  // An interface the PersonalDataManager uses to notify its clients (observers)
  // when it has finished loading personal data from the web database.  Register
  // the observer via PersonalDataManager::SetObserver.
  class Observer {
   public:
    // Notifies the observer that the PersonalDataManager has finished loading.
    // TODO: OnPersonalDataLoaded should be nuked in favor of only
    // OnPersonalDataChanged.
    virtual void OnPersonalDataLoaded() = 0;

    // Notifies the observer that the PersonalDataManager changed in some way.
    virtual void OnPersonalDataChanged() {}

   protected:
    virtual ~Observer() {}
  };

  // WebDataServiceConsumer implementation:
  virtual void OnWebDataServiceRequestDone(WebDataService::Handle h,
                                           const WDTypedResult* result);

  // AutoFillDialogObserver implementation:
  virtual void OnAutoFillDialogApply(std::vector<AutoFillProfile>* profiles,
                                     std::vector<CreditCard>* credit_cards);

  // Sets the listener to be notified of PersonalDataManager events.
  virtual void SetObserver(PersonalDataManager::Observer* observer);

  // Removes |observer| as the observer of this PersonalDataManager.
  virtual void RemoveObserver(PersonalDataManager::Observer* observer);

  // If AutoFill is able to determine the field types of a significant number of
  // field types that contain information in the FormStructures a profile will
  // be created with all of the information from recognized fields. Returns
  // whether a profile was created.
  bool ImportFormData(const std::vector<FormStructure*>& form_structures,
                      AutoFillManager* autofill_manager);

  // Gets |imported_profile_| and |imported_credit_card_| and returns their
  // values in |profile| and |credit_card| parameters respectively.  One or
  // both may return NULL.  The objects returned are owned by the
  // PersonalDataManager, so should be considered weak references by caller.
  // TODO(dhollowa) Now that we aren't immediately saving the imported form
  // data, we should store the profile and CC in the AFM instead of the PDM.
  void GetImportedFormData(AutoFillProfile** profile, CreditCard** credit_card);

  // Saves a credit card value detected in |ImportedFormData|.
  void SaveImportedCreditCard();

  // Sets |web_profiles_| to the contents of |profiles| and updates the web
  // database by adding, updating and removing profiles.  Sets the unique ID of
  // newly-added profiles.
  //
  // The relationship between this and Refresh is subtle.
  // A call to SetProfile could include out-of-date data that may conflict
  // if we didn't refresh-to-latest before an AutoFill window was opened for
  // editing. SetProfile is implemented to make a "best effort" to apply the
  // changes, but in extremely rare edge cases it is possible not all of the
  // updates in |profiles| make it to the DB.  This is why SetProfiles will
  // invoke Refresh after finishing, to ensure we get into a
  // consistent state.  See Refresh for details.
  void SetProfiles(std::vector<AutoFillProfile>* profiles);

  // Sets |credit_cards_| to the contents of |credit_cards| and updates the web
  // database by adding, updating and removing credit cards.  Sets the unique
  // ID of newly-added profiles.
  void SetCreditCards(std::vector<CreditCard>* credit_cards);

  // Gets the possible field types for the given text, determined by matching
  // the text with all known personal information and returning matching types.
  void GetPossibleFieldTypes(const string16& text,
                             FieldTypeSet* possible_types);

  // Returns true if the credit card information is stored with a password.
  bool HasPassword();

  // Returns whether the personal data has been loaded from the web database.
  virtual bool IsDataLoaded() const { return is_data_loaded_; }

  // This PersonalDataManager owns these profiles and credit cards.  Their
  // lifetime is until the web database is updated with new profile and credit
  // card information, respectively.  |profiles()| returns both web and
  // auxiliary profiles.  |web_profiles()| returns only web profiles.
  const std::vector<AutoFillProfile*>& profiles();
  virtual const std::vector<AutoFillProfile*>& web_profiles();
  virtual const std::vector<CreditCard*>& credit_cards() {
    return credit_cards_.get();
  }

  // Creates a profile labeled |label|, with it's own locally unique ID.
  // This must be called on the DB thread with the expectation that the
  // returned form will be synchronously persisted to the WebDatabase.  See
  // Refresh and SetProfiles for details.
  AutoFillProfile* CreateNewEmptyAutoFillProfileForDBThread(
      const string16& label);

  // Re-loads profiles and credit cards from the WebDatabase asynchronously.
  // In the general case, this is a no-op and will re-create the same
  // in-memory model as existed prior to the call.  If any change occurred to
  // profiles in the WebDatabase directly, as is the case if the browser sync
  // engine processed a change from the cloud, we will learn of these as a
  // result of this call.
  //
  // Note that there is a subtle relationship with ID generation.  IDs can be
  // generated by CreateNewEmptyAutoFillProfileForDBThread (in a synchronized
  // way), meaning that it is possible we are aware of this new profile only
  // by having it's ID tracked in unique_profile_ids_ for a period of time.
  // Because the expectation of that call is that the ID we generate will be
  // synchronously persisted to the DB, we are guaranteed to read it via
  // the next call to Refresh.  It could get deleted before we
  // manage, but this is safe (we just hold on to the ID a bit longer).
  //
  // Also see SetProfile for more details.
  virtual void Refresh();

  // Kicks off asynchronous loading of profiles and credit cards.
  void Init(Profile* profile);

 protected:
  // Make sure that only Profile and certain tests can create an instance of
  // PersonalDataManager.
  friend class base::RefCountedThreadSafe<PersonalDataManager>;
  friend class PersonalDataManagerTest;
  friend class ProfileImpl;
  friend class ProfileSyncServiceAutofillTest;

  PersonalDataManager();
  ~PersonalDataManager();

  // Returns the profile of the tab contents.
  Profile* profile();

  // This will create and reserve a new unique ID for a profile.
  int CreateNextUniqueID(std::set<int>* unique_ids);

  // Loads the saved profiles from the web database.
  virtual void LoadProfiles();

  // Loads the auxiliary profiles.  Currently Mac only.
  void LoadAuxiliaryProfiles();

  // Loads the saved credit cards from the web database.
  virtual void LoadCreditCards();

  // Receives the loaded profiles from the web data service and stores them in
  // |credit_cards_|.
  void ReceiveLoadedProfiles(WebDataService::Handle h,
                             const WDTypedResult* result);

  // Receives the loaded credit cards from the web data service and stores them
  // in |credit_cards_|.
  void ReceiveLoadedCreditCards(WebDataService::Handle h,
                                const WDTypedResult* result);

  // Cancels a pending query to the web database.  |handle| is a pointer to the
  // query handle.
  void CancelPendingQuery(WebDataService::Handle* handle);

  // Ensures that all profile labels are unique by appending an increasing digit
  // to the end of non-unique labels.
  // TODO(jhawkins): Create a new interface for labeled entities and turn these
  // two methods into one.
  void SetUniqueProfileLabels(std::vector<AutoFillProfile>* profiles);
  void SetUniqueCreditCardLabels(std::vector<CreditCard>* credit_cards);

  // Saves |imported_profile_| to the WebDB if it exists.
  void SaveImportedProfile();

  // The profile hosting this PersonalDataManager.
  Profile* profile_;

  // True if personal data has been loaded from the web database.
  bool is_data_loaded_;

  // The set of already created unique IDs, shared by both profiles and credit
  // cards, since IDs must be unique among the two groups.
  std::set<int> unique_ids_;

  // The set of already created unique profile IDs, used to create a new unique
  // profile ID.
  std::set<int> unique_profile_ids_;

  // The set of already created unique credit card IDs, used to create a new
  // unique credit card ID.
  std::set<int> unique_creditcard_ids_;

  // Protects unique_*_ids_ members.
  Lock unique_ids_lock_;

  // The loaded web profiles.
  ScopedVector<AutoFillProfile> web_profiles_;

  // Auxiliary profiles.
  ScopedVector<AutoFillProfile> auxiliary_profiles_;

  // Storage for combined web and auxiliary profiles.  Contents are weak
  // references.  Lifetime managed by |web_profiles_| and |auxiliary_profiles_|.
  std::vector<AutoFillProfile*> profiles_;

  // The loaded credit cards.
  ScopedVector<CreditCard> credit_cards_;

  // The profile that is imported from a web form by ImportFormData.
  scoped_ptr<AutoFillProfile> imported_profile_;

  // The credit card that is imported from a web form by ImportFormData.
  scoped_ptr<CreditCard> imported_credit_card_;

  // The hash of the password used to store the credit card.  This is empty if
  // no password exists.
  string16 password_hash_;

  // When the manager makes a request from WebDataService, the database
  // is queried on another thread, we record the query handle until we
  // get called back.  We store handles for both profile and credit card queries
  // so they can be loaded at the same time.
  WebDataService::Handle pending_profiles_query_;
  WebDataService::Handle pending_creditcards_query_;

  // The observers.
  ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(PersonalDataManager);
};

#endif  // CHROME_BROWSER_AUTOFILL_PERSONAL_DATA_MANAGER_H_
