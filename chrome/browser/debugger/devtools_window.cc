// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app/l10n_util.h"
#include "base/command_line.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_theme_provider.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/debugger/devtools_window.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "chrome/browser/tab_contents/navigation_entry.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/common/bindings_policy.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "grit/generated_resources.h"

const std::wstring DevToolsWindow::kDevToolsApp = L"DevToolsApp";

// static
TabContents* DevToolsWindow::GetDevToolsContents(TabContents* inspected_tab) {
  if (!inspected_tab) {
    return NULL;
  }

  if (!DevToolsManager::GetInstance())
    return NULL;  // Happens only in tests.

  DevToolsClientHost* client_host = DevToolsManager::GetInstance()->
          GetDevToolsClientHostFor(inspected_tab->render_view_host());
  if (!client_host) {
    return NULL;
  }

  DevToolsWindow* window = client_host->AsDevToolsWindow();
  if (!window || !window->is_docked()) {
    return NULL;
  }
  return window->tab_contents();
}

DevToolsWindow::DevToolsWindow(Profile* profile,
                               RenderViewHost* inspected_rvh,
                               bool docked)
    : profile_(profile),
      browser_(NULL),
      docked_(docked),
      is_loaded_(false),
      action_on_load_(DEVTOOLS_TOGGLE_ACTION_NONE) {
  // Create TabContents with devtools.
  tab_contents_ = new TabContents(profile, NULL, MSG_ROUTING_NONE, NULL);
  tab_contents_->render_view_host()->AllowBindings(BindingsPolicy::DOM_UI);
  tab_contents_->controller().LoadURL(
      GetDevToolsUrl(), GURL(), PageTransition::START_PAGE);

  // Wipe out page icon so that the default application icon is used.
  NavigationEntry* entry = tab_contents_->controller().GetActiveEntry();
  entry->favicon().set_bitmap(SkBitmap());
  entry->favicon().set_is_valid(true);

  // Register on-load actions.
  registrar_.Add(this,
                 NotificationType::LOAD_STOP,
                 Source<NavigationController>(&tab_contents_->controller()));
  registrar_.Add(this,
                 NotificationType::TAB_CLOSING,
                 Source<NavigationController>(&tab_contents_->controller()));
  registrar_.Add(this, NotificationType::BROWSER_THEME_CHANGED,
                 NotificationService::AllSources());
  inspected_tab_ = inspected_rvh->delegate()->GetAsTabContents();
}

DevToolsWindow::~DevToolsWindow() {
}

DevToolsWindow* DevToolsWindow::AsDevToolsWindow() {
  return this;
}

void DevToolsWindow::SendMessageToClient(const IPC::Message& message) {
  RenderViewHost* target_host = tab_contents_->render_view_host();
  IPC::Message* m =  new IPC::Message(message);
  m->set_routing_id(target_host->routing_id());
  target_host->Send(m);
}

void DevToolsWindow::InspectedTabClosing() {
  if (docked_) {
    // Update dev tools to reflect removed dev tools window.

    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      inspected_window->UpdateDevTools();
    // In case of docked tab_contents we own it, so delete here.
    delete tab_contents_;

    delete this;
  } else {
    // First, initiate self-destruct to free all the registrars.
    // Then close all tabs. Browser will take care of deleting tab_contents
    // for us.
    Browser* browser = browser_;
    delete this;
    browser->CloseAllTabs();
  }
}

void DevToolsWindow::Show(DevToolsToggleAction action) {
  if (docked_) {
    // Just tell inspected browser to update splitter.
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window) {
      tab_contents_->set_delegate(this);
      inspected_window->UpdateDevTools();
      SetAttachedWindow();
      tab_contents_->view()->SetInitialFocus();
      ScheduleAction(action);
      return;
    } else {
      // Sometimes we don't know where to dock. Stay undocked.
      docked_ = false;
    }
  }

  if (!browser_)
    CreateDevToolsBrowser();

  // Avoid consecutive window switching if the devtools window has been opened
  // and the Inspect Element shortcut is pressed in the inspected tab.
  bool should_show_window =
      !browser_ || action != DEVTOOLS_TOGGLE_ACTION_INSPECT;
  if (should_show_window)
    browser_->window()->Show();
  SetAttachedWindow();
  if (should_show_window)
    tab_contents_->view()->SetInitialFocus();

  ScheduleAction(action);
}

void DevToolsWindow::Activate() {
  if (!docked_) {
    if (!browser_->window()->IsActive()) {
      browser_->window()->Activate();
    }
  } else {
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      tab_contents_->view()->Focus();
  }
}

void DevToolsWindow::SetDocked(bool docked) {
  if (docked_ == docked)
    return;
  if (docked && !GetInspectedBrowserWindow()) {
    // Cannot dock, avoid window flashing due to close-reopen cycle.
    return;
  }
  docked_ = docked;

  if (docked) {
    // Detach window from the external devtools browser. It will lead to
    // the browser object's close and delete. Remove observer first.
    TabStripModel* tabstrip_model = browser_->tabstrip_model();
    tabstrip_model->DetachTabContentsAt(
        tabstrip_model->GetIndexOfTabContents(tab_contents_));
    browser_ = NULL;
  } else {
    // Update inspected window to hide split and reset it.
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window) {
      inspected_window->UpdateDevTools();
      inspected_window = NULL;
    }
  }
  Show(DEVTOOLS_TOGGLE_ACTION_NONE);
}

RenderViewHost* DevToolsWindow::GetRenderViewHost() {
  return tab_contents_->render_view_host();
}

void DevToolsWindow::CreateDevToolsBrowser() {
  // TODO(pfeldman): Make browser's getter for this key static.
  std::wstring wp_key = L"";
  wp_key.append(prefs::kBrowserWindowPlacement);
  wp_key.append(L"_");
  wp_key.append(kDevToolsApp);

  PrefService* prefs = g_browser_process->local_state();
  if (!prefs->FindPreference(wp_key.c_str())) {
    prefs->RegisterDictionaryPref(wp_key.c_str());
  }

  const DictionaryValue* wp_pref = prefs->GetDictionary(wp_key.c_str());
  if (!wp_pref) {
    DictionaryValue* defaults = prefs->GetMutableDictionary(wp_key.c_str());
    defaults->SetInteger(L"left", 100);
    defaults->SetInteger(L"top", 100);
    defaults->SetInteger(L"right", 740);
    defaults->SetInteger(L"bottom", 740);
    defaults->SetBoolean(L"maximized", false);
    defaults->SetBoolean(L"always_on_top", false);
  }

  browser_ = Browser::CreateForDevTools(profile_);
  browser_->tabstrip_model()->AddTabContents(
      tab_contents_, -1, PageTransition::START_PAGE,
      TabStripModel::ADD_SELECTED);
}

BrowserWindow* DevToolsWindow::GetInspectedBrowserWindow() {
  for (BrowserList::const_iterator it = BrowserList::begin();
       it != BrowserList::end(); ++it) {
    Browser* browser = *it;
    for (int i = 0; i < browser->tab_count(); ++i) {
      TabContents* tab_contents = browser->GetTabContentsAt(i);
      if (tab_contents == inspected_tab_) {
        return browser->window();
      }
    }
  }
  return NULL;
}

void DevToolsWindow::SetAttachedWindow() {
  tab_contents_->render_view_host()->
      ExecuteJavascriptInWebFrame(
          L"", docked_ ? L"WebInspector.setAttachedWindow(true);" :
                         L"WebInspector.setAttachedWindow(false);");
}

void DevToolsWindow::Observe(NotificationType type,
                             const NotificationSource& source,
                             const NotificationDetails& details) {
  if (type == NotificationType::LOAD_STOP) {
    SetAttachedWindow();
    is_loaded_ = true;
    UpdateTheme();
    DoAction();
  } else if (type == NotificationType::TAB_CLOSING) {
    if (Source<NavigationController>(source).ptr() ==
            &tab_contents_->controller()) {
      // This happens when browser closes all of its tabs as a result
      // of window.Close event.
      // Notify manager that this DevToolsClientHost no longer exists and
      // initiate self-destuct here.
      NotifyCloseListener();
      delete this;
    }
  } else if (type == NotificationType::BROWSER_THEME_CHANGED) {
    UpdateTheme();
  }
}

void DevToolsWindow::ScheduleAction(DevToolsToggleAction action) {
  action_on_load_ = action;
  if (is_loaded_)
    DoAction();
}

void DevToolsWindow::DoAction() {
  // TODO: these messages should be pushed through the WebKit API instead.
  switch (action_on_load_) {
    case DEVTOOLS_TOGGLE_ACTION_SHOW_CONSOLE:
      tab_contents_->render_view_host()->
          ExecuteJavascriptInWebFrame(L"", L"WebInspector.showConsole();");
      break;
    case DEVTOOLS_TOGGLE_ACTION_INSPECT:
      tab_contents_->render_view_host()->
          ExecuteJavascriptInWebFrame(
              L"", L"WebInspector.toggleSearchingForNode();");
    case DEVTOOLS_TOGGLE_ACTION_NONE:
      // Do nothing.
      break;
    default:
      NOTREACHED();
  }
  action_on_load_ = DEVTOOLS_TOGGLE_ACTION_NONE;
}

std::string SkColorToRGBAString(SkColor color) {
  // We convert the alpha using DoubleToString because StringPrintf will use
  // locale specific formatters (e.g., use , instead of . in German).
  return StringPrintf("rgba(%d,%d,%d,%s)", SkColorGetR(color),
      SkColorGetG(color), SkColorGetB(color),
      DoubleToString(SkColorGetA(color) / 255.0).c_str());
}

GURL DevToolsWindow::GetDevToolsUrl() {
  BrowserThemeProvider* tp = profile_->GetThemeProvider();
  CHECK(tp);

  SkColor color_toolbar =
      tp->GetColor(BrowserThemeProvider::COLOR_TOOLBAR);
  SkColor color_tab_text =
      tp->GetColor(BrowserThemeProvider::COLOR_BOOKMARK_TEXT);

  std::string url_string = StringPrintf(
      "%sdevtools.html?docked=%s&toolbar_color=%s&text_color=%s",
      chrome::kChromeUIDevToolsURL,
      docked_ ? "true" : "false",
      SkColorToRGBAString(color_toolbar).c_str(),
      SkColorToRGBAString(color_tab_text).c_str());
  return GURL(url_string);
}

void DevToolsWindow::UpdateTheme() {
  BrowserThemeProvider* tp = profile_->GetThemeProvider();
  CHECK(tp);

  SkColor color_toolbar =
      tp->GetColor(BrowserThemeProvider::COLOR_TOOLBAR);
  SkColor color_tab_text =
      tp->GetColor(BrowserThemeProvider::COLOR_BOOKMARK_TEXT);
  std::string command = StringPrintf(
      "WebInspector.setToolbarColors(\"%s\", \"%s\")",
      SkColorToRGBAString(color_toolbar).c_str(),
      SkColorToRGBAString(color_tab_text).c_str());
  tab_contents_->render_view_host()->
      ExecuteJavascriptInWebFrame(L"", UTF8ToWide(command));
}

bool DevToolsWindow::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event, bool* is_keyboard_shortcut) {
  if (docked_) {
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      return inspected_window->PreHandleKeyboardEvent(
          event, is_keyboard_shortcut);
  }
  return false;
}

void DevToolsWindow::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  if (docked_) {
    BrowserWindow* inspected_window = GetInspectedBrowserWindow();
    if (inspected_window)
      inspected_window->HandleKeyboardEvent(event);
  }
}
