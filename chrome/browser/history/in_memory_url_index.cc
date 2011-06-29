// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/in_memory_url_index.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include "base/file_util.h"
#include "base/i18n/break_iterator.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/history_provider_util.h"
#include "chrome/browser/history/url_database.h"
#include "chrome/browser/profiles/profile.h"
#include "net/base/escape.h"
#include "net/base/net_util.h"
#include "third_party/protobuf/src/google/protobuf/repeated_field.h"
#include "ui/base/l10n/l10n_util.h"

using google::protobuf::RepeatedField;
using google::protobuf::RepeatedPtrField;

using in_memory_url_index::InMemoryURLIndexCacheItem;
namespace history {

typedef imui::InMemoryURLIndexCacheItem_WordListItem WordListItem;
typedef imui::InMemoryURLIndexCacheItem_WordMapItem_WordMapEntry WordMapEntry;
typedef imui::InMemoryURLIndexCacheItem_WordMapItem WordMapItem;
typedef imui::InMemoryURLIndexCacheItem_CharWordMapItem CharWordMapItem;
typedef imui::InMemoryURLIndexCacheItem_CharWordMapItem_CharWordMapEntry
    CharWordMapEntry;
typedef imui::InMemoryURLIndexCacheItem_WordIDHistoryMapItem
    WordIDHistoryMapItem;
typedef imui::
    InMemoryURLIndexCacheItem_WordIDHistoryMapItem_WordIDHistoryMapEntry
    WordIDHistoryMapEntry;
typedef imui::InMemoryURLIndexCacheItem_HistoryInfoMapItem HistoryInfoMapItem;
typedef imui::InMemoryURLIndexCacheItem_HistoryInfoMapItem_HistoryInfoMapEntry
    HistoryInfoMapEntry;

const size_t InMemoryURLIndex::kNoCachedResultForTerm = -1;

ScoredHistoryMatch::ScoredHistoryMatch() : raw_score(0) {}

ScoredHistoryMatch::ScoredHistoryMatch(const URLRow& url_info,
                                       size_t input_location,
                                       bool match_in_scheme,
                                       bool innermost_match,
                                       int score)
    : HistoryMatch(url_info, input_location, match_in_scheme, innermost_match),
      raw_score(score) {
}

struct InMemoryURLIndex::TermCharWordSet {
  TermCharWordSet()  // Required for STL resize().
      : char_(0),
        word_id_set_(),
        used_(false) {}
  TermCharWordSet(const char16& uni_char,
                  const WordIDSet& word_id_set,
                  bool used)
      : char_(uni_char),
        word_id_set_(word_id_set),
        used_(used) {}

  // Predicate for STL algorithm use.
  bool is_not_used() const { return !used_; }

  char16 char_;
  WordIDSet word_id_set_;
  bool used_;  // true if this set has been used for the current term search.
};

InMemoryURLIndex::InMemoryURLIndex(const FilePath& history_dir)
    : history_dir_(history_dir),
      history_item_count_(0) {
}

// Called only by unit tests.
InMemoryURLIndex::InMemoryURLIndex()
    : history_item_count_(0) {
}

InMemoryURLIndex::~InMemoryURLIndex() {}

// Indexing

bool InMemoryURLIndex::Init(history::URLDatabase* history_db,
                            const std::string& languages) {
  // TODO(mrossetti): Register for profile/language change notifications.
  languages_ = languages;
  return ReloadFromHistory(history_db, false);
}

void InMemoryURLIndex::ShutDown() {
  // Write our cache.
  SaveToCacheFile();
}

bool InMemoryURLIndex::IndexRow(const URLRow& row) {
  const GURL& gurl(row.url());
  string16 url(net::FormatUrl(gurl, languages_,
      net::kFormatUrlOmitUsernamePassword,
      UnescapeRule::SPACES | UnescapeRule::URL_SPECIAL_CHARS,
      NULL, NULL, NULL));

  // TODO(mrossetti): Detect row_id > std::numeric_limits<HistoryID>::max().
  HistoryID history_id = static_cast<HistoryID>(row.id());

  // Add the row for quick lookup in the history info store.
  url = l10n_util::ToLower(url);
  URLRow new_row(GURL(url), row.id());
  new_row.set_visit_count(row.visit_count());
  new_row.set_typed_count(row.typed_count());
  new_row.set_last_visit(row.last_visit());
  new_row.set_title(row.title());
  history_info_map_[history_id] = new_row;

  // Split into individual, unique words.
  String16Set words = WordSetFromString16(url);

  // For each word, add a new entry into the word index referring to the
  // associated history item.
  for (String16Set::iterator iter = words.begin();
       iter != words.end(); ++iter) {
    String16Set::value_type uni_word = *iter;
    AddWordToIndex(uni_word, history_id);
  }
  ++history_item_count_;
  return true;
}

bool InMemoryURLIndex::ReloadFromHistory(history::URLDatabase* history_db,
                                         bool clear_cache) {
  ClearPrivateData();

  if (!history_db)
    return false;

  if (clear_cache || !RestoreFromCacheFile()) {
    base::TimeTicks beginning_time = base::TimeTicks::Now();
    // The index has to be built from scratch.
    URLDatabase::URLEnumerator history_enum;
    if (!history_db->InitURLEnumeratorForSignificant(&history_enum))
      return false;
    URLRow row;
    while (history_enum.GetNextURL(&row)) {
      if (!IndexRow(row))
        return false;
    }
    UMA_HISTOGRAM_TIMES("History.InMemoryURLIndexingTime",
                        base::TimeTicks::Now() - beginning_time);
    SaveToCacheFile();
  }
  return true;
}

void InMemoryURLIndex::ClearPrivateData() {
  history_item_count_ = 0;
  word_list_.clear();
  word_map_.clear();
  char_word_map_.clear();
  word_id_history_map_.clear();
  term_char_word_set_cache_.clear();
  history_info_map_.clear();
}

bool InMemoryURLIndex::RestoreFromCacheFile() {
  // TODO(mrossetti): Figure out how to determine if the cache is up-to-date.
  // That is: ensure that the database has not been modified since the cache
  // was last saved. DB file modification date is inadequate. There are no
  // SQLite table checksums automatically stored.
  base::TimeTicks beginning_time = base::TimeTicks::Now();
  FilePath file_path;
  if (!GetCacheFilePath(&file_path))
    return false;
  std::string data;
  if (!file_util::ReadFileToString(file_path, &data)) {
    LOG(WARNING) << "Failed to read InMemoryURLIndex cache from "
                 << file_path.value();
    return false;
  }

  InMemoryURLIndexCacheItem index_cache;
  if (!index_cache.ParseFromArray(data.c_str(), data.size())) {
    LOG(WARNING) << "Failed to parse InMemoryURLIndex cache data read from "
                 << file_path.value();
    return false;
  }

  if (!RestorePrivateData(index_cache)) {
    ClearPrivateData();  // Back to square one -- must build from scratch.
    return false;
  }

  UMA_HISTOGRAM_TIMES("History.InMemoryURLIndexRestoreCacheTime",
                      base::TimeTicks::Now() - beginning_time);
  UMA_HISTOGRAM_COUNTS("History.InMemoryURLHistoryItems", history_item_count_);
  UMA_HISTOGRAM_COUNTS("History.InMemoryURLCacheSize", data.size());
  UMA_HISTOGRAM_COUNTS_10000("History.InMemoryURLWords", word_map_.size());
  UMA_HISTOGRAM_COUNTS_10000("History.InMemoryURLChars", char_word_map_.size());
  return true;
}

bool InMemoryURLIndex::SaveToCacheFile() {
  base::TimeTicks beginning_time = base::TimeTicks::Now();
  InMemoryURLIndexCacheItem index_cache;
  SavePrivateData(&index_cache);
  std::string data;
  if (!index_cache.SerializeToString(&data)) {
    LOG(WARNING) << "Failed to serialize the InMemoryURLIndex cache.";
    return false;
  }

  // Write the cache to a file then swap it for the old cache, if any, and
  // delete the old cache.
  FilePath file_path;
  if (!GetCacheFilePath(&file_path))
    return false;
  file_util::ScopedFILE file(file_util::OpenFile(file_path, "w"));
  if (!file.get())
    return false;

  int size = data.size();
  if (file_util::WriteFile(file_path, data.c_str(), size) != size) {
    LOG(WARNING) << "Failed to write " << file_path.value();
    return false;
  }
  UMA_HISTOGRAM_TIMES("History.InMemoryURLIndexSaveCacheTime",
                      base::TimeTicks::Now() - beginning_time);
  return true;
}

void InMemoryURLIndex::UpdateURL(URLID row_id, const URLRow& row) {
  // The row may or may not already be in our index. If it is not already
  // indexed and it qualifies then it gets indexed. If it is already
  // indexed and still qualifies then it gets updated, otherwise it
  // is deleted from the index.
  HistoryInfoMap::iterator row_pos = history_info_map_.find(row_id);
  if (row_pos == history_info_map_.end()) {
    // This new row should be indexed if it qualifies.
    if (RowQualifiesAsSignificant(row, base::Time()))
      IndexRow(row);
  } else if (RowQualifiesAsSignificant(row, base::Time())) {
    // This indexed row still qualifies and will be re-indexed.
    // The url won't have changed but the title, visit count, etc.
    // might have changed.
    URLRow& old_row = row_pos->second;
    old_row.set_visit_count(row.visit_count());
    old_row.set_typed_count(row.typed_count());
    old_row.set_last_visit(row.last_visit());
    // TODO(mrossetti): When we start indexing the title the next line
    // will need attention.
    old_row.set_title(row.title());
  } else {
    // This indexed row no longer qualifies and will be de-indexed.
    history_info_map_.erase(row_id);
  }
  // This invalidates the cache.
  term_char_word_set_cache_.clear();
  // TODO(mrossetti): Record this transaction in the cache.
}

void InMemoryURLIndex::DeleteURL(URLID row_id) {
  // Note that this does not remove any reference to this row from the
  // word_id_history_map_. That map will continue to contain (and return)
  // hits against this row until that map is rebuilt, but since the
  // history_info_map_ no longer references the row no erroneous results
  // will propagate to the user.
  history_info_map_.erase(row_id);
  // This invalidates the word cache.
  term_char_word_set_cache_.clear();
  // TODO(mrossetti): Record this transaction in the cache.
}

// Searching

ScoredHistoryMatches InMemoryURLIndex::HistoryItemsForTerms(
    const String16Vector& terms) {
  ScoredHistoryMatches scored_items;
  if (!terms.empty()) {
    // Reset used_ flags for term_char_word_set_cache_. We use a basic mark-
    // and-sweep approach.
    ResetTermCharWordSetCache();

    // Lowercase the terms.
    // TODO(mrossetti): Another opportunity for a transform algorithm.
    String16Vector lower_terms;
    for (String16Vector::const_iterator term_iter = terms.begin();
         term_iter != terms.end(); ++term_iter) {
      String16Vector::value_type lower_string(*term_iter);
      std::transform(lower_string.begin(),
                     lower_string.end(),
                     lower_string.begin(),
                     tolower);
      lower_terms.push_back(lower_string);
    }

    String16Vector::value_type all_terms(JoinString(lower_terms, ' '));
    HistoryIDSet history_id_set = HistoryIDSetFromWords(all_terms);

    // Pass over all of the candidates filtering out any without a proper
    // substring match, inserting those which pass in order by score.
    scored_items = std::for_each(history_id_set.begin(), history_id_set.end(),
                                 AddHistoryMatch(*this,
                                                 lower_terms)).ScoredMatches();
  }

  // Remove any stale TermCharWordSet's.
  term_char_word_set_cache_.erase(
      std::remove_if(term_char_word_set_cache_.begin(),
                     term_char_word_set_cache_.end(),
                     std::mem_fun_ref(&TermCharWordSet::is_not_used)),
      term_char_word_set_cache_.end());
  return scored_items;
}

void InMemoryURLIndex::ResetTermCharWordSetCache() {
  // TODO(mrossetti): Consider keeping more of the cache around for possible
  // repeat searches, except a 'shortcuts' approach might be better for that.
  // TODO(mrossetti): Change TermCharWordSet to a class and use for_each.
  for (TermCharWordSetVector::iterator iter =
       term_char_word_set_cache_.begin();
       iter != term_char_word_set_cache_.end(); ++iter)
    iter->used_ = false;
}

InMemoryURLIndex::HistoryIDSet InMemoryURLIndex::HistoryIDSetFromWords(
    const string16& uni_string) {
  // Break the terms down into individual terms (words), get the candidate
  // set for each term, and intersect each to get a final candidate list.
  // Note that a single 'term' from the user's perspective might be
  // a string like "http://www.somewebsite.com" which, from our perspective,
  // is four words: 'http', 'www', 'somewebsite', and 'com'.
  HistoryIDSet history_id_set;
  String16Set words = WordSetFromString16(uni_string);
  bool first_word = true;
  for (String16Set::iterator iter = words.begin();
       iter != words.end(); ++iter) {
    String16Set::value_type uni_word = *iter;
    HistoryIDSet term_history_id_set =
        InMemoryURLIndex::HistoryIDsForTerm(uni_word);
    if (first_word) {
      history_id_set.swap(term_history_id_set);
      first_word = false;
    } else {
      HistoryIDSet old_history_id_set(history_id_set);
      history_id_set.clear();
      std::set_intersection(old_history_id_set.begin(),
                            old_history_id_set.end(),
                            term_history_id_set.begin(),
                            term_history_id_set.end(),
                            std::inserter(history_id_set,
                                          history_id_set.begin()));
    }
  }
  return history_id_set;
}

InMemoryURLIndex::HistoryIDSet InMemoryURLIndex::HistoryIDsForTerm(
    const string16& uni_word) {
  InMemoryURLIndex::HistoryIDSet history_id_set;

  // For each unique character in the word, in order of first appearance, get
  // the char/word_id map entry and intersect with the set in an incremental
  // manner.
  Char16Vector uni_chars = Char16VectorFromString16(uni_word);
  WordIDSet word_id_set(WordIDSetForTermChars(uni_chars));

  // If any words resulted then we can compose a set of history IDs by unioning
  // the sets from each word.
  if (!word_id_set.empty()) {
    for (WordIDSet::iterator word_id_iter = word_id_set.begin();
         word_id_iter != word_id_set.end(); ++word_id_iter) {
      WordID word_id = *word_id_iter;
      WordIDHistoryMap::iterator word_iter = word_id_history_map_.find(word_id);
      if (word_iter != word_id_history_map_.end()) {
        HistoryIDSet& word_history_id_set(word_iter->second);
        history_id_set.insert(word_history_id_set.begin(),
                              word_history_id_set.end());
      }
    }
  }

  return history_id_set;
}

// Utility Functions

// static
InMemoryURLIndex::String16Set InMemoryURLIndex::WordSetFromString16(
    const string16& uni_string) {
  String16Set words;
  base::BreakIterator iter(&uni_string, base::BreakIterator::BREAK_WORD);
  if (iter.Init()) {
    while (iter.Advance()) {
      if (iter.IsWord())
        words.insert(iter.GetString());
    }
  }
  return words;
}

// static
InMemoryURLIndex::Char16Vector InMemoryURLIndex::Char16VectorFromString16(
    const string16& uni_word) {
  InMemoryURLIndex::Char16Vector characters;
  InMemoryURLIndex::Char16Set unique_characters;
  for (string16::const_iterator iter = uni_word.begin();
       iter != uni_word.end(); ++iter) {
    if (!unique_characters.count(*iter)) {
      unique_characters.insert(*iter);
      characters.push_back(*iter);
    }
  }
  return characters;
}

// static
InMemoryURLIndex::Char16Set InMemoryURLIndex::Char16SetFromString16(
    const string16& uni_word) {
  Char16Set characters;
  for (string16::const_iterator iter = uni_word.begin();
       iter != uni_word.end(); ++iter)
    characters.insert(*iter);
  return characters;
}

void InMemoryURLIndex::AddWordToIndex(const string16& uni_word,
                                      HistoryID history_id) {
  WordMap::iterator word_pos = word_map_.find(uni_word);
  if (word_pos != word_map_.end())
    UpdateWordHistory(word_pos->second, history_id);
  else
    AddWordHistory(uni_word, history_id);
}

void InMemoryURLIndex::UpdateWordHistory(WordID word_id, HistoryID history_id) {
    WordIDHistoryMap::iterator history_pos = word_id_history_map_.find(word_id);
    DCHECK(history_pos != word_id_history_map_.end());
    HistoryIDSet& history_id_set(history_pos->second);
    history_id_set.insert(history_id);
}

// Add a new word to the word list and the word map, and then create a
// new entry in the word/history map.
void InMemoryURLIndex::AddWordHistory(const string16& uni_word,
                                      HistoryID history_id) {
  word_list_.push_back(uni_word);
  WordID word_id = word_list_.size() - 1;
  word_map_[uni_word] = word_id;
  HistoryIDSet history_id_set;
  history_id_set.insert(history_id);
  word_id_history_map_[word_id] = history_id_set;
  // For each character in the newly added word (i.e. a word that is not
  // already in the word index), add the word to the character index.
  Char16Set characters = Char16SetFromString16(uni_word);
  for (Char16Set::iterator uni_char_iter = characters.begin();
       uni_char_iter != characters.end(); ++uni_char_iter) {
    Char16Set::value_type uni_char = *uni_char_iter;
    CharWordIDMap::iterator char_iter = char_word_map_.find(uni_char);
    if (char_iter != char_word_map_.end()) {
      // Update existing entry in the char/word index.
      WordIDSet& word_id_set(char_iter->second);
      word_id_set.insert(word_id);
    } else {
      // Create a new entry in the char/word index.
      WordIDSet word_id_set;
      word_id_set.insert(word_id);
      char_word_map_[uni_char] = word_id_set;
    }
  }
}

InMemoryURLIndex::WordIDSet InMemoryURLIndex::WordIDSetForTermChars(
    const InMemoryURLIndex::Char16Vector& uni_chars) {
  size_t index = CachedResultsIndexForTerm(uni_chars);

  // If there were no unprocessed characters in the search term |uni_chars|
  // then we can use the cached one as-is as the results with no further
  // filtering.
  if (index != kNoCachedResultForTerm && index == uni_chars.size() - 1)
    return term_char_word_set_cache_[index].word_id_set_;

  // Some or all of the characters remain to be indexed so trim the cache.
  if (index + 1 < term_char_word_set_cache_.size())
    term_char_word_set_cache_.resize(index + 1);
  WordIDSet word_id_set;
  // Take advantage of our cached starting point, if any.
  Char16Vector::const_iterator c_iter = uni_chars.begin();
  if (index != kNoCachedResultForTerm) {
    word_id_set = term_char_word_set_cache_[index].word_id_set_;
    c_iter += index + 1;
  }
  // Now process the remaining characters in the search term.
  for (; c_iter != uni_chars.end(); ++c_iter) {
    Char16Vector::value_type uni_char = *c_iter;
    CharWordIDMap::iterator char_iter = char_word_map_.find(uni_char);
    if (char_iter == char_word_map_.end()) {
      // A character was not found so there are no matching results: bail.
      word_id_set.clear();
      break;
    }
    WordIDSet& char_word_id_set(char_iter->second);
    // It is possible for there to no longer be any words associated with
    // a particular character. Give up in that case.
    if (char_word_id_set.empty()) {
      word_id_set.clear();
      break;
    }

    if (word_id_set.empty()) {
      word_id_set = char_word_id_set;
    } else {
      WordIDSet old_word_id_set(word_id_set);
      word_id_set.clear();
      std::set_intersection(old_word_id_set.begin(),
                            old_word_id_set.end(),
                            char_word_id_set.begin(),
                            char_word_id_set.end(),
                            std::inserter(word_id_set,
                            word_id_set.begin()));
    }
    // Add this new char/set instance to the cache.
    term_char_word_set_cache_.push_back(TermCharWordSet(
        uni_char, word_id_set, true));
  }
  return word_id_set;
}

size_t InMemoryURLIndex::CachedResultsIndexForTerm(
    const InMemoryURLIndex::Char16Vector& uni_chars) {
  TermCharWordSetVector::iterator t_iter = term_char_word_set_cache_.begin();
  for (Char16Vector::const_iterator c_iter = uni_chars.begin();
       (c_iter != uni_chars.end()) &&
       (t_iter != term_char_word_set_cache_.end()) &&
       (*c_iter == t_iter->char_);
       ++c_iter, ++t_iter)
    t_iter->used_ = true;  // Update the cache.
  return t_iter - term_char_word_set_cache_.begin() - 1;
}

// static
int InMemoryURLIndex::RawScoreForURL(const URLRow& row,
                                     const String16Vector& terms,
                                     size_t* first_term_location) {
  GURL gurl = row.url();
  if (!gurl.is_valid())
    return 0;

  string16 url = UTF8ToUTF16(gurl.spec());

  // Collect all term start positions so we can see if they appear in order.
  std::vector<size_t> term_locations;
  int out_of_order = 0;  // Count the terms which are out of order.
  size_t start_location_total = 0;
  size_t term_length_total = 0;
  for (String16Vector::const_iterator iter = terms.begin(); iter != terms.end();
       ++iter) {
    string16 term = *iter;
    size_t term_location = url.find(term);
    if (term_location == string16::npos)
      return 0;  // A term was not found.  This should not happen.
    if (iter != terms.begin()) {
      // See if this term is out-of-order.
      for (std::vector<size_t>::const_iterator order_iter =
               term_locations.begin(); order_iter != term_locations.end();
               ++order_iter) {
        if (term_location <= *order_iter)
          ++out_of_order;
      }
    } else {
      *first_term_location = term_location;
    }
    term_locations.push_back(term_location);
    start_location_total += term_location;
    term_length_total += term.size();
  }

  // Calculate a raw score.
  // TODO(mrossetti): This is good enough for now but must be fine-tuned.
  const float kOrderMaxValue = 10.0;
  float order_value = 10.0;
  if (terms.size() > 1) {
    int max_possible_out_of_order = (terms.size() * (terms.size() - 1)) / 2;
    order_value =
        (static_cast<float>(max_possible_out_of_order - out_of_order) /
         max_possible_out_of_order) * kOrderMaxValue;
  }

  const float kStartMaxValue = 10.0;
  const size_t kMaxSignificantStart = 20;
  float start_value =
      (static_cast<float>(kMaxSignificantStart -
       std::min(kMaxSignificantStart, start_location_total / terms.size()))) /
      static_cast<float>(kMaxSignificantStart) * kStartMaxValue;

  const float kCompleteMaxValue = 10.0;
  float complete_value =
      (static_cast<float>(term_length_total) / static_cast<float>(url.size())) *
      kStartMaxValue;

  const float kLastVisitMaxValue = 10.0;
  const base::TimeDelta kMaxSignificantDay = base::TimeDelta::FromDays(30);
  int64 delta_time = (kMaxSignificantDay -
      std::min((base::Time::Now() - row.last_visit()),
               kMaxSignificantDay)).ToInternalValue();
  float last_visit_value =
      (static_cast<float>(delta_time) /
       static_cast<float>(kMaxSignificantDay.ToInternalValue())) *
      kLastVisitMaxValue;

  const float kVisitCountMaxValue = 10.0;
  const int kMaxSignificantVisits = 10;
  float visit_count_value =
      (static_cast<float>(std::min(row.visit_count(),
       kMaxSignificantVisits))) / static_cast<float>(kMaxSignificantVisits) *
      kVisitCountMaxValue;

  const float kTypedCountMaxValue = 20.0;
  const int kMaxSignificantTyped = 10;
  float typed_count_value =
      (static_cast<float>(std::min(row.typed_count(),
       kMaxSignificantTyped))) / static_cast<float>(kMaxSignificantTyped) *
      kTypedCountMaxValue;

  float raw_score = order_value + start_value + complete_value +
      last_visit_value + visit_count_value + typed_count_value;

  // Normalize the score.
  const float kMaxNormalizedRawScore = 1000.0;
  raw_score =
      (raw_score / (kOrderMaxValue + kStartMaxValue + kCompleteMaxValue +
                    kLastVisitMaxValue + kVisitCountMaxValue +
                    kTypedCountMaxValue)) *
      kMaxNormalizedRawScore;
  return static_cast<int>(raw_score);
}

// static
base::Time InMemoryURLIndex::RecentThreshold() {
  return base::Time::Now() -
      base::TimeDelta::FromDays(kLowQualityMatchAgeLimitInDays);
}

InMemoryURLIndex::AddHistoryMatch::AddHistoryMatch(
    const InMemoryURLIndex& index,
    const String16Vector& lower_terms)
    : index_(index),
      lower_terms_(lower_terms) {
}

InMemoryURLIndex::AddHistoryMatch::~AddHistoryMatch() {}

void InMemoryURLIndex::AddHistoryMatch::operator()(
    const InMemoryURLIndex::HistoryID history_id) {
  HistoryInfoMap::const_iterator hist_pos =
      index_.history_info_map_.find(history_id);
  // Note that a history_id may be present in the word_id_history_map_ yet not
  // be found in the history_info_map_. This occurs when an item has been
  // deleted by the user or the item no longer qualifies as a quick result.
  if (hist_pos != index_.history_info_map_.end()) {
    const URLRow& hist_item = hist_pos->second;
    // TODO(mrossetti): Accommodate multiple term highlighting.
    size_t input_location = 0;
    int score = InMemoryURLIndex::RawScoreForURL(
        hist_item, lower_terms_, &input_location);
    if (score != 0) {
      // We only retain the top 10 highest scoring results so
      // see if this one fits into the top 10 and, if so, where.
      ScoredHistoryMatches::iterator scored_iter = scored_matches_.begin();
      while (scored_iter != scored_matches_.end() &&
             (*scored_iter).raw_score > score)
        ++scored_iter;
      if ((scored_matches_.size() < 10) ||
          (scored_iter != scored_matches_.end())) {
        // Create and insert the new item.
        // TODO(mrossetti): Properly set |match_in_scheme| and
        // |innermost_match|.
        bool match_in_scheme = false;
        bool innermost_match = true;
        ScoredHistoryMatch match(hist_item, input_location,
            match_in_scheme, innermost_match, score);
        if (!scored_matches_.empty())
          scored_matches_.insert(scored_iter, match);
        else
          scored_matches_.push_back(match);
        // Trim any entries beyond 10.
        if (scored_matches_.size() > 10) {
          scored_matches_.erase(scored_matches_.begin() + 10,
                                scored_matches_.end());
        }
      }
    }
  }
}

bool InMemoryURLIndex::GetCacheFilePath(FilePath* file_path) {
  if (history_dir_.empty())
    return false;
  *file_path = history_dir_.Append(FILE_PATH_LITERAL("History Provider Cache"));
  return true;
}

void InMemoryURLIndex::SavePrivateData(InMemoryURLIndexCacheItem* cache) const {
  DCHECK(cache);
  cache->set_timestamp(base::Time::Now().ToInternalValue());
  cache->set_history_item_count(history_item_count_);
  SaveWordList(cache);
  SaveWordMap(cache);
  SaveCharWordMap(cache);
  SaveWordIDHistoryMap(cache);
  SaveHistoryInfoMap(cache);
}

bool InMemoryURLIndex::RestorePrivateData(
    const InMemoryURLIndexCacheItem& cache) {
  last_saved_ = base::Time::FromInternalValue(cache.timestamp());
  history_item_count_ = cache.history_item_count();
  return (history_item_count_ == 0) || (RestoreWordList(cache) &&
      RestoreWordMap(cache) && RestoreCharWordMap(cache) &&
      RestoreWordIDHistoryMap(cache) && RestoreHistoryInfoMap(cache));
}


void InMemoryURLIndex::SaveWordList(InMemoryURLIndexCacheItem* cache) const {
  if (word_list_.empty())
    return;
  WordListItem* list_item = cache->mutable_word_list();
  list_item->set_word_count(word_list_.size());
  for (String16Vector::const_iterator iter = word_list_.begin();
       iter != word_list_.end(); ++iter)
    list_item->add_word(UTF16ToUTF8(*iter));
}

bool InMemoryURLIndex::RestoreWordList(const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_word_list())
    return false;
  const WordListItem& list_item(cache.word_list());
  uint32 expected_item_count = list_item.word_count();
  uint32 actual_item_count = list_item.word_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<std::string>& words(list_item.word());
  for (RepeatedPtrField<std::string>::const_iterator iter = words.begin();
       iter != words.end(); ++iter)
    word_list_.push_back(UTF8ToUTF16(*iter));
  return true;
}

void InMemoryURLIndex::SaveWordMap(InMemoryURLIndexCacheItem* cache) const {
  if (word_map_.empty())
    return;
  WordMapItem* map_item = cache->mutable_word_map();
  map_item->set_item_count(word_map_.size());
  for (WordMap::const_iterator iter = word_map_.begin();
       iter != word_map_.end(); ++iter) {
    WordMapEntry* map_entry = map_item->add_word_map_entry();
    map_entry->set_word(UTF16ToUTF8(iter->first));
    map_entry->set_word_id(iter->second);
  }
}

bool InMemoryURLIndex::RestoreWordMap(const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_word_map())
    return false;
  const WordMapItem& list_item(cache.word_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.word_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<WordMapEntry>& entries(list_item.word_map_entry());
  for (RepeatedPtrField<WordMapEntry>::const_iterator iter = entries.begin();
       iter != entries.end(); ++iter)
    word_map_[UTF8ToUTF16(iter->word())] = iter->word_id();
  return true;
}

void InMemoryURLIndex::SaveCharWordMap(InMemoryURLIndexCacheItem* cache) const {
  if (char_word_map_.empty())
    return;
  CharWordMapItem* map_item = cache->mutable_char_word_map();
  map_item->set_item_count(char_word_map_.size());
  for (CharWordIDMap::const_iterator iter = char_word_map_.begin();
       iter != char_word_map_.end(); ++iter) {
    CharWordMapEntry* map_entry = map_item->add_char_word_map_entry();
    map_entry->set_char_16(iter->first);
    const WordIDSet& word_id_set(iter->second);
    map_entry->set_item_count(word_id_set.size());
    for (WordIDSet::const_iterator set_iter = word_id_set.begin();
         set_iter != word_id_set.end(); ++set_iter)
      map_entry->add_word_id(*set_iter);
  }
}

bool InMemoryURLIndex::RestoreCharWordMap(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_char_word_map())
    return false;
  const CharWordMapItem& list_item(cache.char_word_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.char_word_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<CharWordMapEntry>&
      entries(list_item.char_word_map_entry());
  for (RepeatedPtrField<CharWordMapEntry>::const_iterator iter =
       entries.begin(); iter != entries.end(); ++iter) {
    expected_item_count = iter->item_count();
    actual_item_count = iter->word_id_size();
    if (actual_item_count == 0 || actual_item_count != expected_item_count)
      return false;
    char16 uni_char = static_cast<char16>(iter->char_16());
    WordIDSet word_id_set;
    const RepeatedField<int32>& word_ids(iter->word_id());
    for (RepeatedField<int32>::const_iterator jiter = word_ids.begin();
         jiter != word_ids.end(); ++jiter)
      word_id_set.insert(*jiter);
    char_word_map_[uni_char] = word_id_set;
  }
  return true;
}

void InMemoryURLIndex::SaveWordIDHistoryMap(InMemoryURLIndexCacheItem* cache)
    const {
  if (word_id_history_map_.empty())
    return;
  WordIDHistoryMapItem* map_item = cache->mutable_word_id_history_map();
  map_item->set_item_count(word_id_history_map_.size());
  for (WordIDHistoryMap::const_iterator iter = word_id_history_map_.begin();
       iter != word_id_history_map_.end(); ++iter) {
    WordIDHistoryMapEntry* map_entry =
        map_item->add_word_id_history_map_entry();
    map_entry->set_word_id(iter->first);
    const HistoryIDSet& history_id_set(iter->second);
    map_entry->set_item_count(history_id_set.size());
    for (HistoryIDSet::const_iterator set_iter = history_id_set.begin();
         set_iter != history_id_set.end(); ++set_iter)
      map_entry->add_history_id(*set_iter);
  }
}

bool InMemoryURLIndex::RestoreWordIDHistoryMap(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_word_id_history_map())
    return false;
  const WordIDHistoryMapItem& list_item(cache.word_id_history_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.word_id_history_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<WordIDHistoryMapEntry>&
      entries(list_item.word_id_history_map_entry());
  for (RepeatedPtrField<WordIDHistoryMapEntry>::const_iterator iter =
       entries.begin(); iter != entries.end(); ++iter) {
    expected_item_count = iter->item_count();
    actual_item_count = iter->history_id_size();
    if (actual_item_count == 0 || actual_item_count != expected_item_count)
      return false;
    WordID word_id = iter->word_id();
    HistoryIDSet history_id_set;
    const RepeatedField<int64>& history_ids(iter->history_id());
    for (RepeatedField<int64>::const_iterator jiter = history_ids.begin();
         jiter != history_ids.end(); ++jiter)
      history_id_set.insert(*jiter);
    word_id_history_map_[word_id] = history_id_set;
  }
  return true;
}

void InMemoryURLIndex::SaveHistoryInfoMap(
    InMemoryURLIndexCacheItem* cache) const {
  if (history_info_map_.empty())
    return;
  HistoryInfoMapItem* map_item = cache->mutable_history_info_map();
  map_item->set_item_count(history_info_map_.size());
  for (HistoryInfoMap::const_iterator iter = history_info_map_.begin();
       iter != history_info_map_.end(); ++iter) {
    HistoryInfoMapEntry* map_entry = map_item->add_history_info_map_entry();
    map_entry->set_history_id(iter->first);
    const URLRow& url_row(iter->second);
    // Note: We only save information that contributes to the index so there
    // is no need to save term_char_word_set_cache_ (not persistent),
    // languages_, etc.
    map_entry->set_visit_count(url_row.visit_count());
    map_entry->set_typed_count(url_row.typed_count());
    map_entry->set_last_visit(url_row.last_visit().ToInternalValue());
    map_entry->set_url(url_row.url().spec());
    map_entry->set_title(UTF16ToUTF8(url_row.title()));
  }
}

bool InMemoryURLIndex::RestoreHistoryInfoMap(
    const InMemoryURLIndexCacheItem& cache) {
  if (!cache.has_history_info_map())
    return false;
  const HistoryInfoMapItem& list_item(cache.history_info_map());
  uint32 expected_item_count = list_item.item_count();
  uint32 actual_item_count = list_item.history_info_map_entry_size();
  if (actual_item_count == 0 || actual_item_count != expected_item_count)
    return false;
  const RepeatedPtrField<HistoryInfoMapEntry>&
      entries(list_item.history_info_map_entry());
  for (RepeatedPtrField<HistoryInfoMapEntry>::const_iterator iter =
       entries.begin(); iter != entries.end(); ++iter) {
    HistoryID history_id = iter->history_id();
    GURL url(iter->url());
    URLRow url_row(url, history_id);
    url_row.set_visit_count(iter->visit_count());
    url_row.set_typed_count(iter->typed_count());
    url_row.set_last_visit(base::Time::FromInternalValue(iter->last_visit()));
    if (iter->has_title()) {
      string16 title(UTF8ToUTF16(iter->title()));
      url_row.set_title(title);
    }
    history_info_map_[history_id] = url_row;
  }
  return true;
}

}  // namespace history
