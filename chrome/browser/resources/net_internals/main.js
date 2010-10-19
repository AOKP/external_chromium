// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Dictionary of constants (initialized by browser).
 */
var LogEventType = null;
var LogEventPhase = null;
var ClientInfo = null;
var LogSourceType = null;
var NetError = null;
var LoadFlag = null;

/**
 * Object to communicate between the renderer and the browser.
 * @type {!BrowserBridge}
 */
var g_browser = null;

/**
 * Main entry point. called once the page has loaded.
 */
function onLoaded() {
  g_browser = new BrowserBridge();

  // Create the view which displays events lists, and lets you select, filter
  // and delete them.
  var eventsView = new EventsView('eventsListTableBody',
                                  'filterInput',
                                  'filterCount',
                                  'deleteSelected',
                                  'deleteAll',
                                  'selectAll',
                                  'sortById',
                                  'sortBySource',
                                  'sortByDescription',

                                  // IDs for the details view.
                                  "detailsTabHandles",
                                  "detailsLogTab",
                                  "detailsTimelineTab",
                                  "detailsLogBox",
                                  "detailsTimelineBox",

                                  // IDs for the layout boxes.
                                  "filterBox",
                                  "eventsBox",
                                  "actionBox",
                                  "splitterBox");

  // Create a view which will display info on the proxy setup.
  var proxyView = new ProxyView("proxyTabContent",
                                "proxyOriginalSettings",
                                "proxyEffectiveSettings",
                                "proxyReloadSettings",
                                "badProxiesTableBody",
                                "clearBadProxies");

  // Create a view which will display information on the host resolver.
  var dnsView = new DnsView("dnsTabContent",
                            "hostResolverCacheTbody",
                            "clearHostResolverCache",
                            "hostResolverCacheCapacity",
                            "hostResolverCacheTTLSuccess",
                            "hostResolverCacheTTLFailure");

  // Create a view which will display import/export options to control the
  // captured data.
  var dataView = new DataView("dataTabContent", "exportedDataText",
                              "exportToText", "securityStrippingCheckbox",
                              "passivelyCapturedCount",
                              "activelyCapturedCount",
                              "dataViewDeleteAll");

  // Create a view which will display the results and controls for connection
  // tests.
  var testView = new TestView("testTabContent", "testUrlInput",
                              "connectionTestsForm", "testSummary");

  var httpCacheView = new HttpCacheView("httpCacheTabContent",
                                        "httpCacheStats");

  var socketsView = new SocketsView("socketsTabContent",
                                    "socketPoolDiv",
                                    "socketPoolGroupsDiv");


  var serviceView;
  if (g_browser.isPlatformWindows()) {
    serviceView = new ServiceProvidersView("serviceProvidersTab",
                                           "serviceProvidersTabContent",
                                           "serviceProvidersTbody",
                                           "namespaceProvidersTbody");
  }

  // Create a view which lets you tab between the different sub-views.
  var categoryTabSwitcher =
      new TabSwitcherView(new DivView('categoryTabHandles'));

  // Populate the main tabs.
  categoryTabSwitcher.addTab('eventsTab', eventsView, false);
  categoryTabSwitcher.addTab('proxyTab', proxyView, false);
  categoryTabSwitcher.addTab('dnsTab', dnsView, false);
  categoryTabSwitcher.addTab('socketsTab', socketsView, false);
  categoryTabSwitcher.addTab('httpCacheTab', httpCacheView, false);
  categoryTabSwitcher.addTab('dataTab', dataView, false);
  if (g_browser.isPlatformWindows())
    categoryTabSwitcher.addTab('serviceProvidersTab', serviceView, false);
  categoryTabSwitcher.addTab('testTab', testView, false);

  // Build a map from the anchor name of each tab handle to its "tab ID".
  // We will consider navigations to the #hash as a switch tab request.
  var anchorMap = {};
  var tabIds = categoryTabSwitcher.getAllTabIds();
  for (var i = 0; i < tabIds.length; ++i) {
    var aNode = document.getElementById(tabIds[i]);
    anchorMap[aNode.hash] = tabIds[i];
  }
  // Default the empty hash to the data tab.
  anchorMap['#'] = anchorMap[''] = 'dataTab';

  window.onhashchange = function() {
    var tabId = anchorMap[window.location.hash];
    if (tabId)
      categoryTabSwitcher.switchToTab(tabId);
  };

  // Make this category tab widget the primary view, that fills the whole page.
  var windowView = new WindowView(categoryTabSwitcher);

  // Trigger initial layout.
  windowView.resetGeometry();

  // Select the initial view based on the current URL.
  window.onhashchange();

  // Tell the browser that we are ready to start receiving log events.
  g_browser.sendReady();
}

/**
 * This class provides a "bridge" for communicating between the javascript and
 * the browser.
 *
 * @constructor
 */
function BrowserBridge() {
  // List of observers for various bits of browser state.
  this.logObservers_ = [];
  this.connectionTestsObservers_ = [];
  this.proxySettings_ = new PollableDataHelper('onProxySettingsChanged');
  this.badProxies_ = new PollableDataHelper('onBadProxiesChanged');
  this.httpCacheInfo_ = new PollableDataHelper('onHttpCacheInfoChanged');
  this.hostResolverCache_ =
      new PollableDataHelper('onHostResolverCacheChanged');
  this.socketPoolInfo_ = new PollableDataHelper('onSocketPoolInfoChanged');
  this.serviceProviders_ = new PollableDataHelper('onServiceProvidersChanged');

  // Cache of the data received.
  this.numPassivelyCapturedEvents_ = 0;
  this.capturedEvents_ = [];

  // Next unique id to be assigned to a log entry without a source.
  // Needed to simplify deletion, identify associated GUI elements, etc.
  this.nextSourcelessEventId_ = -1;
}

/**
 * Delay in milliseconds between polling of certain browser information.
 */
BrowserBridge.POLL_INTERVAL_MS = 5000;

//------------------------------------------------------------------------------
// Messages sent to the browser
//------------------------------------------------------------------------------

BrowserBridge.prototype.sendReady = function() {
  chrome.send('notifyReady');

  // Some of the data we are interested is not currently exposed as a stream,
  // so we will poll the browser to find out when it changes and then notify
  // the observers.
  window.setInterval(
      this.doPolling_.bind(this), BrowserBridge.POLL_INTERVAL_MS);
};

BrowserBridge.prototype.isPlatformWindows = function() {
  return /Win/.test(navigator.platform);
};

BrowserBridge.prototype.sendGetProxySettings = function() {
  // The browser will call receivedProxySettings on completion.
  chrome.send('getProxySettings');
};

BrowserBridge.prototype.sendReloadProxySettings = function() {
  chrome.send('reloadProxySettings');
};

BrowserBridge.prototype.sendGetBadProxies = function() {
  // The browser will call receivedBadProxies on completion.
  chrome.send('getBadProxies');
};

BrowserBridge.prototype.sendGetHostResolverCache = function() {
  // The browser will call receivedHostResolverCache on completion.
  chrome.send('getHostResolverCache');
};

BrowserBridge.prototype.sendClearBadProxies = function() {
  chrome.send('clearBadProxies');
};

BrowserBridge.prototype.sendClearHostResolverCache = function() {
  chrome.send('clearHostResolverCache');
};

BrowserBridge.prototype.sendStartConnectionTests = function(url) {
  chrome.send('startConnectionTests', [url]);
};

BrowserBridge.prototype.sendGetHttpCacheInfo = function() {
  chrome.send('getHttpCacheInfo');
};

BrowserBridge.prototype.sendGetSocketPoolInfo = function() {
  chrome.send('getSocketPoolInfo');
};

BrowserBridge.prototype.sendGetServiceProviders = function() {
  chrome.send('getServiceProviders');
};

//------------------------------------------------------------------------------
// Messages received from the browser
//------------------------------------------------------------------------------

BrowserBridge.prototype.receivedLogEntry = function(logEntry) {
  // Silently drop entries received before ready to receive them.
  if (!this.areLogTypesReady_())
    return;
  // Assign unique ID, if needed.
  if (logEntry.source.id == 0) {
    logEntry.source.id = this.nextSourcelessEventId_;
    --this.nextSourcelessEventId_;
  }
  this.capturedEvents_.push(logEntry);
  for (var i = 0; i < this.logObservers_.length; ++i)
    this.logObservers_[i].onLogEntryAdded(logEntry);
};

BrowserBridge.prototype.receivedLogEventTypeConstants = function(constantsMap) {
  LogEventType = constantsMap;
};

BrowserBridge.prototype.receivedClientInfo =
function(info) {
  ClientInfo = info;
};

BrowserBridge.prototype.receivedLogEventPhaseConstants =
function(constantsMap) {
  LogEventPhase = constantsMap;
};

BrowserBridge.prototype.receivedLogSourceTypeConstants =
function(constantsMap) {
  LogSourceType = constantsMap;
};

BrowserBridge.prototype.receivedLoadFlagConstants = function(constantsMap) {
  LoadFlag = constantsMap;
};

BrowserBridge.prototype.receivedNetErrorConstants = function(constantsMap) {
  NetError = constantsMap;
};

BrowserBridge.prototype.receivedTimeTickOffset = function(timeTickOffset) {
  this.timeTickOffset_ = timeTickOffset;
};

BrowserBridge.prototype.receivedProxySettings = function(proxySettings) {
  this.proxySettings_.update(proxySettings);
};

BrowserBridge.prototype.receivedBadProxies = function(badProxies) {
  this.badProxies_.update(badProxies);
};

BrowserBridge.prototype.receivedHostResolverCache =
function(hostResolverCache) {
  this.hostResolverCache_.update(hostResolverCache);
};

BrowserBridge.prototype.receivedSocketPoolInfo = function(socketPoolInfo) {
  this.socketPoolInfo_.update(socketPoolInfo);
};

BrowserBridge.prototype.receivedServiceProviders = function(serviceProviders) {
  this.serviceProviders_.update(serviceProviders);
};

BrowserBridge.prototype.receivedPassiveLogEntries = function(entries) {
  this.numPassivelyCapturedEvents_ += entries.length;
  for (var i = 0; i < entries.length; ++i) {
    var entry = entries[i];
    entry.wasPassivelyCaptured = true;
    this.receivedLogEntry(entry);
  }
};


BrowserBridge.prototype.receivedStartConnectionTestSuite = function() {
  for (var i = 0; i < this.connectionTestsObservers_.length; ++i)
    this.connectionTestsObservers_[i].onStartedConnectionTestSuite();
};

BrowserBridge.prototype.receivedStartConnectionTestExperiment = function(
    experiment) {
  for (var i = 0; i < this.connectionTestsObservers_.length; ++i) {
    this.connectionTestsObservers_[i].onStartedConnectionTestExperiment(
        experiment);
  }
};

BrowserBridge.prototype.receivedCompletedConnectionTestExperiment =
function(info) {
  for (var i = 0; i < this.connectionTestsObservers_.length; ++i) {
    this.connectionTestsObservers_[i].onCompletedConnectionTestExperiment(
        info.experiment, info.result);
  }
};

BrowserBridge.prototype.receivedCompletedConnectionTestSuite = function() {
  for (var i = 0; i < this.connectionTestsObservers_.length; ++i)
    this.connectionTestsObservers_[i].onCompletedConnectionTestSuite();
};

BrowserBridge.prototype.receivedHttpCacheInfo = function(info) {
  this.httpCacheInfo_.update(info);
};

BrowserBridge.prototype.areLogTypesReady_ = function() {
  return (LogEventType  != null &&
          LogEventPhase != null &&
          LogSourceType != null);
}

//------------------------------------------------------------------------------

/**
 * Adds a listener of log entries. |observer| will be called back when new log
 * data arrives, through:
 *
 *   observer.onLogEntryAdded(logEntry)
 */
BrowserBridge.prototype.addLogObserver = function(observer) {
  this.logObservers_.push(observer);
};

/**
 * Adds a listener of the proxy settings. |observer| will be called back when
 * data is received, through:
 *
 *   observer.onProxySettingsChanged(proxySettings)
 *
 * |proxySettings| is a dictionary with (up to) two properties:
 *
 *   "original"  -- The settings that chrome was configured to use
 *                  (i.e. system settings.)
 *   "effective" -- The "effective" proxy settings that chrome is using.
 *                  (decides between the manual/automatic modes of the
 *                  fetched settings).
 *
 * Each of these two configurations is formatted as a string, and may be
 * omitted if not yet initialized.
 *
 * TODO(eroman): send a dictionary instead.
 */
BrowserBridge.prototype.addProxySettingsObserver = function(observer) {
  this.proxySettings_.addObserver(observer);
};

/**
 * Adds a listener of the proxy settings. |observer| will be called back when
 * data is received, through:
 *
 *   observer.onBadProxiesChanged(badProxies)
 *
 * |badProxies| is an array, where each entry has the property:
 *   badProxies[i].proxy_uri: String identify the proxy.
 *   badProxies[i].bad_until: The time when the proxy stops being considered
 *                            bad. Note the time is in time ticks.
 */
BrowserBridge.prototype.addBadProxiesObsever = function(observer) {
  this.badProxies_.addObserver(observer);
};

/**
 * Adds a listener of the host resolver cache. |observer| will be called back
 * when data is received, through:
 *
 *   observer.onHostResolverCacheChanged(hostResolverCache)
 */
BrowserBridge.prototype.addHostResolverCacheObserver = function(observer) {
  this.hostResolverCache_.addObserver(observer);
};

/**
 * Adds a listener of the socket pool. |observer| will be called back
 * when data is received, through:
 *
 *   observer.onSocketPoolInfoChanged(socketPoolInfo)
 */
BrowserBridge.prototype.addSocketPoolInfoObserver = function(observer) {
  this.socketPoolInfo_.addObserver(observer);
};

/**
 * Adds a listener of the service providers info. |observer| will be called
 * back when data is received, through:
 *
 *   observer.onServiceProvidersChanged(serviceProviders)
 */
BrowserBridge.prototype.addServiceProvidersObserver = function(observer) {
  this.serviceProviders_.addObserver(observer);
};

/**
 * Adds a listener for the progress of the connection tests.
 * The observer will be called back with:
 *
 *   observer.onStartedConnectionTestSuite();
 *   observer.onStartedConnectionTestExperiment(experiment);
 *   observer.onCompletedConnectionTestExperiment(experiment, result);
 *   observer.onCompletedConnectionTestSuite();
 */
BrowserBridge.prototype.addConnectionTestsObserver = function(observer) {
  this.connectionTestsObservers_.push(observer);
};

/**
 * Adds a listener for the http cache info results.
 * The observer will be called back with:
 *
 *   observer.onHttpCacheInfoChanged(info);
 */
BrowserBridge.prototype.addHttpCacheInfoObserver = function(observer) {
  this.httpCacheInfo_.addObserver(observer);
};

/**
 * The browser gives us times in terms of "time ticks" in milliseconds.
 * This function converts the tick count to a Date() object.
 *
 * @param {String} timeTicks.
 * @returns {Date} The time that |timeTicks| represents.
 */
BrowserBridge.prototype.convertTimeTicksToDate = function(timeTicks) {
  // Note that the subtraction by 0 is to cast to a number (probably a float
  // since the numbers are big).
  var timeStampMs = (this.timeTickOffset_ - 0) + (timeTicks - 0);
  var d = new Date();
  d.setTime(timeStampMs);
  return d;
};

/**
 * Returns a list of all captured events.
 */
BrowserBridge.prototype.getAllCapturedEvents = function() {
  return this.capturedEvents_;
};

/**
 * Returns the number of events that were captured while we were
 * listening for events.
 */
BrowserBridge.prototype.getNumActivelyCapturedEvents = function() {
  return this.capturedEvents_.length - this.numPassivelyCapturedEvents_;
};

/**
 * Returns the number of events that were captured passively by the
 * browser prior to when the net-internals page was started.
 */
BrowserBridge.prototype.getNumPassivelyCapturedEvents = function() {
  return this.numPassivelyCapturedEvents_;
};

/**
 * Deletes captured events with source IDs in |sourceIds|.
 */
BrowserBridge.prototype.deleteEventsBySourceId = function(sourceIds) {
  var sourceIdDict = {};
  for (var i = 0; i < sourceIds.length; i++)
    sourceIdDict[sourceIds[i]] = true;

  var newEventList = [];
  for (var i = 0; i < this.capturedEvents_.length; ++i) {
    var id = this.capturedEvents_[i].source.id;
    if (id in sourceIdDict) {
      if (this.capturedEvents_[i].wasPassivelyCaptured)
        --this.numPassivelyCapturedEvents_;
      continue;
    }
    newEventList.push(this.capturedEvents_[i]);
  }
  this.capturedEvents_ = newEventList;

  for (var i = 0; i < this.logObservers_.length; ++i)
    this.logObservers_[i].onLogEntriesDeleted(sourceIds);
};

/**
 * Deletes all captured events.
 */
BrowserBridge.prototype.deleteAllEvents = function() {
  this.capturedEvents_ = [];
  this.numPassivelyCapturedEvents_ = 0;
  for (var i = 0; i < this.logObservers_.length; ++i)
    this.logObservers_[i].onAllLogEntriesDeleted();
};

BrowserBridge.prototype.doPolling_ = function() {
  // TODO(eroman): Optimize this by using a separate polling frequency for the
  // data consumed by the currently active view. Everything else can be on a low
  // frequency poll since it won't impact the display.
  this.sendGetProxySettings();
  this.sendGetBadProxies();
  this.sendGetHostResolverCache();
  this.sendGetHttpCacheInfo();
  this.sendGetSocketPoolInfo();
  if (this.isPlatformWindows())
    this.sendGetServiceProviders();
};

/**
 * This is a helper class used by BrowserBridge, to keep track of:
 *   - the list of observers interested in some piece of data.
 *   - the last known value of that piece of data.
 *   - the name of the callback method to invoke on observers.
 * @constructor
 */
function PollableDataHelper(observerMethodName) {
  this.observerMethodName_ = observerMethodName;
  this.observers_ = [];
}

PollableDataHelper.prototype.addObserver = function(observer) {
  this.observers_.push(observer);
};

/**
 * Helper function to handle calling all the observers, but ONLY if the data has
 * actually changed since last time. This is used for data we received from
 * browser on a poll loop.
 */
PollableDataHelper.prototype.update = function(data) {
  var prevData = this.currentData_;

  // If the data hasn't changed since last time, no need to notify observers.
  if (prevData && JSON.stringify(prevData) == JSON.stringify(data))
    return;

  this.currentData_ = data;

  // Ok, notify the observers of the change.
  for (var i = 0; i < this.observers_.length; ++i)
    var observer = this.observers_[i];
    observer[this.observerMethodName_](data);
};
