// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_bypass_rules.h"

#include "base/logging.h"
#include "base/string_tokenizer.h"
#include "base/string_util.h"
#include "net/base/net_util.h"

namespace net {

namespace {

class HostnamePatternRule : public ProxyBypassRules::Rule {
 public:
  HostnamePatternRule(const std::string& optional_scheme,
                      const std::string& hostname_pattern,
                      int optional_port)
      : optional_scheme_(StringToLowerASCII(optional_scheme)),
        hostname_pattern_(StringToLowerASCII(hostname_pattern)),
        optional_port_(optional_port) {
  }

  virtual bool Matches(const GURL& url) const {
    if (optional_port_ != -1 && url.EffectiveIntPort() != optional_port_)
      return false;  // Didn't match port expectation.

    if (!optional_scheme_.empty() && url.scheme() != optional_scheme_)
      return false;  // Didn't match scheme expectation.

    // Note it is necessary to lower-case the host, since GURL uses capital
    // letters for percent-escaped characters.
    return MatchPatternASCII(StringToLowerASCII(url.host()),
                             hostname_pattern_);
  }

  virtual std::string ToString() const {
    std::string str;
    if (!optional_scheme_.empty())
      StringAppendF(&str, "%s://", optional_scheme_.c_str());
    str += hostname_pattern_;
    if (optional_port_ != -1)
      StringAppendF(&str, ":%d", optional_port_);
    return str;
  }

 private:
  const std::string optional_scheme_;
  const std::string hostname_pattern_;
  const int optional_port_;
};

class BypassLocalRule : public ProxyBypassRules::Rule {
 public:
  virtual bool Matches(const GURL& url) const {
    const std::string& host = url.host();
    if (host == "127.0.0.1" || host == "[::1]")
      return true;
    return host.find('.') == std::string::npos;
  }

  virtual std::string ToString() const {
    return "<local>";
  }
};

// Rule for matching a URL that is an IP address, if that IP address falls
// within a certain numeric range. For example, you could use this rule to
// match all the IPs in the CIDR block 10.10.3.4/24.
class BypassIPBlockRule : public ProxyBypassRules::Rule {
 public:
  // |ip_prefix| + |prefix_length| define the IP block to match.
  BypassIPBlockRule(const std::string& description,
                    const std::string& optional_scheme,
                    const IPAddressNumber& ip_prefix,
                    size_t prefix_length_in_bits)
      : description_(description),
        optional_scheme_(optional_scheme),
        ip_prefix_(ip_prefix),
        prefix_length_in_bits_(prefix_length_in_bits) {
  }

  virtual bool Matches(const GURL& url) const {
    if (!url.HostIsIPAddress())
      return false;

    if (!optional_scheme_.empty() && url.scheme() != optional_scheme_)
      return false;  // Didn't match scheme expectation.

    // Parse the input IP literal to a number.
    IPAddressNumber ip_number;
    if (!ParseIPLiteralToNumber(url.HostNoBrackets(), &ip_number))
      return false;

    // Test if it has the expected prefix.
    return IPNumberMatchesPrefix(ip_number, ip_prefix_,
                                 prefix_length_in_bits_);
  }

  virtual std::string ToString() const {
    return description_;
  }

 private:
  const std::string description_;
  const std::string optional_scheme_;
  const IPAddressNumber ip_prefix_;
  const size_t prefix_length_in_bits_;
};

// Returns true if the given string represents an IP address.
bool IsIPAddress(const std::string& domain) {
  // From GURL::HostIsIPAddress()
  url_canon::RawCanonOutputT<char, 128> ignored_output;
  url_canon::CanonHostInfo host_info;
  url_parse::Component domain_comp(0, domain.size());
  url_canon::CanonicalizeIPAddress(domain.c_str(), domain_comp,
                                   &ignored_output, &host_info);
  return host_info.IsIPAddress();
}

}  // namespace

ProxyBypassRules::~ProxyBypassRules() {
}

bool ProxyBypassRules::Matches(const GURL& url) const {
  for (RuleList::const_iterator it = rules_.begin(); it != rules_.end(); ++it) {
    if ((*it)->Matches(url))
      return true;
  }
  return false;
}

bool ProxyBypassRules::Equals(const ProxyBypassRules& other) const {
  if (rules_.size() != other.rules().size())
    return false;

  for (size_t i = 0; i < rules_.size(); ++i) {
    if (!rules_[i]->Equals(*other.rules()[i]))
      return false;
  }
  return true;
}

void ProxyBypassRules::ParseFromString(const std::string& raw) {
  ParseFromStringInternal(raw, false);
}

void ProxyBypassRules::ParseFromStringUsingSuffixMatching(
    const std::string& raw) {
  ParseFromStringInternal(raw, true);
}

bool ProxyBypassRules::AddRuleForHostname(const std::string& optional_scheme,
                                          const std::string& hostname_pattern,
                                          int optional_port) {
  if (hostname_pattern.empty())
    return false;

  rules_.push_back(new HostnamePatternRule(optional_scheme,
                                           hostname_pattern,
                                           optional_port));
  return true;
}

void ProxyBypassRules::AddRuleToBypassLocal() {
  rules_.push_back(new BypassLocalRule);
}

bool ProxyBypassRules::AddRuleFromString(const std::string& raw) {
  return AddRuleFromStringInternalWithLogging(raw, false);
}

bool ProxyBypassRules::AddRuleFromStringUsingSuffixMatching(
    const std::string& raw) {
  return AddRuleFromStringInternalWithLogging(raw, true);
}

void ProxyBypassRules::Clear() {
  rules_.clear();
}

void ProxyBypassRules::ParseFromStringInternal(
    const std::string& raw,
    bool use_hostname_suffix_matching) {
  Clear();

  StringTokenizer entries(raw, ",;");
  while (entries.GetNext()) {
    AddRuleFromStringInternalWithLogging(entries.token(),
                                         use_hostname_suffix_matching);
  }
}

bool ProxyBypassRules::AddRuleFromStringInternal(
    const std::string& raw_untrimmed,
    bool use_hostname_suffix_matching) {
  std::string raw;
  TrimWhitespaceASCII(raw_untrimmed, TRIM_ALL, &raw);

  // This is the special syntax used by WinInet's bypass list -- we allow it
  // on all platforms and interpret it the same way.
  if (LowerCaseEqualsASCII(raw, "<local>")) {
    AddRuleToBypassLocal();
    return true;
  }

  // Extract any scheme-restriction.
  std::string::size_type scheme_pos = raw.find("://");
  std::string scheme;
  if (scheme_pos != std::string::npos) {
    scheme = raw.substr(0, scheme_pos);
    raw = raw.substr(scheme_pos + 3);
    if (scheme.empty())
      return false;
  }

  if (raw.empty())
    return false;

  // If there is a forward slash in the input, it is probably a CIDR style
  // mask.
  if (raw.find('/') != std::string::npos) {
    IPAddressNumber ip_prefix;
    size_t prefix_length_in_bits;

    if (!ParseCIDRBlock(raw, &ip_prefix, &prefix_length_in_bits))
      return false;

    rules_.push_back(
        new BypassIPBlockRule(raw, scheme, ip_prefix, prefix_length_in_bits));

    return true;
  }

  // Check if we have an <ip-address>[:port] input. We need to treat this
  // separately since the IP literal may not be in a canonical form.
  std::string host;
  int port;
  if (ParseHostAndPort(raw, &host, &port)) {
    if (IsIPAddress(host)) {
      // Canonicalize the IP literal before adding it as a string pattern.
      GURL tmp_url("http://" + host);
      return AddRuleForHostname(scheme, tmp_url.host(), port);
    }
  }

  // Otherwise assume we have <hostname-pattern>[:port].
  std::string::size_type pos_colon = raw.rfind(':');
  host = raw;
  port = -1;
  if (pos_colon != std::string::npos) {
    if (!StringToInt(raw.substr(pos_colon + 1), &port) ||
        (port < 0 || port > 0xFFFF)) {
      return false;  // Port was invalid.
    }
    raw = raw.substr(0, pos_colon);
  }

  // Special-case hostnames that begin with a period.
  // For example, we remap ".google.com" --> "*.google.com".
  if (StartsWithASCII(raw, ".", false))
    raw = "*" + raw;

  // If suffix matching was asked for, make sure the pattern starts with a
  // wildcard.
  if (use_hostname_suffix_matching && !StartsWithASCII(raw, "*", false))
    raw = "*" + raw;

  return AddRuleForHostname(scheme, raw, port);
}

bool ProxyBypassRules::AddRuleFromStringInternalWithLogging(
    const std::string& raw,
    bool use_hostname_suffix_matching) {
  return AddRuleFromStringInternal(raw, use_hostname_suffix_matching);
}

}  // namespace net
