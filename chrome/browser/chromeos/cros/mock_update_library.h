// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CROS_MOCK_UPDATE_LIBRARY_H_
#define CHROME_BROWSER_CHROMEOS_CROS_MOCK_UPDATE_LIBRARY_H_

#include "base/observer_list.h"
#include "chrome/browser/chromeos/cros/update_library.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockUpdateLibrary : public UpdateLibrary {
 public:
  MockUpdateLibrary() {}
  virtual ~MockUpdateLibrary() {}
  MOCK_METHOD1(AddObserver, void(Observer*));  // NOLINT
  MOCK_METHOD1(RemoveObserver, void(Observer*));  // NOLINT

  MOCK_CONST_METHOD0(status, const Status&(void));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockUpdateLibrary);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CROS_MOCK_UPDATE_LIBRARY_H_

