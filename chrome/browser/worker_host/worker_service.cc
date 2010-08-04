// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/worker_host/worker_service.h"

#include "base/command_line.h"
#include "base/singleton.h"
#include "base/sys_info.h"
#include "base/thread.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/host_content_settings_map.h"
#include "chrome/browser/plugin_service.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/renderer_host/resource_message_filter.h"
#include "chrome/browser/worker_host/worker_process_host.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/worker_messages.h"
#include "net/base/registry_controlled_domain.h"

const int WorkerService::kMaxWorkerProcessesWhenSharing = 10;
const int WorkerService::kMaxWorkersWhenSeparate = 64;
const int WorkerService::kMaxWorkersPerTabWhenSeparate = 16;

WorkerService* WorkerService::GetInstance() {
  return Singleton<WorkerService>::get();
}

WorkerService::WorkerService()
    : next_worker_route_id_(0),
      resource_dispatcher_host_(NULL) {
  // Receive a notification if a message filter or WorkerProcessHost is deleted.
  registrar_.Add(this, NotificationType::RESOURCE_MESSAGE_FILTER_SHUTDOWN,
                 NotificationService::AllSources());

  registrar_.Add(this, NotificationType::WORKER_PROCESS_HOST_SHUTDOWN,
                 NotificationService::AllSources());
}

void WorkerService::Initialize(ResourceDispatcherHost* rdh) {
  resource_dispatcher_host_ = rdh;
}

WorkerService::~WorkerService() {
}

bool WorkerService::CreateDedicatedWorker(
    const GURL& url,
    bool is_off_the_record,
    unsigned long long document_id,
    int renderer_pid,
    int render_view_route_id,
    IPC::Message::Sender* sender,
    int sender_route_id,
    int parent_process_id,
    int parent_appcache_host_id,
    ChromeURLRequestContext* request_context) {
  return CreateWorker(url, false, is_off_the_record, string16(),
                      document_id, renderer_pid, render_view_route_id,
                      sender, sender_route_id,
                      parent_process_id, parent_appcache_host_id, 0,
                      request_context);
}

bool WorkerService::CreateSharedWorker(
    const GURL& url,
    bool is_off_the_record,
    const string16& name,
    unsigned long long document_id,
    int renderer_pid,
    int render_view_route_id,
    IPC::Message::Sender* sender,
    int sender_route_id,
    int64 main_resource_appcache_id,
    ChromeURLRequestContext* request_context) {
  return CreateWorker(url, true, is_off_the_record, name,
                      document_id, renderer_pid, render_view_route_id,
                      sender, sender_route_id,
                      0, 0, main_resource_appcache_id,
                      request_context);
}

bool WorkerService::CreateWorker(
    const GURL& url,
    bool is_shared,
    bool off_the_record,
    const string16& name,
    unsigned long long document_id,
    int renderer_id,
    int render_view_route_id,
    IPC::Message::Sender* sender,
    int sender_route_id,
    int parent_process_id,
    int parent_appcache_host_id,
    int64 main_resource_appcache_id,
    ChromeURLRequestContext* request_context) {
  // Generate a unique route id for the browser-worker communication that's
  // unique among all worker processes.  That way when the worker process sends
  // a wrapped IPC message through us, we know which WorkerProcessHost to give
  // it to.
  WorkerProcessHost::WorkerInstance instance(url,
                                             is_shared,
                                             off_the_record,
                                             name,
                                             next_worker_route_id(),
                                             parent_process_id,
                                             parent_appcache_host_id,
                                             main_resource_appcache_id,
                                             request_context);
  instance.AddSender(sender, sender_route_id);
  instance.worker_document_set()->Add(
      sender, document_id, renderer_id, render_view_route_id);

  return CreateWorkerFromInstance(instance);
}

bool WorkerService::CreateWorkerFromInstance(
    WorkerProcessHost::WorkerInstance instance) {

  // TODO(michaeln): We need to ensure that a process is working
  // on behalf of a single profile. The process sharing logic below
  // does not ensure that. Consider making WorkerService a per profile
  // object to help with this.
  WorkerProcessHost* worker = NULL;
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kWebWorkerProcessPerCore)) {
    worker = GetProcessToFillUpCores();
  } else if (CommandLine::ForCurrentProcess()->HasSwitch(
                 switches::kWebWorkerShareProcesses)) {
    worker = GetProcessForDomain(instance.url());
  } else {  // One process per worker.
    if (!CanCreateWorkerProcess(instance)) {
      queued_workers_.push_back(instance);
      return true;
    }
  }

  // Check to see if this shared worker is already running (two pages may have
  // tried to start up the worker simultaneously).
  if (instance.shared()) {
    // See if a worker with this name already exists.
    WorkerProcessHost::WorkerInstance* existing_instance =
        FindSharedWorkerInstance(
            instance.url(), instance.name(), instance.off_the_record());
    WorkerProcessHost::WorkerInstance::SenderInfo sender_info =
        instance.GetSender();
    // If this worker is already running, no need to create a new copy. Just
    // inform the caller that the worker has been created.
    if (existing_instance) {
      // Walk the worker's sender list to see if this client is listed. If not,
      // then it means that the worker started by the client already exited so
      // we should not attach to this new one (http://crbug.com/29243).
      if (!existing_instance->HasSender(sender_info.first, sender_info.second))
        return false;
      sender_info.first->Send(new ViewMsg_WorkerCreated(sender_info.second));
      return true;
    }

    // Look to see if there's a pending instance.
    WorkerProcessHost::WorkerInstance* pending = FindPendingInstance(
        instance.url(), instance.name(), instance.off_the_record());
    // If there's no instance *and* no pending instance (or there is a pending
    // instance but it does not contain our sender info), then it means the
    // worker started up and exited already. Log a warning because this should
    // be a very rare occurrence and is probably a bug, but it *can* happen so
    // handle it gracefully.
    if (!pending ||
        !pending->HasSender(sender_info.first, sender_info.second)) {
      DLOG(WARNING) << "Pending worker already exited";
      return false;
    }

    // Assign the accumulated document set and sender list for this pending
    // worker to the new instance.
    DCHECK(!pending->worker_document_set()->IsEmpty());
    instance.ShareDocumentSet(*pending);
    for (WorkerProcessHost::WorkerInstance::SenderList::const_iterator i =
             pending->senders().begin();
         i != pending->senders().end(); ++i) {
      instance.AddSender(i->first, i->second);
    }
    RemovePendingInstances(
        instance.url(), instance.name(), instance.off_the_record());

    // Remove any queued instances of this worker and copy over the sender to
    // this instance.
    for (WorkerProcessHost::Instances::iterator iter = queued_workers_.begin();
         iter != queued_workers_.end();) {
      if (iter->Matches(instance.url(), instance.name(),
                        instance.off_the_record())) {
        DCHECK(iter->NumSenders() == 1);
        WorkerProcessHost::WorkerInstance::SenderInfo sender_info =
            iter->GetSender();
        instance.AddSender(sender_info.first, sender_info.second);
        iter = queued_workers_.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  if (!worker) {
    worker = new WorkerProcessHost(resource_dispatcher_host_,
                                   instance.request_context());
    if (!worker->Init()) {
      delete worker;
      return false;
    }
  }

  // TODO(michaeln): As written, test can fail per my earlier comment in
  // this method, but that's a bug.
  // DCHECK(worker->request_context() == instance.request_context());

  worker->CreateWorker(instance);
  return true;
}

bool WorkerService::LookupSharedWorker(
    const GURL &url,
    const string16& name,
    bool off_the_record,
    unsigned long long document_id,
    int renderer_id,
    int render_view_route_id,
    IPC::Message::Sender* sender,
    int sender_route_id,
    bool* url_mismatch) {
  bool found_instance = true;
  WorkerProcessHost::WorkerInstance* instance =
      FindSharedWorkerInstance(url, name, off_the_record);

  if (!instance) {
    // If no worker instance currently exists, we need to create a pending
    // instance - this is to make sure that any subsequent lookups passing a
    // mismatched URL get the appropriate url_mismatch error at lookup time.
    // Having named shared workers was a Really Bad Idea due to details like
    // this.
    instance = CreatePendingInstance(url, name, off_the_record);
    found_instance = false;
  }

  // Make sure the passed-in instance matches the URL - if not, return an
  // error.
  if (url != instance->url()) {
    *url_mismatch = true;
    return false;
  } else {
    *url_mismatch = false;
  }

  // Add our route ID to the existing instance so we can send messages to it.
  instance->AddSender(sender, sender_route_id);

  // Add the passed sender/document_id to the worker instance.
  instance->worker_document_set()->Add(
      sender, document_id, renderer_id, render_view_route_id);
  return found_instance;
}

void WorkerService::DocumentDetached(IPC::Message::Sender* sender,
                                     unsigned long long document_id) {
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    worker->DocumentDetached(sender, document_id);
  }

  // Remove any queued shared workers for this document.
  for (WorkerProcessHost::Instances::iterator iter = queued_workers_.begin();
       iter != queued_workers_.end();) {
    if (iter->shared()) {
      iter->worker_document_set()->Remove(sender, document_id);
      if (iter->worker_document_set()->IsEmpty()) {
        iter = queued_workers_.erase(iter);
        continue;
      }
    }
    ++iter;
  }

  // Remove the document from any pending shared workers.
  for (WorkerProcessHost::Instances::iterator iter =
           pending_shared_workers_.begin();
       iter != pending_shared_workers_.end(); ) {
    iter->worker_document_set()->Remove(sender, document_id);
    if (iter->worker_document_set()->IsEmpty()) {
      iter = pending_shared_workers_.erase(iter);
    } else {
      ++iter;
    }
  }

}

void WorkerService::CancelCreateDedicatedWorker(IPC::Message::Sender* sender,
                                                int sender_route_id) {
  for (WorkerProcessHost::Instances::iterator i = queued_workers_.begin();
       i != queued_workers_.end(); ++i) {
    if (i->HasSender(sender, sender_route_id)) {
      DCHECK(!i->shared());
      queued_workers_.erase(i);
      return;
    }
  }

  // There could be a race condition where the WebWorkerProxy told us to cancel
  // the worker right as we sent it a message say it's been created.  Look at
  // the running workers.
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    for (WorkerProcessHost::Instances::const_iterator instance =
             worker->instances().begin();
         instance != worker->instances().end(); ++instance) {
      if (instance->HasSender(sender, sender_route_id)) {
        // Fake a worker destroyed message so that WorkerProcessHost cleans up
        // properly.
        WorkerHostMsg_WorkerContextDestroyed msg(sender_route_id);
        ForwardMessage(msg, sender);
        return;
      }
    }
  }

  DCHECK(false) << "Couldn't find worker to cancel";
}

void WorkerService::ForwardMessage(const IPC::Message& message,
                                   IPC::Message::Sender* sender) {
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    if (worker->FilterMessage(message, sender))
      return;
  }

  // TODO(jabdelmalek): tell sender that callee is gone
}

WorkerProcessHost* WorkerService::GetProcessForDomain(const GURL& url) {
  int num_processes = 0;
  std::string domain =
      net::RegistryControlledDomainService::GetDomainAndRegistry(url);
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    num_processes++;
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    for (WorkerProcessHost::Instances::const_iterator instance =
             worker->instances().begin();
         instance != worker->instances().end(); ++instance) {
      if (net::RegistryControlledDomainService::GetDomainAndRegistry(
              instance->url()) == domain) {
        return worker;
      }
    }
  }

  if (num_processes >= kMaxWorkerProcessesWhenSharing)
    return GetLeastLoadedWorker();

  return NULL;
}

WorkerProcessHost* WorkerService::GetProcessToFillUpCores() {
  int num_processes = 0;
  BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
  for (; !iter.Done(); ++iter)
    num_processes++;

  if (num_processes >= base::SysInfo::NumberOfProcessors())
    return GetLeastLoadedWorker();

  return NULL;
}

WorkerProcessHost* WorkerService::GetLeastLoadedWorker() {
  WorkerProcessHost* smallest = NULL;
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    if (!smallest || worker->instances().size() < smallest->instances().size())
      smallest = worker;
  }

  return smallest;
}

bool WorkerService::CanCreateWorkerProcess(
    const WorkerProcessHost::WorkerInstance& instance) {
  // Worker can be fired off if *any* parent has room.
  const WorkerDocumentSet::DocumentInfoSet& parents =
        instance.worker_document_set()->documents();

  for (WorkerDocumentSet::DocumentInfoSet::const_iterator parent_iter =
           parents.begin();
       parent_iter != parents.end(); ++parent_iter) {
    bool hit_total_worker_limit = false;
    if (TabCanCreateWorkerProcess(parent_iter->renderer_id(),
                                  parent_iter->render_view_route_id(),
                                  &hit_total_worker_limit)) {
      return true;
    }
    // Return false if already at the global worker limit (no need to continue
    // checking parent tabs).
    if (hit_total_worker_limit)
      return false;
  }
  // If we've reached here, none of the parent tabs is allowed to create an
  // instance.
  return false;
}

bool WorkerService::TabCanCreateWorkerProcess(int renderer_id,
                                              int render_view_route_id,
                                              bool* hit_total_worker_limit) {
  int total_workers = 0;
  int workers_per_tab = 0;
  *hit_total_worker_limit = false;
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    for (WorkerProcessHost::Instances::const_iterator cur_instance =
             worker->instances().begin();
         cur_instance != worker->instances().end(); ++cur_instance) {
      total_workers++;
      if (total_workers >= kMaxWorkersWhenSeparate) {
        *hit_total_worker_limit = true;
        return false;
      }
      if (cur_instance->RendererIsParent(renderer_id, render_view_route_id)) {
        workers_per_tab++;
        if (workers_per_tab >= kMaxWorkersPerTabWhenSeparate)
          return false;
      }
    }
  }

  return true;
}

void WorkerService::Observe(NotificationType type,
                            const NotificationSource& source,
                            const NotificationDetails& details) {
  if (type.value == NotificationType::RESOURCE_MESSAGE_FILTER_SHUTDOWN) {
    ResourceMessageFilter* sender = Source<ResourceMessageFilter>(source).ptr();
    SenderShutdown(sender);
  } else if (type.value == NotificationType::WORKER_PROCESS_HOST_SHUTDOWN) {
    WorkerProcessHost* sender = Source<WorkerProcessHost>(source).ptr();
    SenderShutdown(sender);
    WorkerProcessDestroyed(sender);
  } else {
    NOTREACHED();
  }
}

void WorkerService::SenderShutdown(IPC::Message::Sender* sender) {
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    worker->SenderShutdown(sender);
  }

  // See if that render process had any queued workers.
  for (WorkerProcessHost::Instances::iterator i = queued_workers_.begin();
       i != queued_workers_.end();) {
    i->RemoveSenders(sender);
    if (i->NumSenders() == 0) {
      i = queued_workers_.erase(i);
    } else {
      ++i;
    }
  }

  // Also, see if that render process had any pending shared workers.
  for (WorkerProcessHost::Instances::iterator iter =
           pending_shared_workers_.begin();
       iter != pending_shared_workers_.end(); ) {
    iter->worker_document_set()->RemoveAll(sender);
    if (iter->worker_document_set()->IsEmpty()) {
      iter = pending_shared_workers_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void WorkerService::WorkerProcessDestroyed(WorkerProcessHost* process) {
  if (queued_workers_.empty())
    return;

  for (WorkerProcessHost::Instances::iterator i = queued_workers_.begin();
       i != queued_workers_.end();) {
    if (CanCreateWorkerProcess(*i)) {
      WorkerProcessHost::WorkerInstance instance = *i;
      queued_workers_.erase(i);
      CreateWorkerFromInstance(instance);

      // CreateWorkerFromInstance can modify the queued_workers_ list when it
      // coalesces queued instances after starting a shared worker, so we
      // have to rescan the list from the beginning (our iterator is now
      // invalid). This is not a big deal as having any queued workers will be
      // rare in practice so the list will be small.
      i = queued_workers_.begin();
    } else {
      ++i;
    }
  }
}

const WorkerProcessHost::WorkerInstance* WorkerService::FindWorkerInstance(
      int worker_process_id) {
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    if (iter->id() != worker_process_id)
        continue;

    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    WorkerProcessHost::Instances::const_iterator instance =
        worker->instances().begin();
    return instance == worker->instances().end() ? NULL : &*instance;
  }
  return NULL;
}

WorkerProcessHost::WorkerInstance*
WorkerService::FindSharedWorkerInstance(const GURL& url, const string16& name,
                                        bool off_the_record) {
  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::WORKER_PROCESS);
       !iter.Done(); ++iter) {
    WorkerProcessHost* worker = static_cast<WorkerProcessHost*>(*iter);
    for (WorkerProcessHost::Instances::iterator instance_iter =
             worker->mutable_instances().begin();
         instance_iter != worker->mutable_instances().end();
         ++instance_iter) {
      if (instance_iter->Matches(url, name, off_the_record))
        return &(*instance_iter);
    }
  }
  return NULL;
}

WorkerProcessHost::WorkerInstance*
WorkerService::FindPendingInstance(const GURL& url, const string16& name,
                                   bool off_the_record) {
  // Walk the pending instances looking for a matching pending worker.
  for (WorkerProcessHost::Instances::iterator iter =
           pending_shared_workers_.begin();
       iter != pending_shared_workers_.end();
       ++iter) {
    if (iter->Matches(url, name, off_the_record)) {
      return &(*iter);
    }
  }
  return NULL;
}


void WorkerService::RemovePendingInstances(const GURL& url,
                                           const string16& name,
                                           bool off_the_record) {
  // Walk the pending instances looking for a matching pending worker.
  for (WorkerProcessHost::Instances::iterator iter =
           pending_shared_workers_.begin();
       iter != pending_shared_workers_.end(); ) {
    if (iter->Matches(url, name, off_the_record)) {
      iter = pending_shared_workers_.erase(iter);
    } else {
      ++iter;
    }
  }
}

WorkerProcessHost::WorkerInstance*
WorkerService::CreatePendingInstance(const GURL& url,
                                     const string16& name,
                                     bool off_the_record) {
  // Look for an existing pending shared worker.
  WorkerProcessHost::WorkerInstance* instance =
      FindPendingInstance(url, name, off_the_record);
  if (instance)
    return instance;

  // No existing pending worker - create a new one.
  WorkerProcessHost::WorkerInstance pending(
      url, true, off_the_record, name, MSG_ROUTING_NONE, 0, 0, 0, NULL);
  pending_shared_workers_.push_back(pending);
  return &pending_shared_workers_.back();
}
