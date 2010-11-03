// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {

var OptionsPage = options.OptionsPage;

  //
  // AdvancedOptions class
  // Encapsulated handling of advanced options page.
  //
  function AdvancedOptions() {
    OptionsPage.call(this, 'advanced', templateData.advancedPage,
                     'advancedPage');
  }

  cr.addSingletonGetter(AdvancedOptions);

  AdvancedOptions.prototype = {
    // Inherit AdvancedOptions from OptionsPage.
    __proto__: options.OptionsPage.prototype,

    /**
     * Initializes the page.
     */
    initializePage: function() {
      // Call base class implementation to starts preference initialization.
      OptionsPage.prototype.initializePage.call(this);

      // Setup click handlers for buttons.
      $('privacyContentSettingsButton').onclick = function(event) {
        OptionsPage.showPageByName('content');
        OptionsPage.showTab($('cookies-nav-tab'));
        chrome.send('coreOptionsUserMetricsAction',
            ['Options_ContentSettings']);
      };
      $('privacyClearDataButton').onclick = function(event) {
        OptionsPage.showOverlay('clearBrowserDataOverlay');
        chrome.send('coreOptionsUserMetricsAction', ['Options_ClearData']);
      };

      // 'metricsReportingEnabled' element is only present on Chrome branded
      // builds.
      if ($('metricsReportingEnabled')) {
        $('metricsReportingEnabled').onclick = function(event) {
          chrome.send('metricsReportingCheckboxAction',
              [String(event.target.checked)]);
        };
      }

      $('autoOpenFileTypesResetToDefault').onclick = function(event) {
        chrome.send('autoOpenFileTypesAction');
      };
      $('fontSettingsConfigureFontsOnlyButton').onclick = function(event) {
        OptionsPage.showPageByName('fontSettings');
        chrome.send('coreOptionsUserMetricsAction', ['Options_FontSettings']);
      };
      $('optionsReset').onclick = function(event) {
        AlertOverlay.show(undefined,
            localStrings.getString('optionsResetMessage'),
            localStrings.getString('optionsResetOkLabel'),
            localStrings.getString('optionsResetCancelLabel'),
            function() { chrome.send('resetToDefaults'); });
      }

      if (cr.isWindows || cr.isMac) {
        $('certificatesManageButton').onclick = function(event) {
          chrome.send('showManageSSLCertificates');
        };
      } else {
        $('certificatesManageButton').onclick = function(event) {
          OptionsPage.showPageByName('certificateManager');
          OptionsPage.showTab($('personal-certs-nav-tab'));
          chrome.send('coreOptionsUserMetricsAction',
                      ['Options_ManageSSLCertificates']);
        };
      }

      if (!cr.isChromeOS) {
        $('proxiesConfigureButton').onclick = function(event) {
          chrome.send('showNetworkProxySettings');
        };
        $('downloadLocationBrowseButton').onclick = function(event) {
          chrome.send('selectDownloadLocation');
        };

        // Remove Windows-style accelerators from the Browse button label.
        // TODO(csilv): Remove this after the accelerator has been removed from
        // the localized strings file, pending removal of old options window.
        $('downloadLocationBrowseButton').textContent =
            localStrings.getStringWithoutAccelerator(
                'downloadLocationBrowseButton');
      } else {
        $('proxiesConfigureButton').onclick = function(event) {
          OptionsPage.showPageByName('proxy');
          chrome.send('coreOptionsUserMetricsAction',
              ['Options_ShowProxySettings']);
        };
      }

      if (cr.isWindows) {
        $('sslCheckRevocation').onclick = function(event) {
          chrome.send('checkRevocationCheckboxAction',
              [String($('sslCheckRevocation').checked)]);
        };
        $('sslUseSSL2').onclick = function(event) {
          chrome.send('useSSL2CheckboxAction',
              [String($('sslUseSSL2').checked)]);
        };
        $('sslUseSSL3').onclick = function(event) {
          chrome.send('useSSL3CheckboxAction',
              [String($('sslUseSSL3').checked)]);
        };
        $('sslUseTLS1').onclick = function(event) {
          chrome.send('useTLS1CheckboxAction',
              [String($('sslUseTLS1').checked)]);
        };
        $('gearSettingsConfigureGearsButton').onclick = function(event) {
          chrome.send('showGearsSettings');
        };
      }

      // 'cloudPrintProxyEnabled' is true for Chrome branded builds on
      // certain platforms, or could be enabled by a lab.
      if (!cr.isChromeOS &&
          localStrings.getString('enable-cloud-print-proxy') == 'true') {
        $('cloudPrintProxySetupButton').onclick = function(event) {
          if ($('cloudPrintProxyManageButton').style.display == 'none') {
            // Disable the button, set it's text to the intermediate state.
            $('cloudPrintProxySetupButton').textContent =
              localStrings.getString('cloudPrintProxyEnablingButton');
            $('cloudPrintProxySetupButton').disabled = true;
            chrome.send('showCloudPrintSetupDialog');
          } else {
            chrome.send('disableCloudPrintProxy');
          }
        };
        $('cloudPrintProxyManageButton').onclick = function(event) {
          chrome.send('showCloudPrintManagePage');
        };
      }
    },

    /**
     * Show a 'restart required' alert.
     * @private
     */
    showRestartRequiredAlert_: function() {
      AlertOverlay.show(undefined,
          localStrings.getString('optionsRestartRequired'),
          undefined, '', undefined);
    }
  };

  //
  // Chrome callbacks
  //

  // Set the checked state of the metrics reporting checkbox.
  AdvancedOptions.SetMetricsReportingCheckboxState = function(checked,
      disabled, user_changed) {
    $('metricsReportingEnabled').checked = checked;
    $('metricsReportingEnabled').disabled = disabled;

    if (user_changed)
      AdvancedOptions.getInstance().showRestartRequiredAlert_();
  }

  // Set the download path.
  AdvancedOptions.SetDownloadLocationPath = function(path) {
    if (!cr.isChromeOS)
      $('downloadLocationPath').value = path;
  };

  // Set the enabled state for the autoOpenFileTypesResetToDefault button.
  AdvancedOptions.SetAutoOpenFileTypesDisabledAttribute = function(disabled) {
    $('autoOpenFileTypesResetToDefault').disabled = disabled;
  };

  // Set the enabled state for the proxy settings button.
  AdvancedOptions.SetupProxySettingsSection = function(disabled, label) {
    $('proxiesConfigureButton').disabled = disabled;
    $('proxiesLabel').textContent = label;
  };

  // Set the checked state for the sslCheckRevocation checkbox.
  AdvancedOptions.SetCheckRevocationCheckboxState = function(checked,
      disabled) {
    $('sslCheckRevocation').checked = checked;
    $('sslCheckRevocation').disabled = disabled;
  };

  // Set the checked state for the sslUseSSL2 checkbox.
  AdvancedOptions.SetUseSSL2CheckboxState = function(checked, disabled) {
    $('sslUseSSL2').checked = checked;
    $('sslUseSSL2').disabled = disabled;
  };

  // Set the checked state for the sslUseSSL3 checkbox.
  AdvancedOptions.SetUseSSL3CheckboxState = function(checked, disabled) {
    $('sslUseSSL3').checked = checked;
    $('sslUseSSL3').disabled = disabled;
  };

  // Set the checked state for the sslUseTLS1 checkbox.
  AdvancedOptions.SetUseTLS1CheckboxState = function(checked, disabled) {
    $('sslUseTLS1').checked = checked;
    $('sslUseTLS1').disabled = disabled;
  };

  // Set the Cloud Print proxy UI to enabled, disabled, or processing.
  AdvancedOptions.SetupCloudPrintProxySection = function(disabled, label) {
    if (!cr.isChromeOS) {
      $('cloudPrintProxyLabel').textContent = label;
      if (disabled) {
        $('cloudPrintProxySetupButton').textContent =
          localStrings.getString('cloudPrintProxyDisabledButton');
        $('cloudPrintProxyManageButton').style.display = 'none';
      } else {
        $('cloudPrintProxySetupButton').textContent =
          localStrings.getString('cloudPrintProxyEnabledButton');
        $('cloudPrintProxyManageButton').style.display = 'inline';
      }
      $('cloudPrintProxySetupButton').disabled = false;
    }
  };

  // Export
  return {
    AdvancedOptions: AdvancedOptions
  };

});
