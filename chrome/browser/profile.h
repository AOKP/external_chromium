// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class gathers state related to a single user profile.

#ifndef CHROME_BROWSER_PROFILE_H_
#define CHROME_BROWSER_PROFILE_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/timer.h"
#include "chrome/browser/spellcheck_host_observer.h"
#include "chrome/common/notification_registrar.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/preferences.h"
#endif

namespace history {
class TopSites;
}

namespace net {
class TransportSecurityState;
class SSLConfigService;
}

namespace webkit_database {
class DatabaseTracker;
}

class AutocompleteClassifier;
class BackgroundContentsService;
class BookmarkModel;
class BrowserThemeProvider;
class ChromeURLRequestContextGetter;
class DesktopNotificationService;
class DownloadManager;
class Extension;
class ExtensionDevToolsManager;
class ExtensionProcessManager;
class ExtensionMessageService;
class ExtensionsService;
class FaviconService;
class FindBarState;
class GeolocationContentSettingsMap;
class GeolocationPermissionContext;
class HistoryService;
class HostContentSettingsMap;
class HostZoomMap;
class NavigationController;
class NTPResourceCache;
class PasswordStore;
class PersonalDataManager;
class PinnedTabService;
class PrefService;
class ProfileSyncService;
class ProfileSyncFactory;
class SessionService;
class SpellCheckHost;
class SSLConfigServiceManager;
class SSLHostState;
class TransportSecurityPersister;
class SQLitePersistentCookieStore;
class TabRestoreService;
class TemplateURLFetcher;
class TemplateURLModel;
class ThemeProvider;
class TokenService;
class URLRequestContextGetter;
class UserScriptMaster;
class UserStyleSheetWatcher;
class VisitedLinkMaster;
class VisitedLinkEventListener;
class WebDataService;
class WebKitContext;
class WebResourceService;
class CloudPrintProxyService;

typedef intptr_t ProfileId;

class Profile {
 public:
  // Profile services are accessed with the following parameter. This parameter
  // defines what the caller plans to do with the service.
  // The caller is responsible for not performing any operation that would
  // result in persistent implicit records while using an OffTheRecord profile.
  // This flag allows the profile to perform an additional check.
  //
  // It also gives us an opportunity to perform further checks in the future. We
  // could, for example, return an history service that only allow some specific
  // methods.
  enum ServiceAccessType {
    // The caller plans to perform a read or write that takes place as a result
    // of the user input. Use this flag when the operation you are doing can be
    // performed while off the record. (ex: creating a bookmark)
    //
    // Since EXPLICIT_ACCESS means "as a result of a user action", this request
    // always succeeds.
    EXPLICIT_ACCESS,

    // The caller plans to call a method that will permanently change some data
    // in the profile, as part of Chrome's implicit data logging. Use this flag
    // when you are about to perform an operation which is incompatible with the
    // off the record mode.
    IMPLICIT_ACCESS
  };

  // Value that represents no profile Id.
  static const ProfileId InvalidProfileId;

  Profile() : restored_last_session_(false), accessibility_pause_level_(0) {}
  virtual ~Profile() {}

  // Profile prefs are registered as soon as the prefs are loaded for the first
  // time.
  static void RegisterUserPrefs(PrefService* prefs);

  // Create a new profile given a path.
  static Profile* CreateProfile(const FilePath& path);

  // Returns the request context for the "default" profile.  This may be called
  // from any thread.  This CAN return NULL if a first request context has not
  // yet been created.  If necessary, listen on the UI thread for
  // NOTIFY_DEFAULT_REQUEST_CONTEXT_AVAILABLE.
  static URLRequestContextGetter* GetDefaultRequestContext();

  // Returns a unique Id that can be used to identify this profile at runtime.
  // This Id is not persistent and will not survive a restart of the browser.
  virtual ProfileId GetRuntimeId() = 0;

  // Returns the path of the directory where this profile's data is stored.
  virtual FilePath GetPath() = 0;

  // Return whether this profile is off the record. Default is false.
  virtual bool IsOffTheRecord() = 0;

  // Return the off the record version of this profile. The returned pointer
  // is owned by the receiving profile. If the receiving profile is off the
  // record, the same profile is returned.
  virtual Profile* GetOffTheRecordProfile() = 0;

  // Destroys the off the record profile.
  virtual void DestroyOffTheRecordProfile() = 0;

  // True if an off the record profile exists.
  virtual bool HasOffTheRecordProfile() = 0;

  // Return the original "recording" profile. This method returns this if the
  // profile is not off the record.
  virtual Profile* GetOriginalProfile() = 0;

  // Returns a pointer to the DatabaseTracker instance for this profile.
  virtual webkit_database::DatabaseTracker* GetDatabaseTracker() = 0;

  // Returns a pointer to the TopSites (thumbnail manager) instance
  // for this profile.
  virtual history::TopSites* GetTopSites() = 0;

  // Retrieves a pointer to the VisitedLinkMaster associated with this
  // profile.  The VisitedLinkMaster is lazily created the first time
  // that this method is called.
  virtual VisitedLinkMaster* GetVisitedLinkMaster() = 0;

  // Retrieves a pointer to the ExtensionsService associated with this
  // profile. The ExtensionsService is created at startup.
  virtual ExtensionsService* GetExtensionsService() = 0;

  // Retrieves a pointer to the UserScriptMaster associated with this
  // profile.  The UserScriptMaster is lazily created the first time
  // that this method is called.
  virtual UserScriptMaster* GetUserScriptMaster() = 0;

  // Retrieves a pointer to the ExtensionDevToolsManager associated with this
  // profile.  The instance is created at startup.
  virtual ExtensionDevToolsManager* GetExtensionDevToolsManager() = 0;

  // Retrieves a pointer to the ExtensionProcessManager associated with this
  // profile.  The instance is created at startup.
  virtual ExtensionProcessManager* GetExtensionProcessManager() = 0;

  // Retrieves a pointer to the ExtensionMessageService associated with this
  // profile.  The instance is created at startup.
  virtual ExtensionMessageService* GetExtensionMessageService() = 0;

  // Retrieves a pointer to the SSLHostState associated with this profile.
  // The SSLHostState is lazily created the first time that this method is
  // called.
  virtual SSLHostState* GetSSLHostState() = 0;

  // Retrieves a pointer to the TransportSecurityState associated with
  // this profile.  The TransportSecurityState is lazily created the
  // first time that this method is called.
  virtual net::TransportSecurityState*
      GetTransportSecurityState() = 0;

  // Retrieves a pointer to the FaviconService associated with this
  // profile.  The FaviconService is lazily created the first time
  // that this method is called.
  //
  // Although FaviconService is refcounted, this will not addref, and callers
  // do not need to do any reference counting as long as they keep the pointer
  // only for the local scope (which they should do anyway since the browser
  // process may decide to shut down).
  //
  // |access| defines what the caller plans to do with the service. See
  // the ServiceAccessType definition above.
  virtual FaviconService* GetFaviconService(ServiceAccessType access) = 0;

  // Retrieves a pointer to the HistoryService associated with this
  // profile.  The HistoryService is lazily created the first time
  // that this method is called.
  //
  // Although HistoryService is refcounted, this will not addref, and callers
  // do not need to do any reference counting as long as they keep the pointer
  // only for the local scope (which they should do anyway since the browser
  // process may decide to shut down).
  //
  // |access| defines what the caller plans to do with the service. See
  // the ServiceAccessType definition above.
  virtual HistoryService* GetHistoryService(ServiceAccessType access) = 0;

  // Similar to GetHistoryService(), but won't create the history service if it
  // doesn't already exist.
  virtual HistoryService* GetHistoryServiceWithoutCreating() = 0;

  // Retrieves a pointer to the AutocompleteClassifier associated with this
  // profile. The AutocompleteClassifier is lazily created the first time that
  // this method is called.
  virtual AutocompleteClassifier* GetAutocompleteClassifier() = 0;

  // Returns the WebDataService for this profile. This is owned by
  // the Profile. Callers that outlive the life of this profile need to be
  // sure they refcount the returned value.
  //
  // |access| defines what the caller plans to do with the service. See
  // the ServiceAccessType definition above.
  virtual WebDataService* GetWebDataService(ServiceAccessType access) = 0;

  // Similar to GetWebDataService(), but won't create the web data service if it
  // doesn't already exist.
  virtual WebDataService* GetWebDataServiceWithoutCreating() = 0;

  // Returns the PasswordStore for this profile. This is owned by the Profile.
  virtual PasswordStore* GetPasswordStore(ServiceAccessType access) = 0;

  // Retrieves a pointer to the PrefService that manages the preferences
  // for this user profile.  The PrefService is lazily created the first
  // time that this method is called.
  virtual PrefService* GetPrefs() = 0;

  // Returns the TemplateURLModel for this profile. This is owned by the
  // the Profile.
  virtual TemplateURLModel* GetTemplateURLModel() = 0;

  // Returns the TemplateURLFetcher for this profile. This is owned by the
  // profile.
  virtual TemplateURLFetcher* GetTemplateURLFetcher() = 0;

  // Returns the DownloadManager associated with this profile.
  virtual DownloadManager* GetDownloadManager() = 0;
  virtual bool HasCreatedDownloadManager() const = 0;

  // Returns the PersonalDataManager associated with this profile.
  virtual PersonalDataManager* GetPersonalDataManager() = 0;

  // Init our themes system.
  virtual void InitThemes() = 0;

  // Set the theme to the specified extension.
  virtual void SetTheme(Extension* extension) = 0;

  // Set the theme to the machine's native theme.
  virtual void SetNativeTheme() = 0;

  // Clear the theme and reset it to default.
  virtual void ClearTheme() = 0;

  // Gets the theme that was last set. Returns NULL if the theme is no longer
  // installed, if there is no installed theme, or the theme was cleared.
  virtual Extension* GetTheme() = 0;

  // Returns or creates the ThemeProvider associated with this profile
  virtual BrowserThemeProvider* GetThemeProvider() = 0;

  // Returns the request context information associated with this profile.  Call
  // this only on the UI thread, since it can send notifications that should
  // happen on the UI thread.
  virtual URLRequestContextGetter* GetRequestContext() = 0;

  // Returns the request context for media resources asociated with this
  // profile.
  virtual URLRequestContextGetter* GetRequestContextForMedia() = 0;

  // Returns the request context used for extension-related requests.  This
  // is only used for a separate cookie store currently.
  virtual URLRequestContextGetter* GetRequestContextForExtensions() = 0;

  // Called by the ExtensionsService that lives in this profile. Gives the
  // profile a chance to react to the load event before the EXTENSION_LOADED
  // notification has fired. The purpose for handling this event first is to
  // avoid race conditions by making sure URLRequestContexts learn about new
  // extensions before anything else needs them to know.
  virtual void RegisterExtensionWithRequestContexts(Extension* extension) {}

  // Called by the ExtensionsService that lives in this profile. Lets the
  // profile clean up its RequestContexts once all the listeners to the
  // EXTENSION_UNLOADED notification have finished running.
  virtual void UnregisterExtensionWithRequestContexts(Extension* extension) {}

  // Returns the SSLConfigService for this profile.
  virtual net::SSLConfigService* GetSSLConfigService() = 0;

  // Returns the Hostname <-> Content settings map for this profile.
  virtual HostContentSettingsMap* GetHostContentSettingsMap() = 0;

  // Returns the Hostname <-> Zoom Level map for this profile.
  virtual HostZoomMap* GetHostZoomMap() = 0;

  // Returns the geolocation settings map for this profile.
  virtual GeolocationContentSettingsMap* GetGeolocationContentSettingsMap() = 0;

  // Returns the geolocation permission context for this profile.
  virtual GeolocationPermissionContext* GetGeolocationPermissionContext() = 0;

  // Returns the user style sheet watcher.
  virtual UserStyleSheetWatcher* GetUserStyleSheetWatcher() = 0;

  // Returns the find bar state for this profile.  The find bar state is lazily
  // created the first time that this method is called.
  virtual FindBarState* GetFindBarState() = 0;

  // Returns the session service for this profile. This may return NULL. If
  // this profile supports a session service (it isn't off the record), and
  // the session service hasn't yet been created, this forces creation of
  // the session service.
  //
  // This returns NULL in two situations: the profile is off the record, or the
  // session service has been explicitly shutdown (browser is exiting). Callers
  // should always check the return value for NULL.
  virtual SessionService* GetSessionService() = 0;

  // If this profile has a session service, it is shut down. To properly record
  // the current state this forces creation of the session service, then shuts
  // it down.
  virtual void ShutdownSessionService() = 0;

  // Returns true if this profile has a session service.
  virtual bool HasSessionService() const = 0;

  // Returns true if the last time this profile was open it was exited cleanly.
  virtual bool DidLastSessionExitCleanly() = 0;

  // Returns the BookmarkModel, creating if not yet created.
  virtual BookmarkModel* GetBookmarkModel() = 0;

  // Returns the Gaia Token Service, creating if not yet created.
  virtual TokenService* GetTokenService() = 0;

  // Returns the ProfileSyncService, creating if not yet created.
  virtual ProfileSyncService* GetProfileSyncService() = 0;

  // Returns the CloudPrintProxyService, creating if not yet created.
  virtual CloudPrintProxyService* GetCloudPrintProxyService() = 0;

  // Return whether 2 profiles are the same. 2 profiles are the same if they
  // represent the same profile. This can happen if there is pointer equality
  // or if one profile is the off the record version of another profile (or vice
  // versa).
  virtual bool IsSameProfile(Profile* profile) = 0;

  // Returns the time the profile was started. This is not the time the profile
  // was created, rather it is the time the user started chrome and logged into
  // this profile. For the single profile case, this corresponds to the time
  // the user started chrome.
  virtual base::Time GetStartTime() const = 0;

  // Returns the TabRestoreService. This returns NULL when off the record.
  virtual TabRestoreService* GetTabRestoreService() = 0;

  virtual void ResetTabRestoreService() = 0;

  // May return NULL.
  virtual SpellCheckHost* GetSpellCheckHost() = 0;

  // If |force| is false, and the spellchecker is already initialized (or is in
  // the process of initializing), then do nothing. Otherwise clobber the
  // current spellchecker and replace it with a new one.
  virtual void ReinitializeSpellCheckHost(bool force) = 0;

  // Returns the WebKitContext assigned to this profile.
  virtual WebKitContext* GetWebKitContext() = 0;

  // Returns the provider of desktop notifications for this profile.
  virtual DesktopNotificationService* GetDesktopNotificationService() = 0;

  // Returns the service that manages BackgroundContents for this profile.
  virtual BackgroundContentsService* GetBackgroundContentsService() = 0;

  // Marks the profile as cleanly shutdown.
  //
  // NOTE: this is invoked internally on a normal shutdown, but is public so
  // that it can be invoked when the user logs out/powers down (WM_ENDSESSION).
  virtual void MarkAsCleanShutdown() = 0;

  virtual void InitExtensions() = 0;

  // Start up service that gathers data from web resource feeds.
  virtual void InitWebResources() = 0;

  // Returns the new tab page resource cache.
  virtual NTPResourceCache* GetNTPResourceCache() = 0;

  // Returns the last directory that was chosen for uploading or opening a file.
  virtual FilePath last_selected_directory() = 0;
  virtual void set_last_selected_directory(const FilePath& path) = 0;

#ifdef UNIT_TEST
  // Use with caution.  GetDefaultRequestContext may be called on any thread!
  static void set_default_request_context(URLRequestContextGetter* c) {
    default_request_context_ = c;
  }
#endif

  // Did the user restore the last session? This is set by SessionRestore.
  void set_restored_last_session(bool restored_last_session) {
    restored_last_session_ = restored_last_session;
  }
  bool restored_last_session() const {
    return restored_last_session_;
  }

  // Stop sending accessibility events until ResumeAccessibilityEvents().
  // Calls to Pause nest; no events will be sent until the number of
  // Resume calls matches the number of Pause calls received.
  void PauseAccessibilityEvents() {
    accessibility_pause_level_++;
  }

  void ResumeAccessibilityEvents() {
    DCHECK(accessibility_pause_level_ > 0);
    accessibility_pause_level_--;
  }

  bool ShouldSendAccessibilityEvents() {
    return 0 == accessibility_pause_level_;
  }

  // Checks whether sync is configurable by the user. Returns false if sync is
  // disabled or controlled by configuration management.
  bool IsSyncAccessible();

 protected:
  static URLRequestContextGetter* default_request_context_;

 private:
  bool restored_last_session_;

  // Accessibility events will only be propagated when the pause
  // level is zero.  PauseAccessibilityEvents and ResumeAccessibilityEvents
  // increment and decrement the level, respectively, rather than set it to
  // true or false, so that calls can be nested.
  int accessibility_pause_level_;
};

class OffTheRecordProfileImpl;

// The default profile implementation.
class ProfileImpl : public Profile,
                    public SpellCheckHostObserver,
                    public NotificationObserver {
 public:
  virtual ~ProfileImpl();

  // Profile implementation.
  virtual ProfileId GetRuntimeId();
  virtual FilePath GetPath();
  virtual bool IsOffTheRecord();
  virtual Profile* GetOffTheRecordProfile();
  virtual void DestroyOffTheRecordProfile();
  virtual bool HasOffTheRecordProfile();
  virtual Profile* GetOriginalProfile();
  virtual webkit_database::DatabaseTracker* GetDatabaseTracker();
  virtual history::TopSites* GetTopSites();
  virtual VisitedLinkMaster* GetVisitedLinkMaster();
  virtual UserScriptMaster* GetUserScriptMaster();
  virtual SSLHostState* GetSSLHostState();
  virtual net::TransportSecurityState* GetTransportSecurityState();
  virtual ExtensionsService* GetExtensionsService();
  virtual ExtensionDevToolsManager* GetExtensionDevToolsManager();
  virtual ExtensionProcessManager* GetExtensionProcessManager();
  virtual ExtensionMessageService* GetExtensionMessageService();
  virtual FaviconService* GetFaviconService(ServiceAccessType sat);
  virtual HistoryService* GetHistoryService(ServiceAccessType sat);
  virtual HistoryService* GetHistoryServiceWithoutCreating();
  virtual AutocompleteClassifier* GetAutocompleteClassifier();
  virtual WebDataService* GetWebDataService(ServiceAccessType sat);
  virtual WebDataService* GetWebDataServiceWithoutCreating();
  virtual PasswordStore* GetPasswordStore(ServiceAccessType sat);
  virtual PrefService* GetPrefs();
  virtual TemplateURLModel* GetTemplateURLModel();
  virtual TemplateURLFetcher* GetTemplateURLFetcher();
  virtual DownloadManager* GetDownloadManager();
  virtual PersonalDataManager* GetPersonalDataManager();
  virtual void InitThemes();
  virtual void SetTheme(Extension* extension);
  virtual void SetNativeTheme();
  virtual void ClearTheme();
  virtual Extension* GetTheme();
  virtual BrowserThemeProvider* GetThemeProvider();
  virtual bool HasCreatedDownloadManager() const;
  virtual URLRequestContextGetter* GetRequestContext();
  virtual URLRequestContextGetter* GetRequestContextForMedia();
  virtual URLRequestContextGetter* GetRequestContextForExtensions();
  virtual void RegisterExtensionWithRequestContexts(Extension* extension);
  virtual void UnregisterExtensionWithRequestContexts(Extension* extension);
  virtual net::SSLConfigService* GetSSLConfigService();
  virtual HostContentSettingsMap* GetHostContentSettingsMap();
  virtual HostZoomMap* GetHostZoomMap();
  virtual GeolocationContentSettingsMap* GetGeolocationContentSettingsMap();
  virtual GeolocationPermissionContext* GetGeolocationPermissionContext();
  virtual UserStyleSheetWatcher* GetUserStyleSheetWatcher();
  virtual FindBarState* GetFindBarState();
  virtual SessionService* GetSessionService();
  virtual void ShutdownSessionService();
  virtual bool HasSessionService() const;
  virtual bool DidLastSessionExitCleanly();
  virtual BookmarkModel* GetBookmarkModel();
  virtual bool IsSameProfile(Profile* profile);
  virtual base::Time GetStartTime() const;
  virtual TabRestoreService* GetTabRestoreService();
  virtual void ResetTabRestoreService();
  virtual SpellCheckHost* GetSpellCheckHost();
  virtual void ReinitializeSpellCheckHost(bool force);
  virtual WebKitContext* GetWebKitContext();
  virtual DesktopNotificationService* GetDesktopNotificationService();
  virtual BackgroundContentsService* GetBackgroundContentsService();
  virtual void MarkAsCleanShutdown();
  virtual void InitExtensions();
  virtual void InitWebResources();
  virtual NTPResourceCache* GetNTPResourceCache();
  virtual FilePath last_selected_directory();
  virtual void set_last_selected_directory(const FilePath& path);
  virtual ProfileSyncService* GetProfileSyncService();
  virtual TokenService* GetTokenService();
  void InitSyncService();
  virtual CloudPrintProxyService* GetCloudPrintProxyService();
  void InitCloudPrintProxyService();

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // SpellCheckHostObserver implementation.
  virtual void SpellCheckHostInitialized();

 private:
  friend class Profile;

  explicit ProfileImpl(const FilePath& path);

  void CreateWebDataService();
  FilePath GetPrefFilePath();

  void CreatePasswordStore();

  void StopCreateSessionServiceTimer();

  void EnsureRequestContextCreated() {
    GetRequestContext();
  }

  void EnsureSessionServiceCreated() {
    GetSessionService();
  }

  NotificationRegistrar registrar_;

  FilePath path_;
  FilePath base_cache_path_;
  scoped_ptr<VisitedLinkEventListener> visited_link_event_listener_;
  scoped_ptr<VisitedLinkMaster> visited_link_master_;
  scoped_refptr<ExtensionsService> extensions_service_;
  scoped_refptr<UserScriptMaster> user_script_master_;
  scoped_refptr<ExtensionDevToolsManager> extension_devtools_manager_;
  scoped_ptr<ExtensionProcessManager> extension_process_manager_;
  scoped_refptr<ExtensionMessageService> extension_message_service_;
  scoped_ptr<SSLHostState> ssl_host_state_;
  scoped_refptr<net::TransportSecurityState>
      transport_security_state_;
  scoped_refptr<TransportSecurityPersister>
      transport_security_persister_;
  scoped_ptr<PrefService> prefs_;
  scoped_ptr<TemplateURLFetcher> template_url_fetcher_;
  scoped_ptr<TemplateURLModel> template_url_model_;
  scoped_ptr<BookmarkModel> bookmark_bar_model_;
  scoped_refptr<WebResourceService> web_resource_service_;
  scoped_ptr<NTPResourceCache> ntp_resource_cache_;

  scoped_ptr<TokenService> token_service_;
  scoped_ptr<ProfileSyncFactory> profile_sync_factory_;
  scoped_ptr<ProfileSyncService> sync_service_;
  scoped_ptr<CloudPrintProxyService> cloud_print_proxy_service_;

  scoped_refptr<ChromeURLRequestContextGetter> request_context_;

  scoped_refptr<ChromeURLRequestContextGetter> media_request_context_;

  scoped_refptr<ChromeURLRequestContextGetter> extensions_request_context_;

  scoped_ptr<SSLConfigServiceManager> ssl_config_service_manager_;

  scoped_refptr<HostContentSettingsMap> host_content_settings_map_;
  scoped_refptr<HostZoomMap> host_zoom_map_;
  scoped_refptr<GeolocationContentSettingsMap>
      geolocation_content_settings_map_;
  scoped_refptr<GeolocationPermissionContext>
      geolocation_permission_context_;
  scoped_refptr<UserStyleSheetWatcher> user_style_sheet_watcher_;
  scoped_ptr<FindBarState> find_bar_state_;
  scoped_refptr<DownloadManager> download_manager_;
  scoped_refptr<HistoryService> history_service_;
  scoped_refptr<FaviconService> favicon_service_;
  scoped_ptr<AutocompleteClassifier> autocomplete_classifier_;
  scoped_refptr<WebDataService> web_data_service_;
  scoped_refptr<PasswordStore> password_store_;
  scoped_refptr<SessionService> session_service_;
  scoped_ptr<BrowserThemeProvider> theme_provider_;
  scoped_refptr<WebKitContext> webkit_context_;
  scoped_ptr<DesktopNotificationService> desktop_notification_service_;
  scoped_ptr<BackgroundContentsService> background_contents_service_;
  scoped_refptr<PersonalDataManager> personal_data_manager_;
  scoped_ptr<PinnedTabService> pinned_tab_service_;
  bool history_service_created_;
  bool favicon_service_created_;
  bool created_web_data_service_;
  bool created_password_store_;
  bool created_download_manager_;
  bool created_theme_provider_;
  // Whether or not the last session exited cleanly. This is set only once.
  bool last_session_exited_cleanly_;

  base::OneShotTimer<ProfileImpl> create_session_service_timer_;

  scoped_ptr<OffTheRecordProfileImpl> off_the_record_profile_;

  // See GetStartTime for details.
  base::Time start_time_;

  scoped_refptr<TabRestoreService> tab_restore_service_;

  scoped_refptr<SpellCheckHost> spellcheck_host_;

  // Indicates whether |spellcheck_host_| has told us initialization is
  // finished.
  bool spellcheck_host_ready_;

  // Set to true when ShutdownSessionService is invoked. If true
  // GetSessionService won't recreate the SessionService.
  bool shutdown_session_service_;

  // The main database tracker for this profile.
  // Should be used only on the file thread.
  scoped_refptr<webkit_database::DatabaseTracker> db_tracker_;

  scoped_refptr<history::TopSites> top_sites_;  // For history and thumbnails.

#if defined(OS_CHROMEOS)
  chromeos::Preferences chromeos_preferences_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ProfileImpl);
};

#endif  // CHROME_BROWSER_PROFILE_H_
