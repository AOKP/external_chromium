// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_pool_base.h"

#include "base/compiler_specific.h"
#include "base/format_macros.h"
#include "base/message_loop.h"
#include "base/stats_counters.h"
#include "base/stl_util-inl.h"
#include "base/string_util.h"
#include "base/time.h"
#include "net/base/net_log.h"
#include "net/base/net_errors.h"
#include "net/socket/client_socket_handle.h"

using base::TimeDelta;

namespace {

// The timeout value, in seconds, used to clean up idle sockets that can't be
// reused.
//
// Note: It's important to close idle sockets that have received data as soon
// as possible because the received data may cause BSOD on Windows XP under
// some conditions.  See http://crbug.com/4606.
const int kCleanupInterval = 10;  // DO NOT INCREASE THIS TIMEOUT.

}  // namespace

namespace net {

ConnectJob::ConnectJob(const std::string& group_name,
                       base::TimeDelta timeout_duration,
                       Delegate* delegate,
                       const BoundNetLog& net_log)
    : group_name_(group_name),
      timeout_duration_(timeout_duration),
      delegate_(delegate),
      net_log_(net_log),
      idle_(true) {
  DCHECK(!group_name.empty());
  DCHECK(delegate);
  net_log.BeginEvent(NetLog::TYPE_SOCKET_POOL_CONNECT_JOB, NULL);
}

ConnectJob::~ConnectJob() {
  net_log().EndEvent(NetLog::TYPE_SOCKET_POOL_CONNECT_JOB, NULL);
}

int ConnectJob::Connect() {
  if (timeout_duration_ != base::TimeDelta())
    timer_.Start(timeout_duration_, this, &ConnectJob::OnTimeout);

  idle_ = false;

  LogConnectStart();

  int rv = ConnectInternal();

  if (rv != ERR_IO_PENDING) {
    LogConnectCompletion(rv);
    delegate_ = NULL;
  }

  return rv;
}

void ConnectJob::set_socket(ClientSocket* socket) {
  if (socket) {
    net_log().AddEvent(NetLog::TYPE_CONNECT_JOB_SET_SOCKET,
                       new NetLogSourceParameter("source_dependency",
                                                 socket->NetLog().source()));
  }
  socket_.reset(socket);
}

void ConnectJob::NotifyDelegateOfCompletion(int rv) {
  // The delegate will delete |this|.
  Delegate *delegate = delegate_;
  delegate_ = NULL;

  LogConnectCompletion(rv);
  delegate->OnConnectJobComplete(rv, this);
}

void ConnectJob::ResetTimer(base::TimeDelta remaining_time) {
  timer_.Stop();
  timer_.Start(remaining_time, this, &ConnectJob::OnTimeout);
}

void ConnectJob::LogConnectStart() {
  net_log().BeginEvent(NetLog::TYPE_SOCKET_POOL_CONNECT_JOB_CONNECT,
                       new NetLogStringParameter("group_name", group_name_));
}

void ConnectJob::LogConnectCompletion(int net_error) {
  scoped_refptr<NetLog::EventParameters> params;
  if (net_error != OK)
    params = new NetLogIntegerParameter("net_error", net_error);
  net_log().EndEvent(NetLog::TYPE_SOCKET_POOL_CONNECT_JOB_CONNECT, params);
}

void ConnectJob::OnTimeout() {
  // Make sure the socket is NULL before calling into |delegate|.
  set_socket(NULL);

  net_log_.AddEvent(NetLog::TYPE_SOCKET_POOL_CONNECT_JOB_TIMED_OUT, NULL);

  NotifyDelegateOfCompletion(ERR_TIMED_OUT);
}

namespace internal {

ClientSocketPoolBaseHelper::Request::Request(
    ClientSocketHandle* handle,
    CompletionCallback* callback,
    RequestPriority priority,
    const BoundNetLog& net_log)
    : handle_(handle), callback_(callback), priority_(priority),
      net_log_(net_log) {}

ClientSocketPoolBaseHelper::Request::~Request() {}

ClientSocketPoolBaseHelper::ClientSocketPoolBaseHelper(
    int max_sockets,
    int max_sockets_per_group,
    base::TimeDelta unused_idle_socket_timeout,
    base::TimeDelta used_idle_socket_timeout,
    ConnectJobFactory* connect_job_factory)
    : idle_socket_count_(0),
      connecting_socket_count_(0),
      handed_out_socket_count_(0),
      max_sockets_(max_sockets),
      max_sockets_per_group_(max_sockets_per_group),
      unused_idle_socket_timeout_(unused_idle_socket_timeout),
      used_idle_socket_timeout_(used_idle_socket_timeout),
      connect_job_factory_(connect_job_factory),
      backup_jobs_enabled_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)),
      pool_generation_number_(0) {
  DCHECK_LE(0, max_sockets_per_group);
  DCHECK_LE(max_sockets_per_group, max_sockets);

  NetworkChangeNotifier::AddObserver(this);
}

ClientSocketPoolBaseHelper::~ClientSocketPoolBaseHelper() {
  CancelAllConnectJobs();

  // Clean up any idle sockets.  Assert that we have no remaining active
  // sockets or pending requests.  They should have all been cleaned up prior
  // to the manager being destroyed.
  CloseIdleSockets();
  CHECK(group_map_.empty());
  DCHECK(pending_callback_map_.empty());
  DCHECK_EQ(0, connecting_socket_count_);

  NetworkChangeNotifier::RemoveObserver(this);
}

// InsertRequestIntoQueue inserts the request into the queue based on
// priority.  Highest priorities are closest to the front.  Older requests are
// prioritized over requests of equal priority.
//
// static
void ClientSocketPoolBaseHelper::InsertRequestIntoQueue(
    const Request* r, RequestQueue* pending_requests) {
  RequestQueue::iterator it = pending_requests->begin();
  while (it != pending_requests->end() && r->priority() >= (*it)->priority())
    ++it;
  pending_requests->insert(it, r);
}

// static
const ClientSocketPoolBaseHelper::Request*
ClientSocketPoolBaseHelper::RemoveRequestFromQueue(
    RequestQueue::iterator it, RequestQueue* pending_requests) {
  const Request* req = *it;
  pending_requests->erase(it);
  return req;
}

int ClientSocketPoolBaseHelper::RequestSocket(
    const std::string& group_name,
    const Request* request) {
  request->net_log().BeginEvent(NetLog::TYPE_SOCKET_POOL, NULL);
  Group& group = group_map_[group_name];

  int rv = RequestSocketInternal(group_name, request);
  if (rv != ERR_IO_PENDING) {
    request->net_log().EndEvent(NetLog::TYPE_SOCKET_POOL, NULL);
    CHECK(!request->handle()->is_initialized());
    delete request;
  } else {
    InsertRequestIntoQueue(request, &group.pending_requests);
  }
  return rv;
}

int ClientSocketPoolBaseHelper::RequestSocketInternal(
    const std::string& group_name,
    const Request* request) {
  DCHECK_GE(request->priority(), 0);
  CompletionCallback* const callback = request->callback();
  CHECK(callback);
  ClientSocketHandle* const handle = request->handle();
  CHECK(handle);
  Group& group = group_map_[group_name];

  // Try to reuse a socket.
  if (AssignIdleSocketToGroup(&group, request))
    return OK;

  // Can we make another active socket now?
  if (!group.HasAvailableSocketSlot(max_sockets_per_group_)) {
    request->net_log().AddEvent(
        NetLog::TYPE_SOCKET_POOL_STALLED_MAX_SOCKETS_PER_GROUP, NULL);
    return ERR_IO_PENDING;
  }

  if (ReachedMaxSocketsLimit()) {
    if (idle_socket_count() > 0) {
      CloseOneIdleSocket();
    } else {
      // We could check if we really have a stalled group here, but it requires
      // a scan of all groups, so just flip a flag here, and do the check later.
      request->net_log().AddEvent(
          NetLog::TYPE_SOCKET_POOL_STALLED_MAX_SOCKETS, NULL);
      return ERR_IO_PENDING;
    }
  }

  // We couldn't find a socket to reuse, so allocate and connect a new one.
  scoped_ptr<ConnectJob> connect_job(
      connect_job_factory_->NewConnectJob(group_name, *request, this));

  int rv = connect_job->Connect();
  if (rv == OK) {
    LogBoundConnectJobToRequest(connect_job->net_log().source(), request);
    HandOutSocket(connect_job->ReleaseSocket(), false /* not reused */,
                  handle, base::TimeDelta(), &group, request->net_log());
  } else if (rv == ERR_IO_PENDING) {
    // If we don't have any sockets in this group, set a timer for potentially
    // creating a new one.  If the SYN is lost, this backup socket may complete
    // before the slow socket, improving end user latency.
    if (group.IsEmpty() && !group.backup_job && backup_jobs_enabled_) {
      group.backup_job = connect_job_factory_->NewConnectJob(group_name,
                                                             *request,
                                                             this);
      StartBackupSocketTimer(group_name);
    }

    connecting_socket_count_++;

    ConnectJob* job = connect_job.release();
    group.jobs.insert(job);
  } else {
    LogBoundConnectJobToRequest(connect_job->net_log().source(), request);
    connect_job->GetAdditionalErrorState(handle);
    ClientSocket* error_socket = connect_job->ReleaseSocket();
    if (error_socket) {
      HandOutSocket(error_socket, false /* not reused */, handle,
                    base::TimeDelta(), &group, request->net_log());
    } else if (group.IsEmpty()) {
      group_map_.erase(group_name);
    }
  }

  return rv;
}

bool ClientSocketPoolBaseHelper::AssignIdleSocketToGroup(
    Group* group, const Request* request) {
  // Iterate through the list of idle sockets until we find one or exhaust
  // the list.
  while (!group->idle_sockets.empty()) {
    IdleSocket idle_socket = group->idle_sockets.back();
    group->idle_sockets.pop_back();
    DecrementIdleCount();
    if (idle_socket.socket->IsConnectedAndIdle()) {
      // We found one we can reuse!
      base::TimeDelta idle_time =
          base::TimeTicks::Now() - idle_socket.start_time;
      HandOutSocket(
          idle_socket.socket, idle_socket.used, request->handle(), idle_time,
          group, request->net_log());
      return true;
    }
    delete idle_socket.socket;
  }
  return false;
}

// static
void ClientSocketPoolBaseHelper::LogBoundConnectJobToRequest(
    const NetLog::Source& connect_job_source, const Request* request) {
  request->net_log().AddEvent(
      NetLog::TYPE_SOCKET_POOL_BOUND_TO_CONNECT_JOB,
      new NetLogSourceParameter("source_dependency", connect_job_source));
}

void ClientSocketPoolBaseHelper::StartBackupSocketTimer(
    const std::string& group_name) {
  CHECK(ContainsKey(group_map_, group_name));
  Group& group = group_map_[group_name];

  // Only allow one timer pending to create a backup socket.
  if (group.backup_task)
    return;

  group.backup_task = method_factory_.NewRunnableMethod(
      &ClientSocketPoolBaseHelper::OnBackupSocketTimerFired, group_name);
  MessageLoop::current()->PostDelayedTask(FROM_HERE, group.backup_task,
                                          ConnectRetryIntervalMs());
}

void ClientSocketPoolBaseHelper::OnBackupSocketTimerFired(
    const std::string& group_name) {
  CHECK(ContainsKey(group_map_, group_name));

  Group& group = group_map_[group_name];

  CHECK(group.backup_task);
  group.backup_task = NULL;

  CHECK(group.backup_job);

  // If there are no more jobs pending, there is no work to do.
  // If we've done our cleanups correctly, this should not happen.
  if (group.jobs.empty()) {
    NOTREACHED();
    return;
  }

  // If our backup job is waiting on DNS, or if we can't create any sockets
  // right now due to limits, just reset the timer.
  if (ReachedMaxSocketsLimit() ||
      !group.HasAvailableSocketSlot(max_sockets_per_group_) ||
      (*group.jobs.begin())->GetLoadState() == LOAD_STATE_RESOLVING_HOST) {
    StartBackupSocketTimer(group_name);
    return;
  }

  group.backup_job->net_log().AddEvent(NetLog::TYPE_SOCKET_BACKUP_CREATED,
                                       NULL);
  SIMPLE_STATS_COUNTER("socket.backup_created");
  int rv = group.backup_job->Connect();
  connecting_socket_count_++;
  group.jobs.insert(group.backup_job);
  ConnectJob* job = group.backup_job;
  group.backup_job = NULL;
  if (rv != ERR_IO_PENDING)
    OnConnectJobComplete(rv, job);
}

void ClientSocketPoolBaseHelper::CancelRequest(
    const std::string& group_name, ClientSocketHandle* handle) {
  PendingCallbackMap::iterator callback_it = pending_callback_map_.find(handle);
  if (callback_it != pending_callback_map_.end()) {
    int result = callback_it->second.result;
    pending_callback_map_.erase(callback_it);
    ClientSocket* socket = handle->release_socket();
    if (socket) {
      if (result != OK)
        socket->Disconnect();
      ReleaseSocket(handle->group_name(), socket, handle->id());
    }
    return;
  }

  CHECK(ContainsKey(group_map_, group_name));

  Group& group = group_map_[group_name];

  // Search pending_requests for matching handle.
  RequestQueue::iterator it = group.pending_requests.begin();
  for (; it != group.pending_requests.end(); ++it) {
    if ((*it)->handle() == handle) {
      const Request* req = RemoveRequestFromQueue(it, &group.pending_requests);
      req->net_log().AddEvent(NetLog::TYPE_CANCELLED, NULL);
      req->net_log().EndEvent(NetLog::TYPE_SOCKET_POOL, NULL);
      delete req;

      // We let the job run, unless we're at the socket limit.
      if (group.jobs.size() && ReachedMaxSocketsLimit()) {
        RemoveConnectJob(*group.jobs.begin(), &group);
        CheckForStalledSocketGroups();
      }
      break;
    }
  }
}

void ClientSocketPoolBaseHelper::CloseIdleSockets() {
  CleanupIdleSockets(true);
}

int ClientSocketPoolBaseHelper::IdleSocketCountInGroup(
    const std::string& group_name) const {
  GroupMap::const_iterator i = group_map_.find(group_name);
  CHECK(i != group_map_.end());

  return i->second.idle_sockets.size();
}

LoadState ClientSocketPoolBaseHelper::GetLoadState(
    const std::string& group_name,
    const ClientSocketHandle* handle) const {
  if (ContainsKey(pending_callback_map_, handle))
    return LOAD_STATE_CONNECTING;

  if (!ContainsKey(group_map_, group_name)) {
    NOTREACHED() << "ClientSocketPool does not contain group: " << group_name
                 << " for handle: " << handle;
    return LOAD_STATE_IDLE;
  }

  // Can't use operator[] since it is non-const.
  const Group& group = group_map_.find(group_name)->second;

  // Search pending_requests for matching handle.
  RequestQueue::const_iterator it = group.pending_requests.begin();
  for (size_t i = 0; it != group.pending_requests.end(); ++it, ++i) {
    if ((*it)->handle() == handle) {
      if (i < group.jobs.size()) {
        LoadState max_state = LOAD_STATE_IDLE;
        for (ConnectJobSet::const_iterator job_it = group.jobs.begin();
             job_it != group.jobs.end(); ++job_it) {
          max_state = std::max(max_state, (*job_it)->GetLoadState());
        }
        return max_state;
      } else {
        // TODO(wtc): Add a state for being on the wait list.
        // See http://www.crbug.com/5077.
        return LOAD_STATE_IDLE;
      }
    }
  }

  NOTREACHED();
  return LOAD_STATE_IDLE;
}

bool ClientSocketPoolBaseHelper::IdleSocket::ShouldCleanup(
    base::TimeTicks now,
    base::TimeDelta timeout) const {
  bool timed_out = (now - start_time) >= timeout;
  return timed_out ||
      !(used ? socket->IsConnectedAndIdle() : socket->IsConnected());
}

void ClientSocketPoolBaseHelper::CleanupIdleSockets(bool force) {
  if (idle_socket_count_ == 0)
    return;

  // Current time value. Retrieving it once at the function start rather than
  // inside the inner loop, since it shouldn't change by any meaningful amount.
  base::TimeTicks now = base::TimeTicks::Now();

  GroupMap::iterator i = group_map_.begin();
  while (i != group_map_.end()) {
    Group& group = i->second;

    std::deque<IdleSocket>::iterator j = group.idle_sockets.begin();
    while (j != group.idle_sockets.end()) {
      base::TimeDelta timeout =
          j->used ? used_idle_socket_timeout_ : unused_idle_socket_timeout_;
      if (force || j->ShouldCleanup(now, timeout)) {
        delete j->socket;
        j = group.idle_sockets.erase(j);
        DecrementIdleCount();
      } else {
        ++j;
      }
    }

    // Delete group if no longer needed.
    if (group.IsEmpty()) {
      group_map_.erase(i++);
    } else {
      ++i;
    }
  }
}

void ClientSocketPoolBaseHelper::IncrementIdleCount() {
  if (++idle_socket_count_ == 1)
    timer_.Start(TimeDelta::FromSeconds(kCleanupInterval), this,
                 &ClientSocketPoolBaseHelper::OnCleanupTimerFired);
}

void ClientSocketPoolBaseHelper::DecrementIdleCount() {
  if (--idle_socket_count_ == 0)
    timer_.Stop();
}

void ClientSocketPoolBaseHelper::ReleaseSocket(const std::string& group_name,
                                               ClientSocket* socket,
                                               int id) {
  GroupMap::iterator i = group_map_.find(group_name);
  CHECK(i != group_map_.end());

  Group& group = i->second;

  CHECK_GT(handed_out_socket_count_, 0);
  handed_out_socket_count_--;

  CHECK_GT(group.active_socket_count, 0);
  group.active_socket_count--;

  const bool can_reuse = socket->IsConnectedAndIdle() &&
      id == pool_generation_number_;
  if (can_reuse) {
    // Add it to the idle list.
    AddIdleSocket(socket, true /* used socket */, &group);
    OnAvailableSocketSlot(group_name, &group);
  } else {
    delete socket;
  }

  CheckForStalledSocketGroups();
}

void ClientSocketPoolBaseHelper::CheckForStalledSocketGroups() {
  // If we have idle sockets, see if we can give one to the top-stalled group.
  std::string top_group_name;
  Group* top_group = NULL;
  if (!FindTopStalledGroup(&top_group, &top_group_name))
    return;

  if (ReachedMaxSocketsLimit()) {
    if (idle_socket_count() > 0) {
      CloseOneIdleSocket();
    } else {
      // We can't activate more sockets since we're already at our global
      // limit.
      return;
    }
  }

  // Note:  we don't loop on waking stalled groups.  If the stalled group is at
  //        its limit, may be left with other stalled groups that could be
  //        woken.  This isn't optimal, but there is no starvation, so to avoid
  //        the looping we leave it at this.
  OnAvailableSocketSlot(top_group_name, top_group);
}

// Search for the highest priority pending request, amongst the groups that
// are not at the |max_sockets_per_group_| limit. Note: for requests with
// the same priority, the winner is based on group hash ordering (and not
// insertion order).
bool ClientSocketPoolBaseHelper::FindTopStalledGroup(Group** group,
                                                     std::string* group_name) {
  Group* top_group = NULL;
  const std::string* top_group_name = NULL;
  bool has_stalled_group = false;
  for (GroupMap::iterator i = group_map_.begin();
       i != group_map_.end(); ++i) {
    Group& group = i->second;
    const RequestQueue& queue = group.pending_requests;
    if (queue.empty())
      continue;
    if (group.IsStalled(max_sockets_per_group_)) {
      has_stalled_group = true;
      bool has_higher_priority = !top_group ||
          group.TopPendingPriority() < top_group->TopPendingPriority();
      if (has_higher_priority) {
        top_group = &group;
        top_group_name = &i->first;
      }
    }
  }

  if (top_group) {
    *group = top_group;
    *group_name = *top_group_name;
  }
  return has_stalled_group;
}

void ClientSocketPoolBaseHelper::OnConnectJobComplete(
    int result, ConnectJob* job) {
  DCHECK_NE(ERR_IO_PENDING, result);
  const std::string group_name = job->group_name();
  GroupMap::iterator group_it = group_map_.find(group_name);
  CHECK(group_it != group_map_.end());
  Group& group = group_it->second;

  scoped_ptr<ClientSocket> socket(job->ReleaseSocket());

  BoundNetLog job_log = job->net_log();

  if (result == OK) {
    DCHECK(socket.get());
    RemoveConnectJob(job, &group);
    if (!group.pending_requests.empty()) {
      scoped_ptr<const Request> r(RemoveRequestFromQueue(
          group.pending_requests.begin(), &group.pending_requests));
      LogBoundConnectJobToRequest(job_log.source(), r.get());
      HandOutSocket(
          socket.release(), false /* unused socket */, r->handle(),
          base::TimeDelta(), &group, r->net_log());
      r->net_log().EndEvent(NetLog::TYPE_SOCKET_POOL, NULL);
      InvokeUserCallbackLater(r->handle(), r->callback(), result);
    } else {
      AddIdleSocket(socket.release(), false /* unused socket */, &group);
      OnAvailableSocketSlot(group_name, &group);
      CheckForStalledSocketGroups();
    }
  } else {
    // If we got a socket, it must contain error information so pass that
    // up so that the caller can retrieve it.
    bool handed_out_socket = false;
    if (!group.pending_requests.empty()) {
      scoped_ptr<const Request> r(RemoveRequestFromQueue(
          group.pending_requests.begin(), &group.pending_requests));
      LogBoundConnectJobToRequest(job_log.source(), r.get());
      job->GetAdditionalErrorState(r->handle());
      RemoveConnectJob(job, &group);
      if (socket.get()) {
        handed_out_socket = true;
        HandOutSocket(socket.release(), false /* unused socket */, r->handle(),
                      base::TimeDelta(), &group, r->net_log());
      }
      r->net_log().EndEvent(NetLog::TYPE_SOCKET_POOL,
                            new NetLogIntegerParameter("net_error", result));
      InvokeUserCallbackLater(r->handle(), r->callback(), result);
    } else {
      RemoveConnectJob(job, &group);
    }
    if (!handed_out_socket) {
      OnAvailableSocketSlot(group_name, &group);
      CheckForStalledSocketGroups();
    }
  }
}

void ClientSocketPoolBaseHelper::OnIPAddressChanged() {
  Flush();
}

void ClientSocketPoolBaseHelper::Flush() {
  pool_generation_number_++;
  CancelAllConnectJobs();
  CloseIdleSockets();
}

void ClientSocketPoolBaseHelper::RemoveConnectJob(const ConnectJob* job,
                                                  Group* group) {
  CHECK_GT(connecting_socket_count_, 0);
  connecting_socket_count_--;

  DCHECK(group);
  DCHECK(ContainsKey(group->jobs, job));
  group->jobs.erase(job);

  // If we've got no more jobs for this group, then we no longer need a
  // backup job either.
  if (group->jobs.empty())
    group->CleanupBackupJob();

  DCHECK(job);
  delete job;
}

void ClientSocketPoolBaseHelper::OnAvailableSocketSlot(
    const std::string& group_name, Group* group) {
  if (!group->pending_requests.empty())
    ProcessPendingRequest(group_name, group);

  if (group->IsEmpty())
    group_map_.erase(group_name);
}

void ClientSocketPoolBaseHelper::ProcessPendingRequest(
    const std::string& group_name, Group* group) {
  int rv = RequestSocketInternal(group_name,
                                 *group->pending_requests.begin());
  if (rv != ERR_IO_PENDING) {
    scoped_ptr<const Request> request(RemoveRequestFromQueue(
          group->pending_requests.begin(), &group->pending_requests));

    scoped_refptr<NetLog::EventParameters> params;
    if (rv != OK)
      params = new NetLogIntegerParameter("net_error", rv);
    request->net_log().EndEvent(NetLog::TYPE_SOCKET_POOL, params);
    InvokeUserCallbackLater(
        request->handle(), request->callback(), rv);
  }
}

void ClientSocketPoolBaseHelper::HandOutSocket(
    ClientSocket* socket,
    bool reused,
    ClientSocketHandle* handle,
    base::TimeDelta idle_time,
    Group* group,
    const BoundNetLog& net_log) {
  DCHECK(socket);
  handle->set_socket(socket);
  handle->set_is_reused(reused);
  handle->set_idle_time(idle_time);
  handle->set_pool_id(pool_generation_number_);

  if (reused) {
    net_log.AddEvent(
        NetLog::TYPE_SOCKET_POOL_REUSED_AN_EXISTING_SOCKET,
        new NetLogIntegerParameter(
            "idle_ms", static_cast<int>(idle_time.InMilliseconds())));
  }

  net_log.AddEvent(NetLog::TYPE_SOCKET_POOL_BOUND_TO_SOCKET,
                   new NetLogSourceParameter(
                       "source_dependency", socket->NetLog().source()));

  handed_out_socket_count_++;
  group->active_socket_count++;
}

void ClientSocketPoolBaseHelper::AddIdleSocket(
    ClientSocket* socket, bool used, Group* group) {
  DCHECK(socket);
  IdleSocket idle_socket;
  idle_socket.socket = socket;
  idle_socket.start_time = base::TimeTicks::Now();
  idle_socket.used = used;

  group->idle_sockets.push_back(idle_socket);
  IncrementIdleCount();
}

void ClientSocketPoolBaseHelper::CancelAllConnectJobs() {
  for (GroupMap::iterator i = group_map_.begin(); i != group_map_.end();) {
    Group& group = i->second;
    connecting_socket_count_ -= group.jobs.size();
    STLDeleteElements(&group.jobs);

    if (group.backup_task) {
      group.backup_task->Cancel();
      group.backup_task = NULL;
    }

    // Delete group if no longer needed.
    if (group.IsEmpty()) {
      group_map_.erase(i++);
    } else {
      ++i;
    }
  }
}

bool ClientSocketPoolBaseHelper::ReachedMaxSocketsLimit() const {
  // Each connecting socket will eventually connect and be handed out.
  int total = handed_out_socket_count_ + connecting_socket_count_ +
      idle_socket_count();
  DCHECK_LE(total, max_sockets_);
  if (total < max_sockets_)
    return false;
  LOG(WARNING) << "ReachedMaxSocketsLimit: " << total << "/" << max_sockets_;
  return true;
}

void ClientSocketPoolBaseHelper::CloseOneIdleSocket() {
  CHECK_GT(idle_socket_count(), 0);

  for (GroupMap::iterator i = group_map_.begin(); i != group_map_.end(); ++i) {
    Group& group = i->second;

    if (!group.idle_sockets.empty()) {
      std::deque<IdleSocket>::iterator j = group.idle_sockets.begin();
      delete j->socket;
      group.idle_sockets.erase(j);
      DecrementIdleCount();
      if (group.IsEmpty())
        group_map_.erase(i);

      return;
    }
  }

  LOG(DFATAL) << "No idle socket found to close!.";
}

void ClientSocketPoolBaseHelper::InvokeUserCallbackLater(
    ClientSocketHandle* handle, CompletionCallback* callback, int rv) {
  CHECK(!ContainsKey(pending_callback_map_, handle));
  pending_callback_map_[handle] = CallbackResultPair(callback, rv);
  MessageLoop::current()->PostTask(
      FROM_HERE,
      NewRunnableMethod(
          this,
          &ClientSocketPoolBaseHelper::InvokeUserCallback,
          handle));
}

void ClientSocketPoolBaseHelper::InvokeUserCallback(
    ClientSocketHandle* handle) {
  PendingCallbackMap::iterator it = pending_callback_map_.find(handle);

  // Exit if the request has already been cancelled.
  if (it == pending_callback_map_.end())
    return;

  CHECK(!handle->is_initialized());
  CompletionCallback* callback = it->second.callback;
  int result = it->second.result;
  pending_callback_map_.erase(it);
  callback->Run(result);
}

}  // namespace internal

}  // namespace net
