// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brought to you by the letter D and the number 2.

#ifndef NET_BASE_COOKIE_MONSTER_H_
#define NET_BASE_COOKIE_MONSTER_H_
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/lock.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "net/base/cookie_store.h"

class GURL;
class Histogram;

namespace net {

// The cookie monster is the system for storing and retrieving cookies. It has
// an in-memory list of all cookies, and synchronizes non-session cookies to an
// optional permanent storage that implements the PersistentCookieStore
// interface.
//
// This class IS thread-safe. Normally, it is only used on the I/O thread, but
// is also accessed directly through Automation for UI testing.
//
// TODO(deanm) Implement CookieMonster, the cookie database.
//  - Verify that our domain enforcement and non-dotted handling is correct
class CookieMonster : public CookieStore {
 public:
  class CanonicalCookie;
  class Delegate;
  class ParsedCookie;
  class PersistentCookieStore;

  // Terminology:
  //    * The 'top level domain' (TLD) of an internet domain name is
  //      the terminal "." free substring (e.g. "com" for google.com
  //      or world.std.com).
  //    * The 'effective top level domain' (eTLD) is the longest
  //      "." initiated terminal substring of an internet domain name
  //      that is controlled by a general domain registrar.
  //      (e.g. "co.uk" for news.bbc.co.uk).
  //    * The 'effective top level domain plus one' (eTLD+1) is the
  //      shortest "." delimited terminal substring of an internet
  //      domain name that is not controlled by a general domain
  //      registrar (e.g. "bbc.co.uk" for news.bbc.co.uk, or
  //      "google.com" for news.google.com).  The general assumption
  //      is that all hosts and domains under an eTLD+1 share some
  //      administrative control.

  // CookieMap is the central data structure of the CookieMonster.  It
  // is a map whose values are pointers to CanonicalCookie data
  // structures (the data structures are owned by the CookieMonster
  // and must be destroyed when removed from the map).  There are two
  // possible keys for the map, controlled on a per-CookieMonster basis
  // by use_effective_domain_key_scheme_/SetKeyScheme()
  // (defaulted by use_effective_domain_key_default_):

  // If use_effective_domain_key_scheme_ is true (default), then the key is
  // based on the effective domain of the cookies.  If the domain
  // of the cookie has an eTLD+1, that is the key for the map.  If the
  // domain of the cookie does not have an eTLD+1, the key of the map
  // is the host the cookie applies to (it is not legal to have domain
  // cookies without an eTLD+1).  This rule excludes cookies for,
  // e.g, ".com", ".co.uk", or ".internalnetwork".

  // If use_effective_domain_key_scheme_ is false, then the key is
  // just the domain of the cookie.  Eventually, this option will be
  // removed.

  // NOTE(deanm):
  // I benchmarked hash_multimap vs multimap.  We're going to be query-heavy
  // so it would seem like hashing would help.  However they were very
  // close, with multimap being a tiny bit faster.  I think this is because
  // our map is at max around 1000 entries, and the additional complexity
  // for the hashing might not overcome the O(log(1000)) for querying
  // a multimap.  Also, multimap is standard, another reason to use it.
  // TODO(rdsmith): This benchmark should be re-done now that we're allowing
  // subtantially more entries in the map.
  typedef std::multimap<std::string, CanonicalCookie*> CookieMap;
  typedef std::pair<CookieMap::iterator, CookieMap::iterator> CookieMapItPair;
  typedef std::vector<CanonicalCookie> CookieList;

  // The store passed in should not have had Init() called on it yet. This
  // class will take care of initializing it. The backing store is NOT owned by
  // this class, but it must remain valid for the duration of the cookie
  // monster's existence. If |store| is NULL, then no backing store will be
  // updated. If |delegate| is non-NULL, it will be notified on
  // creation/deletion of cookies.
  CookieMonster(PersistentCookieStore* store, Delegate* delegate);

  // Only used during unit testing.
  CookieMonster(PersistentCookieStore* store,
                Delegate* delegate,
                int last_access_threshold_milliseconds);

  // Parses the string with the cookie time (very forgivingly).
  static base::Time ParseCookieTime(const std::string& time_string);

  // Returns true if a domain string represents a host-only cookie,
  // i.e. it doesn't begin with a leading '.' character.
  static bool DomainIsHostOnly(const std::string& domain_string);

  // CookieStore implementation.

  // Sets the cookies specified by |cookie_list| returned from |url|
  // with options |options| in effect.
  virtual bool SetCookieWithOptions(const GURL& url,
                                    const std::string& cookie_line,
                                    const CookieOptions& options);

  // Gets all cookies that apply to |url| given |options|.
  // The returned cookies are ordered by longest path, then earliest
  // creation date.
  virtual std::string GetCookiesWithOptions(const GURL& url,
                                            const CookieOptions& options);

  // Deletes all cookies with that might apply to |url| that has |cookie_name|.
  virtual void DeleteCookie(const GURL& url, const std::string& cookie_name);

  virtual CookieMonster* GetCookieMonster() { return this; }

  // Sets a cookie given explicit user-provided cookie attributes. The cookie
  // name, value, domain, etc. are each provided as separate strings. This
  // function expects each attribute to be well-formed. It will check for
  // disallowed characters (e.g. the ';' character is disallowed within the
  // cookie value attribute) and will return false without setting the cookie
  // if such characters are found.
  bool SetCookieWithDetails(const GURL& url,
                            const std::string& name,
                            const std::string& value,
                            const std::string& domain,
                            const std::string& path,
                            const base::Time& expiration_time,
                            bool secure, bool http_only);

  // Returns all the cookies, for use in management UI, etc. This does not mark
  // the cookies as having been accessed.
  // The returned cookies are ordered by longest path, then by earliest
  // creation date.
  CookieList GetAllCookies();

  // Returns all the cookies, for use in management UI, etc. Filters results
  // using given url scheme, host / domain and path. This does not mark the
  // cookies as having been accessed.
  // The returned cookies are ordered by longest path, then earliest
  // creation date.
  CookieList GetAllCookiesForURL(const GURL& url);

  // Deletes all of the cookies.
  int DeleteAll(bool sync_to_store);
  // Deletes all of the cookies that have a creation_date greater than or equal
  // to |delete_begin| and less than |delete_end|
  int DeleteAllCreatedBetween(const base::Time& delete_begin,
                              const base::Time& delete_end,
                              bool sync_to_store);
  // Deletes all of the cookies that have a creation_date more recent than the
  // one passed into the function via |delete_after|.
  int DeleteAllCreatedAfter(const base::Time& delete_begin, bool sync_to_store);

  // Deletes all cookies that match the host of the given URL
  // regardless of path.  This includes all http_only and secure cookies,
  // but does not include any domain cookies that may apply to this host.
  // Returns the number of cookies deleted.
  int DeleteAllForHost(const GURL& url);

  // Deletes one specific cookie.
  bool DeleteCanonicalCookie(const CanonicalCookie& cookie);

  // Override the default list of schemes that are allowed to be set in
  // this cookie store.  Calling his overrides the value of
  // "enable_file_scheme_".
  // If this this method is called, it must be called before first use of
  // the instance (i.e. as part of the instance initialization process).
  void SetCookieableSchemes(const char* schemes[], size_t num_schemes);

  // Overrides the default key scheme.  This function must be called
  // before initialization.
  void SetKeyScheme(bool use_effective_domain_key);

  // There are some unknowns about how to correctly handle file:// cookies,
  // and our implementation for this is not robust enough. This allows you
  // to enable support, but it should only be used for testing. Bug 1157243.
  // Must be called before creating a CookieMonster instance.
  static void EnableFileScheme();
  static bool enable_file_scheme_;

 private:
  ~CookieMonster();

  // Testing support.
  // For SetCookieWithCreationTime.
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest,
                           TestCookieDeleteAllCreatedAfterTimestamp);
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest,
                           TestCookieDeleteAllCreatedBetweenTimestamps);

  // For gargage collection constants.
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest, TestHostGarbageCollection);
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest, TestTotalGarbageCollection);

  // For validation of key values.
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest, TestDomainTree);
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest, TestImport);
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest, GetKey);
  FRIEND_TEST_ALL_PREFIXES(CookieMonsterTest, TestGetKey);

  // Cookie garbage collection thresholds.  Based off of the Mozilla defaults.
  // When the number of cookies gets to k{Domain,}MaxCookies
  // purge down to k{Domain,}MaxCookies - k{Domain,}PurgeCookies.
  // It might seem scary to have a high purge value, but really it's not.
  // You just make sure that you increase the max to cover the increase
  // in purge, and we would have been purging the same amount of cookies.
  // We're just going through the garbage collection process less often.
  // Note that the DOMAIN values are per eTLD+1; see comment for the
  // CookieMap typedef.  So, e.g., the maximum number of cookies allowed for
  // google.com and all of its subdomains will be 150-180.
  //
  // Present in .h file to make accessible to tests through FRIEND_TEST.
  // Actual definitions are in cookie_monster.cc.
  static const size_t kDomainMaxCookies;
  static const size_t kDomainPurgeCookies;
  static const size_t kMaxCookies;
  static const size_t kPurgeCookies;

  // Default value for key scheme.  true means to use the new
  // key scheme based on effective domain; false to use the
  // old key scheme based on full domain.
  static const bool use_effective_domain_key_default_ = true;

  bool SetCookieWithCreationTime(const GURL& url,
                                 const std::string& cookie_line,
                                 const base::Time& creation_time);

  // Called by all non-static functions to ensure that the cookies store has
  // been initialized. This is not done during creating so it doesn't block
  // the window showing.
  // Note: this method should always be called with lock_ held.
  void InitIfNecessary() {
    if (!initialized_) {
      if (store_)
        InitStore();
      initialized_ = true;
    }
  }

  // Initializes the backing store and reads existing cookies from it.
  // Should only be called by InitIfNecessary().
  void InitStore();

  // Checks that |cookies_| matches our invariants, and tries to repair any
  // inconsistencies. (In other words, it does not have duplicate cookies).
  void EnsureCookiesMapIsValid();

  // Checks for any duplicate cookies for CookieMap key |key| which lie between
  // |begin| and |end|. If any are found, all but the most recent are deleted.
  // Returns the number of duplicate cookies that were deleted.
  int TrimDuplicateCookiesForKey(const std::string& key,
                                 CookieMap::iterator begin,
                                 CookieMap::iterator end);

  void SetDefaultCookieableSchemes();

  void FindCookiesForHostAndDomain(const GURL& url,
                                   const CookieOptions& options,
                                   bool update_access_time,
                                   std::vector<CanonicalCookie*>* cookies);

  void FindCookiesForKey(const std::string& key,
                         const GURL& url,
                         const CookieOptions& options,
                         const base::Time& current,
                         bool update_access_time,
                         std::vector<CanonicalCookie*>* cookies);

  // Delete any cookies that are equivalent to |ecc| (same path, domain, etc).
  // If |skip_httponly| is true, httponly cookies will not be deleted.  The
  // return value with be true if |skip_httponly| skipped an httponly cookie.
  // |key| is the key to find the cookie in cookies_; see the comment before
  // the CookieMap typedef for details.
  // NOTE: There should never be more than a single matching equivalent cookie.
  bool DeleteAnyEquivalentCookie(const std::string& key,
                                 const CanonicalCookie& ecc,
                                 bool skip_httponly);

  // Takes ownership of *cc.
  void InternalInsertCookie(const std::string& key,
                            CanonicalCookie* cc,
                            bool sync_to_store);

  // Helper function that sets cookies with more control.
  // Not exposed as we don't want callers to have the ability
  // to specify (potentially duplicate) creation times.
  bool SetCookieWithCreationTimeAndOptions(const GURL& url,
                                           const std::string& cookie_line,
                                           const base::Time& creation_time,
                                           const CookieOptions& options);


  // Helper function that sets a canonical cookie, deleting equivalents and
  // performing garbage collection.
  bool SetCanonicalCookie(scoped_ptr<CanonicalCookie>* cc,
                          const base::Time& creation_time,
                          const CookieOptions& options);

  void InternalUpdateCookieAccessTime(CanonicalCookie* cc,
                                      const base::Time& current_time);

  enum DeletionCause {
    DELETE_COOKIE_EXPLICIT,
    DELETE_COOKIE_OVERWRITE,
    DELETE_COOKIE_EXPIRED,
    DELETE_COOKIE_EVICTED,
    DELETE_COOKIE_DUPLICATE_IN_BACKING_STORE,
    DELETE_COOKIE_DONT_RECORD,  // e.g. For final cleanup after flush to store.
    DELETE_COOKIE_EVICTED_DOMAIN,
    DELETE_COOKIE_EVICTED_GLOBAL,
    DELETE_COOKIE_LAST_ENTRY = DELETE_COOKIE_EVICTED_GLOBAL
  };

  // |deletion_cause| argument is for collecting statistics.
  void InternalDeleteCookie(CookieMap::iterator it, bool sync_to_store,
                            DeletionCause deletion_cause);

  // If the number of cookies for CookieMap key |key|, or globally, are
  // over the preset maximums above, garbage collect, first for the host and
  // then globally, as described by GarbageCollectRange().
  //
  // Returns the number of cookies deleted (useful for debugging).
  int GarbageCollect(const base::Time& current, const std::string& key);

  // Helper for GarbageCollectRange(); can be called directly as well.  Deletes
  // all expired cookies in |itpair|.  If |cookie_its| is non-NULL, it is
  // populated with all the non-expired cookies from |itpair|.
  //
  // Returns the number of cookies deleted.
  int GarbageCollectExpired(const base::Time& current,
                            const CookieMapItPair& itpair,
                            std::vector<CookieMap::iterator>* cookie_its);

  // If needed, evicts least recently accessed cookies in iterator
  // list until (|num_max| - |num_purge|) cookies remain.
  int GarbageCollectEvict(
      const base::Time& current,
      size_t num_max,
      size_t num_purge,
      DeletionCause cause,
      std::vector<CookieMap::iterator>* cookie_its);

  // Find the key (for lookup in cookies_) based on the given domain.
  // See comment on keys before the CookieMap typedef.
  std::string GetKey(const std::string& domain) const;

  bool HasCookieableScheme(const GURL& url);

  // Statistics support
  // Record statistics every kRecordStatisticsIntervalSeconds of uptime.
  static const int kRecordStatisticsIntervalSeconds = 10 * 60;

  // This function should be called repeatedly, and will record
  // statistics if a sufficient time period has passed.
  void RecordPeriodicStats(const base::Time& current_time);

  // Histogram variables; see CookieMonster::InitializeHistograms() in
  // cookie_monster.cc for details.
  scoped_refptr<Histogram> histogram_expiration_duration_minutes_;
  scoped_refptr<Histogram> histogram_between_access_interval_minutes_;
  scoped_refptr<Histogram> histogram_evicted_last_access_minutes_;
  scoped_refptr<Histogram> histogram_count_;
  scoped_refptr<Histogram> histogram_domain_count_;
  scoped_refptr<Histogram> histogram_etldp1_count_;
  scoped_refptr<Histogram> histogram_domain_per_etldp1_count_;
  scoped_refptr<Histogram> histogram_number_duplicate_db_cookies_;
  scoped_refptr<Histogram> histogram_cookie_deletion_cause_;
  scoped_refptr<Histogram> histogram_time_get_;
  scoped_refptr<Histogram> histogram_time_load_;

  // Initialize the above variables; should only be called from
  // the constructor.
  void InitializeHistograms();

  CookieMap cookies_;

  // Indicates whether the cookie store has been initialized. This happens
  // lazily in InitStoreIfNecessary().
  bool initialized_;

  // Indicates whether this cookie monster uses the new effective domain
  // key scheme or not.
  bool use_effective_domain_key_scheme_;

  scoped_refptr<PersistentCookieStore> store_;

  // The resolution of our time isn't enough, so we do something
  // ugly and increment when we've seen the same time twice.
  base::Time CurrentTime();
  base::Time last_time_seen_;

  // Minimum delay after updating a cookie's LastAccessDate before we will
  // update it again.
  const base::TimeDelta last_access_threshold_;

  std::vector<std::string> cookieable_schemes_;

  scoped_refptr<Delegate> delegate_;

  // Lock for thread-safety
  Lock lock_;

  base::Time last_statistic_record_time_;

  DISALLOW_COPY_AND_ASSIGN(CookieMonster);
};

class CookieMonster::CanonicalCookie {
 public:

  // These constructors do no validation or canonicalization of their inputs;
  // the resulting CanonicalCookies should not be relied on to be canonical
  // unless the caller has done appropriate validation and canonicalization
  // themselves.
  CanonicalCookie();
  CanonicalCookie(const std::string& name,
                  const std::string& value,
                  const std::string& domain,
                  const std::string& path,
                  bool secure,
                  bool httponly,
                  const base::Time& creation,
                  const base::Time& last_access,
                  bool has_expires,
                  const base::Time& expires);

  // This constructor does canonicalization but not validation.
  // The result of this constructor should not be relied on in contexts
  // in which pre-validation of the ParsedCookie has not been done.
  CanonicalCookie(const GURL& url, const ParsedCookie& pc);

  ~CanonicalCookie();

  // Supports the default copy constructor.

  // Creates a canonical cookie from unparsed attribute values.
  // Canonicalizes and validates inputs.  May return NULL if an attribute
  // value is invalid.
  static CanonicalCookie* Create(
      const GURL& url, const std::string& name, const std::string& value,
      const std::string& domain, const std::string& path,
      const base::Time& creation_time, const base::Time& expiration_time,
      bool secure, bool http_only);

  const std::string& Name() const { return name_; }
  const std::string& Value() const { return value_; }
  const std::string& Domain() const { return domain_; }
  const std::string& Path() const { return path_; }
  const base::Time& CreationDate() const { return creation_date_; }
  const base::Time& LastAccessDate() const { return last_access_date_; }
  bool DoesExpire() const { return has_expires_; }
  bool IsPersistent() const { return DoesExpire(); }
  const base::Time& ExpiryDate() const { return expiry_date_; }
  bool IsSecure() const { return secure_; }
  bool IsHttpOnly() const { return httponly_; }
  bool IsDomainCookie() const {
    return !domain_.empty() && domain_[0] == '.'; }
  bool IsHostCookie() const { return !IsDomainCookie(); }

  bool IsExpired(const base::Time& current) {
    return has_expires_ && current >= expiry_date_;
  }

  // Are the cookies considered equivalent in the eyes of RFC 2965.
  // The RFC says that name must match (case-sensitive), domain must
  // match (case insensitive), and path must match (case sensitive).
  // For the case insensitive domain compare, we rely on the domain
  // having been canonicalized (in
  // GetCookieDomainWithString->CanonicalizeHost).
  bool IsEquivalent(const CanonicalCookie& ecc) const {
    // It seems like it would make sense to take secure and httponly into
    // account, but the RFC doesn't specify this.
    // NOTE: Keep this logic in-sync with TrimDuplicateCookiesForHost().
    return (name_ == ecc.Name() && domain_ == ecc.Domain()
            && path_ == ecc.Path());
  }

  void SetLastAccessDate(const base::Time& date) {
    last_access_date_ = date;
  }

  bool IsOnPath(const std::string& url_path) const;
  bool IsDomainMatch(const std::string& scheme, const std::string& host) const;

  std::string DebugString() const;
 private:
  std::string name_;
  std::string value_;
  std::string domain_;
  std::string path_;
  base::Time creation_date_;
  base::Time last_access_date_;
  base::Time expiry_date_;
  bool has_expires_;
  bool secure_;
  bool httponly_;
};

class CookieMonster::Delegate
    : public base::RefCountedThreadSafe<CookieMonster::Delegate> {
 public:
  // Will be called when a cookie is added or removed. The function is passed
  // the respective |cookie| which was added to or removed from the cookies.
  // If |removed| is true, the cookie was deleted.
  virtual void OnCookieChanged(const CookieMonster::CanonicalCookie& cookie,
                               bool removed) = 0;
 protected:
  friend class base::RefCountedThreadSafe<CookieMonster::Delegate>;
  virtual ~Delegate() {}
};

class CookieMonster::ParsedCookie {
 public:
  typedef std::pair<std::string, std::string> TokenValuePair;
  typedef std::vector<TokenValuePair> PairList;

  // The maximum length of a cookie string we will try to parse
  static const size_t kMaxCookieSize = 4096;
  // The maximum number of Token/Value pairs.  Shouldn't have more than 8.
  static const int kMaxPairs = 16;

  // Construct from a cookie string like "BLAH=1; path=/; domain=.google.com"
  ParsedCookie(const std::string& cookie_line);
  ~ParsedCookie() { }

  // You should not call any other methods on the class if !IsValid
  bool IsValid() const { return is_valid_; }

  const std::string& Name() const { return pairs_[0].first; }
  const std::string& Token() const { return Name(); }
  const std::string& Value() const { return pairs_[0].second; }

  bool HasPath() const { return path_index_ != 0; }
  const std::string& Path() const { return pairs_[path_index_].second; }
  bool HasDomain() const { return domain_index_ != 0; }
  const std::string& Domain() const { return pairs_[domain_index_].second; }
  bool HasExpires() const { return expires_index_ != 0; }
  const std::string& Expires() const { return pairs_[expires_index_].second; }
  bool HasMaxAge() const { return maxage_index_ != 0; }
  const std::string& MaxAge() const { return pairs_[maxage_index_].second; }
  bool IsSecure() const { return secure_index_ != 0; }
  bool IsHttpOnly() const { return httponly_index_ != 0; }

  // Returns the number of attributes, for example, returning 2 for:
  //   "BLAH=hah; path=/; domain=.google.com"
  size_t NumberOfAttributes() const { return pairs_.size() - 1; }

  // For debugging only!
  std::string DebugString() const;

  // Returns an iterator pointing to the first terminator character found in
  // the given string.
  static std::string::const_iterator FindFirstTerminator(const std::string& s);

  // Given iterators pointing to the beginning and end of a string segment,
  // returns as output arguments token_start and token_end to the start and end
  // positions of a cookie attribute token name parsed from the segment, and
  // updates the segment iterator to point to the next segment to be parsed.
  // If no token is found, the function returns false.
  static bool ParseToken(std::string::const_iterator* it,
                         const std::string::const_iterator& end,
                         std::string::const_iterator* token_start,
                         std::string::const_iterator* token_end);

  // Given iterators pointing to the beginning and end of a string segment,
  // returns as output arguments value_start and value_end to the start and end
  // positions of a cookie attribute value parsed from the segment, and updates
  // the segment iterator to point to the next segment to be parsed.
  static void ParseValue(std::string::const_iterator* it,
                         const std::string::const_iterator& end,
                         std::string::const_iterator* value_start,
                         std::string::const_iterator* value_end);

  // Same as the above functions, except the input is assumed to contain the
  // desired token/value and nothing else.
  static std::string ParseTokenString(const std::string& token);
  static std::string ParseValueString(const std::string& value);

 private:
  static const char kTerminator[];
  static const int  kTerminatorLen;
  static const char kWhitespace[];
  static const char kValueSeparator[];
  static const char kTokenSeparator[];

  void ParseTokenValuePairs(const std::string& cookie_line);
  void SetupAttributes();

  PairList pairs_;
  bool is_valid_;
  // These will default to 0, but that should never be valid since the
  // 0th index is the user supplied token/value, not an attribute.
  // We're really never going to have more than like 8 attributes, so we
  // could fit these into 3 bits each if we're worried about size...
  size_t path_index_;
  size_t domain_index_;
  size_t expires_index_;
  size_t maxage_index_;
  size_t secure_index_;
  size_t httponly_index_;

  DISALLOW_COPY_AND_ASSIGN(ParsedCookie);
};

typedef base::RefCountedThreadSafe<CookieMonster::PersistentCookieStore>
    RefcountedPersistentCookieStore;

class CookieMonster::PersistentCookieStore
    : public RefcountedPersistentCookieStore {
 public:
  virtual ~PersistentCookieStore() { }

  // Initializes the store and retrieves the existing cookies. This will be
  // called only once at startup.
  virtual bool Load(std::vector<CookieMonster::CanonicalCookie*>*) = 0;

  virtual void AddCookie(const CanonicalCookie&) = 0;
  virtual void UpdateCookieAccessTime(const CanonicalCookie&) = 0;
  virtual void DeleteCookie(const CanonicalCookie&) = 0;

 protected:
  PersistentCookieStore() { }

 private:
  DISALLOW_COPY_AND_ASSIGN(PersistentCookieStore);
};

}  // namespace net

#endif  // NET_BASE_COOKIE_MONSTER_H_
