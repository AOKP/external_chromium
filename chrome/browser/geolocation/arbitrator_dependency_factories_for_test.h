// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GEOLOCATION_ARBITRATOR_DEPENDENCY_FACTORIES_FOR_TEST_H_
#define CHROME_BROWSER_GEOLOCATION_ARBITRATOR_DEPENDENCY_FACTORIES_FOR_TEST_H_
#pragma once

#include "chrome/browser/geolocation/arbitrator_dependency_factory.h"

class GeolocationArbitratorDependencyFactoryWithLocationProvider
    : public DefaultGeolocationArbitratorDependencyFactory {
 public:
  typedef LocationProviderBase* (*LocationProviderFactoryFunction)(void);

  GeolocationArbitratorDependencyFactoryWithLocationProvider(
      LocationProviderFactoryFunction factory_function)
      : factory_function_(factory_function) {
  }

  virtual LocationProviderBase* NewNetworkLocationProvider(
      AccessTokenStore* access_token_store,
      URLRequestContextGetter* context,
      const GURL& url,
      const string16& access_token) {
    return factory_function_();
  }

  virtual LocationProviderBase* NewSystemLocationProvider() {
    return NULL;
  }

 protected:
  LocationProviderFactoryFunction factory_function_;
};


#endif  // CHROME_BROWSER_GEOLOCATION_ARBITRATOR_DEPENDENCY_FACTORIES_FOR_TEST_H_
