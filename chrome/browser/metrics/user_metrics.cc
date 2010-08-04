// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/profile.h"
#include "chrome/common/notification_service.h"

void UserMetrics::RecordAction(const UserMetricsAction& action,
                               Profile* profile) {
  Record(action.str_, profile);
}

void UserMetrics::RecordComputedAction(const std::string& action,
                                       Profile* profile) {
  Record(action.c_str(), profile);
}

void UserMetrics::Record(const char *action, Profile *profile) {
  Record(action);
}

void UserMetrics::RecordAction(const UserMetricsAction& action) {
  Record(action.str_);
}

void UserMetrics::RecordComputedAction(const std::string& action) {
  Record(action.c_str());
}

void UserMetrics::Record(const char *action) {
  NotificationService::current()->Notify(NotificationType::USER_ACTION,
                                         NotificationService::AllSources(),
                                         Details<const char*>(&action));
}



