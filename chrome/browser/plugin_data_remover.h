// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PLUGIN_DATA_REMOVER_H_
#define CHROME_BROWSER_PLUGIN_DATA_REMOVER_H_
#pragma once

#include "base/ref_counted.h"
#include "base/time.h"
#include "chrome/browser/plugin_process_host.h"
#include "ipc/ipc_channel.h"

class Task;

namespace base {
class MessageLoopProxy;
}

class PluginDataRemover : public base::RefCountedThreadSafe<PluginDataRemover>,
                          public PluginProcessHost::Client,
                          public IPC::Channel::Listener {
 public:
  PluginDataRemover();

  // Starts removing plug-in data stored since |begin_time|. If |done_task| is
  // not NULL, it is run on the current thread when removing has finished.
  void StartRemoving(base::Time begin_time, Task* done_task);

  // Returns whether there is a plug-in installed that supports removing
  // LSO data. Because this method possibly has to load the plug-in list, it
  // should only be called on the FILE thread.
  static bool IsSupported();

  bool is_removing() const { return is_removing_; }

  // Sets the task to run when removing has finished. Takes ownership of
  // the passed task.
  void set_done_task(Task* task) { done_task_.reset(task); }

  // PluginProcessHost::Client methods
  virtual int ID();
  virtual bool OffTheRecord();
  virtual void SetPluginInfo(const webkit::npapi::WebPluginInfo& info);
  virtual void OnChannelOpened(const IPC::ChannelHandle& handle);
  virtual void OnError();

  // IPC::Channel::Listener methods
  virtual bool OnMessageReceived(const IPC::Message& message);
  virtual void OnChannelError();

 private:
  friend class base::RefCountedThreadSafe<PluginDataRemover>;
  ~PluginDataRemover();

  void SignalDone();
  void ConnectToChannel(const IPC::ChannelHandle& handle);
  void OnClearSiteDataResult(bool success);
  void OnTimeout();

  scoped_refptr<base::MessageLoopProxy> message_loop_;
  bool is_removing_;
  scoped_ptr<Task> done_task_;
  // The point in time when we start removing data.
  base::Time remove_start_time_;
  // The point in time from which on we remove data.
  base::Time begin_time_;
  // We own the channel, but it's used on the IO thread, so it needs to be
  // deleted there as well.
  IPC::Channel* channel_;
};

#endif  // CHROME_BROWSER_PLUGIN_DATA_REMOVER_H_
