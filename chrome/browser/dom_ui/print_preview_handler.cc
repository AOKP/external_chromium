// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/print_preview_handler.h"

#include "base/values.h"
#include "printing/backend/print_backend.h"

PrintPreviewHandler::PrintPreviewHandler()
    : print_backend_(printing::PrintBackend::CreateInstance(NULL)) {
}

PrintPreviewHandler::~PrintPreviewHandler() {
}

void PrintPreviewHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback("getPrinters",
      NewCallback(this, &PrintPreviewHandler::HandleGetPrinters));
}

void PrintPreviewHandler::HandleGetPrinters(const ListValue*) {
  ListValue printers;

  printing::PrinterList printer_list;
  print_backend_->EnumeratePrinters(&printer_list);
  for (printing::PrinterList::iterator index = printer_list.begin();
       index != printer_list.end(); ++index) {
    printers.Append(new StringValue(index->printer_name));
  }

  dom_ui_->CallJavascriptFunction(L"setPrinters", printers);
}
