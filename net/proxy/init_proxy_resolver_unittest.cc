// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/base/net_log_unittest.h"
#include "net/base/test_completion_callback.h"
#include "net/proxy/init_proxy_resolver.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_resolver.h"
#include "net/proxy/proxy_script_fetcher.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

enum Error {
  kFailedDownloading = -100,
  kFailedParsing = -200,
};

class Rules {
 public:
  struct Rule {
    Rule(const GURL& url,
         int fetch_error,
         int set_pac_error)
        : url(url),
          fetch_error(fetch_error),
          set_pac_error(set_pac_error) {
    }

    string16 text() const {
      if (set_pac_error == OK)
        return UTF8ToUTF16(url.spec() + "!valid-script");
      if (fetch_error == OK)
        return UTF8ToUTF16(url.spec() + "!invalid-script");
      return string16();
    }

    GURL url;
    int fetch_error;
    int set_pac_error;
  };

  Rule AddSuccessRule(const char* url) {
    Rule rule(GURL(url), OK /*fetch_error*/, OK /*set_pac_error*/);
    rules_.push_back(rule);
    return rule;
  }

  void AddFailDownloadRule(const char* url) {
    rules_.push_back(Rule(GURL(url), kFailedDownloading /*fetch_error*/,
        ERR_UNEXPECTED /*set_pac_error*/));
  }

  void AddFailParsingRule(const char* url) {
    rules_.push_back(Rule(GURL(url), OK /*fetch_error*/,
        kFailedParsing /*set_pac_error*/));
  }

  const Rule& GetRuleByUrl(const GURL& url) const {
    for (RuleList::const_iterator it = rules_.begin(); it != rules_.end();
         ++it) {
      if (it->url == url)
        return *it;
    }
    LOG(FATAL) << "Rule not found for " << url;
    return rules_[0];
  }

  const Rule& GetRuleByText(const string16& text) const {
    for (RuleList::const_iterator it = rules_.begin(); it != rules_.end();
         ++it) {
      if (it->text() == text)
        return *it;
    }
    LOG(FATAL) << "Rule not found for " << text;
    return rules_[0];
  }

 private:
  typedef std::vector<Rule> RuleList;
  RuleList rules_;
};

class RuleBasedProxyScriptFetcher : public ProxyScriptFetcher {
 public:
  explicit RuleBasedProxyScriptFetcher(const Rules* rules) : rules_(rules) {}

  // ProxyScriptFetcher implementation.
  virtual int Fetch(const GURL& url,
                    string16* text,
                    CompletionCallback* callback) {
    const Rules::Rule& rule = rules_->GetRuleByUrl(url);
    int rv = rule.fetch_error;
    EXPECT_NE(ERR_UNEXPECTED, rv);
    if (rv == OK)
      *text = rule.text();
    return rv;
  }

  virtual void Cancel() {}

 private:
  const Rules* rules_;
};

class RuleBasedProxyResolver : public ProxyResolver {
 public:
  RuleBasedProxyResolver(const Rules* rules, bool expects_pac_bytes)
      : ProxyResolver(expects_pac_bytes), rules_(rules) {}

  // ProxyResolver implementation:
  virtual int GetProxyForURL(const GURL& /*url*/,
                             ProxyInfo* /*results*/,
                             CompletionCallback* /*callback*/,
                             RequestHandle* /*request_handle*/,
                             const BoundNetLog& /*net_log*/) {
    NOTREACHED();
    return ERR_UNEXPECTED;
  }

  virtual void CancelRequest(RequestHandle request_handle) {
    NOTREACHED();
  }

  virtual int SetPacScript(
      const scoped_refptr<ProxyResolverScriptData>& script_data,
      CompletionCallback* callback) {

   const GURL url =
      script_data->type() == ProxyResolverScriptData::TYPE_SCRIPT_URL ?
          script_data->url() : GURL();

    const Rules::Rule& rule = expects_pac_bytes() ?
        rules_->GetRuleByText(script_data->utf16()) :
        rules_->GetRuleByUrl(url);

    int rv = rule.set_pac_error;
    EXPECT_NE(ERR_UNEXPECTED, rv);

    if (expects_pac_bytes()) {
      EXPECT_EQ(rule.text(), script_data->utf16());
    } else {
      EXPECT_EQ(rule.url, url);
    }

    if (rv == OK)
      script_data_ = script_data;
    return rv;
  }

  const ProxyResolverScriptData* script_data() const { return script_data_; }

 private:
  const Rules* rules_;
  scoped_refptr<ProxyResolverScriptData> script_data_;
};

// Succeed using custom PAC script.
TEST(InitProxyResolverTest, CustomPacSucceeds) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  Rules::Rule rule = rules.AddSuccessRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  CapturingNetLog log(CapturingNetLog::kUnbounded);
  InitProxyResolver init(&resolver, &fetcher, &log);
  EXPECT_EQ(OK, init.Init(config, &callback));
  EXPECT_EQ(rule.text(), resolver.script_data()->utf16());

  // Check the NetLog was filled correctly.
  EXPECT_EQ(6u, log.entries().size());
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 0, NetLog::TYPE_INIT_PROXY_RESOLVER));
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 1, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 2, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 3, NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 4, NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 5, NetLog::TYPE_INIT_PROXY_RESOLVER));
}

// Fail downloading the custom PAC script.
TEST(InitProxyResolverTest, CustomPacFails1) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  rules.AddFailDownloadRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  CapturingNetLog log(CapturingNetLog::kUnbounded);
  InitProxyResolver init(&resolver, &fetcher, &log);
  EXPECT_EQ(kFailedDownloading, init.Init(config, &callback));
  EXPECT_EQ(NULL, resolver.script_data());

  // Check the NetLog was filled correctly.
  EXPECT_EQ(4u, log.entries().size());
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 0, NetLog::TYPE_INIT_PROXY_RESOLVER));
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 1, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 2, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 3, NetLog::TYPE_INIT_PROXY_RESOLVER));
}

// Fail parsing the custom PAC script.
TEST(InitProxyResolverTest, CustomPacFails2) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  rules.AddFailParsingRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  InitProxyResolver init(&resolver, &fetcher, NULL);
  EXPECT_EQ(kFailedParsing, init.Init(config, &callback));
  EXPECT_EQ(NULL, resolver.script_data());
}

// Fail downloading the custom PAC script, because the fetcher was NULL.
TEST(InitProxyResolverTest, HasNullProxyScriptFetcher) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);

  ProxyConfig config;
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  TestCompletionCallback callback;
  InitProxyResolver init(&resolver, NULL, NULL);
  EXPECT_EQ(ERR_UNEXPECTED, init.Init(config, &callback));
  EXPECT_EQ(NULL, resolver.script_data());
}

// Succeeds in choosing autodetect (wpad).
TEST(InitProxyResolverTest, AutodetectSuccess) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_auto_detect(true);

  Rules::Rule rule = rules.AddSuccessRule("http://wpad/wpad.dat");

  TestCompletionCallback callback;
  InitProxyResolver init(&resolver, &fetcher, NULL);
  EXPECT_EQ(OK, init.Init(config, &callback));
  EXPECT_EQ(rule.text(), resolver.script_data()->utf16());
}

// Fails at WPAD (downloading), but succeeds in choosing the custom PAC.
TEST(InitProxyResolverTest, AutodetectFailCustomSuccess1) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_auto_detect(true);
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  rules.AddFailDownloadRule("http://wpad/wpad.dat");
  Rules::Rule rule = rules.AddSuccessRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  InitProxyResolver init(&resolver, &fetcher, NULL);
  EXPECT_EQ(OK, init.Init(config, &callback));
  EXPECT_EQ(rule.text(), resolver.script_data()->utf16());
}

// Fails at WPAD (parsing), but succeeds in choosing the custom PAC.
TEST(InitProxyResolverTest, AutodetectFailCustomSuccess2) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_auto_detect(true);
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  rules.AddFailParsingRule("http://wpad/wpad.dat");
  Rules::Rule rule = rules.AddSuccessRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  CapturingNetLog log(CapturingNetLog::kUnbounded);
  InitProxyResolver init(&resolver, &fetcher, &log);
  EXPECT_EQ(OK, init.Init(config, &callback));
  EXPECT_EQ(rule.text(), resolver.script_data()->utf16());

  // Check the NetLog was filled correctly.
  // (Note that the Fetch and Set states are repeated since both WPAD and custom
  // PAC scripts are tried).
  EXPECT_EQ(11u, log.entries().size());
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 0, NetLog::TYPE_INIT_PROXY_RESOLVER));
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 1, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 2, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 3, NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 4, NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEvent(
      log.entries(), 5,
      NetLog::TYPE_INIT_PROXY_RESOLVER_FALLING_BACK_TO_NEXT_PAC_URL,
      NetLog::PHASE_NONE));
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 6, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 7, NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsBeginEvent(
      log.entries(), 8, NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 9, NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT));
  EXPECT_TRUE(LogContainsEndEvent(
      log.entries(), 10, NetLog::TYPE_INIT_PROXY_RESOLVER));
}

// Fails at WPAD (downloading), and fails at custom PAC (downloading).
TEST(InitProxyResolverTest, AutodetectFailCustomFails1) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_auto_detect(true);
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  rules.AddFailDownloadRule("http://wpad/wpad.dat");
  rules.AddFailDownloadRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  InitProxyResolver init(&resolver, &fetcher, NULL);
  EXPECT_EQ(kFailedDownloading, init.Init(config, &callback));
  EXPECT_EQ(NULL, resolver.script_data());
}

// Fails at WPAD (downloading), and fails at custom PAC (parsing).
TEST(InitProxyResolverTest, AutodetectFailCustomFails2) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, true /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_auto_detect(true);
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  rules.AddFailDownloadRule("http://wpad/wpad.dat");
  rules.AddFailParsingRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  InitProxyResolver init(&resolver, &fetcher, NULL);
  EXPECT_EQ(kFailedParsing, init.Init(config, &callback));
  EXPECT_EQ(NULL, resolver.script_data());
}

// Fails at WPAD (parsing), but succeeds in choosing the custom PAC.
// This is the same as AutodetectFailCustomSuccess2, but using a ProxyResolver
// that doesn't |expects_pac_bytes|.
TEST(InitProxyResolverTest, AutodetectFailCustomSuccess2_NoFetch) {
  Rules rules;
  RuleBasedProxyResolver resolver(&rules, false /*expects_pac_bytes*/);
  RuleBasedProxyScriptFetcher fetcher(&rules);

  ProxyConfig config;
  config.set_auto_detect(true);
  config.set_pac_url(GURL("http://custom/proxy.pac"));

  rules.AddFailParsingRule("");  // Autodetect.
  Rules::Rule rule = rules.AddSuccessRule("http://custom/proxy.pac");

  TestCompletionCallback callback;
  InitProxyResolver init(&resolver, &fetcher, NULL);
  EXPECT_EQ(OK, init.Init(config, &callback));
  EXPECT_EQ(rule.url, resolver.script_data()->url());
}

}  // namespace
}  // namespace net
