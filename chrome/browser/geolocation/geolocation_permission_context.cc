// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/geolocation/geolocation_permission_context.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/geolocation/geolocation_content_settings_map.h"
#include "chrome/browser/geolocation/geolocation_dispatcher_host.h"
#include "chrome/browser/geolocation/location_arbitrator.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/render_view_host_notification_task.h"
#include "chrome/browser/tab_contents/infobar_delegate.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "net/base/net_util.h"

// This class controls the geolocation infobar queue per profile, and it's an
// internal class to GeolocationPermissionContext.
// An alternate approach would be to have this queue per tab, and use
// notifications to broadcast when permission is set / listen to notification to
// cancel pending requests. This may be specially useful if there are other
// things listening for such notifications.
// For the time being this class is self-contained and it doesn't seem pulling
// the notification infrastructure would simplify.
class GeolocationInfoBarQueueController {
 public:
  GeolocationInfoBarQueueController(
      GeolocationPermissionContext* geolocation_permission_context,
      Profile* profile);
  ~GeolocationInfoBarQueueController();

  // The InfoBar will be displayed immediately if the tab is not already
  // displaying one, otherwise it'll be queued.
  void CreateInfoBarRequest(
      int render_process_id, int render_view_id, int bridge_id,
      const GURL& requesting_frame, const GURL& emebedder);

  // Cancels a specific infobar request.
  void CancelInfoBarRequest(
      int render_process_id, int render_view_id, int bridge_id);

  // Called by the InfoBarDelegate to notify it's closed. It'll display a new
  // InfoBar if there's any request pending for this tab.
  void OnInfoBarClosed(
      int render_process_id, int render_view_id, int bridge_id);

  // Called by the InfoBarDelegate to notify permission has been set.
  // It'll notify and dismiss any other pending InfoBar request for the same
  // |requesting_frame| and embedder.
  void OnPermissionSet(
      int render_process_id, int render_view_id, int bridge_id,
      const GURL& requesting_frame, const GURL& embedder, bool allowed);

 private:
  struct PendingInfoBarRequest;
  typedef std::vector<PendingInfoBarRequest> PendingInfoBarRequests;

  // Shows the first pending infobar for this tab.
  void ShowQueuedInfoBar(int render_process_id, int render_view_id);

  // Cancels an InfoBar request and returns the next iterator position.
  std::vector<PendingInfoBarRequest>::iterator CancelInfoBarRequestInternal(
      std::vector<PendingInfoBarRequest>::iterator i);

  GeolocationPermissionContext* const geolocation_permission_context_;
  Profile* const profile_;
  // Contains all pending infobar requests.
  PendingInfoBarRequests pending_infobar_requests_;
};

namespace {

// This is the delegate used to display the confirmation info bar.
class GeolocationConfirmInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  GeolocationConfirmInfoBarDelegate(
      TabContents* tab_contents, GeolocationInfoBarQueueController* controller,
      int render_process_id, int render_view_id, int bridge_id,
      const GURL& requesting_frame_url,
      const std::string& display_languages)
      : ConfirmInfoBarDelegate(tab_contents),
        tab_contents_(tab_contents),
        controller_(controller),
        render_process_id_(render_process_id),
        render_view_id_(render_view_id),
        bridge_id_(bridge_id),
        requesting_frame_url_(requesting_frame_url),
        display_languages_(display_languages) {
  }

  // ConfirmInfoBarDelegate
  virtual void InfoBarClosed() {
    controller_->OnInfoBarClosed(render_process_id_, render_view_id_,
                                 bridge_id_);
    delete this;
  }
  virtual Type GetInfoBarType() { return PAGE_ACTION_TYPE; }
  virtual bool Accept() { return OnPermissionSet(true); }
  virtual bool Cancel() { return OnPermissionSet(false); }
  virtual int GetButtons() const { return BUTTON_OK | BUTTON_CANCEL; }
  virtual string16 GetButtonLabel(InfoBarButton button) const {
    switch (button) {
      case BUTTON_OK:
        return l10n_util::GetStringUTF16(IDS_GEOLOCATION_ALLOW_BUTTON);
      case BUTTON_CANCEL:
        return l10n_util::GetStringUTF16(IDS_GEOLOCATION_DENY_BUTTON);
      default:
        // All buttons are labeled above.
        NOTREACHED() << "Bad button id " << button;
        return string16();
    }
  }
  virtual string16 GetMessageText() const {
    return l10n_util::GetStringFUTF16(
        IDS_GEOLOCATION_INFOBAR_QUESTION,
        net::FormatUrl(requesting_frame_url_.GetOrigin(), display_languages_));
  }
  virtual SkBitmap* GetIcon() const {
    return ResourceBundle::GetSharedInstance().GetBitmapNamed(
        IDR_GEOLOCATION_INFOBAR_ICON);
  }
  virtual string16 GetLinkText() {
    return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
  }
  virtual bool LinkClicked(WindowOpenDisposition disposition) {
    // Ignore the click dispostion and always open in a new top level tab.
    tab_contents_->OpenURL(
        GURL(l10n_util::GetStringUTF8(IDS_LEARN_MORE_GEOLOCATION_URL)), GURL(),
        NEW_FOREGROUND_TAB, PageTransition::LINK);
    return false;  // Do not dismiss the info bar.
  }

 private:
  bool OnPermissionSet(bool confirm) {
    controller_->OnPermissionSet(
        render_process_id_, render_view_id_, bridge_id_, requesting_frame_url_,
        tab_contents_->GetURL(), confirm);
    return true;
  }

  TabContents* tab_contents_;
  GeolocationInfoBarQueueController* controller_;
  int render_process_id_;
  int render_view_id_;
  int bridge_id_;
  GURL requesting_frame_url_;
  std::string display_languages_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(GeolocationConfirmInfoBarDelegate);
};

}  // namespace

struct GeolocationInfoBarQueueController::PendingInfoBarRequest {
  int render_process_id;
  int render_view_id;
  int bridge_id;
  GURL requesting_frame;
  GURL embedder;
  // If non-NULL, it's the current geolocation infobar for this tab.
  InfoBarDelegate* infobar_delegate;

  bool IsForTab(int p_render_process_id, int p_render_view_id) const {
    return render_process_id == p_render_process_id &&
           render_view_id == p_render_view_id;
  }

  bool IsForPair(const GURL& p_requesting_frame, const GURL& p_embedder) const {
    return requesting_frame == p_requesting_frame &&
           embedder == p_embedder;
  }

  bool Equals(int p_render_process_id,
              int p_render_view_id,
              int p_bridge_id) const {
    return IsForTab(p_render_process_id, p_render_view_id) &&
           bridge_id == p_bridge_id;
  }
};

GeolocationInfoBarQueueController::GeolocationInfoBarQueueController(
    GeolocationPermissionContext* geolocation_permission_context,
    Profile* profile)
    : geolocation_permission_context_(geolocation_permission_context),
      profile_(profile) {
}

GeolocationInfoBarQueueController::~GeolocationInfoBarQueueController() {
}

void GeolocationInfoBarQueueController::CreateInfoBarRequest(
    int render_process_id, int render_view_id, int bridge_id,
    const GURL& requesting_frame, const GURL& embedder) {
  // This makes sure that no duplicates are added to
  // |pending_infobar_requests_| as an artificial permission request may
  // already exist in the queue as per
  // GeolocationPermissionContext::StartUpdatingRequested
  // See http://crbug.com/51899 for more details.
  // TODO(joth): Once we have CLIENT_BASED_GEOLOCATION and
  // WTF_USE_PREEMPT_GEOLOCATION_PERMISSION set in WebKit we should be able to
  // just use a DCHECK to check if a duplicate is attempting to be added.
  PendingInfoBarRequests::iterator i = pending_infobar_requests_.begin();
  while (i != pending_infobar_requests_.end()) {
    if (i->Equals(render_process_id, render_view_id, bridge_id)) {
      // The request already exists.
      DCHECK(i->IsForPair(requesting_frame, embedder));
      return;
    }
    ++i;
  }
  PendingInfoBarRequest pending_infobar_request;
  pending_infobar_request.render_process_id = render_process_id;
  pending_infobar_request.render_view_id = render_view_id;
  pending_infobar_request.bridge_id = bridge_id;
  pending_infobar_request.requesting_frame = requesting_frame;
  pending_infobar_request.embedder = embedder;
  pending_infobar_request.infobar_delegate = NULL;
  pending_infobar_requests_.push_back(pending_infobar_request);
  ShowQueuedInfoBar(render_process_id, render_view_id);
}

void GeolocationInfoBarQueueController::CancelInfoBarRequest(
    int render_process_id, int render_view_id, int bridge_id) {
  for (PendingInfoBarRequests::iterator i = pending_infobar_requests_.begin();
       i != pending_infobar_requests_.end(); ++i) {
    if (i->Equals(render_process_id, render_view_id, bridge_id)) {
      CancelInfoBarRequestInternal(i);
      break;
    }
  }
}

void GeolocationInfoBarQueueController::OnInfoBarClosed(
    int render_process_id, int render_view_id, int bridge_id) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  for (PendingInfoBarRequests::iterator i = pending_infobar_requests_.begin();
       i != pending_infobar_requests_.end(); ++i) {
    if (i->Equals(render_process_id, render_view_id, bridge_id)) {
      pending_infobar_requests_.erase(i);
      break;
    }
  }
  ShowQueuedInfoBar(render_process_id, render_view_id);
}

void GeolocationInfoBarQueueController::OnPermissionSet(
    int render_process_id, int render_view_id, int bridge_id,
    const GURL& requesting_frame, const GURL& embedder, bool allowed) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  // Persist the permission.
  ContentSetting content_setting =
      allowed ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK;
  profile_->GetGeolocationContentSettingsMap()->SetContentSetting(
      requesting_frame.GetOrigin(), embedder.GetOrigin(), content_setting);

  // Now notify all pending requests that the permission has been set.
  for (PendingInfoBarRequests::iterator i = pending_infobar_requests_.begin();
       i != pending_infobar_requests_.end();) {
    if (i->IsForPair(requesting_frame, embedder)) {
      // There was a pending request for the same [frame, embedder].
      if (i->Equals(render_process_id, render_view_id, bridge_id)) {
        // The request that set permission will be removed by TabContents
        // itself, that is, we should not try to cancel the infobar that has
        // just notified us.
        i->infobar_delegate = NULL;
      }
      // Cancel it first, and then notify the permission.
      // Note: if the pending request had an infobar, TabContents will
      // eventually close it and we will pump the queue via OnInfoBarClosed().
      PendingInfoBarRequest other_request = *i;
      i = CancelInfoBarRequestInternal(i);
      geolocation_permission_context_->NotifyPermissionSet(
          other_request.render_process_id, other_request.render_view_id,
          other_request.bridge_id, other_request.requesting_frame, allowed);
    } else {
      ++i;
    }
  }
}

void GeolocationInfoBarQueueController::ShowQueuedInfoBar(
    int render_process_id, int render_view_id) {
  TabContents* tab_contents =
      tab_util::GetTabContentsByID(render_process_id, render_view_id);
  for (PendingInfoBarRequests::iterator i = pending_infobar_requests_.begin();
       i != pending_infobar_requests_.end();) {
    if (!i->IsForTab(render_process_id, render_view_id)) {
      ++i;
      continue;
    }
    if (!tab_contents) {
      i = pending_infobar_requests_.erase(i);
      continue;
    }
    // Check if already displayed.
    if (i->infobar_delegate)
      break;
    i->infobar_delegate = new GeolocationConfirmInfoBarDelegate(
        tab_contents, this,
        render_process_id, render_view_id,
        i->bridge_id, i->requesting_frame,
        profile_->GetPrefs()->GetString(prefs::kAcceptLanguages));
    tab_contents->AddInfoBar(i->infobar_delegate);
    break;
  }
}

std::vector<GeolocationInfoBarQueueController::PendingInfoBarRequest>::iterator
    GeolocationInfoBarQueueController::CancelInfoBarRequestInternal(
        std::vector<PendingInfoBarRequest>::iterator i) {
  TabContents* tab_contents =
      tab_util::GetTabContentsByID(i->render_process_id, i->render_view_id);
  if (tab_contents && i->infobar_delegate) {
    // TabContents will destroy the InfoBar, which will remove from our vector
    // asynchronously.
    tab_contents->RemoveInfoBar(i->infobar_delegate);
    return ++i;
  } else {
    // Remove it directly from the pending vector.
    return pending_infobar_requests_.erase(i);
  }
}

GeolocationPermissionContext::GeolocationPermissionContext(
    Profile* profile)
    : profile_(profile),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          geolocation_infobar_queue_controller_(
             new GeolocationInfoBarQueueController(this, profile))) {
}

GeolocationPermissionContext::~GeolocationPermissionContext() {
}

void GeolocationPermissionContext::RequestGeolocationPermission(
    int render_process_id, int render_view_id, int bridge_id,
    const GURL& requesting_frame) {
  if (!ChromeThread::CurrentlyOn(ChromeThread::UI)) {
    ChromeThread::PostTask(
        ChromeThread::UI, FROM_HERE,
        NewRunnableMethod(this,
            &GeolocationPermissionContext::RequestGeolocationPermission,
            render_process_id, render_view_id, bridge_id, requesting_frame));
    return;
  }
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));

  ExtensionsService* extensions = profile_->GetExtensionsService();
  if (extensions) {
    Extension* ext = extensions->GetExtensionByURL(requesting_frame);
    if (!ext)
      ext = extensions->GetExtensionByWebExtent(requesting_frame);
    if (ext && ext->HasApiPermission(Extension::kGeolocationPermission)) {
      ExtensionProcessManager* epm = profile_->GetExtensionProcessManager();
      RenderProcessHost* process = epm->GetExtensionProcess(requesting_frame);
      if (process && process->id() == render_process_id) {
        NotifyPermissionSet(render_process_id, render_view_id, bridge_id,
                            requesting_frame, true);
        return;
      }
    }
  }

  TabContents* tab_contents =
      tab_util::GetTabContentsByID(render_process_id, render_view_id);
  if (!tab_contents) {
    // The tab may have gone away, or the request may not be from a tab at all.
    LOG(WARNING) << "Attempt to use geolocation tabless renderer: "
        << render_process_id << "," << render_view_id << "," << bridge_id
        << " (can't prompt user without a visible tab)";
    NotifyPermissionSet(render_process_id, render_view_id, bridge_id,
                        requesting_frame, false);
    return;
  }

  GURL embedder = tab_contents->GetURL();
  if (!requesting_frame.is_valid() || !embedder.is_valid()) {
    LOG(WARNING) << "Attempt to use geolocation from an invalid URL: "
        << requesting_frame << "," << embedder
        << " (geolocation is not supported in popups)";
    NotifyPermissionSet(render_process_id, render_view_id, bridge_id,
                        requesting_frame, false);
    return;
  }

  ContentSetting content_setting =
      profile_->GetGeolocationContentSettingsMap()->GetContentSetting(
          requesting_frame, embedder);
  if (content_setting == CONTENT_SETTING_BLOCK) {
    NotifyPermissionSet(render_process_id, render_view_id, bridge_id,
                        requesting_frame, false);
  } else if (content_setting == CONTENT_SETTING_ALLOW) {
    NotifyPermissionSet(render_process_id, render_view_id, bridge_id,
                        requesting_frame, true);
  } else { // setting == ask. Prompt the user.
    geolocation_infobar_queue_controller_->CreateInfoBarRequest(
        render_process_id, render_view_id, bridge_id, requesting_frame,
        embedder);
  }
}

void GeolocationPermissionContext::CancelGeolocationPermissionRequest(
    int render_process_id, int render_view_id, int bridge_id,
    const GURL& requesting_frame) {
  CancelPendingInfoBarRequest(render_process_id, render_view_id, bridge_id);
}

GeolocationArbitrator* GeolocationPermissionContext::StartUpdatingRequested(
    int render_process_id, int render_view_id, int bridge_id,
    const GURL& requesting_frame) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  // Note we cannot store the arbitrator as a member as it is not thread safe.
  GeolocationArbitrator* arbitrator = GeolocationArbitrator::GetInstance();

  // WebKit will not request permission until it has received a valid
  // location, but the google network location provider will not give a
  // valid location until the user has granted permission. So we cut the Gordian
  // Knot by reusing the the 'start updating' request to also trigger
  // a 'permission request' should the provider still be awaiting permission.
  if (!arbitrator->HasPermissionBeenGranted()) {
    RequestGeolocationPermission(render_process_id, render_view_id, bridge_id,
                                 requesting_frame);
  }
  return arbitrator;
}

void GeolocationPermissionContext::StopUpdatingRequested(
    int render_process_id, int render_view_id, int bridge_id) {
  CancelPendingInfoBarRequest(render_process_id, render_view_id, bridge_id);
}

void GeolocationPermissionContext::NotifyPermissionSet(
    int render_process_id, int render_view_id, int bridge_id,
    const GURL& requesting_frame, bool allowed) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));

  TabContents* tab_contents =
      tab_util::GetTabContentsByID(render_process_id, render_view_id);

  // TabContents may have gone away (or not exists for extension).
  if (tab_contents) {
    TabSpecificContentSettings* content_settings =
        tab_contents->GetTabSpecificContentSettings();
    content_settings->OnGeolocationPermissionSet(requesting_frame.GetOrigin(),
                                                 allowed);
  }

  CallRenderViewHost(
      render_process_id, render_view_id,
      &RenderViewHost::Send,
      new ViewMsg_Geolocation_PermissionSet(render_view_id, bridge_id,
          allowed));
  if (allowed) {
    ChromeThread::PostTask(
        ChromeThread::IO, FROM_HERE,
        NewRunnableMethod(this,
            &GeolocationPermissionContext::NotifyArbitratorPermissionGranted,
            requesting_frame));
  }
}

void GeolocationPermissionContext::NotifyArbitratorPermissionGranted(
    const GURL& requesting_frame) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  GeolocationArbitrator::GetInstance()->OnPermissionGranted(requesting_frame);
}

void GeolocationPermissionContext::CancelPendingInfoBarRequest(
    int render_process_id, int render_view_id, int bridge_id) {
  if (!ChromeThread::CurrentlyOn(ChromeThread::UI)) {
    ChromeThread::PostTask(
        ChromeThread::UI, FROM_HERE,
        NewRunnableMethod(this,
            &GeolocationPermissionContext::CancelPendingInfoBarRequest,
            render_process_id, render_view_id, bridge_id));
     return;
  }
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  geolocation_infobar_queue_controller_->CancelInfoBarRequest(
      render_process_id, render_view_id, bridge_id);
}
