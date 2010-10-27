// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_SETUP_FLOW_H_
#define CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_SETUP_FLOW_H_

#include <string>
#include <vector>

#include "app/l10n_util.h"
#include "base/time.h"
#include "chrome/browser/dom_ui/html_dialog_ui.h"
#include "chrome/common/net/gaia/gaia_auth_consumer.h"
#include "chrome/common/net/gaia/gaia_authenticator2.h"
#include "gfx/native_widget_types.h"
#include "grit/generated_resources.h"

class GaiaAuthenticator2;
class CloudPrintServiceProcessHelper;
class CloudPrintSetupMessageHandler;
class ServiceProcessControl;
class GoogleServiceAuthError;
class Browser;

// This class is responsible for showing a cloud print setup dialog
// and perform operations to fill the content of the dialog and handle
// user actions in the dialog.
//
// It is responsible for:
// 1. Showing the setup dialog.
// 2. Providing the URL for the content of the dialog.
// 3. Providing a data source to provide the content HTML files.
// 4. Providing a message handler to handle user actions in the DOM UI.
// 5. Responding to actions received in the message handler.
//
// The architecture for DOMUI is designed such that only the message handler
// can access the DOMUI. This splits the flow control across the message
// handler and this class. In order to centralize all the flow control and
// content in the DOMUI, the DOMUI object is given to this object by the
// message handler through the Attach(DOMUI*) method.
class CloudPrintSetupFlow : public HtmlDialogUIDelegate,
                            public GaiaAuthConsumer {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}
    // Called when the setup dialog is closed.
    virtual void OnDialogClosed() = 0;
  };
  virtual ~CloudPrintSetupFlow();

  // Runs a flow from |start| to |end|, and does the work of actually showing
  // the HTML dialog.  |container| is kept up-to-date with the lifetime of the
  // flow (e.g it is emptied on dialog close).
  static CloudPrintSetupFlow* OpenDialog(Profile* service, Delegate* delegate,
                                         gfx::NativeWindow parent_window);

  // Disables the cloud print proxy if it's enabled and running.
  static void DisableCloudPrintProxy(Profile* profile);

  // Ping the cloud print proxy service in order to get the true
  // enablement state and user e-mail that the service is using, and
  // reflect those back into the browser preferences.
  static void RefreshPreferencesFromService(
      Profile* profile, Callback2<bool, std::string>::Type* callback);

  // Focuses the dialog.  This is useful in cases where the dialog has been
  // obscured by a browser window.
  void Focus();

  // HtmlDialogUIDelegate implementation.
  virtual GURL GetDialogContentURL() const;
  virtual void GetDOMMessageHandlers(
      std::vector<DOMMessageHandler*>* handlers) const;
  virtual void GetDialogSize(gfx::Size* size) const;
  virtual std::string GetDialogArgs() const;
  virtual void OnDialogClosed(const std::string& json_retval);
  virtual void OnCloseContents(TabContents* source, bool* out_close_dialog);
  virtual std::wstring GetDialogTitle() const;
  virtual bool IsDialogModal() const;

  // GaiaAuthConsumer implementation.
  virtual void OnClientLoginFailure(
      const GoogleServiceAuthError& error);
  virtual void OnClientLoginSuccess(
      const GaiaAuthConsumer::ClientLoginResult& credentials);

 private:
  friend class CloudPrintServiceProcessHelper;
  friend class CloudPrintSetupMessageHandler;

  // Use static Run method to get an instance.
  CloudPrintSetupFlow(const std::string& args, Profile* profile,
                      Delegate* delegate);

  // Called CloudPrintSetupMessageHandler when a DOM is attached. This method
  // is called when the HTML page is fully loaded. We then operate on this
  // DOMUI object directly.
  void Attach(DOMUI* dom_ui);

  // Called by CloudPrintSetupMessageHandler when user authentication is
  // registered.
  void OnUserSubmittedAuth(const std::string& user,
                           const std::string& password,
                           const std::string& captcha);

  // Event triggered when the service process was launched.
  void OnProcessLaunched();

  // The following methods control which iframe is visible.
  void ShowGaiaLogin(const DictionaryValue& args);
  void ShowGaiaSuccessAndSettingUp();
  void ShowGaiaFailed(const GoogleServiceAuthError& error);
  void ShowSetupDone();
  void ExecuteJavascriptInIFrame(const std::wstring& iframe_xpath,
                                 const std::wstring& js);

  // Pointer to the DOM UI. This is provided by CloudPrintSetupMessageHandler
  // when attached.
  DOMUI* dom_ui_;

  // The args to pass to the initial page.
  std::string dialog_start_args_;
  Profile* profile_;

  // Fetcher to obtain the Chromoting Directory token.
  scoped_ptr<GaiaAuthenticator2> authenticator_;
  std::string login_;
  std::string lsid_;

  // Handle to the ServiceProcessControl which talks to the service process.
  ServiceProcessControl* process_control_;
  scoped_refptr<CloudPrintServiceProcessHelper> service_process_helper_;

  Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(CloudPrintSetupFlow);
};

#endif  // CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_SETUP_FLOW_H_
