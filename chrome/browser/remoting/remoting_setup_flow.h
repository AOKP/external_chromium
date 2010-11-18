// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_REMOTING_REMOTING_SETUP_FLOW_H_
#define CHROME_BROWSER_REMOTING_REMOTING_SETUP_FLOW_H_

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
class RemotingServiceProcessHelper;
class RemotingSetupMessageHandler;
class ServiceProcessControl;
class GoogleServiceAuthError;

// This class is responsible for showing a remoting setup dialog and perform
// operations to fill the content of the dialog and handle user actions
// in the dialog.
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
class RemotingSetupFlow : public HtmlDialogUIDelegate,
                          public GaiaAuthConsumer {
 public:
  virtual ~RemotingSetupFlow();

  // Runs a flow from |start| to |end|, and does the work of actually showing
  // the HTML dialog.  |container| is kept up-to-date with the lifetime of the
  // flow (e.g it is emptied on dialog close).
  static RemotingSetupFlow* OpenDialog(Profile* service);

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
  virtual bool ShouldShowDialogTitle() const;

  // GaiaAuthConsumer implementation.
  virtual void OnClientLoginFailure(
      const GoogleServiceAuthError& error);
  virtual void OnClientLoginSuccess(
      const GaiaAuthConsumer::ClientLoginResult& credentials);
  virtual void OnIssueAuthTokenSuccess(const std::string& service,
                                       const std::string& auth_token);
  virtual void OnIssueAuthTokenFailure(const std::string& service,
                                       const GoogleServiceAuthError& error);

 private:
  friend class RemotingServiceProcessHelper;
  friend class RemotingSetupMessageHandler;

  // Use static Run method to get an instance.
  RemotingSetupFlow(const std::string& args, Profile* profile);

  // Called RemotingSetupMessageHandler when a DOM is attached. This method
  // is called when the HTML page is fully loaded. We then operate on this
  // DOMUI object directly.
  void Attach(DOMUI* dom_ui);

  // Called by RemotingSetupMessageHandler when user authentication is
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

  // Pointer to the DOM UI. This is provided by RemotingSetupMessageHandler
  // when attached.
  DOMUI* dom_ui_;

  // The args to pass to the initial page.
  std::string dialog_start_args_;
  Profile* profile_;

  // Fetcher to obtain the Chromoting Directory token.
  scoped_ptr<GaiaAuthenticator2> authenticator_;
  std::string login_;
  std::string remoting_token_;
  std::string sync_token_;

  // Handle to the ServiceProcessControl which talks to the service process.
  ServiceProcessControl* process_control_;
  scoped_refptr<RemotingServiceProcessHelper> service_process_helper_;

  DISALLOW_COPY_AND_ASSIGN(RemotingSetupFlow);
};

#endif  // CHROME_BROWSER_REMOTING_REMOTING_SETUP_FLOW_H_
