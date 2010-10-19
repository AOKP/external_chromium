// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/switch_utils.h"

#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(SwitchUtilsTest, RemoveSwitches) {
#if defined(OS_WIN)
  // All these command line args (except foo and bar) will
  // be removed after RemoveSwitchesForAutostart:
  CommandLine cmd_line = CommandLine::FromString(
      L"program"
      L" --app=http://www.google.com/"
      L" --first-run"
      L" --import"
      L" --import-from-file=c:\\test.html"
      L" --make-default-browser"
      L" --foo"
      L" --bar");
  EXPECT_FALSE(cmd_line.command_line_string().empty());
#elif defined(OS_POSIX)
  const char* argv[] = {
    "program",
    "--app=http://www.google.com/",
    "--first-run",
    "--import",
    "--import-from-file=c:\\test.html",
    "--make-default-browser",
    "--foo",
    "--bar"};
  CommandLine cmd_line(arraysize(argv), argv);
#endif

  std::map<std::string, CommandLine::StringType> switches =
      cmd_line.GetSwitches();
  EXPECT_EQ(7U, switches.size());

  switches::RemoveSwitchesForAutostart(&switches);
  EXPECT_EQ(2U, switches.size());
  EXPECT_TRUE(cmd_line.HasSwitch("foo"));
  EXPECT_TRUE(cmd_line.HasSwitch("bar"));
}
