// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/autofill_profile_model_associator.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/webdata/web_database.h"

using sync_api::ReadNode;
namespace browser_sync {

const char kAutofillProfileTag[] = "google_chrome_autofill_profile";

AutofillProfileModelAssociator::AutofillProfileModelAssociator(
    ProfileSyncService* sync_service,
    WebDatabase* web_database,
    PersonalDataManager* personal_data)
    : sync_service_(sync_service),
      web_database_(web_database),
      personal_data_(personal_data),
      autofill_node_id_(sync_api::kInvalidId),
      abort_association_pending_(false) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  DCHECK(sync_service_);
  DCHECK(web_database_);
  DCHECK(personal_data_);
}

AutofillProfileModelAssociator::~AutofillProfileModelAssociator() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
}

bool AutofillProfileModelAssociator::TraverseAndAssociateChromeAutoFillProfiles(
    sync_api::WriteTransaction* write_trans,
    const sync_api::ReadNode& autofill_root,
    const std::vector<AutoFillProfile*>& all_profiles_from_db,
    std::set<std::string>* current_profiles,
    std::vector<AutoFillProfile*>* updated_profiles,
    std::vector<AutoFillProfile*>* new_profiles,
    std::vector<std::string>* profiles_to_delete) {

  // Alias the all_profiles_from_db so we fit in 80 characters
  const std::vector<AutoFillProfile*>& profiles(all_profiles_from_db);
  for (std::vector<AutoFillProfile*>::const_iterator ix = profiles.begin();
      ix != profiles.end();
      ++ix) {
    std::string guid((*ix)->guid());

    ReadNode node(write_trans);
    if (node.InitByClientTagLookup(syncable::AUTOFILL_PROFILE, guid)) {
      const sync_pb::AutofillProfileSpecifics& autofill(
          node.GetAutofillProfileSpecifics());
      if (OverwriteProfileWithServerData(*ix, autofill)) {
        updated_profiles->push_back(*ix);
      }
      Associate(&guid, node.GetId());
      current_profiles->insert(guid);
    } else {
      MakeNewAutofillProfileSyncNodeIfNeeded(write_trans,
          autofill_root,
          (**ix),
          new_profiles,
          current_profiles,
          profiles_to_delete);
    }
  }

  return true;
}

bool AutofillProfileModelAssociator::LoadAutofillData(
    std::vector<AutoFillProfile*>* profiles) {
  if (IsAbortPending())
    return false;

  if (!web_database_->GetAutoFillProfiles(profiles))
    return false;

  return true;
}

bool AutofillProfileModelAssociator::AssociateModels() {
  VLOG(1) << "Associating Autofill Models";
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  {
    AutoLock lock(abort_association_pending_lock_);
    abort_association_pending_ = false;
  }

  ScopedVector<AutoFillProfile> profiles;

  if (!LoadAutofillData(&profiles.get())) {
    LOG(ERROR) << "Could not get the autofill data from WebDatabase.";
    return false;
  }

  DataBundle bundle;
  {
    // The write transaction lock is held inside this block.
    // We do all the web db operations outside this block.
    sync_api::WriteTransaction trans(
        sync_service_->backend()->GetUserShareHandle());

    sync_api::ReadNode autofill_root(&trans);
    if (!autofill_root.InitByTagLookup(kAutofillProfileTag)) {
      LOG(ERROR) << "Server did not create the top-level autofill node. We "
                 << "might be running against an out-of-date server.";
      return false;
    }

    if (!TraverseAndAssociateChromeAutoFillProfiles(&trans, autofill_root,
            profiles.get(), &bundle.current_profiles,
            &bundle.updated_profiles,
            &bundle.new_profiles,
            &bundle.profiles_to_delete) ||
        !TraverseAndAssociateAllSyncNodes(&trans, autofill_root, &bundle)) {
      return false;
    }
  }

  if (!SaveChangesToWebData(bundle)) {
    LOG(ERROR) << "Failed to update autofill entries.";
    return false;
  }

  // TODO(lipalani) Bug 64111- split out the OptimisticRefreshTask
  // into its own class
  // from autofill_model_associator
  // Will be done as part of the autofill_model_associator work.
  // BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
  // new DoOptimisticRefreshTask(personal_data_));
  return true;
}

bool AutofillProfileModelAssociator::DisassociateModels() {
  id_map_.clear();
  id_map_inverse_.clear();
  return true;
}

// Helper to compare the local value and cloud value of a field, merge into
// the local value if they differ, and return whether the merge happened.
bool AutofillProfileModelAssociator::MergeField(FormGroup* f,
    AutoFillFieldType t,
    const std::string& specifics_field) {
  if (UTF16ToUTF8(f->GetFieldText(AutoFillType(t))) == specifics_field)
    return false;
  f->SetInfo(AutoFillType(t), UTF8ToUTF16(specifics_field));
  return true;
}
bool AutofillProfileModelAssociator::SyncModelHasUserCreatedNodes(
    bool *has_nodes) {
  CHECK_NE(has_nodes, reinterpret_cast<bool*>(NULL));
  sync_api::ReadTransaction trans(
      sync_service_->backend()->GetUserShareHandle());

  sync_api::ReadNode node(&trans);

  if (!node.InitByClientTagLookup(
        syncable::AUTOFILL_PROFILE,
        kAutofillProfileTag)) {
    LOG(ERROR) << "Sever did not create a top level node"
               << "Out of data server or autofill type not enabled";
    return false;
  }

  *has_nodes = sync_api::kInvalidId != node.GetFirstChildId();
  return true;
}
// static
bool AutofillProfileModelAssociator::OverwriteProfileWithServerData(
    AutoFillProfile* merge_into,
    const sync_pb::AutofillProfileSpecifics& specifics) {
  bool diff = false;
  AutoFillProfile* p = merge_into;
  const sync_pb::AutofillProfileSpecifics& s(specifics);
  diff = MergeField(p, NAME_FIRST, s.name_first()) || diff;
  diff = MergeField(p, NAME_LAST, s.name_last()) || diff;
  diff = MergeField(p, NAME_MIDDLE, s.name_middle()) || diff;
  diff = MergeField(p, ADDRESS_HOME_LINE1, s.address_home_line1()) || diff;
  diff = MergeField(p, ADDRESS_HOME_LINE2, s.address_home_line2()) || diff;
  diff = MergeField(p, ADDRESS_HOME_CITY, s.address_home_city()) || diff;
  diff = MergeField(p, ADDRESS_HOME_STATE, s.address_home_state()) || diff;
  diff = MergeField(p, ADDRESS_HOME_COUNTRY, s.address_home_country()) || diff;
  diff = MergeField(p, ADDRESS_HOME_ZIP, s.address_home_zip()) || diff;
  diff = MergeField(p, EMAIL_ADDRESS, s.email_address()) || diff;
  diff = MergeField(p, COMPANY_NAME, s.company_name()) || diff;
  diff = MergeField(p, PHONE_FAX_WHOLE_NUMBER, s.phone_fax_whole_number())
      || diff;
  diff = MergeField(p, PHONE_HOME_WHOLE_NUMBER, s.phone_home_whole_number())
      || diff;
  return diff;
}


int64 AutofillProfileModelAssociator::FindSyncNodeWithProfile(
    sync_api::WriteTransaction* trans,
    const sync_api::BaseNode& autofill_root,
    const AutoFillProfile& profile_from_db) {
  int64 sync_child_id = autofill_root.GetFirstChildId();
  while (sync_child_id != sync_api::kInvalidId) {
    ReadNode read_node(trans);
    AutoFillProfile p;
    if (read_node.InitByIdLookup(sync_child_id)) {
      LOG(ERROR) << "unable to find the id given by getfirst child " <<
        sync_child_id;
      return sync_api::kInvalidId;
    }
    const sync_pb::AutofillProfileSpecifics& autofill_specifics(
        read_node.GetAutofillProfileSpecifics());
    OverwriteProfileWithServerData(&p, autofill_specifics);
    if (p.Compare(profile_from_db) == 0) {
      return sync_child_id;
    }
    sync_child_id = read_node.GetSuccessorId();
  }

  return sync_api::kInvalidId;
}
bool AutofillProfileModelAssociator::MakeNewAutofillProfileSyncNodeIfNeeded(
    sync_api::WriteTransaction* trans,
    const sync_api::BaseNode& autofill_root,
    const AutoFillProfile& profile,
    std::vector<AutoFillProfile*>* new_profiles,
    std::set<std::string>* current_profiles,
    std::vector<std::string>* profiles_to_delete) {

  int64 sync_node_id = FindSyncNodeWithProfile(trans, autofill_root, profile);
  if (sync_node_id != sync_api::kInvalidId) {
    // In case of duplicates throw away the local profile and apply the
    // server profile.(The only difference between the 2 profiles are the guids)
    profiles_to_delete->push_back(profile.guid());
    sync_api::ReadNode read_node(trans);
    if (!read_node.InitByIdLookup(sync_node_id)) {
      LOG(ERROR);
      return false;
    }
    const sync_pb::AutofillProfileSpecifics& autofill_specifics(
        read_node.GetAutofillProfileSpecifics());
    AutoFillProfile* p = new AutoFillProfile(autofill_specifics.guid());
    OverwriteProfileWithServerData(p, autofill_specifics);
    new_profiles->push_back(p);
    std::string guid = autofill_specifics.guid();
    Associate(&guid, sync_node_id);
    current_profiles->insert(autofill_specifics.guid());
  } else {
    sync_api::WriteNode node(trans);
    if (!node.InitUniqueByCreation(
             syncable::AUTOFILL_PROFILE, autofill_root, profile.guid())) {
      LOG(ERROR) << "Failed to create autofill sync node.";
      return false;
    }
    node.SetTitle(UTF8ToWide(profile.guid()));

    // TODO(lipalani) -Bug 64111 This needs rewriting. This will be tackled
    // when rewriting autofill change processor.
    // AutofillChangeProcessor::WriteAutofillProfile(profile, &node);
  }
  return true;
}

bool AutofillProfileModelAssociator::TraverseAndAssociateAllSyncNodes(
    sync_api::WriteTransaction* write_trans,
    const sync_api::ReadNode& autofill_root,
    DataBundle* bundle) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));

  int64 sync_child_id = autofill_root.GetFirstChildId();
  while (sync_child_id != sync_api::kInvalidId) {
    ReadNode sync_child(write_trans);
    if (!sync_child.InitByIdLookup(sync_child_id)) {
      LOG(ERROR) << "Failed to fetch child node.";
      return false;
    }
    const sync_pb::AutofillProfileSpecifics& autofill(
        sync_child.GetAutofillProfileSpecifics());

    AddNativeProfileIfNeeded(autofill, bundle, sync_child);

    sync_child_id = sync_child.GetSuccessorId();
  }
  return true;
}

void AutofillProfileModelAssociator::AddNativeProfileIfNeeded(
    const sync_pb::AutofillProfileSpecifics& profile,
    DataBundle* bundle,
    const sync_api::ReadNode& node) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));

  if (bundle->current_profiles.find(profile.guid()) ==
      bundle->current_profiles.end()) {
    std::string guid(profile.guid());
    Associate(&guid, node.GetId());
    AutoFillProfile* p = new AutoFillProfile(profile.guid());
    OverwriteProfileWithServerData(p, profile);
    bundle->new_profiles.push_back(p);
  }
}

bool AutofillProfileModelAssociator::SaveChangesToWebData(
    const DataBundle& bundle) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));

  if (IsAbortPending())
    return false;

  for (size_t i = 0; i < bundle.new_profiles.size(); i++) {
    if (IsAbortPending())
      return false;
    if (!web_database_->AddAutoFillProfile(*bundle.new_profiles[i]))
      return false;
  }

  for (size_t i = 0; i < bundle.updated_profiles.size(); i++) {
    if (IsAbortPending())
      return false;
    if (!web_database_->UpdateAutoFillProfile(*bundle.updated_profiles[i]))
      return false;
  }

  for (size_t i = 0; i< bundle.profiles_to_delete.size(); ++i) {
    if (IsAbortPending())
      return false;
    if (!web_database_->RemoveAutoFillProfile(bundle.profiles_to_delete[i]))
      return false;
  }
  return true;
}

void AutofillProfileModelAssociator::Associate(
    const std::string* autofill,
    int64 sync_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  DCHECK_NE(sync_api::kInvalidId, sync_id);
  DCHECK(id_map_.find(*autofill) == id_map_.end());
  DCHECK(id_map_inverse_.find(sync_id) == id_map_inverse_.end());
  id_map_[*autofill] = sync_id;
  id_map_inverse_[sync_id] = *autofill;
}

void AutofillProfileModelAssociator::Disassociate(int64 sync_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  SyncIdToAutofillMap::iterator iter = id_map_inverse_.find(sync_id);
  if (iter == id_map_inverse_.end())
    return;
  CHECK(id_map_.erase(iter->second));
  id_map_inverse_.erase(iter);
}

int64 AutofillProfileModelAssociator::GetSyncIdFromChromeId(
    const std::string autofill) {
  AutofillToSyncIdMap::const_iterator iter = id_map_.find(autofill);
  return iter == id_map_.end() ? sync_api::kInvalidId : iter->second;
}

void AutofillProfileModelAssociator::AbortAssociation() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  AutoLock lock(abort_association_pending_lock_);
  abort_association_pending_ = true;
}

bool AutofillProfileModelAssociator::IsAbortPending() {
  AutoLock lock(abort_association_pending_lock_);
  return abort_association_pending_;
}

}  // namespace browser_sync

