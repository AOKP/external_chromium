// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_registry_extension_provider_win.h"

#include "base/file_path.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/version.h"
#include "base/win/registry.h"

namespace {

// The Registry hive where to look for external extensions.
const HKEY kRegRoot = HKEY_LOCAL_MACHINE;

// The Registry subkey that contains information about external extensions.
const char kRegistryExtensions[] = "Software\\Google\\Chrome\\Extensions";

// Registry value of of that key that defines the path to the .crx file.
const wchar_t kRegistryExtensionPath[] = L"path";

// Registry value of that key that defines the current version of the .crx file.
const wchar_t kRegistryExtensionVersion[] = L"version";

bool OpenKeyById(const std::string& id, base::win::RegKey *key) {
  std::wstring key_path = ASCIIToWide(kRegistryExtensions);
  key_path.append(L"\\");
  key_path.append(ASCIIToWide(id));

  return key->Open(kRegRoot, key_path.c_str(), KEY_READ);
}

}  // namespace

ExternalRegistryExtensionProvider::ExternalRegistryExtensionProvider() {
}

ExternalRegistryExtensionProvider::~ExternalRegistryExtensionProvider() {
}

void ExternalRegistryExtensionProvider::VisitRegisteredExtension(
    Visitor* visitor) const {
  base::win::RegistryKeyIterator iterator(
      kRegRoot, ASCIIToWide(kRegistryExtensions).c_str());
  while (iterator.Valid()) {
    base::win::RegKey key;
    std::wstring key_path = ASCIIToWide(kRegistryExtensions);
    key_path.append(L"\\");
    key_path.append(iterator.Name());
    if (key.Open(kRegRoot, key_path.c_str(), KEY_READ)) {
      std::wstring extension_path;
      if (key.ReadValue(kRegistryExtensionPath, &extension_path)) {
        std::wstring extension_version;
        if (key.ReadValue(kRegistryExtensionVersion, &extension_version)) {
          std::string id = WideToASCII(iterator.Name());
          StringToLowerASCII(&id);

          scoped_ptr<Version> version;
          version.reset(Version::GetVersionFromString(
                            WideToASCII(extension_version)));
          if (!version.get()) {
            LOG(ERROR) << "Invalid version value " << extension_version
                       << " for key " << key_path;
            ++iterator;
            continue;
          }

          FilePath path = FilePath::FromWStringHack(extension_path);
          visitor->OnExternalExtensionFileFound(id, version.get(), path,
                                                Extension::EXTERNAL_REGISTRY);
        } else {
          // TODO(erikkay): find a way to get this into about:extensions
          LOG(ERROR) << "Missing value " << kRegistryExtensionVersion
                     << " for key " << key_path;
        }
      } else {
        // TODO(erikkay): find a way to get this into about:extensions
        LOG(ERROR) << "Missing value " << kRegistryExtensionPath
                   << " for key " << key_path;
      }
    }
    ++iterator;
  }
}


bool ExternalRegistryExtensionProvider::HasExtension(
    const std::string& id) const {
  base::win::RegKey key;
  return OpenKeyById(id, &key);
}

bool ExternalRegistryExtensionProvider::GetExtensionDetails(
    const std::string& id,
    Extension::Location* location,
    scoped_ptr<Version>* version) const  {
  base::win::RegKey key;
  if (!OpenKeyById(id, &key))
    return false;

  std::wstring extension_version;
  if (!key.ReadValue(kRegistryExtensionVersion, &extension_version))
    return false;

  if (version) {
    version->reset(Version::GetVersionFromString(
                       WideToASCII(extension_version)));
  }

  if (location)
    *location = Extension::EXTERNAL_REGISTRY;
  return true;
}
