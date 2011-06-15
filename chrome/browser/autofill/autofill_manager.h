// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_AUTOFILL_MANAGER_H_
#define CHROME_BROWSER_AUTOFILL_AUTOFILL_MANAGER_H_
#pragma once

#include <list>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/scoped_ptr.h"
#include "base/scoped_vector.h"
#include "chrome/browser/autofill/autofill_dialog.h"
#include "chrome/browser/autofill/autofill_download.h"
#include "chrome/browser/autofill/personal_data_manager.h"
<<<<<<< HEAD
#ifndef ANDROID
#include "chrome/browser/renderer_host/render_view_host_delegate.h"
#endif
=======
#include "chrome/browser/tab_contents/tab_contents_observer.h"
>>>>>>> chromium.org at r11.0.672.0

#ifndef ANDROID
class AutoFillCCInfoBarDelegate;
#endif
class AutoFillProfile;
class AutoFillMetrics;
class CreditCard;
class FormStructure;
class PrefService;
class RenderViewHost;

#ifdef ANDROID
class AutoFillHost;
#endif

namespace webkit_glue {
struct FormData;
class FormField;
}  // namespace webkit_glue

// Manages saving and restoring the user's personal information entered into web
// forms.
<<<<<<< HEAD
class AutoFillManager :
#ifndef ANDROID
  public RenderViewHostDelegate::AutoFill,
#endif
  public AutoFillDownloadManager::Observer {
=======
class AutoFillManager : public TabContentsObserver,
                        public AutoFillDownloadManager::Observer {
>>>>>>> chromium.org at r11.0.672.0
 public:
  explicit AutoFillManager(TabContents* tab_contents);
  virtual ~AutoFillManager();

  // Registers our browser prefs.
  static void RegisterBrowserPrefs(PrefService* prefs);

  // Registers our Enable/Disable AutoFill pref.
  static void RegisterUserPrefs(PrefService* prefs);

  // Returns the TabContents hosting this AutoFillManager.
  TabContents* tab_contents() const { return tab_contents_; }

  // TabContentsObserver implementation.
  virtual void DidNavigateMainFramePostCommit(
      const NavigationController::LoadCommittedDetails& details,
      const ViewHostMsg_FrameNavigate_Params& params);
  virtual bool OnMessageReceived(const IPC::Message& message);

  // Called by the AutoFillCCInfoBarDelegate when the user interacts with the
  // infobar.
  virtual void OnInfoBarClosed(bool should_save);

  // AutoFillDownloadManager::Observer implementation:
  virtual void OnLoadedAutoFillHeuristics(const std::string& heuristic_xml);
  virtual void OnUploadedAutoFillHeuristics(const std::string& form_signature);
  virtual void OnHeuristicsRequestError(
      const std::string& form_signature,
      AutoFillDownloadManager::AutoFillRequestType request_type,
      int http_error);

  // Returns the value of the AutoFillEnabled pref.
  virtual bool IsAutoFillEnabled() const;

  // Imports the form data, submitted by the user, into |personal_data_|.
  void ImportFormData(const FormStructure& submitted_form);

  // Uploads the form data to the AutoFill server.
  void UploadFormData(const FormStructure& submitted_form);

  // Reset cache.
  void Reset();

 protected:
  // For tests.
  AutoFillManager();
  AutoFillManager(TabContents* tab_contents,
                  PersonalDataManager* personal_data);

  void set_personal_data_manager(PersonalDataManager* personal_data) {
    personal_data_ = personal_data;
  }

  const AutoFillMetrics* metric_logger() const {
    return metric_logger_.get();
  }
  void set_metric_logger(const AutoFillMetrics* metric_logger);

  ScopedVector<FormStructure>* form_structures() { return &form_structures_; }

  // Maps GUIDs to and from IDs that are used to identify profiles and credit
  // cards sent to and from the renderer process.
  virtual int GUIDToID(const std::string& guid);
  virtual const std::string IDToGUID(int id);

  // Methods for packing and unpacking credit card and profile IDs for sending
  // and receiving to and from the renderer process.
  int PackGUIDs(const std::string& cc_guid, const std::string& profile_guid);
  void UnpackGUIDs(int id, std::string* cc_guid, std::string* profile_guid);

 private:
  void OnFormSubmitted(const webkit_glue::FormData& form);
  void OnFormsSeen(const std::vector<webkit_glue::FormData>& forms);
  void OnQueryFormFieldAutoFill(int query_id,
                                const webkit_glue::FormData& form,
                                const webkit_glue::FormField& field);
  void OnFillAutoFillFormData(int query_id,
                              const webkit_glue::FormData& form,
                              const webkit_glue::FormField& field,
                              int unique_id);
  void OnShowAutoFillDialog();
  void OnDidFillAutoFillFormData();
  void OnDidShowAutoFillSuggestions();

  // Fills |host| with the RenderViewHost for this tab.
  // Returns false if AutoFill is disabled or if the host is unavailable.
  bool GetHost(const std::vector<AutoFillProfile*>& profiles,
               const std::vector<CreditCard*>& credit_cards,
#ifdef ANDROID
               AutoFillHost** host) WARN_UNUSED_RESULT;
#else
               RenderViewHost** host) WARN_UNUSED_RESULT;
#endif

  // Fills |form_structure| cached element corresponding to |form|.
  // Returns false if the cached element was not found.
  bool FindCachedForm(const webkit_glue::FormData& form,
                      FormStructure** form_structure) WARN_UNUSED_RESULT;

  // Fills |form_structure| and |autofill_field| with the cached elements
  // corresponding to |form| and |field|. Returns false if the cached elements
  // were not found.
  bool FindCachedFormAndField(
      const webkit_glue::FormData& form,
      const webkit_glue::FormField& field,
      FormStructure** form_structure,
      AutoFillField** autofill_field) WARN_UNUSED_RESULT;

  // Returns a list of values from the stored profiles that match |type| and the
  // value of |field| and returns the labels of the matching profiles. |labels|
  // is filled with the Profile label.
  void GetProfileSuggestions(FormStructure* form,
                             const webkit_glue::FormField& field,
                             AutoFillType type,
                             std::vector<string16>* values,
                             std::vector<string16>* labels,
                             std::vector<string16>* icons,
                             std::vector<int>* unique_ids);

  // Returns a list of values from the stored credit cards that match |type| and
  // the value of |field| and returns the labels of the matching credit cards.
  void GetCreditCardSuggestions(FormStructure* form,
                                const webkit_glue::FormField& field,
                                AutoFillType type,
                                std::vector<string16>* values,
                                std::vector<string16>* labels,
                                std::vector<string16>* icons,
                                std::vector<int>* unique_ids);

  // Set |field| argument's value based on |type| and contents of the
  // |credit_card|.
  void FillCreditCardFormField(const CreditCard* credit_card,
                               AutoFillType type,
                               webkit_glue::FormField* field);

  // Set |field| argument's value based on |type| and contents of the |profile|.
  void FillFormField(const AutoFillProfile* profile,
                     AutoFillType type,
                     webkit_glue::FormField* field);

  // Set |field| argument's value for phone/fax number based on contents of the
  // |profile|. |type| is the type of the phone.
  void FillPhoneNumberField(const AutoFillProfile* profile,
                            AutoFillType type,
                            webkit_glue::FormField* field);

  // Parses the forms using heuristic matching and querying the AutoFill server.
  void ParseForms(const std::vector<webkit_glue::FormData>& forms);

  // Uses existing personal data to determine possible field types for the
  // |submitted_form|.
  void DeterminePossibleFieldTypesForUpload(FormStructure* submitted_form);

  void LogMetricsAboutSubmittedForm(const webkit_glue::FormData& form,
                                    const FormStructure* submitted_form);

  // The TabContents hosting this AutoFillManager.
  // Weak reference.
  // May not be NULL.
  TabContents* tab_contents_;

  // The personal data manager, used to save and load personal data to/from the
  // web database.  This is overridden by the AutoFillManagerTest.
  // Weak reference.
  // May be NULL.  NULL indicates OTR.
  PersonalDataManager* personal_data_;

  std::list<std::string> autofilled_forms_signatures_;
  // Handles queries and uploads to AutoFill servers.
  AutoFillDownloadManager download_manager_;

  // Should be set to true in AutoFillManagerTest and other tests, false in
  // AutoFillDownloadManagerTest and in non-test environment. Is false by
  // default for the public constructor, and true by default for the test-only
  // constructors.
  bool disable_download_manager_requests_;

  // For logging UMA metrics. Overridden by metrics tests.
  scoped_ptr<const AutoFillMetrics> metric_logger_;

  // Our copy of the form data.
  ScopedVector<FormStructure> form_structures_;

<<<<<<< HEAD
  // The form data the user has submitted.
  scoped_ptr<FormStructure> upload_form_structure_;

#ifdef ANDROID
  // To minimize merge conflicts, we keep this pointer around, but never use it.
  void* cc_infobar_;
#else
=======
>>>>>>> chromium.org at r11.0.672.0
  // The InfoBar that asks for permission to store credit card information.
  // Deletes itself when closed.
  AutoFillCCInfoBarDelegate* cc_infobar_;
#endif

  // The imported credit card that should be saved if the user accepts the
  // infobar.
  scoped_ptr<const CreditCard> imported_credit_card_;

  // GUID to ID mapping.  We keep two maps to convert back and forth.
  std::map<std::string, int> guid_id_map_;
  std::map<int, std::string> id_guid_map_;

  friend class AutoFillManagerTest;
  friend class FormStructureBrowserTest;
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillCreditCardForm);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest,
                           FillCreditCardFormNoYearNoMonth);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillCreditCardFormYearNoMonth);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillCreditCardFormNoYearMonth);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillCreditCardFormYearMonth);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillAddressForm);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillAddressAndCreditCardForm);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillFormWithMultipleSections);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillAutoFilledForm);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FillPhoneNumber);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FormChangesRemoveField);
  FRIEND_TEST_ALL_PREFIXES(AutoFillManagerTest, FormChangesAddField);
  FRIEND_TEST_ALL_PREFIXES(AutoFillMetricsTest, QualityMetrics);
  FRIEND_TEST_ALL_PREFIXES(AutoFillMetricsTest,
                           NoQualityMetricsForNonAutoFillableForms);
  FRIEND_TEST_ALL_PREFIXES(AutoFillMetricsTest, SaneMetricsWithCacheMismatch);
  FRIEND_TEST_ALL_PREFIXES(AutoFillMetricsTest, QualityMetricsForFailure);
  FRIEND_TEST_ALL_PREFIXES(AutoFillMetricsTest, QualityMetricsWithExperimentId);

  DISALLOW_COPY_AND_ASSIGN(AutoFillManager);
};

#endif  // CHROME_BROWSER_AUTOFILL_AUTOFILL_MANAGER_H_
