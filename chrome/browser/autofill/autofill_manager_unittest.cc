// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "app/l10n_util.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/scoped_vector.h"
#include "base/string_number_conversions.h"
#include "base/string16.h"
#include "base/stringprintf.h"
#include "base/tuple.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_common_test.h"
#include "chrome/browser/autofill/autofill_manager.h"
#include "chrome/browser/autofill/autofill_profile.h"
#include "chrome/browser/autofill/credit_card.h"
#include "chrome/browser/autofill/personal_data_manager.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_host/test/test_render_view_host.h"
#include "chrome/browser/tab_contents/test_tab_contents.h"
#include "chrome/common/ipc_test_sink.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/form_data.h"
#include "webkit/glue/form_field.h"

using webkit_glue::FormData;
using webkit_glue::FormField;

namespace {

// The page ID sent to the AutoFillManager from the RenderView, used to send
// an IPC message back to the renderer.
const int kDefaultPageID = 137;

typedef Tuple5<int,
               std::vector<string16>,
               std::vector<string16>,
               std::vector<string16>,
               std::vector<int> > AutoFillParam;

class TestPersonalDataManager : public PersonalDataManager {
 public:
  TestPersonalDataManager() {
    CreateTestAutoFillProfiles(&web_profiles_);
    CreateTestCreditCards(&credit_cards_);
  }

  virtual void InitializeIfNeeded() {}
  virtual void SaveImportedFormData() {}
  virtual bool IsDataLoaded() const { return true; }

  AutoFillProfile* GetLabeledProfile(const char* label) {
    for (std::vector<AutoFillProfile *>::iterator it = web_profiles_.begin();
         it != web_profiles_.end(); ++it) {
      if (!(*it)->Label().compare(ASCIIToUTF16(label)))
        return *it;
    }
    return NULL;
  }

  CreditCard* GetLabeledCreditCard(const char* label) {
    for (std::vector<CreditCard *>::iterator it = credit_cards_.begin();
         it != credit_cards_.end(); ++it) {
      if (!(*it)->Label().compare(ASCIIToUTF16(label)))
        return *it;
    }
    return NULL;
  }

  void AddProfile(AutoFillProfile* profile) {
    web_profiles_->push_back(profile);
  }

  void ClearAutoFillProfiles() {
    web_profiles_.reset();
  }

  void ClearCreditCards() {
    credit_cards_.reset();
  }

 private:
  void CreateTestAutoFillProfiles(ScopedVector<AutoFillProfile>* profiles) {
    AutoFillProfile* profile = new AutoFillProfile;
    autofill_test::SetProfileInfo(profile, "Home", "Elvis", "Aaron",
                                  "Presley", "theking@gmail.com", "RCA",
                                  "3734 Elvis Presley Blvd.", "Apt. 10",
                                  "Memphis", "Tennessee", "38116", "USA",
                                  "12345678901", "");
    profile->set_guid("00000000-0000-0000-0000-000000000001");
    profiles->push_back(profile);
    profile = new AutoFillProfile;
    autofill_test::SetProfileInfo(profile, "Work", "Charles", "Hardin",
                                  "Holley", "buddy@gmail.com", "Decca",
                                  "123 Apple St.", "unit 6", "Lubbock",
                                  "Texas", "79401", "USA", "23456789012",
                                  "");
    profile->set_guid("00000000-0000-0000-0000-000000000002");
    profiles->push_back(profile);
    profile = new AutoFillProfile;
    autofill_test::SetProfileInfo(profile, "Empty", "", "", "", "", "", "", "",
                                  "", "", "", "", "", "");
    profile->set_guid("00000000-0000-0000-0000-000000000003");
    profiles->push_back(profile);
  }

  void CreateTestCreditCards(ScopedVector<CreditCard>* credit_cards) {
    CreditCard* credit_card = new CreditCard;
    autofill_test::SetCreditCardInfo(credit_card, "First", "Elvis Presley",
                                     "4234567890123456", // Visa
                                     "04", "2012");
    credit_card->set_guid("00000000-0000-0000-0000-000000000004");
    credit_cards->push_back(credit_card);
    credit_card = new CreditCard;
    autofill_test::SetCreditCardInfo(credit_card, "Second", "Buddy Holly",
                                     "5187654321098765", // Mastercard
                                     "10", "2014");
    credit_card->set_guid("00000000-0000-0000-0000-000000000005");
    credit_cards->push_back(credit_card);
    credit_card = new CreditCard;
    autofill_test::SetCreditCardInfo(credit_card, "Empty", "", "", "", "");
    credit_card->set_guid("00000000-0000-0000-0000-000000000006");
    credit_cards->push_back(credit_card);
  }

  DISALLOW_COPY_AND_ASSIGN(TestPersonalDataManager);
};

// Populates |form| with data corresponding to a simple address form.
// Note that this actually appends fields to the form data, which can be useful
// for building up more complex test forms.
void CreateTestAddressFormData(FormData* form) {
  form->name = ASCIIToUTF16("MyForm");
  form->method = ASCIIToUTF16("POST");
  form->origin = GURL("http://myform.com/form.html");
  form->action = GURL("http://myform.com/submit.html");
  form->user_submitted = true;

  FormField field;
  autofill_test::CreateTestFormField(
      "First Name", "firstname", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Middle Name", "middlename", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Last Name", "lastname", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 1", "addr1", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Address Line 2", "addr2", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "City", "city", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "State", "state", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Postal Code", "zipcode", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Country", "country", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Phone Number", "phonenumber", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Fax", "fax", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Email", "email", "", "text", &field);
  form->fields.push_back(field);
}

// Populates |form| with data corresponding to a simple credit card form.
// Note that this actually appends fields to the form data, which can be useful
// for building up more complex test forms.
void CreateTestCreditCardFormData(FormData* form, bool is_https) {
  form->name = ASCIIToUTF16("MyForm");
  form->method = ASCIIToUTF16("POST");
  if (is_https) {
    form->origin = GURL("https://myform.com/form.html");
    form->action = GURL("https://myform.com/submit.html");
  } else {
    form->origin = GURL("http://myform.com/form.html");
    form->action = GURL("http://myform.com/submit.html");
  }
  form->user_submitted = true;

  FormField field;
  autofill_test::CreateTestFormField(
      "Name on Card", "nameoncard", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Card Number", "cardnumber", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Expiration Date", "ccmonth", "", "text", &field);
  form->fields.push_back(field);
  autofill_test::CreateTestFormField(
      "", "ccyear", "", "text", &field);
  form->fields.push_back(field);
}

void ExpectSuggestions(int page_id,
                       const std::vector<string16>& values,
                       const std::vector<string16>& labels,
                       const std::vector<string16>& icons,
                       const std::vector<int>& unique_ids,
                       int expected_page_id,
                       size_t expected_num_suggestions,
                       const string16 expected_values[],
                       const string16 expected_labels[],
                       const string16 expected_icons[],
                       const int expected_unique_ids[]) {
  EXPECT_EQ(expected_page_id, page_id);
  ASSERT_EQ(expected_num_suggestions, values.size());
  ASSERT_EQ(expected_num_suggestions, labels.size());
  ASSERT_EQ(expected_num_suggestions, icons.size());
  ASSERT_EQ(expected_num_suggestions, unique_ids.size());
  for (size_t i = 0; i < expected_num_suggestions; ++i) {
    SCOPED_TRACE(StringPrintf("i: %" PRIuS, i));
    EXPECT_EQ(expected_values[i], values[i]);
    EXPECT_EQ(expected_labels[i], labels[i]);
    EXPECT_EQ(expected_icons[i], icons[i]);
    EXPECT_EQ(expected_unique_ids[i], unique_ids[i]);
  }
}

// Verifies that the |filled_form| has been filled with the given data.
// Verifies address fields if |has_address_fields| is true, and verifies
// credit card fields if |has_credit_card_fields| is true. Verifies both if both
// are true.
void ExpectFilledForm(int page_id,
                      const FormData& filled_form,
                      int expected_page_id,
                      const char* first,
                      const char* middle,
                      const char* last,
                      const char* address1,
                      const char* address2,
                      const char* city,
                      const char* state,
                      const char* postal_code,
                      const char* country,
                      const char* phone,
                      const char* fax,
                      const char* email,
                      const char* name_on_card,
                      const char* card_number,
                      const char* expiration_month,
                      const char* expiration_year,
                      bool has_address_fields,
                      bool has_credit_card_fields) {
  // The number of fields in the address and credit card forms created above.
  const size_t kAddressFormSize = 12;
  const size_t kCreditCardFormSize = 4;

  EXPECT_EQ(expected_page_id, page_id);
  EXPECT_EQ(ASCIIToUTF16("MyForm"), filled_form.name);
  EXPECT_EQ(ASCIIToUTF16("POST"), filled_form.method);
  if (has_credit_card_fields) {
    EXPECT_EQ(GURL("https://myform.com/form.html"), filled_form.origin);
    EXPECT_EQ(GURL("https://myform.com/submit.html"), filled_form.action);
  } else {
    EXPECT_EQ(GURL("http://myform.com/form.html"), filled_form.origin);
    EXPECT_EQ(GURL("http://myform.com/submit.html"), filled_form.action);
  }
  EXPECT_TRUE(filled_form.user_submitted);

  size_t form_size = 0;
  if (has_address_fields)
    form_size += kAddressFormSize;
  if (has_credit_card_fields)
    form_size += kCreditCardFormSize;
  ASSERT_EQ(form_size, filled_form.fields.size());

  FormField field;
  if (has_address_fields) {
    autofill_test::CreateTestFormField(
        "First Name", "firstname", first, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[0]));
    autofill_test::CreateTestFormField(
        "Middle Name", "middlename", middle, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[1]));
    autofill_test::CreateTestFormField(
         "Last Name", "lastname", last, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[2]));
    autofill_test::CreateTestFormField(
         "Address Line 1", "addr1", address1, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[3]));
    autofill_test::CreateTestFormField(
         "Address Line 2", "addr2", address2, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[4]));
    autofill_test::CreateTestFormField(
         "City", "city", city, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[5]));
    autofill_test::CreateTestFormField(
         "State", "state", state, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[6]));
    autofill_test::CreateTestFormField(
         "Postal Code", "zipcode", postal_code, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[7]));
    autofill_test::CreateTestFormField(
         "Country", "country", country, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[8]));
    autofill_test::CreateTestFormField(
        "Phone Number", "phonenumber", phone, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[9]));
    autofill_test::CreateTestFormField(
        "Fax", "fax", fax, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[10]));
    autofill_test::CreateTestFormField(
         "Email", "email", email, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[11]));
  }

  if (has_credit_card_fields) {
    size_t offset = has_address_fields? kAddressFormSize : 0;
    autofill_test::CreateTestFormField(
        "Name on Card", "nameoncard", name_on_card, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[offset + 0]));
    autofill_test::CreateTestFormField(
        "Card Number", "cardnumber", card_number, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[offset + 1]));
    autofill_test::CreateTestFormField(
        "Expiration Date", "ccmonth", expiration_month, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[offset + 2]));
    autofill_test::CreateTestFormField(
        "", "ccyear", expiration_year, "text", &field);
    EXPECT_TRUE(field.StrictlyEqualsHack(filled_form.fields[offset + 3]));
  }
}

void ExpectFilledAddressFormElvis(int page_id,
                                  const FormData& filled_form,
                                  int expected_page_id,
                                  bool has_credit_card_fields) {
  ExpectFilledForm(page_id, filled_form, expected_page_id, "Elvis", "Aaron",
                   "Presley", "3734 Elvis Presley Blvd.", "Apt. 10", "Memphis",
                   "Tennessee", "38116", "USA", "12345678901", "",
                   "theking@gmail.com", "", "", "", "", true,
                   has_credit_card_fields);
}

void ExpectFilledCreditCardFormElvis(int page_id,
                                     const FormData& filled_form,
                                     int expected_page_id,
                                     bool has_address_fields) {
  ExpectFilledForm(page_id, filled_form, expected_page_id,
                   "", "", "", "", "", "", "", "", "", "", "", "",
                   "Elvis Presley", "4234567890123456", "04", "2012",
                   has_address_fields, true);
}

class TestAutoFillManager : public AutoFillManager {
 public:
  TestAutoFillManager(TabContents* tab_contents,
                      TestPersonalDataManager* personal_manager)
      : AutoFillManager(tab_contents, personal_manager),
        autofill_enabled_(true) {
    test_personal_data_ = personal_manager;
  }

  virtual bool IsAutoFillEnabled() const { return autofill_enabled_; }

  void set_autofill_enabled(bool autofill_enabled) {
    autofill_enabled_ = autofill_enabled;
  }

  AutoFillProfile* GetLabeledProfile(const char* label) {
    return test_personal_data_->GetLabeledProfile(label);
  }

  CreditCard* GetLabeledCreditCard(const char* label) {
    return test_personal_data_->GetLabeledCreditCard(label);
  }

  void AddProfile(AutoFillProfile* profile) {
    test_personal_data_->AddProfile(profile);
  }

  int GetPackedCreditCardID(int credit_card_id) {
    return PackGUIDs(IDToGUID(credit_card_id), std::string());
  }

  virtual int GUIDToID(const std::string& guid) OVERRIDE {
    if (guid.empty())
      return 0;

    int id;
    EXPECT_TRUE(base::StringToInt(guid.substr(guid.rfind("-") + 1), &id));
    return id;
  }

  virtual const std::string IDToGUID(int id) OVERRIDE {
    EXPECT_TRUE(id >= 0);
    if (id <= 0)
      return std::string();

    return base::StringPrintf("00000000-0000-0000-0000-%012d", id);
  }

 private:
  TestPersonalDataManager* test_personal_data_;
  bool autofill_enabled_;

  DISALLOW_COPY_AND_ASSIGN(TestAutoFillManager);
};

}  // namespace

class AutoFillManagerTest : public RenderViewHostTestHarness {
 public:
  AutoFillManagerTest() {}
  virtual ~AutoFillManagerTest() {
    // Order of destruction is important as AutoFillManager relies on
    // PersonalDataManager to be around when it gets destroyed.
    autofill_manager_.reset(NULL);
    test_personal_data_ = NULL;
  }

  virtual void SetUp() {
    RenderViewHostTestHarness::SetUp();
    test_personal_data_ = new TestPersonalDataManager();
    autofill_manager_.reset(new TestAutoFillManager(contents(),
                                                    test_personal_data_.get()));
  }

  Profile* profile() { return contents()->profile(); }

  bool GetAutoFillSuggestionsMessage(int* page_id,
                                     std::vector<string16>* values,
                                     std::vector<string16>* labels,
                                     std::vector<string16>* icons,
                                     std::vector<int>* unique_ids) {
    const uint32 kMsgID = ViewMsg_AutoFillSuggestionsReturned::ID;
    const IPC::Message* message =
        process()->sink().GetFirstMessageMatching(kMsgID);
    if (!message)
      return false;

    AutoFillParam autofill_param;
    ViewMsg_AutoFillSuggestionsReturned::Read(message, &autofill_param);
    if (page_id)
      *page_id = autofill_param.a;
    if (values)
      *values = autofill_param.b;
    if (labels)
      *labels = autofill_param.c;
    if (icons)
      *icons = autofill_param.d;
    if (unique_ids)
      *unique_ids = autofill_param.e;
    return true;
  }

  bool GetAutoFillFormDataFilledMessage(int *page_id, FormData* results) {
    const uint32 kMsgID = ViewMsg_AutoFillFormDataFilled::ID;
    const IPC::Message* message =
        process()->sink().GetFirstMessageMatching(kMsgID);
    if (!message)
      return false;
    Tuple2<int, FormData> autofill_param;
    ViewMsg_AutoFillFormDataFilled::Read(message, &autofill_param);
    if (page_id)
      *page_id = autofill_param.a;
    if (results)
      *results = autofill_param.b;
    return true;
  }

 protected:
  scoped_ptr<TestAutoFillManager> autofill_manager_;
  scoped_refptr<TestPersonalDataManager> test_personal_data_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AutoFillManagerTest);
};

// Test that we return all address profile suggestions when all form fields are
// empty.
TEST_F(AutoFillManagerTest, GetProfileSuggestionsEmptyValue) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("Elvis"),
    ASCIIToUTF16("Charles")
  };
  // Inferred labels include full first relevant field, which in this case is
  // the address line 1.
  string16 expected_labels[] = {
    ASCIIToUTF16("3734 Elvis Presley Blvd."),
    ASCIIToUTF16("123 Apple St.")
  };
  string16 expected_icons[] = {string16(), string16()};
  int expected_unique_ids[] = {1, 2};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we return only matching address profile suggestions when the
// selected form field has been partially filled out.
TEST_F(AutoFillManagerTest, GetProfileSuggestionsMatchCharacter) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  FormField field;
  autofill_test::CreateTestFormField("First Name", "firstname", "E", "text",
                                     &field);
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {ASCIIToUTF16("Elvis")};
  string16 expected_labels[] = {ASCIIToUTF16("3734 Elvis Presley Blvd.")};
  string16 expected_icons[] = {string16()};
  int expected_unique_ids[] = {1};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we return no suggestions when the form has no relevant fields.
TEST_F(AutoFillManagerTest, GetProfileSuggestionsUnknownFields) {
  // Set up our form data.
  FormData form;
  form.name = ASCIIToUTF16("MyForm");
  form.method = ASCIIToUTF16("POST");
  form.origin = GURL("http://myform.com/form.html");
  form.action = GURL("http://myform.com/submit.html");
  form.user_submitted = true;

  FormField field;
  autofill_test::CreateTestFormField("Username", "username", "", "text",
                                     &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField("Password", "password", "", "password",
                                     &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField("Quest", "quest", "", "quest", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField("Color", "color", "", "text", &field);
  form.fields.push_back(field);

  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_FALSE(autofill_manager_->GetAutoFillSuggestions(form, field));
}

// Test that we cull duplicate profile suggestions.
TEST_F(AutoFillManagerTest, GetProfileSuggestionsWithDuplicates) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // Add a duplicate profile.
  AutoFillProfile* duplicate_profile = static_cast<AutoFillProfile*>(
      autofill_manager_->GetLabeledProfile("Home")->Clone());
  autofill_manager_->AddProfile(duplicate_profile);

  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("Elvis"),
    ASCIIToUTF16("Charles")
  };
  string16 expected_labels[] = {
    ASCIIToUTF16("3734 Elvis Presley Blvd."),
    ASCIIToUTF16("123 Apple St.")
  };
  string16 expected_icons[] = {string16(), string16()};
  int expected_unique_ids[] = {1, 2};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we return no suggestions when autofill is disabled.
TEST_F(AutoFillManagerTest, GetProfileSuggestionsAutofillDisabledByUser) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // Disable AutoFill.
  autofill_manager_->set_autofill_enabled(false);

  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_FALSE(autofill_manager_->GetAutoFillSuggestions(form, field));
}

// Test that we return a warning explaining that autofill suggestions are
// unavailable when the form method is GET rather than POST.
TEST_F(AutoFillManagerTest, GetProfileSuggestionsMethodGet) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  form.method = ASCIIToUTF16("GET");
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_FORM_DISABLED)
  };
  string16 expected_labels[] = {string16()};
  string16 expected_icons[] = {string16()};
  int expected_unique_ids[] = {-1};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);

  // Now add some Autocomplete suggestions. We should return the autocomplete
  // suggestions and the warning; these will be culled by the renderer.
  process()->sink().ClearMessages();
  const int kPageID2 = 2;
  rvh()->ResetAutoFillState(kPageID2);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  std::vector<string16> suggestions;
  suggestions.push_back(ASCIIToUTF16("Jay"));
  suggestions.push_back(ASCIIToUTF16("Jason"));
  rvh()->AutocompleteSuggestionsReturned(suggestions);

  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values2[] = {
    l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_FORM_DISABLED),
    ASCIIToUTF16("Jay"),
    ASCIIToUTF16("Jason")
  };
  string16 expected_labels2[] = {string16(), string16(), string16()};
  string16 expected_icons2[] = {string16(), string16(), string16()};
  int expected_unique_ids2[] = {-1, 0, 0};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kPageID2, arraysize(expected_values2), expected_values2,
                    expected_labels2, expected_icons2, expected_unique_ids2);

  // Now clear the test profiles and try again -- we shouldn't return a warning.
  test_personal_data_->ClearAutoFillProfiles();
  EXPECT_FALSE(autofill_manager_->GetAutoFillSuggestions(form, field));
}

// Test that we return all credit card profile suggestions when all form fields
// are empty.
TEST_F(AutoFillManagerTest, GetCreditCardSuggestionsEmptyValue) {
  // Set up our form data.
  FormData form;
  CreateTestCreditCardFormData(&form, true);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  FormField field = form.fields[1];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("************3456"),
    ASCIIToUTF16("************8765")
  };
  string16 expected_labels[] = {ASCIIToUTF16("*3456"), ASCIIToUTF16("*8765")};
  string16 expected_icons[] = {
    ASCIIToUTF16("visaCC"),
    ASCIIToUTF16("masterCardCC")
  };
  int expected_unique_ids[] = {
    autofill_manager_->GetPackedCreditCardID(4),
    autofill_manager_->GetPackedCreditCardID(5)
  };
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we return only matching credit card profile suggestions when the
// selected form field has been partially filled out.
TEST_F(AutoFillManagerTest, GetCreditCardSuggestionsMatchCharacter) {
  // Set up our form data.
  FormData form;
  CreateTestCreditCardFormData(&form, true);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  FormField field;
  autofill_test::CreateTestFormField(
      "Card Number", "cardnumber", "4", "text", &field);
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {ASCIIToUTF16("************3456")};
  string16 expected_labels[] = {ASCIIToUTF16("*3456")};
  string16 expected_icons[] = {ASCIIToUTF16("visaCC")};
  int expected_unique_ids[] = {autofill_manager_->GetPackedCreditCardID(4)};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we return credit card profile suggestions when the selected form
// field is not the credit card number field.
TEST_F(AutoFillManagerTest, GetCreditCardSuggestionsNonCCNumber) {
  // Set up our form data.
  FormData form;
  CreateTestCreditCardFormData(&form, true);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("Elvis Presley"),
    ASCIIToUTF16("Buddy Holly")
  };
  string16 expected_labels[] = {ASCIIToUTF16("*3456"), ASCIIToUTF16("*8765")};
  string16 expected_icons[] = {
    ASCIIToUTF16("visaCC"),
    ASCIIToUTF16("masterCardCC")
  };
  int expected_unique_ids[] = {
    autofill_manager_->GetPackedCreditCardID(4),
    autofill_manager_->GetPackedCreditCardID(5)
  };
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we return a warning explaining that credit card profile suggestions
// are unavailable when the form is not https.
TEST_F(AutoFillManagerTest, GetCreditCardSuggestionsNonHTTPS) {
  // Set up our form data.
  FormData form;
  CreateTestCreditCardFormData(&form, false);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_INSECURE_CONNECTION)
  };
  string16 expected_labels[] = {string16()};
  string16 expected_icons[] = {string16()};
  int expected_unique_ids[] = {-1};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);

  // Now add some Autocomplete suggestions. We should show the autocomplete
  // suggestions and the warning.
  process()->sink().ClearMessages();
  const int kPageID2 = 2;
  rvh()->ResetAutoFillState(kPageID2);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  std::vector<string16> suggestions;
  suggestions.push_back(ASCIIToUTF16("Jay"));
  suggestions.push_back(ASCIIToUTF16("Jason"));
  rvh()->AutocompleteSuggestionsReturned(suggestions);

  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));
  string16 expected_values2[] = {
    l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_INSECURE_CONNECTION),
    ASCIIToUTF16("Jay"),
    ASCIIToUTF16("Jason")
  };
  string16 expected_labels2[] = {string16(), string16(), string16()};
  string16 expected_icons2[] = {string16(), string16(), string16()};
  int expected_unique_ids2[] = {-1, 0, 0};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kPageID2, arraysize(expected_values2), expected_values2,
                    expected_labels2, expected_icons2, expected_unique_ids2);

  // Clear the test credit cards and try again -- we shouldn't return a warning.
  test_personal_data_->ClearCreditCards();
  EXPECT_FALSE(autofill_manager_->GetAutoFillSuggestions(form, field));
}

// Test that we return profile and credit card suggestions for combined forms.
TEST_F(AutoFillManagerTest, GetAddressAndCreditCardSuggestions) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  CreateTestCreditCardFormData(&form, true);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  FormField field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right address suggestions to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("Elvis"),
    ASCIIToUTF16("Charles")
  };
  string16 expected_labels[] = {
    ASCIIToUTF16("3734 Elvis Presley Blvd."),
    ASCIIToUTF16("123 Apple St.")
  };
  string16 expected_icons[] = {string16(), string16()};
  int expected_unique_ids[] = {1, 2};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);

  process()->sink().ClearMessages();
  const int kPageID2 = 2;
  autofill_test::CreateTestFormField(
      "Card Number", "cardnumber", "", "text", &field);
  rvh()->ResetAutoFillState(kPageID2);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the credit card suggestions to the renderer.
  page_id = 0;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values2[] = {
    ASCIIToUTF16("************3456"),
    ASCIIToUTF16("************8765")
  };
  string16 expected_labels2[] = {ASCIIToUTF16("*3456"), ASCIIToUTF16("*8765")};
  string16 expected_icons2[] = {
    ASCIIToUTF16("visaCC"),
    ASCIIToUTF16("masterCardCC")
  };
  int expected_unique_ids2[] = {
    autofill_manager_->GetPackedCreditCardID(4),
    autofill_manager_->GetPackedCreditCardID(5)
  };
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kPageID2, arraysize(expected_values2), expected_values2,
                    expected_labels2, expected_icons2, expected_unique_ids2);
}

// Test that for non-https forms with both address and credit card fields, we
// only return address suggestions. Instead of credit card suggestions, we
// should return a warning explaining that credit card profile suggestions are
// unavailable when the form is not https.
TEST_F(AutoFillManagerTest, GetAddressAndCreditCardSuggestionsNonHttps) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  CreateTestCreditCardFormData(&form, false);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  FormField field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right address suggestions to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("Elvis"),
    ASCIIToUTF16("Charles")
  };
  string16 expected_labels[] = {
    ASCIIToUTF16("3734 Elvis Presley Blvd."),
    ASCIIToUTF16("123 Apple St.")
  };
  string16 expected_icons[] = {string16(), string16()};
  int expected_unique_ids[] = {1, 2};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);

  process()->sink().ClearMessages();
  autofill_test::CreateTestFormField(
      "Card Number", "cardnumber", "", "text", &field);
  const int kPageID2 = 2;
  rvh()->ResetAutoFillState(kPageID2);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values2[] = {
    l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_INSECURE_CONNECTION)
  };
  string16 expected_labels2[] = {string16()};
  string16 expected_icons2[] = {string16()};
  int expected_unique_ids2[] = {-1};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kPageID2, arraysize(expected_values2), expected_values2,
                    expected_labels2, expected_icons2, expected_unique_ids2);

  // Clear the test credit cards and try again -- we shouldn't return a warning.
  test_personal_data_->ClearCreditCards();
  EXPECT_FALSE(autofill_manager_->GetAutoFillSuggestions(form, field));
}

// Test that we correctly combine autofill and autocomplete suggestions.
TEST_F(AutoFillManagerTest, GetCombinedAutoFillAndAutocompleteSuggestions) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // Add some Autocomplete suggestions.
  // This triggers the combined message send.
  std::vector<string16> suggestions;
  suggestions.push_back(ASCIIToUTF16("Jay"));
  // This suggestion is a duplicate, and should be trimmed.
  suggestions.push_back(ASCIIToUTF16("Elvis"));
  suggestions.push_back(ASCIIToUTF16("Jason"));
  rvh()->AutocompleteSuggestionsReturned(suggestions);

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("Elvis"),
    ASCIIToUTF16("Charles"),
    ASCIIToUTF16("Jay"),
    ASCIIToUTF16("Jason")
  };
  string16 expected_labels[] = {
    ASCIIToUTF16("3734 Elvis Presley Blvd."),
    ASCIIToUTF16("123 Apple St."),
    string16(),
    string16()
  };
  string16 expected_icons[] = {string16(), string16(), string16(), string16()};
  int expected_unique_ids[] = {1, 2, 0, 0};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we return autocomplete-like suggestions when trying to autofill
// already filled forms.
TEST_F(AutoFillManagerTest, GetFieldSuggestionsWhenFormIsAutoFilled) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // Mark one of the fields as filled.
  form.fields[2].set_autofilled(true);
  const FormField& field = form.fields[0];
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));
  string16 expected_values[] = {
    ASCIIToUTF16("Elvis"),
    ASCIIToUTF16("Charles")
  };
  string16 expected_labels[] = {string16(), string16()};
  string16 expected_icons[] = {string16(), string16()};
  int expected_unique_ids[] = {1, 2};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that nothing breaks when there are autocomplete suggestions but no
// autofill suggestions.
TEST_F(AutoFillManagerTest, GetFieldSuggestionsForAutocompleteOnly) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  FormField field;
  autofill_test::CreateTestFormField(
      "Some Field", "somefield", "", "text", &field);
  form.fields.push_back(field);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_FALSE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // Add some Autocomplete suggestions.
  // This triggers the combined message send.
  std::vector<string16> suggestions;
  suggestions.push_back(ASCIIToUTF16("one"));
  suggestions.push_back(ASCIIToUTF16("two"));
  rvh()->AutocompleteSuggestionsReturned(suggestions);

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("one"),
    ASCIIToUTF16("two")
  };
  string16 expected_labels[] = {string16(), string16()};
  string16 expected_icons[] = {string16(), string16()};
  int expected_unique_ids[] = {0, 0};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we do not return duplicate values drawn from multiple profiles when
// filling an already filled field.
TEST_F(AutoFillManagerTest, GetFieldSuggestionsWithDuplicateValues) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // |profile| will be owned by the mock PersonalDataManager.
  AutoFillProfile* profile = new AutoFillProfile;
  autofill_test::SetProfileInfo(profile, "Duplicate", "Elvis", "", "", "", "",
                                "", "", "", "", "", "", "", "");
  profile->set_guid("00000000-0000-0000-0000-000000000101");
  autofill_manager_->AddProfile(profile);

  FormField& field = form.fields[0];
  field.set_autofilled(true);
  rvh()->ResetAutoFillState(kDefaultPageID);
  EXPECT_TRUE(autofill_manager_->GetAutoFillSuggestions(form, field));

  // No suggestions provided, so send an empty vector as the results.
  // This triggers the combined message send.
  rvh()->AutocompleteSuggestionsReturned(std::vector<string16>());

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::vector<string16> values;
  std::vector<string16> labels;
  std::vector<string16> icons;
  std::vector<int> unique_ids;
  EXPECT_TRUE(GetAutoFillSuggestionsMessage(&page_id, &values, &labels, &icons,
                                            &unique_ids));

  string16 expected_values[] = {
    ASCIIToUTF16("Elvis"),
    ASCIIToUTF16("Charles")
  };
  string16 expected_labels[] = {string16(), string16()};
  string16 expected_icons[] = {string16(), string16()};
  int expected_unique_ids[] = {1, 2};
  ExpectSuggestions(page_id, values, labels, icons, unique_ids,
                    kDefaultPageID, arraysize(expected_values), expected_values,
                    expected_labels, expected_icons, expected_unique_ids);
}

// Test that we correctly fill an address form.
TEST_F(AutoFillManagerTest, FillAddressForm) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  std::string guid = autofill_manager_->GetLabeledProfile("Home")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kDefaultPageID, form, form.fields[0],
      autofill_manager_->PackGUIDs(std::string(), guid)));

  int page_id = 0;
  FormData results;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  ExpectFilledAddressFormElvis(page_id, results, kDefaultPageID, false);
}

// Test that we correctly fill a credit card form.
TEST_F(AutoFillManagerTest, FillCreditCardForm) {
  // Set up our form data.
  FormData form;
  CreateTestCreditCardFormData(&form, true);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  std::string guid = autofill_manager_->GetLabeledCreditCard("First")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kDefaultPageID, form, *form.fields.begin(),
      autofill_manager_->PackGUIDs(guid, std::string())));

  int page_id = 0;
  FormData results;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  ExpectFilledCreditCardFormElvis(page_id, results, kDefaultPageID, false);
}

// Test that we correctly fill a combined address and credit card form.
TEST_F(AutoFillManagerTest, FillAddressAndCreditCardForm) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  CreateTestCreditCardFormData(&form, true);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // First fill the address data.
  std::string guid = autofill_manager_->GetLabeledProfile("Home")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kDefaultPageID, form, form.fields[0],
      autofill_manager_->PackGUIDs(std::string(), guid)));

  int page_id = 0;
  FormData results;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  {
    SCOPED_TRACE("Address");
    ExpectFilledAddressFormElvis(page_id, results, kDefaultPageID, true);
  }

  // Now fill the credit card data.
  process()->sink().ClearMessages();
  const int kPageID2 = 2;
  guid = autofill_manager_->GetLabeledCreditCard("First")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kPageID2, form, form.fields.back(),
      autofill_manager_->PackGUIDs(guid, std::string())));

  page_id = 0;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  {
    SCOPED_TRACE("Credit card");
    ExpectFilledCreditCardFormElvis(page_id, results, kPageID2, true);
  }
}

// Test that we correctly fill a previously auto-filled form.
TEST_F(AutoFillManagerTest, FillAutoFilledForm) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);
  // Mark one of the address fields as autofilled.
  form.fields[4].set_autofilled(true);
  CreateTestCreditCardFormData(&form, true);
  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // First fill the address data.
  std::string guid = autofill_manager_->GetLabeledProfile("Home")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kDefaultPageID, form, *form.fields.begin(),
      autofill_manager_->PackGUIDs(std::string(), guid)));

  int page_id = 0;
  FormData results;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  {
    SCOPED_TRACE("Address");
    ExpectFilledForm(page_id, results, kDefaultPageID,
                     "Elvis", "", "", "", "", "", "", "", "", "", "", "",
                     "", "", "", "", true, true);
  }

  // Now fill the credit card data.
  process()->sink().ClearMessages();
  const int kPageID2 = 2;
  guid = autofill_manager_->GetLabeledCreditCard("First")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kPageID2, form, form.fields.back(),
      autofill_manager_->PackGUIDs(guid, std::string())));

  page_id = 0;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  {
    SCOPED_TRACE("Credit card 1");
    ExpectFilledCreditCardFormElvis(page_id, results, kPageID2, true);
  }

  // Now set the credit card fields to also be auto-filled, and try again to
  // fill the credit card data
  for (std::vector<FormField>::iterator iter = form.fields.begin();
       iter != form.fields.end();
       ++iter){
    iter->set_autofilled(true);
  }

  process()->sink().ClearMessages();
  const int kPageID3 = 3;
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kPageID3, form, *form.fields.rbegin(),
      autofill_manager_->PackGUIDs(guid, std::string())));

  page_id = 0;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  {
    SCOPED_TRACE("Credit card 2");
    ExpectFilledForm(page_id, results, kPageID3,
                   "", "", "", "", "", "", "", "", "", "", "", "",
                   "", "", "", "2012", true, true);
  }
}

// Test that we correctly fill a phone number split across multiple fields.
TEST_F(AutoFillManagerTest, FillPhoneNumber) {
  // Set up our form data.
  FormData form;
  form.name = ASCIIToUTF16("MyPhoneForm");
  form.method = ASCIIToUTF16("POST");
  form.origin = GURL("http://myform.com/phone_form.html");
  form.action = GURL("http://myform.com/phone_submit.html");
  form.user_submitted = true;

  FormField field;
  autofill_test::CreateTestFormField(
      "country code", "country code", "", "text", &field);
  field.set_max_length(1);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "area code", "area code", "", "text", &field);
  field.set_max_length(3);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "phone", "phone prefix", "1", "text", &field);
  field.set_max_length(3);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "-", "phone suffix", "", "text", &field);
  field.set_max_length(4);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "Phone Extension", "ext", "", "text", &field);
  field.set_max_length(3);
  form.fields.push_back(field);

  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  AutoFillProfile *work_profile = autofill_manager_->GetLabeledProfile("Work");
  ASSERT_TRUE(work_profile != NULL);
  const AutoFillType phone_type(PHONE_HOME_NUMBER);
  string16 saved_phone = work_profile->GetFieldText(phone_type);

  char test_data[] = "1234567890123456";
  for (int i = arraysize(test_data) - 1; i >= 0; --i) {
    test_data[i] = 0;
    work_profile->SetInfo(phone_type, ASCIIToUTF16(test_data));
    // The page ID sent to the AutoFillManager from the RenderView, used to send
    // an IPC message back to the renderer.
    int page_id = 100 - i;
    process()->sink().ClearMessages();
    EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
        page_id, form, *form.fields.begin(),
        autofill_manager_->PackGUIDs(std::string(), work_profile->guid())));
    page_id = 0;
    FormData results;
    EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));

    if (i != 7) {
      EXPECT_EQ(ASCIIToUTF16(test_data), results.fields[2].value());
      EXPECT_EQ(ASCIIToUTF16(test_data), results.fields[3].value());
    } else {
      // The only size that is parsed and split, right now is 7:
      EXPECT_EQ(ASCIIToUTF16("123"), results.fields[2].value());
      EXPECT_EQ(ASCIIToUTF16("4567"), results.fields[3].value());
    }
  }

  work_profile->SetInfo(phone_type, saved_phone);
}

// Test that we can still fill a form when a field has been removed from it.
TEST_F(AutoFillManagerTest, FormChangesRemoveField) {
  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);

  // Add a field -- we'll remove it again later.
  FormField field;
  autofill_test::CreateTestFormField("Some", "field", "", "text", &field);
  form.fields.insert(form.fields.begin() + 3, field);

  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // Now, after the call to |FormsSeen|, we remove the field before filling.
  form.fields.erase(form.fields.begin() + 3);

  std::string guid = autofill_manager_->GetLabeledProfile("Home")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kDefaultPageID, form, form.fields[0],
      autofill_manager_->PackGUIDs(std::string(), guid)));

  int page_id = 0;
  FormData results;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  ExpectFilledAddressFormElvis(page_id, results, kDefaultPageID, false);
}

// Test that we can still fill a form when a field has been added to it.
TEST_F(AutoFillManagerTest, FormChangesAddField) {
  // The offset of the fax field in the address form.
  const int kFaxFieldOffset = 10;

  // Set up our form data.
  FormData form;
  CreateTestAddressFormData(&form);

  // Remove the fax field -- we'll add it back later.
  std::vector<FormField>::iterator pos = form.fields.begin() + kFaxFieldOffset;
  FormField field = *pos;
  pos = form.fields.erase(pos);

  std::vector<FormData> forms(1, form);
  autofill_manager_->FormsSeen(forms);

  // Now, after the call to |FormsSeen|, we restore the field before filling.
  form.fields.insert(pos, field);

  std::string guid = autofill_manager_->GetLabeledProfile("Home")->guid();
  EXPECT_TRUE(autofill_manager_->FillAutoFillFormData(
      kDefaultPageID, form, form.fields[0],
      autofill_manager_->PackGUIDs(std::string(), guid)));

  int page_id = 0;
  FormData results;
  EXPECT_TRUE(GetAutoFillFormDataFilledMessage(&page_id, &results));
  ExpectFilledAddressFormElvis(page_id, results, kDefaultPageID, false);
}

TEST_F(AutoFillManagerTest, HiddenFields) {
  FormData form;
  form.name = ASCIIToUTF16("MyForm");
  form.method = ASCIIToUTF16("POST");
  form.origin = GURL("http://myform.com/form.html");
  form.action = GURL("http://myform.com/submit.html");
  form.user_submitted = true;

  FormField field;
  autofill_test::CreateTestFormField(
      "E-mail", "one", "one", "hidden", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "E-mail", "two", "two", "hidden", &field);
  form.fields.push_back(field);
  autofill_test::CreateTestFormField(
      "E-mail", "three", "three", "hidden", &field);
  form.fields.push_back(field);

  // Set up our form data.
  std::vector<FormData> forms;
  forms.push_back(form);
  autofill_manager_->FormsSeen(forms);

  // Submit the form.
  autofill_manager_->FormSubmitted(form);

  // TODO(jhawkins): We can't use the InfoBar anymore to determine if we saved
  // fields.  Need to query the PDM.
}

// Checks that resetting the auxiliary profile enabled preference does the right
// thing on all platforms.
TEST_F(AutoFillManagerTest, AuxiliaryProfilesReset) {
#if defined(OS_MACOSX)
  // Auxiliary profiles is implemented on Mac only.  It enables Mac Address
  // Book integration.
  ASSERT_TRUE(profile()->GetPrefs()->GetBoolean(
      prefs::kAutoFillAuxiliaryProfilesEnabled));
  profile()->GetPrefs()->SetBoolean(
      prefs::kAutoFillAuxiliaryProfilesEnabled, false);
  profile()->GetPrefs()->ClearPref(prefs::kAutoFillAuxiliaryProfilesEnabled);
  ASSERT_TRUE(profile()->GetPrefs()->GetBoolean(
      prefs::kAutoFillAuxiliaryProfilesEnabled));
#else
  ASSERT_FALSE(profile()->GetPrefs()->GetBoolean(
      prefs::kAutoFillAuxiliaryProfilesEnabled));
  profile()->GetPrefs()->SetBoolean(
      prefs::kAutoFillAuxiliaryProfilesEnabled, true);
  profile()->GetPrefs()->ClearPref(prefs::kAutoFillAuxiliaryProfilesEnabled);
  ASSERT_FALSE(profile()->GetPrefs()->GetBoolean(
      prefs::kAutoFillAuxiliaryProfilesEnabled));
#endif
}

