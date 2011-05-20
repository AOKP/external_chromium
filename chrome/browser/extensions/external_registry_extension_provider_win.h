// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTERNAL_REGISTRY_EXTENSION_PROVIDER_WIN_H_
#define CHROME_BROWSER_EXTENSIONS_EXTERNAL_REGISTRY_EXTENSION_PROVIDER_WIN_H_
#pragma once

#include "chrome/browser/extensions/external_extension_provider.h"

class Version;

// A specialization of the ExternalExtensionProvider that uses the Registry to
// look up which external extensions are registered.
class ExternalRegistryExtensionProvider : public ExternalExtensionProvider {
 public:
  ExternalRegistryExtensionProvider();
  virtual ~ExternalRegistryExtensionProvider();

  // ExternalExtensionProvider implementation:
  virtual void VisitRegisteredExtension(Visitor* visitor) const;

  virtual bool HasExtension(const std::string& id) const;

  virtual bool GetExtensionDetails(const std::string& id,
                                   Extension::Location* location,
                                   scoped_ptr<Version>* version) const;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTERNAL_REGISTRY_EXTENSION_PROVIDER_WIN_H_
