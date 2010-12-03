// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var MAX_APPS_PER_ROW = [];
MAX_APPS_PER_ROW[LayoutMode.SMALL] = 4;
MAX_APPS_PER_ROW[LayoutMode.NORMAL] = 6;

// The URL prefix used in the app link 'ping' attributes.
var PING_APP_LAUNCH_PREFIX = 'record-app-launch';

// The URL prefix used in the webstore link 'ping' attributes.
var PING_WEBSTORE_LAUNCH_PREFIX = 'record-webstore-launch';

function getAppsCallback(data) {
  logEvent('received apps');
  var appsSection = $('apps');
  var appsSectionContent = $('apps-content');
  var appsMiniview = appsSection.getElementsByClassName('miniview')[0];
  var appsPromo = $('apps-promo');
  var appsPromoPing = PING_WEBSTORE_LAUNCH_PREFIX + '+' + data.showPromo;
  var webStoreEntry;

  appsMiniview.textContent = '';
  appsSectionContent.textContent = '';

  apps.showPromo = data.showPromo;

  data.apps.sort(function(a,b) {
    return a.app_launch_index - b.app_launch_index;
  });

  clearClosedMenu(apps.menu);
  if (data.apps.length == 0 && !data.showLauncher) {
    appsSection.classList.add('disabled');
    layoutSections();
  } else {
    data.apps.forEach(function(app) {
      appsSectionContent.appendChild(apps.createElement(app));
    });

    webStoreEntry = apps.createWebStoreElement();
    webStoreEntry.querySelector('a').setAttribute('ping', appsPromoPing);
    appsSectionContent.appendChild(webStoreEntry);

    data.apps.slice(0, MAX_MINIVIEW_ITEMS).forEach(function(app) {
      appsMiniview.appendChild(apps.createMiniviewElement(app));
      addClosedMenuEntryWithLink(apps.menu, apps.createClosedMenuElement(app));
    });

    if (!(shownSections & MINIMIZED_APPS)) {
      appsSection.classList.remove('disabled');
    }
  }
  addClosedMenuFooter(apps.menu, 'apps', MINIMIZED_APPS, Section.APPS);

  apps.loaded = true;
  if (data.showPromo)
    document.documentElement.classList.add('apps-promo-visible');
  else
    document.documentElement.classList.remove('apps-promo-visible');
  $('apps-promo-link').setAttribute('ping', appsPromoPing);
  maybeDoneLoading();

  if (data.apps.length > 0 && isDoneLoading()) {
    if (!data.showPromo && data.apps.length >= MAX_APPS_PER_ROW[layoutMode])
      webStoreEntry.classList.add('loner');
    else
      webStoreEntry.classList.remove('loner');

    updateMiniviewClipping(appsMiniview);
    layoutSections();
  }
}

function appsPrefChangeCallback(data) {
  // Currently the only pref that is watched is the launch type.
  data.apps.forEach(function(app) {
    var appLink = document.querySelector('.app a[app-id=' + app['id'] + ']');
    if (appLink)
      appLink.setAttribute('launch-type', app['launch_type']);
  });
}

var apps = (function() {

  function createElement(app) {
    var div = document.createElement('div');
    div.className = 'app';

    var a = div.appendChild(document.createElement('a'));
    a.setAttribute('app-id', app['id']);
    a.setAttribute('launch-type', app['launch_type']);
    a.xtitle = a.textContent = app['name'];
    a.href = app['launch_url'];

    return div;
  }

  function createContextMenu(app) {
    var menu = new cr.ui.Menu;
    var button = document.createElement(button);
  }

  function launchApp(appId) {
    var appsSection = $('apps');
    var expanded = !appsSection.classList.contains('hidden');
    var element = document.querySelector(
        (expanded ? '.maxiview' : '.miniview') + ' a[app-id=' + appId + ']');

    // TODO(arv): Handle zoom?
    var rect = element.getBoundingClientRect();
    var cs = getComputedStyle(element);
    var size = cs.backgroundSize.split(/\s+/);  // background-size has the
                                                // format '123px 456px'.

    var width = parseInt(size[0], 10);
    var height = parseInt(size[1], 10);

    var top, left;
    if (expanded) {
      // We are using background-position-x 50%.
      top = rect.top + parseInt(cs.backgroundPositionY, 10);
      left = rect.left + ((rect.width - width) >> 1);  // Integer divide by 2.

    } else {
      // We are using background-position-y 50%.
      top = rect.top + ((rect.height - width) >> 1);  // Integer divide by 2.
      if (getComputedStyle(element).direction == 'rtl')
        left = rect.left + rect.width - width;
      else
        left = rect.left;
    }

    chrome.send('launchApp', [appId,
                              String(left), String(top),
                              String(width), String(height)]);
  }

  /**
   * @this {!HTMLAnchorElement}
   */
  function handleClick(e) {
    var appId = e.currentTarget.getAttribute('app-id');
    launchApp(appId);
    return false;
  }

  // Keep in sync with LaunchType in extension_prefs.h
  var LaunchType = {
    LAUNCH_PINNED: 0,
    LAUNCH_REGULAR: 1,
    LAUNCH_FULLSCREEN: 2
  };

  // Keep in sync with LaunchContainer in extension.h
  var LaunchContainer = {
    LAUNCH_WINDOW: 0,
    LAUNCH_PANEL: 1,
    LAUNCH_TAB: 2
  };

  var currentApp;

  function addContextMenu(el, app) {
    el.addEventListener('contextmenu', cr.ui.contextMenuHandler);
    el.addEventListener('keydown', cr.ui.contextMenuHandler);
    el.addEventListener('keyup', cr.ui.contextMenuHandler);

    Object.defineProperty(el, 'contextMenu', {
      get: function() {
        currentApp = app;

        $('apps-launch-command').label = app['name'];
        $('apps-options-command').canExecuteChange();

        var appLinkSel = '.app a[app-id=' + app['id'] + ']';
        var launchType =
            el.querySelector(appLinkSel).getAttribute('launch-type');

        var launchContainer = app['launch_container'];
        var isPanel = launchContainer == LaunchContainer.LAUNCH_PANEL;

        // Update the commands related to the launch type.
        var launchTypeIds = ['apps-launch-type-pinned',
                             'apps-launch-type-regular',
                             'apps-launch-type-fullscreen'];
        launchTypeIds.forEach(function(id) {
          var command = $(id);
          command.disabled = isPanel;
          command.checked = !isPanel &&
              launchType == command.getAttribute('launch-type');
        });

        return $('app-context-menu');
      }
    });
  }

  document.addEventListener('command', function(e) {
    if (!currentApp)
      return;

    var commandId = e.command.id;
    switch (commandId) {
      case 'apps-options-command':
        window.location = currentApp['options_url'];
        break;
      case 'apps-launch-command':
        launchApp(currentApp['id']);
        break;
      case 'apps-uninstall-command':
        chrome.send('uninstallApp', [currentApp['id']]);
        break;
      case 'apps-launch-type-pinned':
      case 'apps-launch-type-regular':
      case 'apps-launch-type-fullscreen':
        chrome.send('setLaunchType',
            [currentApp['id'], e.command.getAttribute('launch-type')]);
        break;
    }
  });

  document.addEventListener('canExecute', function(e) {
    switch (e.command.id) {
      case 'apps-options-command':
        e.canExecute = currentApp && currentApp['options_url'];
        break;
      case 'apps-launch-command':
      case 'apps-uninstall-command':
        e.canExecute = true;
        break;
    }
  });

  return {
    loaded: false,

    menu: $('apps-menu'),

    showPromo: false,

    createElement: function(app) {
      var div = createElement(app);
      var a = div.firstChild;

      a.onclick = handleClick;
      a.setAttribute('ping', PING_APP_LAUNCH_PREFIX + '+' + this.showPromo);
      a.style.backgroundImage = url(app['icon_big']);
      if (hashParams['app-id'] == app['id']) {
        div.setAttribute('new', 'new');
        // Delay changing the attribute a bit to let the page settle down a bit.
        setTimeout(function() {
          // Make sure the new icon is scrolled into view.
          document.body.scrollTop = document.body.scrollHeight;

          // This will trigger the 'bounce' animation defined in apps.css.
          div.setAttribute('new', 'installed');
        }, 500);
        div.addEventListener('webkitAnimationEnd', function(e) {
          div.removeAttribute('new');

          // If we get new data (eg because something installs in another tab,
          // or because we uninstall something here), don't run the install
          // animation again.
          document.documentElement.setAttribute("install-animation-enabled",
                                                "false");
        });
      }

      var settingsButton = div.appendChild(new cr.ui.ContextMenuButton);
      settingsButton.className = 'app-settings';
      settingsButton.title = localStrings.getString('appsettings');

      addContextMenu(div, app);

      return div;
    },

    createMiniviewElement: function(app) {
      var span = document.createElement('span');
      var a = span.appendChild(document.createElement('a'));

      a.setAttribute('app-id', app['id']);
      a.textContent = app['name'];
      a.href = app['launch_url'];
      a.onclick = handleClick;
      a.setAttribute('ping', PING_APP_LAUNCH_PREFIX + '+' + this.showPromo);
      a.style.backgroundImage = url(app['icon_small']);
      a.className = 'item';
      span.appendChild(a);

      addContextMenu(span, app);

      return span;
    },

    createClosedMenuElement: function(app) {
      var a = document.createElement('a');
      a.setAttribute('app-id', app['id']);
      a.textContent = app['name'];
      a.href = app['launch_url'];
      a.onclick = handleClick;
      a.setAttribute('ping', PING_APP_LAUNCH_PREFIX + '+' + this.showPromo);
      a.style.backgroundImage = url(app['icon_small']);
      a.className = 'item';
      return a;
    },

    createWebStoreElement: function() {
      var elm = createElement({
        'id': 'web-store-entry',
        'name': localStrings.getString('web_store_title'),
        'launch_url': localStrings.getString('web_store_url')
      });
      elm.setAttribute('app-id', 'web-store-entry');
      return elm;
    }
  };
})();
