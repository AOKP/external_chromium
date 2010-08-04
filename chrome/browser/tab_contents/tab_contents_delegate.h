// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TAB_CONTENTS_TAB_CONTENTS_DELEGATE_H_
#define CHROME_BROWSER_TAB_CONTENTS_TAB_CONTENTS_DELEGATE_H_

#include <string>

#include "base/basictypes.h"
#include "chrome/browser/automation/automation_resource_routing_delegate.h"
#include "chrome/browser/tab_contents/navigation_entry.h"
#include "chrome/common/content_settings_types.h"
#include "chrome/common/native_web_keyboard_event.h"
#include "chrome/common/page_transition_types.h"
#include "gfx/native_widget_types.h"
#include "gfx/rect.h"
#include "webkit/glue/context_menu.h"
#include "webkit/glue/window_open_disposition.h"

class Browser;
class DownloadItem;
class ExtensionFunctionDispatcher;
class GURL;
class HtmlDialogUIDelegate;
class Profile;
class RenderViewHost;
class TabContents;
class TemplateURL;

namespace webkit_glue {
struct WebApplicationInfo;
}

// Objects implement this interface to get notified about changes in the
// TabContents and to provide necessary functionality.
class TabContentsDelegate : public AutomationResourceRoutingDelegate {
 public:
  // Opens a new URL inside the passed in TabContents (if source is 0 open
  // in the current front-most tab), unless |disposition| indicates the url
  // should be opened in a new tab or window.
  //
  // A NULL source indicates the current tab (callers should probably use
  // OpenURL() for these cases which does it for you).
  virtual void OpenURLFromTab(TabContents* source,
                              const GURL& url, const GURL& referrer,
                              WindowOpenDisposition disposition,
                              PageTransition::Type transition) = 0;

  // Called to inform the delegate that the tab content's navigation state
  // changed. The |changed_flags| indicates the parts of the navigation state
  // that have been updated, and is any combination of the
  // |TabContents::InvalidateTypes| bits.
  virtual void NavigationStateChanged(const TabContents* source,
                                      unsigned changed_flags) = 0;

  // Creates a new tab with the already-created TabContents 'new_contents'.
  // The window for the added contents should be reparented correctly when this
  // method returns.  If |disposition| is NEW_POPUP, |pos| should hold the
  // initial position.
  virtual void AddNewContents(TabContents* source,
                              TabContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture) = 0;

  // Selects the specified contents, bringing its container to the front.
  virtual void ActivateContents(TabContents* contents) = 0;

  // Notifies the delegate that this contents is starting or is done loading
  // some resource. The delegate should use this notification to represent
  // loading feedback. See TabContents::is_loading()
  virtual void LoadingStateChanged(TabContents* source) = 0;

  // Request the delegate to close this tab contents, and do whatever cleanup
  // it needs to do.
  virtual void CloseContents(TabContents* source) = 0;

  // Request the delegate to move this tab contents to the specified position
  // in screen coordinates.
  virtual void MoveContents(TabContents* source, const gfx::Rect& pos) = 0;

  // Causes the delegate to detach |source| and clean up any internal data
  // pointing to it.  After this call ownership of |source| passes to the
  // caller, and it is safe to call "source->set_delegate(someone_else);".
  virtual void DetachContents(TabContents* source);

  // Called to determine if the TabContents is contained in a popup window.
  virtual bool IsPopup(TabContents* source) = 0;

  // If |source| is constrained, returns the tab containing it.  Otherwise
  // returns |source|.
  virtual TabContents* GetConstrainingContents(TabContents* source);

  // Notification that some of our content has changed size as
  // part of an animation.
  virtual void ToolbarSizeChanged(TabContents* source, bool is_animating) = 0;

  // Notification that the starredness of the current URL changed.
  virtual void URLStarredChanged(TabContents* source, bool starred) = 0;

  // Notification that the target URL has changed.
  virtual void UpdateTargetURL(TabContents* source, const GURL& url) = 0;

  // Notification that there was a mouse event, along with the absolute
  // coordinates of the mouse pointer and whether it was a normal motion event
  // (otherwise, the pointer left the contents area).
  virtual void ContentsMouseEvent(
      TabContents* source, const gfx::Point& location, bool motion);

  // Request the delegate to change the zoom level of the current tab.
  virtual void ContentsZoomChange(bool zoom_in);

  // Notifies the delegate that something has changed about what content the
  // TabContents is blocking.  Interested parties should call
  // TabContents::IsContentBlocked() to see if something they care about has
  // changed.
  virtual void OnContentSettingsChange(TabContents* source);

  // Check whether this contents is inside a window dedicated to running a web
  // application.
  virtual bool IsApplication() const;

  // Detach the given tab and convert it to a "webapp" view.  The tab must be
  // a TabContents with a valid WebApp set.
  virtual void ConvertContentsToApplication(TabContents* source);

  // Whether the specified tab can be reloaded.
  // Reloading can be disabled e. g. for the DevTools window.
  virtual bool CanReloadContents(TabContents* source) const;

  // Return the rect where to display the resize corner, if any, otherwise
  // an empty rect.
  virtual gfx::Rect GetRootWindowResizerRect() const;

  // Show a dialog with HTML content. |delegate| contains a pointer to the
  // delegate who knows how to display the dialog (which file URL and JSON
  // string input to use during initialization). |parent_window| is the window
  // that should be parent of the dialog, or NULL for the default.
  virtual void ShowHtmlDialog(HtmlDialogUIDelegate* delegate,
                              gfx::NativeWindow parent_window);

  // Tells us that we've finished firing this tab's beforeunload event.
  // The proceed bool tells us whether the user chose to proceed closing the
  // tab. Returns true if the tab can continue on firing it's unload event.
  // If we're closing the entire browser, then we'll want to delay firing
  // unload events until all the beforeunload events have fired.
  virtual void BeforeUnloadFired(TabContents* tab,
                                 bool proceed,
                                 bool* proceed_to_fire_unload);

  // Send IPC to external host. Default implementation is do nothing.
  virtual void ForwardMessageToExternalHost(const std::string& message,
                                            const std::string& origin,
                                            const std::string& target);

  // If the delegate is hosting tabs externally.
  virtual bool IsExternalTabContainer() const;

  // Sets focus to the location bar or some other place that is appropriate.
  // This is called when the tab wants to encourage user input, like for the
  // new tab page.
  virtual void SetFocusToLocationBar(bool select_all);

  // Called when a popup select is about to be displayed. The delegate can use
  // this to disable inactive rendering for the frame in the window the select
  // is opened within if necessary.
  virtual void RenderWidgetShowing();

  // This is used when the contents is an extension that needs to route
  // api calls through to the Browser process.
  virtual ExtensionFunctionDispatcher* CreateExtensionFunctionDispatcher(
      RenderViewHost* render_view_host,
      const std::string& extension_id);

  // This is called when webkit tells us that it is done tabbing through
  // controls on the page. Provides a way for TabContentsDelegates to handle
  // this. Returns true if the delegate successfully handled it.
  virtual bool TakeFocus(bool reverse);

  // Changes the blocked state of the tab at |index|. TabContents are
  // considered blocked while displaying a tab modal dialog. During that time
  // renderer host will ignore any UI interaction within TabContent outside of
  // the currently displaying dialog.
  virtual void SetTabContentBlocked(TabContents* contents, bool blocked);

  // Notification that |tab_contents| has gained focus.
  virtual void TabContentsFocused(TabContents* tab_content);

  // Return much extra vertical space should be allotted to the
  // render view widget during various animations (e.g. infobar closing).
  // This is used to make painting look smoother.
  virtual int GetExtraRenderViewHeight() const;

  virtual bool CanDownload(int request_id);

  virtual void OnStartDownload(DownloadItem* download);

  // Returns true if the context menu operation was handled by the delegate.
  virtual bool HandleContextMenu(const ContextMenuParams& params);

  // Returns true if the context menu command was handled
  virtual bool ExecuteContextMenuCommand(int command);

  // Shows a confirmation UI that the specified |template_url| is to be added as
  // a search engine.
  virtual void ConfirmAddSearchProvider(const TemplateURL* template_url,
                                        Profile* profile);

  // Shows the page info using the specified information.
  // |url| is the url of the page/frame the info applies to, |ssl| is the SSL
  // information for that page/frame.  If |show_history| is true, a section
  // showing how many times that URL has been visited is added to the page info.
  virtual void ShowPageInfo(Profile* profile,
                            const GURL& url,
                            const NavigationEntry::SSLStatus& ssl,
                            bool show_history);

  // Allows delegates to handle keyboard events before sending to the renderer.
  // Returns true if the |event| was handled. Otherwise, if the |event| would be
  // handled in HandleKeyboardEvent() method as a normal keyboard shortcut,
  // |*is_keyboard_shortcut| should be set to true.
  virtual bool PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                      bool* is_keyboard_shortcut);

  // Allows delegates to handle unhandled keyboard messages coming back from
  // the renderer.
  virtual void HandleKeyboardEvent(const NativeWebKeyboardEvent& event);

  // Shows the repost form confirmation dialog box.
  virtual void ShowRepostFormWarningDialog(TabContents* tab_contents);

  // Shows the Content Settings dialog for a given content type.
  virtual void ShowContentSettingsWindow(ContentSettingsType content_type);

  // Shows the cookies collected in the tab contents.
  virtual void ShowCollectedCookiesDialog(TabContents* tab_contents);

  // Allows delegate to override navigation to the history entries.
  // Returns true to allow TabContents to continue with the default processing.
  virtual bool OnGoToEntryOffset(int offset);

  // Returns whether this tab contents should add navigations to history.
  virtual bool ShouldAddNavigationsToHistory() const;

  // Notification when web app info data is available
  virtual void OnDidGetApplicationInfo(TabContents* tab_contents,
                                       int32 page_id);

  // Returns the browser in which the tab contents is being displayed.
  virtual Browser* GetBrowser();

  // Returns the native window framing the view containing the tab contents.
  virtual gfx::NativeWindow GetFrameNativeWindow();

  // Notifies the delegate about the creation of a new TabContents. This
  // typically happens when popups are created.
  virtual void TabContentsCreated(TabContents* new_contents);

  // Returns whether infobars are enabled. Overrideable by child classes.
  virtual bool infobars_enabled();

  // Whether the renderer should report its preferred size when it changes by
  // calling UpdatePreferredSize().
  // Note that this is set when the RenderViewHost is created and cannot be
  // changed after that.
  virtual bool ShouldEnablePreferredSizeNotifications();

  // Notification that the preferred size of the contents has changed.
  // Only called if ShouldEnablePreferredSizeNotifications() returns true.
  virtual void UpdatePreferredSize(const gfx::Size& pref_size);

 protected:
  ~TabContentsDelegate();
};

#endif  // CHROME_BROWSER_TAB_CONTENTS_TAB_CONTENTS_DELEGATE_H_
