// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * This view displays information on the proxy setup:
 *
 *   - Shows the current proxy settings.
 *   - Has a button to reload these settings.
 *   - Shows the list of proxy hostnames that are cached as "bad".
 *   - Has a button to clear the cached bad proxies.
 *
 *  @constructor
 */
function ProxyView(mainBoxId,
                   originalSettingsDivId,
                   effectiveSettingsDivId,
                   reloadSettingsButtonId,
                   badProxiesTbodyId,
                   clearBadProxiesButtonId) {
  DivView.call(this, mainBoxId);

  // Hook up the UI components.
  this.originalSettingsDiv_ = document.getElementById(originalSettingsDivId);
  this.effectiveSettingsDiv_ =
      document.getElementById(effectiveSettingsDivId);
  this.badProxiesTbody_ = document.getElementById(badProxiesTbodyId);

  var reloadSettingsButton = document.getElementById(reloadSettingsButtonId);
  var clearBadProxiesButton = document.getElementById(clearBadProxiesButtonId);

  clearBadProxiesButton.onclick = g_browser.sendClearBadProxies.bind(g_browser);
  reloadSettingsButton.onclick =
      g_browser.sendReloadProxySettings.bind(g_browser);

  // Register to receive proxy information as it changes.
  g_browser.addProxySettingsObserver(this);
  g_browser.addBadProxiesObserver(this);
}

inherits(ProxyView, DivView);

ProxyView.prototype.onProxySettingsChanged = function(proxySettings) {
  var original = proxySettings.original;
  var effective = proxySettings.effective;

  // Both |original| and |effective| are dictionaries describing the settings.
  this.originalSettingsDiv_.innerHTML = ''
  this.effectiveSettingsDiv_.innerHTML = ''

  addTextNode(this.originalSettingsDiv_, proxySettingsToString(original));
  addTextNode(this.effectiveSettingsDiv_, proxySettingsToString(effective));
};

ProxyView.prototype.onBadProxiesChanged = function(badProxies) {
  this.badProxiesTbody_.innerHTML = '';

  // Add a table row for each bad proxy entry.
  for (var i = 0; i < badProxies.length; ++i) {
    var entry = badProxies[i];
    var badUntilDate = g_browser.convertTimeTicksToDate(entry.bad_until);

    var tr = addNode(this.badProxiesTbody_, 'tr');

    var nameCell = addNode(tr, 'td');
    var badUntilCell = addNode(tr, 'td');

    addTextNode(nameCell, entry.proxy_uri);
    addTextNode(badUntilCell, badUntilDate.toLocaleString());
  }
};
