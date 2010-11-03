// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dom_ui/stats_options_handler.h"

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/cros_settings_names.h"
#include "chrome/browser/chromeos/cros_settings_provider_stats.h"
#include "chrome/browser/metrics/user_metrics.h"

namespace chromeos {

StatsOptionsHandler::StatsOptionsHandler()
    : CrosOptionsPageUIHandler(new MetricsCrosSettingsProvider()) {
}

// OptionsUIHandler implementation.
void StatsOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
}
void StatsOptionsHandler::Initialize() {
  SetupMetricsReportingCheckbox(false);
}

// DOMMessageHandler implementation.
void StatsOptionsHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback(
      "metricsReportingCheckboxAction",
      NewCallback(this, &StatsOptionsHandler::HandleMetricsReportingCheckbox));
}

MetricsCrosSettingsProvider* StatsOptionsHandler::provider() const {
  return static_cast<MetricsCrosSettingsProvider*>(settings_provider_.get());
}

void StatsOptionsHandler::HandleMetricsReportingCheckbox(
    const ListValue* args) {
#if defined(GOOGLE_CHROME_BUILD)
  const std::string checked_str = WideToUTF8(ExtractStringValue(args));
  const bool enabled = (checked_str == "true");
  UserMetricsRecordAction(
      enabled ?
      UserMetricsAction("Options_MetricsReportingCheckbox_Enable") :
      UserMetricsAction("Options_MetricsReportingCheckbox_Disable"));
  const bool is_enabled = MetricsCrosSettingsProvider::GetMetricsStatus();
  SetupMetricsReportingCheckbox(enabled == is_enabled);
#endif
}

void StatsOptionsHandler::SetupMetricsReportingCheckbox(bool user_changed) {
#if defined(GOOGLE_CHROME_BUILD)
  FundamentalValue checked(MetricsCrosSettingsProvider::GetMetricsStatus());
  FundamentalValue disabled(false);
  FundamentalValue user_has_changed(user_changed);
  dom_ui_->CallJavascriptFunction(
      L"options.AdvancedOptions.SetMetricsReportingCheckboxState", checked,
      disabled, user_has_changed);
#endif
}

}  // namespace chromeos
