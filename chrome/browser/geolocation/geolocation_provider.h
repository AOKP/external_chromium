// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GEOLOCATION_GEOLOCATION_PROVIDER_H_
#define CHROME_BROWSER_GEOLOCATION_GEOLOCATION_PROVIDER_H_
#pragma once

#include <map>

#include "base/threading/thread.h"
#include "chrome/browser/geolocation/geolocation_observer.h"
#include "chrome/common/geoposition.h"
#include "googleurl/src/gurl.h"

class GeolocationArbitrator;

template<typename Type>
struct DefaultSingletonTraits;

// This is the main API to the geolocaiton subsystem. The application
// will hold a single instance of this class, and can register multiple
// observers which will be notified of location updates. Underlying location
// arbitrator will only be enabled whilst there is at least one observer
// registered.
class GeolocationProvider : public base::Thread, public GeolocationObserver {
 public:
  GeolocationProvider();

  // Must be called from the same thread as the GeolocationProvider was created
  // on. The GeolocationObserverOptions passed are used as a 'hint' for the
  // provider preferences for this particular observer, however the observer
  // could receive callbacks for best available locations from any active
  // provider whilst it is registered.
  // If an existing observer is added a second time it's options are updated
  // but only a single call to RemoveObserver() is required to remove it.
  void AddObserver(GeolocationObserver* delegate,
                   const GeolocationObserverOptions& update_options);
  // Remove a previously registered observer. No-op if not previously registered
  // via AddObserver(). Returns true if the observer was removed.
  bool RemoveObserver(GeolocationObserver* delegate);
  void OnPermissionGranted(const GURL& requesting_frame);
  bool HasPermissionBeenGranted() const;

  // GeolocationObserver
  virtual void OnLocationUpdate(const Geoposition& position);

  // Gets a pointer to the singleton instance of the location relayer, which
  // is in turn bound to the browser's global context objects. Ownership is NOT
  // returned.
  static GeolocationProvider* GetInstance();

  typedef std::map<GeolocationObserver*, GeolocationObserverOptions>
      ObserverMap;

 private:
  friend struct DefaultSingletonTraits<GeolocationProvider>;
  ~GeolocationProvider();

  bool OnClientThread() const;
  bool OnGeolocationThread() const;
  void OnObserversChanged();
  // Passes the observers' geolocation options through to the arbitrator.
  void SetObserverOptions(const GeolocationObserverOptions& options);
  // Update the providers on the geolocation thread, which must be running.
  void InformProvidersPermissionGranted(const GURL& requesting_frame);
  // Notifies observers when a new position fix is available.
  void NotifyObservers(const Geoposition& position);

  // Thread
  virtual void Init();
  virtual void CleanUp();

  scoped_refptr<base::MessageLoopProxy> client_loop_;

  // Only used on client thread
  ObserverMap observers_;
  GURL most_recent_authorized_frame_;
  Geoposition position_;

  // Only to be used on the geolocation thread.
  GeolocationArbitrator* arbitrator_;

  DISALLOW_COPY_AND_ASSIGN(GeolocationProvider);
};

#endif  // CHROME_BROWSER_GEOLOCATION_GEOLOCATION_PROVIDER_H_
