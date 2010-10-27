// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/cros/burn_library.h"

#include <cstring>
#include "base/linked_ptr.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/chromeos/cros/cros_library.h"

namespace chromeos {

BurnLibraryImpl::BurnLibraryImpl() {
  if (CrosLibrary::Get()->EnsureLoaded()) {
      Init();
    } else {
      LOG(ERROR) << "Cros Library has not been loaded";
    }
}

BurnLibraryImpl::~BurnLibraryImpl() {
  if (burn_status_connection_) {
    DisconnectBurnStatus(burn_status_connection_);
  }
}

void BurnLibraryImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void BurnLibraryImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool BurnLibraryImpl::DoBurn(const FilePath& from_path,
                             const FilePath& to_path) {
  BurnLibraryTaskProxy* task = new BurnLibraryTaskProxy(AsWeakPtr());
  task->AddRef();
  task->BurnImage(from_path, to_path);
  ChromeThread::PostTask(ChromeThread::FILE, FROM_HERE,
      NewRunnableMethod(task, &BurnLibraryTaskProxy::BurnImage,
                        from_path, to_path));
  return true;
}

bool BurnLibraryImpl::BurnImage(const FilePath& from_path,
                                const FilePath& to_path) {
  // Make sure we run on file thread.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::FILE));

  // Check if there is a target path already being burnt to.
  if (target_path_ == "") {
    target_path_ = to_path.value();
  } else {
    return false;
  }

  StartBurn(from_path.value().c_str(), to_path.value().c_str(),
            burn_status_connection_);
  return true;
}

void BurnLibraryImpl::BurnStatusChangedHandler(void* object,
                                               const BurnStatus& status,
                                               BurnEventType evt) {
  BurnLibraryImpl* burn = static_cast<BurnLibraryImpl*>(object);

  // Copy burn status because it will be freed after returning from this method.
  ImageBurnStatus* status_copy = new ImageBurnStatus(status);

  BurnLibraryTaskProxy* task = new BurnLibraryTaskProxy(burn->AsWeakPtr());
  task->AddRef();
  ChromeThread::PostTask(ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(task, &BurnLibraryTaskProxy::UpdateBurnStatus,
                        status_copy, evt));
}

void BurnLibraryImpl::Init() {
  burn_status_connection_ = MonitorBurnStatus(&BurnStatusChangedHandler, this);
}

void BurnLibraryImpl::UpdateBurnStatus(const ImageBurnStatus& status,
                                       BurnEventType evt) {
  // Make sure we run on UI thread.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));

  // If burn is finished, remove target paths from paths being burnt to.
  // This has to be done in thread-safe way, hence using task proxy class.
  if ((evt == BURN_CANCELED || evt == BURN_COMPLETE) &&
      target_path_ == status.target_path)
    target_path_ = "";

  FOR_EACH_OBSERVER(Observer, observers_, ProgressUpdated(this, evt, status));
}

BurnLibraryTaskProxy::BurnLibraryTaskProxy(
                        const base::WeakPtr<BurnLibraryImpl>& library)
                            : library_(library) {
}

void BurnLibraryTaskProxy::BurnImage(const FilePath& from_path,
                                     const FilePath& to_path) {
  library_->BurnImage(from_path, to_path);
}

void BurnLibraryTaskProxy::UpdateBurnStatus(ImageBurnStatus* status,
                                            BurnEventType evt) {
  library_->UpdateBurnStatus(*status, evt);
  delete status;
}


class BurnLibraryStubImpl : public BurnLibrary {
 public:
  BurnLibraryStubImpl() {}
  virtual ~BurnLibraryStubImpl() {}

  // BurnLibrary overrides.
  virtual void AddObserver(Observer* observer) {}
  virtual void RemoveObserver(Observer* observer) {}
  virtual bool DoBurn(const FilePath& from_path, const FilePath& to_path) {
    return false;
  }

  DISALLOW_COPY_AND_ASSIGN(BurnLibraryStubImpl);
};

// static
BurnLibrary* BurnLibrary::GetImpl(bool stub) {
  if (stub)
    return new BurnLibraryStubImpl();
  else
    return new BurnLibraryImpl();
}

}  // namespace chromeos

// Allows InvokeLater without adding refcounting. This class is a Singleton and
// won't be deleted until it's last InvokeLater is run.
DISABLE_RUNNABLE_METHOD_REFCOUNT(chromeos::BurnLibraryImpl);

