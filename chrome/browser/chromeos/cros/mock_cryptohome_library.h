// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CROS_MOCK_CRYPTOHOME_LIBRARY_H_
#define CHROME_BROWSER_CHROMEOS_CROS_MOCK_CRYPTOHOME_LIBRARY_H_
#pragma once

#include <string>

#include "chrome/browser/chromeos/cros/cryptohome_library.h"
#include "testing/gmock/include/gmock/gmock.h"

using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::_;

namespace chromeos {

class MockCryptohomeLibrary : public CryptohomeLibrary {
 public:
  MockCryptohomeLibrary() {
    ON_CALL(*this, AsyncCheckKey(_, _, _))
        .WillByDefault(
            WithArgs<2>(Invoke(this, &MockCryptohomeLibrary::DoCallback)));
    ON_CALL(*this, AsyncMigrateKey(_, _, _, _))
        .WillByDefault(
            WithArgs<3>(Invoke(this, &MockCryptohomeLibrary::DoCallback)));
    ON_CALL(*this, AsyncMount(_, _, _, _))
        .WillByDefault(
            WithArgs<3>(Invoke(this, &MockCryptohomeLibrary::DoCallback)));
    ON_CALL(*this, AsyncMountForBwsi(_))
        .WillByDefault(
            WithArgs<0>(Invoke(this, &MockCryptohomeLibrary::DoCallback)));
    ON_CALL(*this, AsyncRemove(_, _))
        .WillByDefault(
            WithArgs<1>(Invoke(this, &MockCryptohomeLibrary::DoCallback)));
  }
  virtual ~MockCryptohomeLibrary() {}
  MOCK_METHOD2(CheckKey, bool(const std::string& user_email,
                              const std::string& passhash));
  MOCK_METHOD3(AsyncCheckKey, bool(const std::string& user_email,
                                   const std::string& passhash,
                                   Delegate* callback));
  MOCK_METHOD3(MigrateKey, bool(const std::string& user_email,
                                const std::string& old_hash,
                                const std::string& new_hash));
  MOCK_METHOD4(AsyncMigrateKey, bool(const std::string& user_email,
                                     const std::string& old_hash,
                                     const std::string& new_hash,
                                     Delegate* callback));
  MOCK_METHOD3(Mount, bool(const std::string& user_email,
                           const std::string& passhash,
                           int* error_code));
  MOCK_METHOD4(AsyncMount, bool(const std::string& user_email,
                                const std::string& passhash,
                                const bool create_if_missing,
                                Delegate* callback));
  MOCK_METHOD1(MountForBwsi, bool(int*));
  MOCK_METHOD1(AsyncMountForBwsi, bool(Delegate* callback));
  MOCK_METHOD1(Remove, bool(const std::string& user_email));
  MOCK_METHOD2(AsyncRemove, bool(const std::string& user_email, Delegate* d));
  MOCK_METHOD0(IsMounted, bool(void));
  MOCK_METHOD0(GetSystemSalt, CryptohomeBlob(void));

  void SetAsyncBehavior(bool outcome, int code) {
    outcome_ = outcome;
    code_ = code;
  }

  bool DoCallback(Delegate* d) {
    d->OnComplete(outcome_, code_);
    return true;
  }

 private:
  bool outcome_;
  int code_;
  DISALLOW_COPY_AND_ASSIGN(MockCryptohomeLibrary);
};
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CROS_MOCK_CRYPTOHOME_LIBRARY_H_
