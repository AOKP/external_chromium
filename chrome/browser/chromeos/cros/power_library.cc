// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/cros/power_library.h"

#include "base/message_loop.h"
#include "base/string_util.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/chromeos/cros/cros_library.h"

namespace chromeos {

class PowerLibraryImpl : public PowerLibrary {
 public:
  PowerLibraryImpl()
      : power_status_connection_(NULL),
        status_(chromeos::PowerStatus()) {
    if (CrosLibrary::Get()->EnsureLoaded()) {
      Init();
    }
  }

  ~PowerLibraryImpl() {
    if (power_status_connection_) {
      chromeos::DisconnectPowerStatus(power_status_connection_);
    }
  }

  void AddObserver(Observer* observer) {
    observers_.AddObserver(observer);
  }

  void RemoveObserver(Observer* observer) {
    observers_.RemoveObserver(observer);
  }

  bool line_power_on() const {
    return status_.line_power_on;
  }

  bool battery_is_present() const {
    return status_.battery_is_present;
  }

  bool battery_fully_charged() const {
    return status_.battery_state == chromeos::BATTERY_STATE_FULLY_CHARGED;
  }

  double battery_percentage() const {
    return status_.battery_percentage;
  }

  base::TimeDelta battery_time_to_empty() const {
    return base::TimeDelta::FromSeconds(status_.battery_time_to_empty);
  }

  base::TimeDelta battery_time_to_full() const {
    return base::TimeDelta::FromSeconds(status_.battery_time_to_full);
  }

 private:
  static void PowerStatusChangedHandler(void* object,
      const chromeos::PowerStatus& status) {
    PowerLibraryImpl* power = static_cast<PowerLibraryImpl*>(object);
    power->UpdatePowerStatus(status);
  }

  void Init() {
    power_status_connection_ = chromeos::MonitorPowerStatus(
        &PowerStatusChangedHandler, this);
  }

  void UpdatePowerStatus(const chromeos::PowerStatus& status) {
    // Make sure we run on UI thread.
    if (!ChromeThread::CurrentlyOn(ChromeThread::UI)) {
      ChromeThread::PostTask(
          ChromeThread::UI, FROM_HERE,
          NewRunnableMethod(
              this, &PowerLibraryImpl::UpdatePowerStatus, status));
      return;
    }

    DLOG(INFO) << "Power" <<
                  " lpo=" << status.line_power_on <<
                  " sta=" << status.battery_state <<
                  " per=" << status.battery_percentage <<
                  " tte=" << status.battery_time_to_empty <<
                  " ttf=" << status.battery_time_to_full;
    status_ = status;
    FOR_EACH_OBSERVER(Observer, observers_, PowerChanged(this));
  }

  ObserverList<Observer> observers_;

  // A reference to the battery power api, to allow callbacks when the battery
  // status changes.
  chromeos::PowerStatusConnection power_status_connection_;

  // The latest power status.
  chromeos::PowerStatus status_;

  DISALLOW_COPY_AND_ASSIGN(PowerLibraryImpl);
};

class PowerLibraryStubImpl : public PowerLibrary {
 public:
  PowerLibraryStubImpl() {}
  ~PowerLibraryStubImpl() {}
  void AddObserver(Observer* observer) {}
  void RemoveObserver(Observer* observer) {}
  bool line_power_on() const { return false; }
  bool battery_is_present() const { return false; }
  bool battery_fully_charged() const { return false; }
  double battery_percentage() const { return false; }
  base::TimeDelta battery_time_to_empty() const {
    return base::TimeDelta::FromSeconds(0);
  }
  base::TimeDelta battery_time_to_full() const {
    return base::TimeDelta::FromSeconds(0);
  }
};

// static
PowerLibrary* PowerLibrary::GetImpl(bool stub) {
  if (stub)
    return new PowerLibraryStubImpl();
  else
    return new PowerLibraryImpl();
}

}  // namespace chromeos

// Allows InvokeLater without adding refcounting. This class is a Singleton and
// won't be deleted until it's last InvokeLater is run.
DISABLE_RUNNABLE_METHOD_REFCOUNT(chromeos::PowerLibraryImpl);

