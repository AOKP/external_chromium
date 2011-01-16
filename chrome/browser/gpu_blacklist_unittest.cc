// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/version.h"
#include "chrome/browser/gpu_blacklist.h"
#include "chrome/common/gpu_info.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(GpuBlacklistTest, BlacklistLogic) {
  GPUInfo gpu_info;
  gpu_info.SetGraphicsInfo(0x10de,  // Vendor ID
                           0x0640,  // Device ID
                           L"1.6.18",  // Driver Version
                           0x0114,  // Pixel Shader Version
                           0x0114,  // Vertex Shader Version
                           0x0201,  // GL version,
                           true);  // can_lose_context
  gpu_info.SetProgress(GPUInfo::kComplete);
  scoped_ptr<Version> os_version(Version::GetVersionFromString("10.6.4"));

  GpuBlacklist blacklist;

  // Default blacklist settings: all feature are allowed.
  GpuFeatureFlags flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsMacosx, os_version.get(), gpu_info);
  EXPECT_EQ(flags.flags(), 0u);

  // Empty list: all features are allowed.
  const std::string empty_list_json =
      "{\n"
      "  \"name\": \"gpu blacklist\",\n"
      "  \"version\": \"0.0\",\n"
      "  \"entries\": [\n"
      "  ]\n"
      "}";
  EXPECT_TRUE(blacklist.LoadGpuBlacklist(empty_list_json, false));
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsMacosx, os_version.get(), gpu_info);
  EXPECT_EQ(flags.flags(), 0u);

  // Blacklist accelerated_compositing with exact setting.
  const std::string exact_list_json =
      "{\n"
      "  \"name\": \"gpu blacklist\",\n"
      "  \"version\": \"0.1\",\n"
      "  \"entries\": [\n"
      "    {\n"
      "      \"os\": {\n"
      "        \"type\": \"macosx\",\n"
      "        \"version\": {\n"
      "          \"op\": \"=\",\n"
      "          \"number\": \"10.6.4\"\n"
      "        }\n"
      "      },\n"
      "      \"vendor_id\": \"0x10de\",\n"
      "      \"device_id\": \"0x0640\",\n"
      "      \"driver_version\": {\n"
      "        \"op\": \"=\",\n"
      "        \"number\": \"1.6.18\"\n"
      "      },\n"
      "      \"blacklist\": [\n"
      "        \"accelerated_compositing\"\n"
      "      ]\n"
      "    }\n"
      "  ]\n"
      "}";
  EXPECT_TRUE(blacklist.LoadGpuBlacklist(exact_list_json, false));
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsMacosx, os_version.get(), gpu_info);
  EXPECT_EQ(
      flags.flags(),
      static_cast<uint32>(GpuFeatureFlags::kGpuFeatureAcceleratedCompositing));

  // Invalid json input should not change the current blacklist settings.
  const std::string invalid_json = "invalid";
  EXPECT_FALSE(blacklist.LoadGpuBlacklist(invalid_json, false));
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsMacosx, os_version.get(), gpu_info);
  EXPECT_EQ(
      flags.flags(),
      static_cast<uint32>(GpuFeatureFlags::kGpuFeatureAcceleratedCompositing));

  // Blacklist a vendor on all OS.
  const std::string vendor_json =
      "{\n"
      "  \"name\": \"gpu blacklist\",\n"
      "  \"version\": \"0.1\",\n"
      "  \"entries\": [\n"
      "    {\n"
      "      \"vendor_id\": \"0x10de\",\n"
      "      \"blacklist\": [\n"
      "        \"webgl\"\n"
      "      ]\n"
      "    }\n"
      "  ]\n"
      "}";
  EXPECT_TRUE(blacklist.LoadGpuBlacklist(vendor_json, false));
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsMacosx, os_version.get(), gpu_info);
  EXPECT_EQ(flags.flags(),
            static_cast<uint32>(GpuFeatureFlags::kGpuFeatureWebgl));
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsWin, os_version.get(), gpu_info);
  EXPECT_EQ(flags.flags(),
            static_cast<uint32>(GpuFeatureFlags::kGpuFeatureWebgl));
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsLinux, os_version.get(), gpu_info);
  EXPECT_EQ(flags.flags(),
            static_cast<uint32>(GpuFeatureFlags::kGpuFeatureWebgl));

  // Blacklist a vendor on Linux only.
  const std::string vendor_linux_json =
      "{\n"
      "  \"name\": \"gpu blacklist\",\n"
      "  \"version\": \"0.1\",\n"
      "  \"entries\": [\n"
      "    {\n"
      "      \"os\": {\n"
      "        \"type\": \"linux\"\n"
      "      },\n"
      "      \"vendor_id\": \"0x10de\",\n"
      "      \"blacklist\": [\n"
      "        \"accelerated_2d_canvas\"\n"
      "      ]\n"
      "    }\n"
      "  ]\n"
      "}";
  EXPECT_TRUE(blacklist.LoadGpuBlacklist(vendor_linux_json, false));
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsMacosx, os_version.get(), gpu_info);
  EXPECT_EQ(flags.flags(), 0u);
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsWin, os_version.get(), gpu_info);
  EXPECT_EQ(flags.flags(), 0u);
  flags = blacklist.DetermineGpuFeatureFlags(
      GpuBlacklist::kOsLinux, os_version.get(), gpu_info);
  EXPECT_EQ(
      flags.flags(),
      static_cast<uint32>(GpuFeatureFlags::kGpuFeatureAccelerated2dCanvas));
}

