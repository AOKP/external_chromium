// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRINTING_PRINT_DIALOG_CLOUD_H_
#define CHROME_BROWSER_PRINTING_PRINT_DIALOG_CLOUD_H_
#pragma once

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"

class Browser;
class FilePath;
namespace IPC {
class Message;
}

class PrintDialogCloud {
 public:
  // Called on the IO thread.
  static void CreatePrintDialogForPdf(const FilePath& path_to_pdf);

 private:
  friend class PrintDialogCloudTest;

  explicit PrintDialogCloud(const FilePath& path_to_pdf);
  ~PrintDialogCloud();

  // Called as a task from the UI thread, creates an object instance
  // to run the HTML/JS based print dialog for printing through the cloud.
  static void CreateDialogImpl(const FilePath& path_to_pdf);

  Browser* browser_;

  DISALLOW_COPY_AND_ASSIGN(PrintDialogCloud);
};

#endif  // CHROME_BROWSER_PRINTING_PRINT_DIALOG_CLOUD_H_
