// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_OPTIONS_CERTIFICATE_MANAGER_HANDLER_H_
#define CHROME_BROWSER_DOM_UI_OPTIONS_CERTIFICATE_MANAGER_HANDLER_H_
#pragma once

#include "base/scoped_ptr.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/certificate_manager_model.h"
#include "chrome/browser/dom_ui/options/options_ui.h"
#include "chrome/browser/shell_dialogs.h"
#include "gfx/native_widget_types.h"
#include "net/base/cert_database.h"

class FileAccessProvider;

class CertificateManagerHandler : public OptionsPageUIHandler,
    public CertificateManagerModel::Observer,
    public SelectFileDialog::Listener {
 public:
  CertificateManagerHandler();
  virtual ~CertificateManagerHandler();

  // OptionsUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings);
  virtual void RegisterMessages();

  // CertificateManagerModel::Observer implementation.
  virtual void CertificatesRefreshed();

  // SelectFileDialog::Listener implementation.
  virtual void FileSelected(const FilePath& path, int index, void* params);
  virtual void FileSelectionCanceled(void* params);

 private:
  // View certificate.
  void View(const ListValue* args);

  // Edit server certificate trust values.
  void EditServer(const ListValue* args);

  // Edit certificate authority trust values.  The sequence goes like:
  //  1. user clicks edit button -> CertificateEditCaTrustOverlay.show ->
  //  GetCATrust -> CertificateEditCaTrustOverlay.populateTrust
  //  2. user clicks ok -> EditCATrust -> CertificateEditCaTrustOverlay.dismiss
  void GetCATrust(const ListValue* args);
  void EditCATrust(const ListValue* args);

  // Cleanup state stored during import or export process.
  void CancelImportExportProcess(const ListValue* args);
  void ImportExportCleanup();

  // Export to PKCS #12 file.  The sequence goes like:
  //  1a. user click on export button -> ExportPersonal -> launches file
  //  selector
  //  1b. user click on export all button -> ExportAllPersonal -> launches file
  //  selector
  //  2. user selects file -> ExportPersonalFileSelected -> launches password
  //  dialog
  //  3. user enters password -> ExportPersonalPasswordSelected -> exports to
  //  memory buffer -> starts async write operation
  //  4. write finishes (or fails) -> ExportPersonalFileWritten
  void ExportPersonal(const ListValue* args);
  void ExportAllPersonal(const ListValue* args);
  void ExportPersonalFileSelected(const FilePath& path);
  void ExportPersonalPasswordSelected(const ListValue* args);
  void ExportPersonalFileWritten(int write_errno, int bytes_written);

  // Import from PKCS #12 file.  The sequence goes like:
  //  1. user click on import button -> StartImportPersonal -> launches file
  //  selector
  //  2. user selects file -> ImportPersonalFileSelected -> launches password
  //  dialog
  //  3. user enters password -> ImportPersonalPasswordSelected -> starts async
  //  read operation
  //  4. read operation completes -> ImportPersonalFileRead -> attempts to
  //  import with previously entered password
  //  5a. if import succeeds -> ImportExportCleanup
  //  5b. if import fails -> show error, ImportExportCleanup
  //  TODO(mattm): allow retrying with different password
  void StartImportPersonal(const ListValue* args);
  void ImportPersonalFileSelected(const FilePath& path);
  void ImportPersonalPasswordSelected(const ListValue* args);
  void ImportPersonalFileRead(int read_errno, std::string data);

  // Import Server certificates from file.  Sequence goes like:
  //  1. user clicks on import button -> ImportServer -> launches file selector
  //  2. user selects file -> ImportServerFileSelected -> starts async read
  //  3. read completes -> ImportServerFileRead -> parse certs -> attempt import
  //  4a. if import succeeds -> ImportExportCleanup
  //  4b. if import fails -> show error, ImportExportCleanup
  void ImportServer(const ListValue* args);
  void ImportServerFileSelected(const FilePath& path);
  void ImportServerFileRead(int read_errno, std::string data);

  // Import Certificate Authorities from file.  Sequence goes like:
  //  1. user clicks on import button -> ImportCA -> launches file selector
  //  2. user selects file -> ImportCAFileSelected -> starts async read
  //  3. read completes -> ImportCAFileRead -> parse certs ->
  //  CertificateEditCaTrustOverlay.showImport
  //  4. user clicks ok -> ImportCATrustSelected -> attempt import
  //  5a. if import succeeds -> ImportExportCleanup
  //  5b. if import fails -> show error, ImportExportCleanup
  void ImportCA(const ListValue* args);
  void ImportCAFileSelected(const FilePath& path);
  void ImportCAFileRead(int read_errno, std::string data);
  void ImportCATrustSelected(const ListValue* args);

  // Export a certificate.
  void Export(const ListValue* args);

  // Delete certificate and private key (if any).
  void Delete(const ListValue* args);

  // Populate the trees in all the tabs.
  void Populate(const ListValue* args);

  // Populate the given tab's tree.
  void PopulateTree(const std::string& tab_name, net::CertType type);

  // Display a domui error message box.
  void ShowError(const std::string& title, const std::string& error) const;

  // Display a domui error message box for import failures.
  // Depends on |selected_cert_list_| being set to the imports that we
  // attempted to import.
  void ShowImportErrors(
      const std::string& title,
      const net::CertDatabase::ImportCertFailureList& not_imported) const;

  gfx::NativeWindow GetParentWindow() const;

  // The Certificates Manager model
  scoped_ptr<CertificateManagerModel> certificate_manager_model_;

  // For multi-step import or export processes, we need to store the path,
  // password, etc the user chose while we wait for them to enter a password,
  // wait for file to be read, etc.
  FilePath file_path_;
  string16 password_;
  net::CertificateList selected_cert_list_;
  scoped_refptr<SelectFileDialog> select_file_dialog_;

  // Used in reading and writing certificate files.
  CancelableRequestConsumer consumer_;
  scoped_refptr<FileAccessProvider> file_access_provider_;

  DISALLOW_COPY_AND_ASSIGN(CertificateManagerHandler);
};

#endif  // CHROME_BROWSER_DOM_UI_OPTIONS_CERTIFICATE_MANAGER_HANDLER_H_
