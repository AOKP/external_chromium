// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_METRICS_HISTOGRAM_SYNCHRONIZER_H_
#define CHROME_BROWSER_METRICS_HISTOGRAM_SYNCHRONIZER_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/condition_variable.h"
#include "base/lock.h"
#include "base/ref_counted.h"
#include "base/time.h"

class MessageLoop;
class Task;

class HistogramSynchronizer : public
    base::RefCountedThreadSafe<HistogramSynchronizer> {
 public:

  enum RendererHistogramRequester {
    ASYNC_HISTOGRAMS,
    SYNCHRONOUS_HISTOGRAMS
  };

  HistogramSynchronizer();

  ~HistogramSynchronizer();

  // Return pointer to the singleton instance, which is allocated and
  // deallocated on the main UI thread (during system startup and teardown).
  static HistogramSynchronizer* CurrentSynchronizer();

  // Contact all renderers, and get them to upload to the browser any/all
  // changes to histograms.  Return when all changes have been acquired, or when
  // the wait time expires (whichever is sooner). This method is called on the
  // main UI thread from about:histograms.
  void FetchRendererHistogramsSynchronously(base::TimeDelta wait_time);

  // Contact all renderers, and get them to upload to the browser any/all
  // changes to histograms.  When all changes have been acquired, or when the
  // wait time expires (whichever is sooner), post the callback_task to the UI
  // thread. Note the callback_task is posted exactly once. This method is
  // called on the IO thread from UMA via PostMessage.
  static void FetchRendererHistogramsAsynchronously(
      MessageLoop* callback_thread, Task* callback_task, int wait_time);

  // This method is called on the IO thread. Desrializes the histograms and
  // records that we have received histograms from a renderer process.
  static void DeserializeHistogramList(
      int sequence_number, const std::vector<std::string>& histograms);

 private:
  // Records that we are waiting for one less histogram from a renderer for the
  // given sequence number. If we have received a response from all histograms,
  // either signal the waiting process or call the callback function. Returns
  // true when we receive histograms from the last of N renderers that were
  // contacted for an update.
  bool DecrementPendingRenderers(int sequence_number);

  void SetCallbackTaskToCallAfterGettingHistograms(
      MessageLoop* callback_thread, Task* callback_task);

  void ForceHistogramSynchronizationDoneCallback(int sequence_number);

  // Calls the callback task, if there is a callback_task.
  void CallCallbackTaskAndResetData();

  // Gets a new sequence number to be sent to renderers from browser process.
  // This will also reset the count of pending renderers for the given type to
  // 1.  After all calls to renderers have been made, a call to
  // DecrementPendingRenderers() must be mode to make it possible for the
  // counter to go to zero (after all renderers have responded).
  int GetNextAvailableSequenceNumber(RendererHistogramRequester requster);

  // Increments the count of the renderers we're waiting for for the request
  // of the given type.
  void IncrementPendingRenderers(RendererHistogramRequester requester);

  // This lock_ protects access to next_sequence_number_,
  // synchronous_renderers_pending_, and synchronous_sequence_number_.
  Lock lock_;

  // This condition variable is used to block caller of the synchronous request
  // to update histograms, and to signal that thread when updates are completed.
  ConditionVariable received_all_renderer_histograms_;

  // When a request is made to asynchronously update the histograms, we store
  // the task and thread we use to post a completion notification in
  // callback_task_ and callback_thread_.
  Task* callback_task_;
  MessageLoop* callback_thread_;

  // We don't track the actual renderers that are contacted for an update, only
  // the count of the number of renderers, and we can sometimes time-out and
  // give up on a "slow to respond" renderer.  We use a sequence_number to be
  // sure a response from a renderer is associated with the current round of
  // requests (and not merely a VERY belated prior response).
  // All sequence numbers used are non-negative.
  // next_available_sequence_number_ is the next available number (used to
  // avoid reuse for a long time).  Access is protected by lock_.
  int next_available_sequence_number_;

  // The sequence number used by the most recent asynchronous update request to
  // contact all renderers.  Access is only permitted on the IO thread.
  int async_sequence_number_;

  // The number of renderers that have not yet responded to requests (as part of
  // an asynchronous update).  Access is only permitted on the IO thread.
  int async_renderers_pending_;

  // The time when we were told to start the fetch histograms asynchronously
  // from renderers.  Access is only permitted on the IO thread.
  base::TimeTicks async_callback_start_time_;

  // The sequence number used by the most recent synchronous update request to
  // contact all renderers.  Protected by lock_.
  int synchronous_sequence_number_;

  // The number of renderers that have not yet responded to requests (as part of
  // a synchronous update).  Protected by lock_.
  int synchronous_renderers_pending_;

  // This singleton instance should be started during the single threaded
  // portion of main(). It initializes globals to provide support for all future
  // calls. This object is created on the UI thread, and it is destroyed after
  // all the other threads have gone away. As a result, it is ok to call it
  // from the UI thread (for UMA uploads), or for about:histograms.
  static HistogramSynchronizer* histogram_synchronizer_;

  DISALLOW_COPY_AND_ASSIGN(HistogramSynchronizer);
};

#endif  // CHROME_BROWSER_METRICS_HISTOGRAM_SYNCHRONIZER_H_
