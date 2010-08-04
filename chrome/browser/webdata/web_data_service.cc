// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/webdata/web_data_service.h"

#include "base/message_loop.h"
#include "base/stl_util-inl.h"
#include "base/task.h"
#include "base/thread.h"
#include "chrome/browser/autofill/autofill_profile.h"
#include "chrome/browser/autofill/credit_card.h"
#include "chrome/browser/webdata/autofill_change.h"
#include "chrome/browser/webdata/autofill_entry.h"
#include "chrome/browser/webdata/web_database.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/notification_details.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_source.h"
#include "chrome/common/notification_type.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "webkit/glue/password_form.h"

////////////////////////////////////////////////////////////////////////////////
//
// WebDataService implementation.
//
////////////////////////////////////////////////////////////////////////////////

using base::Time;
using webkit_glue::FormField;
using webkit_glue::PasswordForm;

WebDataService::WebDataService()
  : is_running_(false),
    db_(NULL),
    failed_init_(false),
    should_commit_(false),
    next_request_handle_(1),
    main_loop_(MessageLoop::current()) {
}

bool WebDataService::Init(const FilePath& profile_path) {
  FilePath path = profile_path;
  path = path.Append(chrome::kWebDataFilename);
  return InitWithPath(path);
}

void WebDataService::Shutdown() {
  UnloadDatabase();
}

bool WebDataService::IsRunning() const {
  return is_running_;
}

void WebDataService::UnloadDatabase() {
  ScheduleTask(NewRunnableMethod(this, &WebDataService::ShutdownDatabase));
}

void WebDataService::CancelRequest(Handle h) {
  AutoLock l(pending_lock_);
  RequestMap::iterator i = pending_requests_.find(h);
  if (i == pending_requests_.end()) {
    NOTREACHED() << "Canceling a nonexistant web data service request";
    return;
  }
  i->second->Cancel();
}

bool WebDataService::IsDatabaseLoaded() {
  return db_ != NULL;
}

WebDatabase* WebDataService::GetDatabase() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::DB));
  return db_;
}

//////////////////////////////////////////////////////////////////////////////
//
// Keywords.
//
//////////////////////////////////////////////////////////////////////////////

void WebDataService::AddKeyword(const TemplateURL& url) {
  GenericRequest<TemplateURL>* request =
    new GenericRequest<TemplateURL>(this, GetNextRequestHandle(), NULL, url);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::AddKeywordImpl,
                                 request));
}

void WebDataService::RemoveKeyword(const TemplateURL& url) {
  GenericRequest<TemplateURL::IDType>* request =
      new GenericRequest<TemplateURL::IDType>(this, GetNextRequestHandle(),
                                              NULL, url.id());
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this, &WebDataService::RemoveKeywordImpl, request));
}

void WebDataService::UpdateKeyword(const TemplateURL& url) {
  GenericRequest<TemplateURL>* request =
      new GenericRequest<TemplateURL>(this, GetNextRequestHandle(), NULL, url);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this, &WebDataService::UpdateKeywordImpl, request));
}

WebDataService::Handle WebDataService::GetKeywords(
                                       WebDataServiceConsumer* consumer) {
  WebDataRequest* request =
      new WebDataRequest(this, GetNextRequestHandle(), consumer);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this,
                        &WebDataService::GetKeywordsImpl,
                        request));
  return request->GetHandle();
}

void WebDataService::SetDefaultSearchProvider(const TemplateURL* url) {
  GenericRequest<TemplateURL::IDType>* request =
    new GenericRequest<TemplateURL::IDType>(this,
                                            GetNextRequestHandle(),
                                            NULL,
                                            url ? url->id() : 0);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this, &WebDataService::SetDefaultSearchProviderImpl,
                        request));
}

void WebDataService::SetBuiltinKeywordVersion(int version) {
  GenericRequest<int>* request =
    new GenericRequest<int>(this, GetNextRequestHandle(), NULL, version);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this, &WebDataService::SetBuiltinKeywordVersionImpl,
                        request));
}

//////////////////////////////////////////////////////////////////////////////
//
// Web Apps
//
//////////////////////////////////////////////////////////////////////////////

void WebDataService::SetWebAppImage(const GURL& app_url,
                                    const SkBitmap& image) {
  GenericRequest2<GURL, SkBitmap>* request =
      new GenericRequest2<GURL, SkBitmap>(this, GetNextRequestHandle(),
                                         NULL, app_url, image);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::SetWebAppImageImpl,
                                 request));
}

void WebDataService::SetWebAppHasAllImages(const GURL& app_url,
                                           bool has_all_images) {
  GenericRequest2<GURL, bool>* request =
      new GenericRequest2<GURL, bool>(this, GetNextRequestHandle(),
                                     NULL, app_url, has_all_images);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::SetWebAppHasAllImagesImpl,
                                 request));
}

void WebDataService::RemoveWebApp(const GURL& app_url) {
  GenericRequest<GURL>* request =
      new GenericRequest<GURL>(this, GetNextRequestHandle(), NULL, app_url);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::RemoveWebAppImpl,
                                 request));
}

WebDataService::Handle WebDataService::GetWebAppImages(
    const GURL& app_url,
    WebDataServiceConsumer* consumer) {
  GenericRequest<GURL>* request =
      new GenericRequest<GURL>(this, GetNextRequestHandle(), consumer, app_url);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::GetWebAppImagesImpl,
                                 request));
  return request->GetHandle();
}

////////////////////////////////////////////////////////////////////////////////
//
// Password manager.
//
////////////////////////////////////////////////////////////////////////////////

void WebDataService::AddLogin(const PasswordForm& form) {
  GenericRequest<PasswordForm>* request =
      new GenericRequest<PasswordForm>(this, GetNextRequestHandle(), NULL,
                                       form);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::AddLoginImpl,
                                 request));
}

void WebDataService::UpdateLogin(const PasswordForm& form) {
  GenericRequest<PasswordForm>* request =
      new GenericRequest<PasswordForm>(this, GetNextRequestHandle(),
                                       NULL, form);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::UpdateLoginImpl,
                                 request));
}

void WebDataService::RemoveLogin(const PasswordForm& form) {
  GenericRequest<PasswordForm>* request =
     new GenericRequest<PasswordForm>(this, GetNextRequestHandle(), NULL,
                                      form);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::RemoveLoginImpl,
                                 request));
}

void WebDataService::RemoveLoginsCreatedBetween(const Time& delete_begin,
                                                const Time& delete_end) {
  GenericRequest2<Time, Time>* request =
    new GenericRequest2<Time, Time>(this,
                                    GetNextRequestHandle(),
                                    NULL,
                                    delete_begin,
                                    delete_end);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
      &WebDataService::RemoveLoginsCreatedBetweenImpl, request));
}

void WebDataService::RemoveLoginsCreatedAfter(const Time& delete_begin) {
  RemoveLoginsCreatedBetween(delete_begin, Time());
}

WebDataService::Handle WebDataService::GetLogins(
                                       const PasswordForm& form,
                                       WebDataServiceConsumer* consumer) {
  GenericRequest<PasswordForm>* request =
      new GenericRequest<PasswordForm>(this, GetNextRequestHandle(),
                                       consumer, form);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this, &WebDataService::GetLoginsImpl,
                                 request));
  return request->GetHandle();
}

WebDataService::Handle WebDataService::GetAutofillableLogins(
    WebDataServiceConsumer* consumer) {
  WebDataRequest* request =
      new WebDataRequest(this, GetNextRequestHandle(), consumer);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::GetAutofillableLoginsImpl,
                                 request));
  return request->GetHandle();
}

WebDataService::Handle WebDataService::GetBlacklistLogins(
    WebDataServiceConsumer* consumer) {
  WebDataRequest* request =
      new WebDataRequest(this, GetNextRequestHandle(), consumer);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::GetBlacklistLoginsImpl,
                                 request));
  return request->GetHandle();
}

////////////////////////////////////////////////////////////////////////////////
//
// AutoFill.
//
////////////////////////////////////////////////////////////////////////////////

void WebDataService::AddFormFields(
    const std::vector<FormField>& fields) {
  GenericRequest<std::vector<FormField> >* request =
      new GenericRequest<std::vector<FormField> >(
          this, GetNextRequestHandle(), NULL, fields);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::AddFormElementsImpl,
                                 request));
}

WebDataService::Handle WebDataService::GetFormValuesForElementName(
    const string16& name, const string16& prefix, int limit,
    WebDataServiceConsumer* consumer) {
  WebDataRequest* request =
      new WebDataRequest(this, GetNextRequestHandle(), consumer);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this,
                        &WebDataService::GetFormValuesForElementNameImpl,
                        request,
                        name,
                        prefix,
                        limit));
  return request->GetHandle();
}

void WebDataService::RemoveFormElementsAddedBetween(const Time& delete_begin,
                                                    const Time& delete_end) {
  GenericRequest2<Time, Time>* request =
    new GenericRequest2<Time, Time>(this,
                                    GetNextRequestHandle(),
                                    NULL,
                                    delete_begin,
                                    delete_end);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
      &WebDataService::RemoveFormElementsAddedBetweenImpl, request));
}

void WebDataService::RemoveFormValueForElementName(
    const string16& name, const string16& value) {
  GenericRequest2<string16, string16>* request =
      new GenericRequest2<string16, string16>(this,
                                              GetNextRequestHandle(),
                                              NULL,
                                              name, value);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this,
                        &WebDataService::RemoveFormValueForElementNameImpl,
                        request));
}

void WebDataService::AddAutoFillProfile(const AutoFillProfile& profile) {
  GenericRequest<AutoFillProfile>* request =
      new GenericRequest<AutoFillProfile>(
          this, GetNextRequestHandle(), NULL, profile);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::AddAutoFillProfileImpl,
                                 request));
}

void WebDataService::UpdateAutoFillProfile(const AutoFillProfile& profile) {
  GenericRequest<AutoFillProfile>* request =
      new GenericRequest<AutoFillProfile>(
          this, GetNextRequestHandle(), NULL, profile);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::UpdateAutoFillProfileImpl,
                                 request));
}

void WebDataService::RemoveAutoFillProfile(int profile_id) {
  GenericRequest<int>* request =
      new GenericRequest<int>(
          this, GetNextRequestHandle(), NULL, profile_id);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::RemoveAutoFillProfileImpl,
                                 request));
}

WebDataService::Handle WebDataService::GetAutoFillProfiles(
    WebDataServiceConsumer* consumer) {
  WebDataRequest* request =
      new WebDataRequest(this, GetNextRequestHandle(), consumer);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this,
                        &WebDataService::GetAutoFillProfilesImpl,
                        request));
  return request->GetHandle();
}

void WebDataService::AddCreditCard(const CreditCard& creditcard) {
  GenericRequest<CreditCard>* request =
      new GenericRequest<CreditCard>(
          this, GetNextRequestHandle(), NULL, creditcard);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::AddCreditCardImpl,
                                 request));
}

void WebDataService::UpdateCreditCard(const CreditCard& creditcard) {
  GenericRequest<CreditCard>* request =
      new GenericRequest<CreditCard>(
          this, GetNextRequestHandle(), NULL, creditcard);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::UpdateCreditCardImpl,
                                 request));
}

void WebDataService::RemoveCreditCard(int creditcard_id) {
  GenericRequest<int>* request =
      new GenericRequest<int>(
          this, GetNextRequestHandle(), NULL, creditcard_id);
  RegisterRequest(request);
  ScheduleTask(NewRunnableMethod(this,
                                 &WebDataService::RemoveCreditCardImpl,
                                 request));
}

WebDataService::Handle WebDataService::GetCreditCards(
    WebDataServiceConsumer* consumer) {
  WebDataRequest* request =
      new WebDataRequest(this, GetNextRequestHandle(), consumer);
  RegisterRequest(request);
  ScheduleTask(
      NewRunnableMethod(this,
                        &WebDataService::GetCreditCardsImpl,
                        request));
  return request->GetHandle();
}

WebDataService::~WebDataService() {
  if (is_running_ && db_) {
    DLOG_ASSERT("WebDataService dtor called without Shutdown");
  }
}

bool WebDataService::InitWithPath(const FilePath& path) {
  path_ = path;
  is_running_ = true;
  ScheduleTask(NewRunnableMethod(this,
      &WebDataService::InitializeDatabaseIfNecessary));
  return true;
}

void WebDataService::RequestCompleted(Handle h) {
  pending_lock_.Acquire();
  RequestMap::iterator i = pending_requests_.find(h);
  if (i == pending_requests_.end()) {
    NOTREACHED() << "Request completed called for an unknown request";
    pending_lock_.Release();
    return;
  }

  // Take ownership of the request object and remove it from the map.
  scoped_ptr<WebDataRequest> request(i->second);
  pending_requests_.erase(i);
  pending_lock_.Release();

  // Notify the consumer if needed.
  WebDataServiceConsumer* consumer;
  if (!request->IsCancelled() && (consumer = request->GetConsumer())) {
    consumer->OnWebDataServiceRequestDone(request->GetHandle(),
                                          request->GetResult());
  } else {
    // Nobody is taken ownership of the result, either because it is canceled
    // or there is no consumer. Destroy results that require special handling.
    WDTypedResult const *result = request->GetResult();
    if (result) {
      if (result->GetType() == AUTOFILL_PROFILES_RESULT) {
        const WDResult<std::vector<AutoFillProfile*> >* r =
            static_cast<const WDResult<std::vector<AutoFillProfile*> >*>(
                result);
        std::vector<AutoFillProfile*> profiles = r->GetValue();
        STLDeleteElements(&profiles);
      } else if (result->GetType() == AUTOFILL_CREDITCARDS_RESULT) {
        const WDResult<std::vector<CreditCard*> >* r =
            static_cast<const WDResult<std::vector<CreditCard*> >*>(result);

        std::vector<CreditCard*> credit_cards = r->GetValue();
        STLDeleteElements(&credit_cards);
      }
    }
  }
}

void WebDataService::RegisterRequest(WebDataRequest* request) {
  AutoLock l(pending_lock_);
  pending_requests_[request->GetHandle()] = request;
}

////////////////////////////////////////////////////////////////////////////////
//
// The following methods are executed in Chrome_WebDataThread.
//
////////////////////////////////////////////////////////////////////////////////

void WebDataService::DBInitFailed(sql::InitStatus init_status) {
  Source<WebDataService> source(this);
  int message_id = (init_status == sql::INIT_FAILURE) ?
      IDS_COULDNT_OPEN_PROFILE_ERROR : IDS_PROFILE_TOO_NEW_ERROR;
  NotificationService::current()->Notify(NotificationType::PROFILE_ERROR,
                                         source, Details<int>(&message_id));
}

void WebDataService::InitializeDatabaseIfNecessary() {
  if (db_ || failed_init_ || path_.empty())
    return;

  // In the rare case where the db fails to initialize a dialog may get shown
  // the blocks the caller, yet allows other messages through. For this reason
  // we only set db_ to the created database if creation is successful. That
  // way other methods won't do anything as db_ is still NULL.
  WebDatabase* db = new WebDatabase();
  sql::InitStatus init_status = db->Init(path_);
  if (init_status != sql::INIT_OK) {
    NOTREACHED() << "Cannot initialize the web database";
    failed_init_ = true;
    delete db;
    if (main_loop_) {
      main_loop_->PostTask(FROM_HERE,
          NewRunnableMethod(this, &WebDataService::DBInitFailed, init_status));
    }
    return;
  }

  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(this, &WebDataService::NotifyDatabaseLoadedOnUIThread));

  db_ = db;
  db_->BeginTransaction();
}

void WebDataService::NotifyDatabaseLoadedOnUIThread() {
  // Notify that the database has been initialized.
  NotificationService::current()->Notify(NotificationType::WEB_DATABASE_LOADED,
                                         Source<WebDataService>(this),
                                         NotificationService::NoDetails());
}

void WebDataService::ShutdownDatabase() {
  should_commit_ = false;

  if (db_) {
    db_->CommitTransaction();
    delete db_;
    db_ = NULL;
  }
}

void WebDataService::Commit() {
  if (should_commit_) {
    should_commit_ = false;

    if (db_) {
      db_->CommitTransaction();
      db_->BeginTransaction();
    }
  }
}

void WebDataService::ScheduleTask(Task* t) {
  if (is_running_)
    ChromeThread::PostTask(ChromeThread::DB, FROM_HERE, t);
  else
    NOTREACHED() << "Task scheduled after Shutdown()";
}

void WebDataService::ScheduleCommit() {
  if (should_commit_ == false) {
    should_commit_ = true;
    ScheduleTask(NewRunnableMethod(this, &WebDataService::Commit));
  }
}

int WebDataService::GetNextRequestHandle() {
  AutoLock l(pending_lock_);
  return ++next_request_handle_;
}

////////////////////////////////////////////////////////////////////////////////
//
// Keywords implementation.
//
////////////////////////////////////////////////////////////////////////////////

void WebDataService::AddKeywordImpl(GenericRequest<TemplateURL>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    db_->AddKeyword(request->GetArgument());
    ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::RemoveKeywordImpl(
    GenericRequest<TemplateURL::IDType>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    DCHECK(request->GetArgument());
    db_->RemoveKeyword(request->GetArgument());
    ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::UpdateKeywordImpl(GenericRequest<TemplateURL>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    if (!db_->UpdateKeyword(request->GetArgument()))
      NOTREACHED();
    ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::GetKeywordsImpl(WebDataRequest* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    WDKeywordsResult result;
    db_->GetKeywords(&result.keywords);
    result.default_search_provider_id = db_->GetDefaulSearchProviderID();
    result.builtin_keyword_version = db_->GetBuitinKeywordVersion();
    request->SetResult(
        new WDResult<WDKeywordsResult>(KEYWORDS_RESULT, result));
  }
  request->RequestComplete();
}

void WebDataService::SetDefaultSearchProviderImpl(
    GenericRequest<TemplateURL::IDType>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    if (!db_->SetDefaultSearchProviderID(request->GetArgument()))
      NOTREACHED();
    ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::SetBuiltinKeywordVersionImpl(
    GenericRequest<int>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    if (!db_->SetBuitinKeywordVersion(request->GetArgument()))
      NOTREACHED();
    ScheduleCommit();
  }
  request->RequestComplete();
}

////////////////////////////////////////////////////////////////////////////////
//
// Web Apps implementation.
//
////////////////////////////////////////////////////////////////////////////////

void WebDataService::SetWebAppImageImpl(
    GenericRequest2<GURL, SkBitmap>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    db_->SetWebAppImage(request->GetArgument1(), request->GetArgument2());
    ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::SetWebAppHasAllImagesImpl(
    GenericRequest2<GURL, bool>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    db_->SetWebAppHasAllImages(request->GetArgument1(),
                               request->GetArgument2());
    ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::RemoveWebAppImpl(GenericRequest<GURL>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    db_->RemoveWebApp(request->GetArgument());
    ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::GetWebAppImagesImpl(GenericRequest<GURL>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    WDAppImagesResult result;
    result.has_all_images = db_->GetWebAppHasAllImages(request->GetArgument());
    db_->GetWebAppImages(request->GetArgument(), &result.images);
    request->SetResult(
        new WDResult<WDAppImagesResult>(WEB_APP_IMAGES, result));
  }
  request->RequestComplete();
}

////////////////////////////////////////////////////////////////////////////////
//
// Password manager implementation.
//
////////////////////////////////////////////////////////////////////////////////

void WebDataService::AddLoginImpl(GenericRequest<PasswordForm>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    if (db_->AddLogin(request->GetArgument()))
      ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::UpdateLoginImpl(GenericRequest<PasswordForm>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    if (db_->UpdateLogin(request->GetArgument()))
      ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::RemoveLoginImpl(GenericRequest<PasswordForm>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    if (db_->RemoveLogin(request->GetArgument()))
      ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::RemoveLoginsCreatedBetweenImpl(
    GenericRequest2<Time, Time>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    if (db_->RemoveLoginsCreatedBetween(request->GetArgument1(),
                                        request->GetArgument2()))
      ScheduleCommit();
  }
  request->RequestComplete();
}

void WebDataService::GetLoginsImpl(GenericRequest<PasswordForm>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    std::vector<PasswordForm*> forms;
    db_->GetLogins(request->GetArgument(), &forms);
    request->SetResult(
        new WDResult<std::vector<PasswordForm*> >(PASSWORD_RESULT, forms));
  }
  request->RequestComplete();
}

void WebDataService::GetAutofillableLoginsImpl(WebDataRequest* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    std::vector<PasswordForm*> forms;
    db_->GetAllLogins(&forms, false);
    request->SetResult(
        new WDResult<std::vector<PasswordForm*> >(PASSWORD_RESULT, forms));
  }
  request->RequestComplete();
}

void WebDataService::GetBlacklistLoginsImpl(WebDataRequest* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    std::vector<PasswordForm*> all_forms;
    db_->GetAllLogins(&all_forms, true);
    std::vector<PasswordForm*> blacklist_forms;
    for (std::vector<PasswordForm*>::iterator i = all_forms.begin();
         i != all_forms.end(); ++i) {
      scoped_ptr<PasswordForm> form(*i);
      if (form->blacklisted_by_user) {
        blacklist_forms.push_back(form.release());
      }
    }
    all_forms.clear();
    request->SetResult(
        new WDResult<std::vector<PasswordForm*> >(PASSWORD_RESULT,
                                                  blacklist_forms));
  }
  request->RequestComplete();
}

////////////////////////////////////////////////////////////////////////////////
//
// AutoFill implementation.
//
////////////////////////////////////////////////////////////////////////////////

void WebDataService::AddFormElementsImpl(
    GenericRequest<std::vector<FormField> >* request) {
  InitializeDatabaseIfNecessary();
  const std::vector<FormField>& form_fields = request->GetArgument();
  if (db_ && !request->IsCancelled()) {
    AutofillChangeList changes;
    if (!db_->AddFormFieldValues(form_fields, &changes))
      NOTREACHED();
    request->SetResult(
        new WDResult<AutofillChangeList>(AUTOFILL_CHANGES, changes));
    ScheduleCommit();

    // Post the notifications including the list of affected keys.
    // This is sent here so that work resulting from this notification will be
    // done on the DB thread, and not the UI thread.
    NotificationService::current()->Notify(
        NotificationType::AUTOFILL_ENTRIES_CHANGED,
        Source<WebDataService>(this),
        Details<AutofillChangeList>(&changes));
  }

  request->RequestComplete();
}

void WebDataService::GetFormValuesForElementNameImpl(WebDataRequest* request,
    const string16& name, const string16& prefix, int limit) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    std::vector<string16> values;
    db_->GetFormValuesForElementName(name, prefix, &values, limit);
    request->SetResult(
        new WDResult<std::vector<string16> >(AUTOFILL_VALUE_RESULT, values));
  }
  request->RequestComplete();
}

void WebDataService::RemoveFormElementsAddedBetweenImpl(
    GenericRequest2<Time, Time>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    AutofillChangeList changes;
    if (db_->RemoveFormElementsAddedBetween(request->GetArgument1(),
                                            request->GetArgument2(),
                                            &changes)) {
      if (changes.size() > 0) {
        request->SetResult(
            new WDResult<AutofillChangeList>(AUTOFILL_CHANGES, changes));

        // Post the notifications including the list of affected keys.
        // This is sent here so that work resulting from this notification
        // will be done on the DB thread, and not the UI thread.
        NotificationService::current()->Notify(
            NotificationType::AUTOFILL_ENTRIES_CHANGED,
            Source<WebDataService>(this),
            Details<AutofillChangeList>(&changes));
      }
      ScheduleCommit();
    }
  }
  request->RequestComplete();
}

void WebDataService::RemoveFormValueForElementNameImpl(
    GenericRequest2<string16, string16>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    const string16& name = request->GetArgument1();
    const string16& value = request->GetArgument2();

    if (db_->RemoveFormElement(name, value)) {
      AutofillChangeList changes;
      changes.push_back(AutofillChange(AutofillChange::REMOVE,
                                       AutofillKey(name, value)));
      request->SetResult(
          new WDResult<AutofillChangeList>(AUTOFILL_CHANGES, changes));
      ScheduleCommit();

      // Post the notifications including the list of affected keys.
      NotificationService::current()->Notify(
          NotificationType::AUTOFILL_ENTRIES_CHANGED,
          Source<WebDataService>(this),
          Details<AutofillChangeList>(&changes));
    }
  }
  request->RequestComplete();
}

void WebDataService::AddAutoFillProfileImpl(
    GenericRequest<AutoFillProfile>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    const AutoFillProfile& profile = request->GetArgument();
    if (!db_->AddAutoFillProfile(profile)) {
      NOTREACHED();
      return;
    }
    ScheduleCommit();

    AutofillProfileChange change(AutofillProfileChange::ADD,
                                 profile.Label(), &profile, string16());
    NotificationService::current()->Notify(
        NotificationType::AUTOFILL_PROFILE_CHANGED,
        Source<WebDataService>(this),
        Details<AutofillProfileChange>(&change));
  }
  request->RequestComplete();
}

void WebDataService::UpdateAutoFillProfileImpl(
    GenericRequest<AutoFillProfile>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    const AutoFillProfile& profile = request->GetArgument();
    // The AUTOFILL_PROFILE_CHANGED contract for an update requires that we
    // send along the label of the un-updated profile, to detect label
    // changes separately.  So first, we query for the existing profile.
    AutoFillProfile* old_profile_ptr = NULL;
    if (!db_->GetAutoFillProfileForID(profile.unique_id(), &old_profile_ptr))
      NOTREACHED();
    if (old_profile_ptr) {
      scoped_ptr<AutoFillProfile> old_profile(old_profile_ptr);
      if (!db_->UpdateAutoFillProfile(profile))
        NOTREACHED();
      ScheduleCommit();

      AutofillProfileChange change(AutofillProfileChange::UPDATE,
                                   profile.Label(), &profile,
                                   old_profile->Label());
      NotificationService::current()->Notify(
          NotificationType::AUTOFILL_PROFILE_CHANGED,
          Source<WebDataService>(this),
          Details<AutofillProfileChange>(&change));
    }
  }
  request->RequestComplete();
}

void WebDataService::RemoveAutoFillProfileImpl(
    GenericRequest<int>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    int profile_id = request->GetArgument();
    AutoFillProfile* profile = NULL;
    if (!db_->GetAutoFillProfileForID(profile_id, &profile))
      NOTREACHED();

    if (profile) {
      scoped_ptr<AutoFillProfile> dead_profile(profile);
      if (!db_->RemoveAutoFillProfile(profile_id))
        NOTREACHED();
      ScheduleCommit();

      AutofillProfileChange change(AutofillProfileChange::REMOVE,
                                   dead_profile->Label(),
                                   NULL, string16());
      NotificationService::current()->Notify(
          NotificationType::AUTOFILL_PROFILE_CHANGED,
          Source<WebDataService>(this),
          Details<AutofillProfileChange>(&change));
    }
  }
  request->RequestComplete();
}

void WebDataService::GetAutoFillProfilesImpl(WebDataRequest* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    std::vector<AutoFillProfile*> profiles;
    db_->GetAutoFillProfiles(&profiles);
    request->SetResult(
        new WDResult<std::vector<AutoFillProfile*> >(AUTOFILL_PROFILES_RESULT,
                                                     profiles));
  }
  request->RequestComplete();
}

void WebDataService::AddCreditCardImpl(
    GenericRequest<CreditCard>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    const CreditCard& creditcard = request->GetArgument();
    if (!db_->AddCreditCard(creditcard))
      NOTREACHED();
    ScheduleCommit();

    AutofillCreditCardChange change(AutofillCreditCardChange::ADD,
        creditcard.Label(), &creditcard);
    NotificationService::current()->Notify(
        NotificationType::AUTOFILL_CREDIT_CARD_CHANGED,
        Source<WebDataService>(this),
        Details<AutofillCreditCardChange>(&change));
  }
  request->RequestComplete();
}

void WebDataService::UpdateCreditCardImpl(
    GenericRequest<CreditCard>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    const CreditCard& creditcard = request->GetArgument();
    if (!db_->UpdateCreditCard(creditcard))
      NOTREACHED();
    ScheduleCommit();

    AutofillCreditCardChange change(AutofillCreditCardChange::UPDATE,
        creditcard.Label(), &creditcard);
    NotificationService::current()->Notify(
        NotificationType::AUTOFILL_CREDIT_CARD_CHANGED,
        Source<WebDataService>(this),
        Details<AutofillCreditCardChange>(&change));
  }
  request->RequestComplete();
}

void WebDataService::RemoveCreditCardImpl(
    GenericRequest<int>* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    int creditcard_id = request->GetArgument();
    CreditCard* dead_card_ptr = NULL;
    if (!db_->GetCreditCardForID(creditcard_id, &dead_card_ptr))
      NOTREACHED();

    scoped_ptr<CreditCard> dead_card(dead_card_ptr);
    if (!db_->RemoveCreditCard(creditcard_id))
      NOTREACHED();

    ScheduleCommit();

    if (dead_card.get()) {
      AutofillCreditCardChange change(AutofillCreditCardChange::REMOVE,
                                      dead_card->Label(), NULL);
      NotificationService::current()->Notify(
          NotificationType::AUTOFILL_CREDIT_CARD_CHANGED,
          Source<WebDataService>(this),
          Details<AutofillCreditCardChange>(&change));
    }
  }
  request->RequestComplete();
}

void WebDataService::GetCreditCardsImpl(WebDataRequest* request) {
  InitializeDatabaseIfNecessary();
  if (db_ && !request->IsCancelled()) {
    std::vector<CreditCard*> creditcards;
    db_->GetCreditCards(&creditcards);
    request->SetResult(
        new WDResult<std::vector<CreditCard*> >(AUTOFILL_CREDITCARDS_RESULT,
                                                creditcards));
  }
  request->RequestComplete();
}

////////////////////////////////////////////////////////////////////////////////
//
// WebDataRequest implementation.
//
////////////////////////////////////////////////////////////////////////////////

WebDataService::WebDataRequest::WebDataRequest(WebDataService* service,
                                               Handle handle,
                                               WebDataServiceConsumer* consumer)
    : service_(service),
      handle_(handle),
      canceled_(false),
      consumer_(consumer),
      result_(NULL) {
  message_loop_ = MessageLoop::current();
}

WebDataService::WebDataRequest::~WebDataRequest() {
  delete result_;
}

WebDataService::Handle WebDataService::WebDataRequest::GetHandle() const {
  return handle_;
}

WebDataServiceConsumer* WebDataService::WebDataRequest::GetConsumer() const {
  return consumer_;
}

bool WebDataService::WebDataRequest::IsCancelled() const {
  return canceled_;
}

void WebDataService::WebDataRequest::Cancel() {
  canceled_ = true;
  consumer_ = NULL;
}

void WebDataService::WebDataRequest::SetResult(WDTypedResult* r) {
  result_ = r;
}

const WDTypedResult* WebDataService::WebDataRequest::GetResult() const {
  return result_;
}

void WebDataService::WebDataRequest::RequestComplete() {
  WebDataService* s = service_;
  Task* t = NewRunnableMethod(s,
                              &WebDataService::RequestCompleted,
                              handle_);
  message_loop_->PostTask(FROM_HERE, t);
}
