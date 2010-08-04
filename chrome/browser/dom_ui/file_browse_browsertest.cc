// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/task.h"
#include "base/values.h"
#include "base/path_service.h"
#include "chrome/browser/dom_ui/dom_ui.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/views/html_dialog_view.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"

namespace {

typedef DOMElementProxy::By By;

class FileBrowseBrowserTest : public InProcessBrowserTest {
 public:
   FileBrowseBrowserTest() {
     EnableDOMAutomation();
   }
};

class FileBrowseUiObserver : public NotificationObserver {
 public:
  FileBrowseUiObserver() : file_browse_tab_(NULL), is_waiting_(false) {
    registrar_.Add(this, NotificationType::LOAD_STOP,
                   NotificationService::AllSources());
    registrar_.Add(this, NotificationType::TAB_CONTENTS_DESTROYED,
                   NotificationService::AllSources());
  }

  void WaitForFileBrowseLoad() {
    if (file_browse_tab_ == NULL) {
      is_waiting_ = true;
      ui_test_utils::RunMessageLoop();
    }
  }

  // File browse tab deletion is a non-nestable task and BrowserTest would
  // not get related notification because test body runs in a task already.
  // Uses a periodical check of the dialog window to implement the wait.
  void WaitForFileBrowseClose() {
    if (file_browse_tab_ != NULL) {
      is_waiting_ = true;
      ui_test_utils::RunMessageLoop();
    }
  }

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) {
    if (type == NotificationType::LOAD_STOP) {
      NavigationController* controller =
          Source<NavigationController>(source).ptr();

      if (controller) {
        TabContents* tab_contents = controller->tab_contents();
        if (tab_contents &&
            tab_contents->GetURL().SchemeIs(chrome::kChromeUIScheme) &&
            tab_contents->GetURL().host() == chrome::kChromeUIFileBrowseHost) {
          file_browse_tab_ = tab_contents;

          if (is_waiting_) {
            is_waiting_ = false;
            MessageLoopForUI::current()->Quit();
          }
        }
      }
    } else if (type == NotificationType::TAB_CONTENTS_DESTROYED) {
      TabContents* tab_contents = Source<TabContents>(source).ptr();
      if (file_browse_tab_ == tab_contents) {
        file_browse_tab_ = NULL;

        if (is_waiting_) {
          is_waiting_ = false;
          MessageLoopForUI::current()->Quit();
        }
      }
    }
  }

  TabContents* file_browse_tab() {
    return file_browse_tab_;
  }

  DOMUI* file_browse_ui() {
    return file_browse_tab_ ? file_browse_tab_->render_manager()->dom_ui() :
                              NULL;
  }

 private:
  NotificationRegistrar registrar_;
  TabContents* file_browse_tab_;
  bool is_waiting_;

  DISALLOW_COPY_AND_ASSIGN(FileBrowseUiObserver);
};

IN_PROC_BROWSER_TEST_F(FileBrowseBrowserTest, InputFileTriggerFileBrowse) {
  HTTPTestServer* server = StartHTTPServer();
  ui_test_utils::NavigateToURL(browser(),
                               server->TestServerPage("files/input_file.html"));

  DOMElementProxyRef doc = ui_test_utils::GetActiveDOMDocument(browser());

  DOMElementProxyRef input_file = doc->FindElement(By::Selectors(".single"));
  ASSERT_TRUE(input_file);

  // Creates FileBrowseUiObserver before we click.
  FileBrowseUiObserver observer;

  // Click on the input control. This should bring up the FileBrowseUI.
  input_file->Click();

  observer.WaitForFileBrowseLoad();
  DOMUI* file_browser_ui = observer.file_browse_ui();
  ASSERT_TRUE(file_browser_ui);

  file_browser_ui->CallJavascriptFunction(L"dialogCancelClick");

  observer.WaitForFileBrowseClose();
}

}  // namespace
