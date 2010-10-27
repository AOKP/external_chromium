// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_API_H__
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_API_H__
#pragma once

#include "base/singleton.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"

class ExtensionsService;

class ExtensionManagementFunction : public SyncExtensionFunction {
 protected:
  ExtensionsService* service();
};

class GetAllExtensionsFunction : public ExtensionManagementFunction {
  ~GetAllExtensionsFunction() {}
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.management.getAll");
};

class LaunchAppFunction : public ExtensionManagementFunction {
  ~LaunchAppFunction() {}
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.management.launchApp");
};

class SetEnabledFunction : public ExtensionManagementFunction {
  ~SetEnabledFunction() {}
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.management.setEnabled");
};

class UninstallFunction : public ExtensionManagementFunction {
  ~UninstallFunction() {}
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME("experimental.management.uninstall");
};

class ExtensionManagementEventRouter : public NotificationObserver {
 public:
  // Get the singleton instance of the event router.
  static ExtensionManagementEventRouter* GetInstance();

  // Performs one-time initialization of our singleton.
  void Init();

 private:
  friend struct DefaultSingletonTraits<ExtensionManagementEventRouter>;

  ExtensionManagementEventRouter();
  virtual ~ExtensionManagementEventRouter();

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionManagementEventRouter);
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_API_H__
