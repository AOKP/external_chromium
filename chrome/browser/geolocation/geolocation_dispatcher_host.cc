// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/geolocation/geolocation_dispatcher_host.h"

#include <map>
#include <set>
#include <utility>

#include "chrome/common/geoposition.h"
#include "chrome/browser/geolocation/geolocation_permission_context.h"
#include "chrome/browser/geolocation/geolocation_provider.h"
#include "chrome/browser/renderer_host/render_message_filter.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/render_view_host_notification_task.h"
#include "chrome/common/render_messages.h"
#include "ipc/ipc_message.h"

namespace {
class GeolocationDispatcherHostImpl : public GeolocationDispatcherHost,
                                      public GeolocationObserver {
 public:
  GeolocationDispatcherHostImpl(
      int render_process_id,
      GeolocationPermissionContext* geolocation_permission_context);

  // GeolocationDispatcherHost
  virtual bool OnMessageReceived(const IPC::Message& msg, bool* msg_was_ok);

  // GeolocationObserver
  virtual void OnLocationUpdate(const Geoposition& position);

 private:
  virtual ~GeolocationDispatcherHostImpl();

  void OnRegisterDispatcher(int render_view_id);
  void OnUnregisterDispatcher(int render_view_id);
  void OnRequestPermission(
      int render_view_id, int bridge_id, const GURL& requesting_frame);
  void OnCancelPermissionRequest(
      int render_view_id, int bridge_id, const GURL& requesting_frame);
  void OnStartUpdating(
      int render_view_id, int bridge_id, const GURL& requesting_frame,
      bool enable_high_accuracy);
  void OnStopUpdating(int render_view_id, int bridge_id);
  void OnSuspend(int render_view_id, int bridge_id);
  void OnResume(int render_view_id, int bridge_id);

  // Updates the |location_arbitrator_| with the currently required update
  // options, based on |bridge_update_options_|.
  void RefreshGeolocationObserverOptions();

  int render_process_id_;
  scoped_refptr<GeolocationPermissionContext> geolocation_permission_context_;

  // Iterated when sending location updates to renderer processes. The fan out
  // to individual bridge IDs happens renderer side, in order to minimize
  // context switches.
  // Only used on the IO thread.
  std::set<int> geolocation_renderer_ids_;
  // Maps <renderer_id, bridge_id> to the location arbitrator update options
  // that correspond to this particular bridge.
  std::map<std::pair<int, int>, GeolocationObserverOptions>
      bridge_update_options_;
  // Only set whilst we are registered with the arbitrator.
  GeolocationProvider* location_provider_;

  DISALLOW_COPY_AND_ASSIGN(GeolocationDispatcherHostImpl);
};

GeolocationDispatcherHostImpl::GeolocationDispatcherHostImpl(
    int render_process_id,
    GeolocationPermissionContext* geolocation_permission_context)
    : render_process_id_(render_process_id),
      geolocation_permission_context_(geolocation_permission_context),
      location_provider_(NULL) {
  // This is initialized by ResourceMessageFilter. Do not add any non-trivial
  // initialization here, defer to OnRegisterBridge which is triggered whenever
  // a javascript geolocation object is actually initialized.
}

GeolocationDispatcherHostImpl::~GeolocationDispatcherHostImpl() {
  if (location_provider_)
    location_provider_->RemoveObserver(this);
}

bool GeolocationDispatcherHostImpl::OnMessageReceived(
    const IPC::Message& msg, bool* msg_was_ok) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  *msg_was_ok = true;
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(GeolocationDispatcherHostImpl, msg, *msg_was_ok)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_RegisterDispatcher,
                        OnRegisterDispatcher)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_UnregisterDispatcher,
                        OnUnregisterDispatcher)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_CancelPermissionRequest,
                        OnCancelPermissionRequest)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_RequestPermission,
                        OnRequestPermission)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_StartUpdating,
                        OnStartUpdating)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_StopUpdating,
                        OnStopUpdating)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_Suspend,
                        OnSuspend)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Geolocation_Resume,
                        OnResume)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void GeolocationDispatcherHostImpl::OnLocationUpdate(
    const Geoposition& geoposition) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  for (std::set<int>::iterator it = geolocation_renderer_ids_.begin();
       it != geolocation_renderer_ids_.end(); ++it) {
    IPC::Message* message =
        new ViewMsg_Geolocation_PositionUpdated(*it, geoposition);
    CallRenderViewHost(render_process_id_, *it,
                       &RenderViewHost::Send, message);
  }
}

void GeolocationDispatcherHostImpl::OnRegisterDispatcher(
    int render_view_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK_EQ(0u, geolocation_renderer_ids_.count(render_view_id));
  geolocation_renderer_ids_.insert(render_view_id);
}

void GeolocationDispatcherHostImpl::OnUnregisterDispatcher(
    int render_view_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK_EQ(1u, geolocation_renderer_ids_.count(render_view_id));
  geolocation_renderer_ids_.erase(render_view_id);
}

void GeolocationDispatcherHostImpl::OnRequestPermission(
    int render_view_id,
    int bridge_id,
    const GURL& requesting_frame) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DVLOG(1) << __FUNCTION__ << " " << render_process_id_ << ":"
           << render_view_id << ":" << bridge_id;
  geolocation_permission_context_->RequestGeolocationPermission(
      render_process_id_, render_view_id, bridge_id,
      requesting_frame);
}

void GeolocationDispatcherHostImpl::OnCancelPermissionRequest(
    int render_view_id,
    int bridge_id,
    const GURL& requesting_frame) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DVLOG(1) << __FUNCTION__ << " " << render_process_id_ << ":"
           << render_view_id << ":" << bridge_id;
  geolocation_permission_context_->CancelGeolocationPermissionRequest(
      render_process_id_, render_view_id, bridge_id,
      requesting_frame);
}

void GeolocationDispatcherHostImpl::OnStartUpdating(
    int render_view_id,
    int bridge_id,
    const GURL& requesting_frame,
    bool enable_high_accuracy) {
#if defined(ENABLE_CLIENT_BASED_GEOLOCATION)
  // StartUpdating() can be invoked as a result of high-accuracy mode
  // being enabled / disabled. No need to register the dispatcher again.
  if (!geolocation_renderer_ids_.count(render_view_id))
    OnRegisterDispatcher(render_view_id);
#endif
  // WebKit sends the startupdating request before checking permissions, to
  // optimize the no-location-available case and reduce latency in the success
  // case (location lookup happens in parallel with the permission request).
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DVLOG(1) << __FUNCTION__ << " " << render_process_id_ << ":"
           << render_view_id << ":" << bridge_id;
  bridge_update_options_[std::make_pair(render_view_id, bridge_id)] =
      GeolocationObserverOptions(enable_high_accuracy);
  geolocation_permission_context_->StartUpdatingRequested(
      render_process_id_, render_view_id, bridge_id,
      requesting_frame);
  RefreshGeolocationObserverOptions();
}

void GeolocationDispatcherHostImpl::OnStopUpdating(int render_view_id,
                                                      int bridge_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DVLOG(1) << __FUNCTION__ << " " << render_process_id_ << ":"
           << render_view_id << ":" << bridge_id;
  if (bridge_update_options_.erase(std::make_pair(render_view_id, bridge_id)))
    RefreshGeolocationObserverOptions();
  geolocation_permission_context_->StopUpdatingRequested(
      render_process_id_, render_view_id, bridge_id);
#if defined(ENABLE_CLIENT_BASED_GEOLOCATION)
  OnUnregisterDispatcher(render_view_id);
#endif
}

void GeolocationDispatcherHostImpl::OnSuspend(int render_view_id,
                                                 int bridge_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DVLOG(1) << __FUNCTION__ << " " << render_process_id_ << ":"
           << render_view_id << ":" << bridge_id;
  // TODO(bulach): connect this with GeolocationArbitrator.
}

void GeolocationDispatcherHostImpl::OnResume(int render_view_id,
                                                int bridge_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DVLOG(1) << __FUNCTION__ << " " << render_process_id_ << ":"
           << render_view_id << ":" << bridge_id;
  // TODO(bulach): connect this with GeolocationArbitrator.
}

void GeolocationDispatcherHostImpl::RefreshGeolocationObserverOptions() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (bridge_update_options_.empty()) {
    if (location_provider_) {
      location_provider_->RemoveObserver(this);
      location_provider_ = NULL;
    }
  } else {
    if (!location_provider_)
      location_provider_ = GeolocationProvider::GetInstance();
    // Re-add to re-establish our options, in case they changed.
    location_provider_->AddObserver(
        this,
        GeolocationObserverOptions::Collapse(bridge_update_options_));
  }
}
}  // namespace

GeolocationDispatcherHost* GeolocationDispatcherHost::New(
    int render_process_id,
    GeolocationPermissionContext* geolocation_permission_context) {
  return new GeolocationDispatcherHostImpl(
      render_process_id,
      geolocation_permission_context);
}
