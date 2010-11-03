// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete.h"

#include <algorithm>

#include "app/l10n_util.h"
#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/i18n/number_formatting.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/history_quick_provider.h"
#include "chrome/browser/autocomplete/history_url_provider.h"
#include "chrome/browser/autocomplete/history_contents_provider.h"
#include "chrome/browser/autocomplete/keyword_provider.h"
#include "chrome/browser/autocomplete/search_provider.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/dom_ui/history_ui.h"
#include "chrome/browser/external_protocol_handler.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "googleurl/src/url_canon_ip.h"
#include "googleurl/src/url_util.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/net_util.h"
#include "net/base/registry_controlled_domain.h"
#include "net/url_request/url_request.h"

using base::TimeDelta;

// AutocompleteInput ----------------------------------------------------------

AutocompleteInput::AutocompleteInput()
  : type_(INVALID),
    prevent_inline_autocomplete_(false),
    prefer_keyword_(false),
    synchronous_only_(false) {
}

AutocompleteInput::AutocompleteInput(const std::wstring& text,
                                     const std::wstring& desired_tld,
                                     bool prevent_inline_autocomplete,
                                     bool prefer_keyword,
                                     bool synchronous_only)
    : desired_tld_(desired_tld),
      prevent_inline_autocomplete_(prevent_inline_autocomplete),
      prefer_keyword_(prefer_keyword),
      synchronous_only_(synchronous_only) {
  // Trim whitespace from edges of input; don't inline autocomplete if there
  // was trailing whitespace.
  if (TrimWhitespace(text, TRIM_ALL, &text_) & TRIM_TRAILING)
    prevent_inline_autocomplete_ = true;

  type_ = Parse(text_, desired_tld, &parts_, &scheme_);

  if (type_ == INVALID)
    return;

  if ((type_ == UNKNOWN) || (type_ == REQUESTED_URL) || (type_ == URL)) {
    GURL canonicalized_url(URLFixerUpper::FixupURL(WideToUTF8(text_),
                                                   WideToUTF8(desired_tld_)));
    if (canonicalized_url.is_valid() &&
        (!canonicalized_url.IsStandard() || canonicalized_url.SchemeIsFile() ||
         !canonicalized_url.host().empty()))
      canonicalized_url_ = canonicalized_url;
  }

  if (type_ == FORCED_QUERY && text_[0] == L'?')
    text_.erase(0, 1);
}

AutocompleteInput::~AutocompleteInput() {
}

// static
std::string AutocompleteInput::TypeToString(Type type) {
  switch (type) {
    case INVALID:       return "invalid";
    case UNKNOWN:       return "unknown";
    case REQUESTED_URL: return "requested-url";
    case URL:           return "url";
    case QUERY:         return "query";
    case FORCED_QUERY:  return "forced-query";

    default:
      NOTREACHED();
      return std::string();
  }
}

// static
AutocompleteInput::Type AutocompleteInput::Parse(
    const std::wstring& text,
    const std::wstring& desired_tld,
    url_parse::Parsed* parts,
    std::wstring* scheme) {
  const size_t first_non_white = text.find_first_not_of(kWhitespaceWide, 0);
  if (first_non_white == std::wstring::npos)
    return INVALID;  // All whitespace.

  if (text.at(first_non_white) == L'?') {
    // If the first non-whitespace character is a '?', we magically treat this
    // as a query.
    return FORCED_QUERY;
  }

  // Ask our parsing back-end to help us understand what the user typed.  We
  // use the URLFixerUpper here because we want to be smart about what we
  // consider a scheme.  For example, we shouldn't consider www.google.com:80
  // to have a scheme.
  url_parse::Parsed local_parts;
  if (!parts)
    parts = &local_parts;
  const std::wstring parsed_scheme(URLFixerUpper::SegmentURL(text, parts));
  if (scheme)
    *scheme = parsed_scheme;

  if (parsed_scheme == L"file") {
    // A user might or might not type a scheme when entering a file URL.  In
    // either case, |parsed_scheme| will tell us that this is a file URL, but
    // |parts->scheme| might be empty, e.g. if the user typed "C:\foo".
    return URL;
  }

  // If the user typed a scheme, and it's HTTP or HTTPS, we know how to parse it
  // well enough that we can fall through to the heuristics below.  If it's
  // something else, we can just determine our action based on what we do with
  // any input of this scheme.  In theory we could do better with some schemes
  // (e.g. "ftp" or "view-source") but I'll wait to spend the effort on that
  // until I run into some cases that really need it.
  if (parts->scheme.is_nonempty() &&
      (parsed_scheme != L"http") && (parsed_scheme != L"https")) {
    // See if we know how to handle the URL internally.
    if (URLRequest::IsHandledProtocol(WideToASCII(parsed_scheme)))
      return URL;

    // There are also some schemes that we convert to other things before they
    // reach the renderer or else the renderer handles internally without
    // reaching the URLRequest logic.  We thus won't catch these above, but we
    // should still claim to handle them.
    if (LowerCaseEqualsASCII(parsed_scheme, chrome::kViewSourceScheme) ||
        LowerCaseEqualsASCII(parsed_scheme, chrome::kJavaScriptScheme) ||
        LowerCaseEqualsASCII(parsed_scheme, chrome::kDataScheme))
      return URL;

    // Finally, check and see if the user has explicitly opened this scheme as
    // a URL before.  We need to do this last because some schemes may be in
    // here as "blocked" (e.g. "javascript") because we don't want pages to open
    // them, but users still can.
    // TODO(viettrungluu): get rid of conversion.
    switch (ExternalProtocolHandler::GetBlockState(WideToUTF8(parsed_scheme))) {
      case ExternalProtocolHandler::DONT_BLOCK:
        return URL;

      case ExternalProtocolHandler::BLOCK:
        // If we don't want the user to open the URL, don't let it be navigated
        // to at all.
        return QUERY;

      default:
        // We don't know about this scheme.  It's likely to be a search operator
        // like "site:" or "link:".  We classify it as UNKNOWN so the user has
        // the option of treating it as a URL if we're wrong.
        // Note that SegmentURL() is smart so we aren't tricked by "c:\foo" or
        // "www.example.com:81" in this case.
        return UNKNOWN;
    }
  }

  // Either the user didn't type a scheme, in which case we need to distinguish
  // between an HTTP URL and a query, or the scheme is HTTP or HTTPS, in which
  // case we should reject invalid formulations.

  // If we have an empty host it can't be a URL.
  if (!parts->host.is_nonempty())
    return QUERY;

  // Likewise, the RCDS can reject certain obviously-invalid hosts.  (We also
  // use the registry length later below.)
  const std::wstring host(text.substr(parts->host.begin, parts->host.len));
  const size_t registry_length =
      net::RegistryControlledDomainService::GetRegistryLength(host, false);
  if (registry_length == std::wstring::npos) {
    // Try to append the desired_tld.
    if (!desired_tld.empty()) {
      std::wstring host_with_tld(host);
      if (host[host.length() - 1] != '.')
        host_with_tld += '.';
      host_with_tld += desired_tld;
      if (net::RegistryControlledDomainService::GetRegistryLength(
          host_with_tld, false) != std::wstring::npos)
        return REQUESTED_URL;  // Something like "99999999999" that looks like a
                               // bad IP address, but becomes valid on attaching
                               // a TLD.
    }
    return QUERY;  // Could be a broken IP address, etc.
  }


  // See if the hostname is valid.  While IE and GURL allow hostnames to contain
  // many other characters (perhaps for weird intranet machines), it's extremely
  // unlikely that a user would be trying to type those in for anything other
  // than a search query.
  url_canon::CanonHostInfo host_info;
  const std::string canonicalized_host(net::CanonicalizeHost(host, &host_info));
  if ((host_info.family == url_canon::CanonHostInfo::NEUTRAL) &&
      !net::IsCanonicalizedHostCompliant(canonicalized_host,
                                         WideToUTF8(desired_tld))) {
    // Invalid hostname.  There are several possible cases:
    // * Our checker is too strict and the user pasted in a real-world URL
    //   that's "invalid" but resolves.  To catch these, we return UNKNOWN when
    //   the user explicitly typed a scheme, so we'll still search by default
    //   but we'll show the accidental search infobar if necessary.
    // * The user is typing a multi-word query.  If we see a space anywhere in
    //   the hostname we assume this is a search and return QUERY.
    // * Our checker is too strict and the user is typing a real-world hostname
    //   that's "invalid" but resolves.  We return UNKNOWN if the TLD is known.
    //   Note that we explicitly excluded hosts with spaces above so that
    //   "toys at amazon.com" will be treated as a search.
    // * The user is typing some garbage string.  Return QUERY.
    //
    // Thus we fall down in the following cases:
    // * Trying to navigate to a hostname with spaces
    // * Trying to navigate to a hostname with invalid characters and an unknown
    //   TLD
    // These are rare, though probably possible in intranets.
    return (parts->scheme.is_nonempty() ||
           ((registry_length != 0) && (host.find(' ') == std::wstring::npos))) ?
        UNKNOWN : QUERY;
  }

  // A port number is a good indicator that this is a URL.  However, it might
  // also be a query like "1.66:1" that looks kind of like an IP address and
  // port number. So here we only check for "port numbers" that are illegal and
  // thus mean this can't be navigated to (e.g. "1.2.3.4:garbage"), and we save
  // handling legal port numbers until after the "IP address" determination
  // below.
  if (parts->port.is_nonempty()) {
    int port;
    if (!base::StringToInt(WideToUTF8(
            text.substr(parts->port.begin, parts->port.len)), &port) ||
        (port < 0) || (port > 65535))
      return QUERY;
  }

  // Now that we've ruled out all schemes other than http or https and done a
  // little more sanity checking, the presence of a scheme means this is likely
  // a URL.
  if (parts->scheme.is_nonempty())
    return URL;

  // See if the host is an IP address.
  if (host_info.family == url_canon::CanonHostInfo::IPV4) {
    // If the user originally typed a host that looks like an IP address (a
    // dotted quad), they probably want to open it.  If the original input was
    // something else (like a single number), they probably wanted to search for
    // it, unless they explicitly typed a scheme.  This is true even if the URL
    // appears to have a path: "1.2/45" is more likely a search (for the answer
    // to a math problem) than a URL.
    if (host_info.num_ipv4_components == 4)
      return URL;
    return desired_tld.empty() ? UNKNOWN : REQUESTED_URL;
  }
  if (host_info.family == url_canon::CanonHostInfo::IPV6)
    return URL;

  // Now that we've ruled out invalid ports and queries that look like they have
  // a port, the presence of a port means this is likely a URL.
  if (parts->port.is_nonempty())
    return URL;

  // Presence of a password means this is likely a URL.  Note that unless the
  // user has typed an explicit "http://" or similar, we'll probably think that
  // the username is some unknown scheme, and bail out in the scheme-handling
  // code above.
  if (parts->password.is_nonempty())
    return URL;

  // The host doesn't look like a number, so see if the user's given us a path.
  if (parts->path.is_nonempty()) {
    // Most inputs with paths are URLs, even ones without known registries (e.g.
    // intranet URLs).  However, if there's no known registry and the path has
    // a space, this is more likely a query with a slash in the first term
    // (e.g. "ps/2 games") than a URL.  We can still open URLs with spaces in
    // the path by escaping the space, and we will still inline autocomplete
    // them if users have typed them in the past, but we default to searching
    // since that's the common case.
    return ((registry_length == 0) &&
            (text.substr(parts->path.begin, parts->path.len).find(' ') !=
                std::wstring::npos)) ? UNKNOWN : URL;
  }

  // If we reach here with a username, our input looks like "user@host".
  // Because there is no scheme explicitly specified, we think this is more
  // likely an email address than an HTTP auth attempt.  Hence, we search by
  // default and let users correct us on a case-by-case basis.
  if (parts->username.is_nonempty())
    return UNKNOWN;

  // We have a bare host string.  If it has a known TLD, it's probably a URL.
  if (registry_length != 0)
    return URL;

  // No TLD that we know about.  This could be:
  // * A string that the user wishes to add a desired_tld to to get a URL.  If
  //   we reach this point, we know there's no known TLD on the string, so the
  //   fixup code will be willing to add one; thus this is a URL.
  // * A single word "foo"; possibly an intranet site, but more likely a search.
  //   This is ideally an UNKNOWN, and we can let the Alternate Nav URL code
  //   catch our mistakes.
  // * A URL with a valid TLD we don't know about yet.  If e.g. a registrar adds
  //   "xxx" as a TLD, then until we add it to our data file, Chrome won't know
  //   "foo.xxx" is a real URL.  So ideally this is a URL, but we can't really
  //   distinguish this case from:
  // * A "URL-like" string that's not really a URL (like
  //   "browser.tabs.closeButtons" or "java.awt.event.*").  This is ideally a
  //   QUERY.  Since the above case and this one are indistinguishable, and this
  //   case is likely to be much more common, just say these are both UNKNOWN,
  //   which should default to the right thing and let users correct us on a
  //   case-by-case basis.
  return desired_tld.empty() ? UNKNOWN : REQUESTED_URL;
}

// static
void AutocompleteInput::ParseForEmphasizeComponents(
    const std::wstring& text,
    const std::wstring& desired_tld,
    url_parse::Component* scheme,
    url_parse::Component* host) {
  url_parse::Parsed parts;
  std::wstring scheme_str;
  Parse(text, desired_tld, &parts, &scheme_str);

  *scheme = parts.scheme;
  *host = parts.host;

  int after_scheme_and_colon = parts.scheme.end() + 1;
  // For the view-source scheme, we should emphasize the scheme and host of the
  // URL qualified by the view-source prefix.
  if (LowerCaseEqualsASCII(scheme_str, chrome::kViewSourceScheme) &&
      (static_cast<int>(text.length()) > after_scheme_and_colon)) {
    // Obtain the URL prefixed by view-source and parse it.
    std::wstring real_url(text.substr(after_scheme_and_colon));
    url_parse::Parsed real_parts;
    AutocompleteInput::Parse(real_url, desired_tld, &real_parts, NULL);
    if (real_parts.scheme.is_nonempty() || real_parts.host.is_nonempty()) {
      if (real_parts.scheme.is_nonempty()) {
        *scheme = url_parse::Component(
            after_scheme_and_colon + real_parts.scheme.begin,
            real_parts.scheme.len);
      } else {
        scheme->reset();
      }
      if (real_parts.host.is_nonempty()) {
        *host = url_parse::Component(
            after_scheme_and_colon + real_parts.host.begin,
            real_parts.host.len);
      } else {
        host->reset();
      }
    }
  }
}

// static
std::wstring AutocompleteInput::FormattedStringWithEquivalentMeaning(
    const GURL& url,
    const std::wstring& formatted_url) {
  if (!net::CanStripTrailingSlash(url))
    return formatted_url;
  const std::wstring url_with_path(formatted_url + L"/");
  return (AutocompleteInput::Parse(formatted_url, std::wstring(), NULL, NULL) ==
          AutocompleteInput::Parse(url_with_path, std::wstring(), NULL, NULL)) ?
      formatted_url : url_with_path;
}


bool AutocompleteInput::Equals(const AutocompleteInput& other) const {
  return (text_ == other.text_) &&
         (type_ == other.type_) &&
         (desired_tld_ == other.desired_tld_) &&
         (scheme_ == other.scheme_) &&
         (prevent_inline_autocomplete_ == other.prevent_inline_autocomplete_) &&
         (prefer_keyword_ == other.prefer_keyword_) &&
         (synchronous_only_ == other.synchronous_only_);
}

void AutocompleteInput::Clear() {
  text_.clear();
  type_ = INVALID;
  parts_ = url_parse::Parsed();
  scheme_.clear();
  desired_tld_.clear();
  prevent_inline_autocomplete_ = false;
  prefer_keyword_ = false;
}

// AutocompleteMatch ----------------------------------------------------------

AutocompleteMatch::AutocompleteMatch()
    : provider(NULL),
      relevance(0),
      deletable(false),
      inline_autocomplete_offset(std::wstring::npos),
      transition(PageTransition::GENERATED),
      is_history_what_you_typed_match(false),
      type(SEARCH_WHAT_YOU_TYPED),
      template_url(NULL),
      starred(false) {
}

AutocompleteMatch::AutocompleteMatch(AutocompleteProvider* provider,
                                     int relevance,
                                     bool deletable,
                                     Type type)
    : provider(provider),
      relevance(relevance),
      deletable(deletable),
      inline_autocomplete_offset(std::wstring::npos),
      transition(PageTransition::TYPED),
      is_history_what_you_typed_match(false),
      type(type),
      template_url(NULL),
      starred(false) {
}

AutocompleteMatch::~AutocompleteMatch() {
}

// static
std::string AutocompleteMatch::TypeToString(Type type) {
  const char* strings[NUM_TYPES] = {
    "url-what-you-typed",
    "history-url",
    "history-title",
    "history-body",
    "history-keyword",
    "navsuggest",
    "search-what-you-typed",
    "search-history",
    "search-suggest",
    "search-other-engine",
    "open-history-page",
  };
  DCHECK(arraysize(strings) == NUM_TYPES);
  return strings[type];
}

// static
int AutocompleteMatch::TypeToIcon(Type type) {
  int icons[NUM_TYPES] = {
    IDR_OMNIBOX_HTTP,
    IDR_OMNIBOX_HTTP,
    IDR_OMNIBOX_HISTORY,
    IDR_OMNIBOX_HISTORY,
    IDR_OMNIBOX_HISTORY,
    IDR_OMNIBOX_HTTP,
    IDR_OMNIBOX_SEARCH,
    IDR_OMNIBOX_SEARCH,
    IDR_OMNIBOX_SEARCH,
    IDR_OMNIBOX_SEARCH,
    IDR_OMNIBOX_MORE,
  };
  DCHECK(arraysize(icons) == NUM_TYPES);
  return icons[type];
}

// static
bool AutocompleteMatch::MoreRelevant(const AutocompleteMatch& elem1,
                                     const AutocompleteMatch& elem2) {
  // For equal-relevance matches, we sort alphabetically, so that providers
  // who return multiple elements at the same priority get a "stable" sort
  // across multiple updates.
  if (elem1.relevance == elem2.relevance)
    return elem1.contents > elem2.contents;

  // A negative relevance indicates the real relevance can be determined by
  // negating the value. If both relevances are negative, negate the result
  // so that we end up with positive relevances, then negative relevances with
  // the negative relevances sorted by absolute values.
  const bool result = elem1.relevance > elem2.relevance;
  return (elem1.relevance < 0 && elem2.relevance < 0) ? !result : result;
}

// static
bool AutocompleteMatch::DestinationSortFunc(const AutocompleteMatch& elem1,
                                            const AutocompleteMatch& elem2) {
  // Sort identical destination_urls together.  Place the most relevant matches
  // first, so that when we call std::unique(), these are the ones that get
  // preserved.
  return (elem1.destination_url != elem2.destination_url) ?
      (elem1.destination_url < elem2.destination_url) :
      MoreRelevant(elem1, elem2);
}

// static
bool AutocompleteMatch::DestinationsEqual(const AutocompleteMatch& elem1,
                                          const AutocompleteMatch& elem2) {
  return elem1.destination_url == elem2.destination_url;
}

// static
void AutocompleteMatch::ClassifyMatchInString(
    const std::wstring& find_text,
    const std::wstring& text,
    int style,
    ACMatchClassifications* classification) {
  ClassifyLocationInString(text.find(find_text), find_text.length(),
                           text.length(), style, classification);
}

void AutocompleteMatch::ClassifyLocationInString(
    size_t match_location,
    size_t match_length,
    size_t overall_length,
    int style,
    ACMatchClassifications* classification) {
  classification->clear();

  // Don't classify anything about an empty string
  // (AutocompleteMatch::Validate() checks this).
  if (overall_length == 0)
    return;

  // Mark pre-match portion of string (if any).
  if (match_location != 0) {
    classification->push_back(ACMatchClassification(0, style));
  }

  // Mark matching portion of string.
  if (match_location == std::wstring::npos) {
    // No match, above classification will suffice for whole string.
    return;
  }
  // Classifying an empty match makes no sense and will lead to validation
  // errors later.
  DCHECK(match_length > 0);
  classification->push_back(ACMatchClassification(match_location,
      (style | ACMatchClassification::MATCH) & ~ACMatchClassification::DIM));

  // Mark post-match portion of string (if any).
  const size_t after_match(match_location + match_length);
  if (after_match < overall_length) {
    classification->push_back(ACMatchClassification(after_match, style));
  }
}

#ifndef NDEBUG
void AutocompleteMatch::Validate() const {
  ValidateClassifications(contents, contents_class);
  ValidateClassifications(description, description_class);
}

void AutocompleteMatch::ValidateClassifications(
    const std::wstring& text,
    const ACMatchClassifications& classifications) const {
  if (text.empty()) {
    DCHECK(classifications.size() == 0);
    return;
  }

  // The classifications should always cover the whole string.
  DCHECK(classifications.size() > 0) << "No classification for text";
  DCHECK(classifications[0].offset == 0) << "Classification misses beginning";
  if (classifications.size() == 1)
    return;

  // The classifications should always be sorted.
  size_t last_offset = classifications[0].offset;
  for (ACMatchClassifications::const_iterator i(classifications.begin() + 1);
       i != classifications.end(); ++i) {
    DCHECK(i->offset > last_offset) << "Classification unsorted";
    DCHECK(i->offset < text.length()) << "Classification out of bounds";
    last_offset = i->offset;
  }
}
#endif

// AutocompleteProvider -------------------------------------------------------

// static
const size_t AutocompleteProvider::kMaxMatches = 3;

AutocompleteProvider::ACProviderListener::~ACProviderListener() {
}

AutocompleteProvider::AutocompleteProvider(ACProviderListener* listener,
                                           Profile* profile,
                                           const char* name)
    : profile_(profile),
      listener_(listener),
      done_(true),
      name_(name) {
}

void AutocompleteProvider::SetProfile(Profile* profile) {
  DCHECK(profile);
  DCHECK(done_);  // The controller should have already stopped us.
  profile_ = profile;
}

void AutocompleteProvider::Stop() {
  done_ = true;
}

void AutocompleteProvider::DeleteMatch(const AutocompleteMatch& match) {
}

AutocompleteProvider::~AutocompleteProvider() {
  Stop();
}

// static
bool AutocompleteProvider::HasHTTPScheme(const std::wstring& input) {
  std::string utf8_input(WideToUTF8(input));
  url_parse::Component scheme;
  if (url_util::FindAndCompareScheme(utf8_input, chrome::kViewSourceScheme,
                                     &scheme))
    utf8_input.erase(0, scheme.end() + 1);
  return url_util::FindAndCompareScheme(utf8_input, chrome::kHttpScheme, NULL);
}

void AutocompleteProvider::UpdateStarredStateOfMatches() {
  if (matches_.empty())
    return;

  if (!profile_)
    return;
  BookmarkModel* bookmark_model = profile_->GetBookmarkModel();
  if (!bookmark_model || !bookmark_model->IsLoaded())
    return;

  for (ACMatches::iterator i = matches_.begin(); i != matches_.end(); ++i)
    i->starred = bookmark_model->IsBookmarked(GURL(i->destination_url));
}

std::wstring AutocompleteProvider::StringForURLDisplay(const GURL& url,
                                                       bool check_accept_lang,
                                                       bool trim_http) const {
  std::string languages = (check_accept_lang && profile_) ?
      profile_->GetPrefs()->GetString(prefs::kAcceptLanguages) : std::string();
  return UTF16ToWideHack(net::FormatUrl(
      url,
      languages,
      net::kFormatUrlOmitAll & ~(trim_http ? 0 : net::kFormatUrlOmitHTTP),
      UnescapeRule::SPACES, NULL, NULL, NULL));
}

// AutocompleteResult ---------------------------------------------------------

// static
const size_t AutocompleteResult::kMaxMatches = 6;

void AutocompleteResult::Selection::Clear() {
  destination_url = GURL();
  provider_affinity = NULL;
  is_history_what_you_typed_match = false;
}

AutocompleteResult::AutocompleteResult() {
  // Reserve space for the max number of matches we'll show. The +1 accounts
  // for the history shortcut match as it isn't included in max_matches.
  matches_.reserve(kMaxMatches + 1);

  // It's probably safe to do this in the initializer list, but there's little
  // penalty to doing it here and it ensures our object is fully constructed
  // before calling member functions.
  default_match_ = end();
}

AutocompleteResult::~AutocompleteResult() {}

void AutocompleteResult::CopyFrom(const AutocompleteResult& rhs) {
  if (this == &rhs)
    return;

  matches_ = rhs.matches_;
  // Careful!  You can't just copy iterators from another container, you have to
  // reconstruct them.
  default_match_ = (rhs.default_match_ == rhs.end()) ?
      end() : (begin() + (rhs.default_match_ - rhs.begin()));

  alternate_nav_url_ = rhs.alternate_nav_url_;
}

void AutocompleteResult::AppendMatches(const ACMatches& matches) {
  std::copy(matches.begin(), matches.end(), std::back_inserter(matches_));
  default_match_ = end();
  alternate_nav_url_ = GURL();
}

void AutocompleteResult::AddMatch(const AutocompleteMatch& match) {
  DCHECK(default_match_ != end());
  ACMatches::iterator insertion_point =
      std::upper_bound(begin(), end(), match, &AutocompleteMatch::MoreRelevant);
  ACMatches::iterator::difference_type default_offset =
      default_match_ - begin();
  if ((insertion_point - begin()) <= default_offset)
    ++default_offset;
  matches_.insert(insertion_point, match);
  default_match_ = begin() + default_offset;
}

void AutocompleteResult::SortAndCull(const AutocompleteInput& input) {
  // Remove duplicates.
  std::sort(matches_.begin(), matches_.end(),
            &AutocompleteMatch::DestinationSortFunc);
  matches_.erase(std::unique(matches_.begin(), matches_.end(),
                             &AutocompleteMatch::DestinationsEqual),
                 matches_.end());

  // Find the top |kMaxMatches| matches.
  if (matches_.size() > kMaxMatches) {
    std::partial_sort(matches_.begin(), matches_.begin() + kMaxMatches,
                      matches_.end(), &AutocompleteMatch::MoreRelevant);
    matches_.erase(matches_.begin() + kMaxMatches, matches_.end());
  }

  // HistoryContentsProvider uses a negative relevance as a way to avoid
  // starving out other provider matches, yet we may end up using this match. To
  // make sure such matches are sorted correctly we search for all
  // relevances < 0 and negate them. If we change our relevance algorithm to
  // properly mix different providers' matches, this can go away.
  for (ACMatches::iterator i = matches_.begin(); i != matches_.end(); ++i) {
    if (i->relevance < 0)
      i->relevance = -i->relevance;
  }

  // Put the final result set in order.
  std::sort(matches_.begin(), matches_.end(), &AutocompleteMatch::MoreRelevant);
  default_match_ = begin();

  // Set the alternate nav URL.
  alternate_nav_url_ = GURL();
  if (((input.type() == AutocompleteInput::UNKNOWN) ||
       (input.type() == AutocompleteInput::REQUESTED_URL)) &&
      (default_match_ != end()) &&
      (default_match_->transition != PageTransition::TYPED) &&
      (default_match_->transition != PageTransition::KEYWORD) &&
      (input.canonicalized_url() != default_match_->destination_url))
    alternate_nav_url_ = input.canonicalized_url();
}

void AutocompleteResult::Reset() {
  matches_.clear();
  default_match_ = end();
}

#ifndef NDEBUG
void AutocompleteResult::Validate() const {
  for (const_iterator i(begin()); i != end(); ++i)
    i->Validate();
}
#endif

// AutocompleteController -----------------------------------------------------

const int AutocompleteController::kNoItemSelected = -1;

namespace {
// The time we'll wait between sending updates to our observers (balances
// flicker against lag).
const int kUpdateDelayMs = 350;
};

AutocompleteController::AutocompleteController(Profile* profile)
    : updated_latest_result_(false),
      delay_interval_has_passed_(false),
      have_committed_during_this_query_(false),
      done_(true) {
  providers_.push_back(new SearchProvider(this, profile));
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableHistoryQuickProvider))
    providers_.push_back(new HistoryQuickProvider(this, profile));
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableHistoryURLProvider))
    providers_.push_back(new HistoryURLProvider(this, profile));
  providers_.push_back(new KeywordProvider(this, profile));
  history_contents_provider_ = new HistoryContentsProvider(this, profile);
  providers_.push_back(history_contents_provider_);
  for (ACProviders::iterator i(providers_.begin()); i != providers_.end(); ++i)
    (*i)->AddRef();
}

AutocompleteController::~AutocompleteController() {
  // The providers may have tasks outstanding that hold refs to them.  We need
  // to ensure they won't call us back if they outlive us.  (Practically,
  // calling Stop() should also cancel those tasks and make it so that we hold
  // the only refs.)  We also don't want to bother notifying anyone of our
  // result changes here, because the notification observer is in the midst of
  // shutdown too, so we don't ask Stop() to clear |result_| (and notify).
  result_.Reset();  // Not really necessary.
  Stop(false);

  for (ACProviders::iterator i(providers_.begin()); i != providers_.end(); ++i)
    (*i)->Release();

  providers_.clear();  // Not really necessary.
}

void AutocompleteController::SetProfile(Profile* profile) {
  Stop(true);
  for (ACProviders::iterator i(providers_.begin()); i != providers_.end(); ++i)
    (*i)->SetProfile(profile);
  input_.Clear();  // Ensure we don't try to do a "minimal_changes" query on a
                   // different profile.
}

void AutocompleteController::Start(const std::wstring& text,
                                   const std::wstring& desired_tld,
                                   bool prevent_inline_autocomplete,
                                   bool prefer_keyword,
                                   bool synchronous_only) {
  const std::wstring old_input_text(input_.text());
  const bool old_synchronous_only = input_.synchronous_only();
  input_ = AutocompleteInput(text, desired_tld, prevent_inline_autocomplete,
                             prefer_keyword, synchronous_only);

  // See if we can avoid rerunning autocomplete when the query hasn't changed
  // much.  When the user presses or releases the ctrl key, the desired_tld
  // changes, and when the user finishes an IME composition, inline autocomplete
  // may no longer be prevented.  In both these cases the text itself hasn't
  // changed since the last query, and some providers can do much less work (and
  // get matches back more quickly).  Taking advantage of this reduces flicker.
  //
  // NOTE: This comes after constructing |input_| above since that construction
  // can change the text string (e.g. by stripping off a leading '?').
  const bool minimal_changes = (input_.text() == old_input_text) &&
      (input_.synchronous_only() == old_synchronous_only);

  // If we're interrupting an old query, and committing its result won't shrink
  // the visible set (which would probably re-expand soon, thus looking very
  // flickery), then go ahead and commit what we've got, in order to feel more
  // responsive when the user is typing rapidly.  In this case it's important
  // that we don't update the edit, as the user has already changed its contents
  // and anything we might do with it (e.g. inline autocomplete) likely no
  // longer applies.
  if (!minimal_changes && !done_ && (latest_result_.size() >= result_.size()))
    CommitResult(false);

  // If the timer is already running, it could fire shortly after starting this
  // query, when we're likely to only have the synchronous results back, thus
  // almost certainly causing flicker.  Reset it, except when we haven't
  // committed anything for the past query, in which case the user is typing
  // quickly and we need to keep running the timer lest we lag too far behind.
  if (have_committed_during_this_query_) {
    update_delay_timer_.Stop();
    delay_interval_has_passed_ = false;
  }

  // Start the new query.
  have_committed_during_this_query_ = false;
  for (ACProviders::iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    (*i)->Start(input_, minimal_changes);
    if (synchronous_only)
      DCHECK((*i)->done());
  }
  CheckIfDone();
  UpdateLatestResult(true);
}

void AutocompleteController::Stop(bool clear_result) {
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    (*i)->Stop();
  }

  update_delay_timer_.Stop();
  updated_latest_result_ = false;
  delay_interval_has_passed_ = false;
  done_ = true;
  if (clear_result && !result_.empty()) {
    result_.Reset();
    NotificationService::current()->Notify(
        NotificationType::AUTOCOMPLETE_CONTROLLER_RESULT_UPDATED,
        Source<AutocompleteController>(this),
        Details<const AutocompleteResult>(&result_));
    // NOTE: We don't notify AUTOCOMPLETE_CONTROLLER_DEFAULT_MATCH_UPDATED since
    // we're trying to only clear the popup, not touch the edit... this is all
    // a mess and should be cleaned up :(
  }
  latest_result_.CopyFrom(result_);
}

void AutocompleteController::DeleteMatch(const AutocompleteMatch& match) {
  DCHECK(match.deletable);
  match.provider->DeleteMatch(match);  // This may synchronously call back to
                                       // OnProviderUpdate().
  CommitResult(true);  // Ensure any new result gets committed immediately.  If
                       // it was committed already or hasn't been modified, this
                       // is harmless.
}

void AutocompleteController::CommitIfQueryHasNeverBeenCommitted() {
  if (!have_committed_during_this_query_)
    CommitResult(true);
}

void AutocompleteController::OnProviderUpdate(bool updated_matches) {
  CheckIfDone();
  if (updated_matches || done_)
    UpdateLatestResult(false);
}

void AutocompleteController::UpdateLatestResult(bool is_synchronous_pass) {
  // Add all providers' matches.
  latest_result_.Reset();
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i)
    latest_result_.AppendMatches((*i)->matches());
  updated_latest_result_ = true;

  // Sort the matches and trim to a small number of "best" matches.
  latest_result_.SortAndCull(input_);

  if (history_contents_provider_)
    AddHistoryContentsShortcut();

#ifndef NDEBUG
  latest_result_.Validate();
#endif

  if (is_synchronous_pass) {
    if (!update_delay_timer_.IsRunning()) {
      update_delay_timer_.Start(
          TimeDelta::FromMilliseconds(kUpdateDelayMs),
          this, &AutocompleteController::DelayTimerFired);
    }

    NotificationService::current()->Notify(
        NotificationType::AUTOCOMPLETE_CONTROLLER_DEFAULT_MATCH_UPDATED,
        Source<AutocompleteController>(this),
        Details<const AutocompleteResult>(&latest_result_));
  }

  // If nothing is visible, commit immediately so that the first character the
  // user types produces an instant response.  If the query has finished and we
  // haven't ever committed a result set, commit immediately to minimize lag.
  // Otherwise, only commit when it's been at least one delay interval since the
  // last commit, to minimize flicker.
  if (result_.empty() || (done_ && !have_committed_during_this_query_) ||
      delay_interval_has_passed_)
    CommitResult(true);
}

void AutocompleteController::DelayTimerFired() {
  delay_interval_has_passed_ = true;
  CommitResult(true);
}

void AutocompleteController::CommitResult(bool notify_default_match) {
  if (done_) {
    update_delay_timer_.Stop();
    delay_interval_has_passed_ = false;
  }

  // Don't send update notifications when nothing's actually changed.
  if (!updated_latest_result_)
    return;

  updated_latest_result_ = false;
  delay_interval_has_passed_ = false;
  have_committed_during_this_query_ = true;
  result_.CopyFrom(latest_result_);
  NotificationService::current()->Notify(
      NotificationType::AUTOCOMPLETE_CONTROLLER_RESULT_UPDATED,
      Source<AutocompleteController>(this),
      Details<const AutocompleteResult>(&result_));
  if (notify_default_match) {
    // This notification must be sent after the other so the popup has time to
    // update its state before the edit calls into it.
    // TODO(pkasting): Eliminate this ordering requirement.
    NotificationService::current()->Notify(
        NotificationType::AUTOCOMPLETE_CONTROLLER_DEFAULT_MATCH_UPDATED,
        Source<AutocompleteController>(this),
        Details<const AutocompleteResult>(&result_));
  }
  if (!done_)
    update_delay_timer_.Reset();
}

ACMatches AutocompleteController::GetMatchesNotInLatestResult(
    const AutocompleteProvider* provider) const {
  DCHECK(provider);

  // Determine the set of destination URLs.
  std::set<GURL> destination_urls;
  for (AutocompleteResult::const_iterator i(latest_result_.begin());
       i != latest_result_.end(); ++i)
    destination_urls.insert(i->destination_url);

  ACMatches matches;
  const ACMatches& provider_matches = provider->matches();
  for (ACMatches::const_iterator i = provider_matches.begin();
       i != provider_matches.end(); ++i) {
    if (destination_urls.find(i->destination_url) == destination_urls.end())
      matches.push_back(*i);
  }

  return matches;
}

void AutocompleteController::AddHistoryContentsShortcut() {
  DCHECK(history_contents_provider_);
  // Only check the history contents provider if the history contents provider
  // is done and has matches.
  if (!history_contents_provider_->done() ||
      !history_contents_provider_->db_match_count()) {
    return;
  }

  if ((history_contents_provider_->db_match_count() <=
          (latest_result_.size() + 1)) ||
      (history_contents_provider_->db_match_count() == 1)) {
    // We only want to add a shortcut if we're not already showing the matches.
    ACMatches matches(GetMatchesNotInLatestResult(history_contents_provider_));
    if (matches.empty())
      return;
    if (matches.size() == 1) {
      // Only one match not shown, add it. The relevance may be negative,
      // which means we need to negate it to get the true relevance.
      AutocompleteMatch& match = matches.front();
      if (match.relevance < 0)
        match.relevance = -match.relevance;
      latest_result_.AddMatch(match);
      return;
    } // else, fall through and add item.
  }

  AutocompleteMatch match(NULL, 0, false, AutocompleteMatch::OPEN_HISTORY_PAGE);
  match.fill_into_edit = input_.text();

  // Mark up the text such that the user input text is bold.
  size_t keyword_offset = std::wstring::npos;  // Offset into match.contents.
  if (history_contents_provider_->db_match_count() ==
      history_contents_provider_->kMaxMatchCount) {
    // History contents searcher has maxed out.
    match.contents = l10n_util::GetStringF(IDS_OMNIBOX_RECENT_HISTORY_MANY,
                                           input_.text(),
                                           &keyword_offset);
  } else {
    // We can report exact matches when there aren't too many.
    std::vector<size_t> content_param_offsets;
    match.contents = l10n_util::GetStringF(
        IDS_OMNIBOX_RECENT_HISTORY,
        UTF16ToWide(base::FormatNumber(history_contents_provider_->
                                           db_match_count())),
        input_.text(),
        &content_param_offsets);

    // content_param_offsets is ordered based on supplied params, we expect
    // that the second one contains the query (first is the number).
    if (content_param_offsets.size() == 2) {
      keyword_offset = content_param_offsets[1];
    } else {
      // See comments on an identical NOTREACHED() in search_provider.cc.
      NOTREACHED();
    }
  }

  // NOTE: This comparison succeeds when keyword_offset == std::wstring::npos.
  if (keyword_offset > 0) {
    match.contents_class.push_back(
        ACMatchClassification(0, ACMatchClassification::NONE));
  }
  match.contents_class.push_back(
      ACMatchClassification(keyword_offset, ACMatchClassification::MATCH));
  if (keyword_offset + input_.text().size() < match.contents.size()) {
    match.contents_class.push_back(
        ACMatchClassification(keyword_offset + input_.text().size(),
                              ACMatchClassification::NONE));
  }
  match.destination_url =
      HistoryUI::GetHistoryURLWithSearchText(WideToUTF16(input_.text()));
  match.transition = PageTransition::AUTO_BOOKMARK;
  match.provider = history_contents_provider_;
  latest_result_.AddMatch(match);
}

void AutocompleteController::CheckIfDone() {
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    if (!(*i)->done()) {
      done_ = false;
      return;
    }
  }
  done_ = true;
}
