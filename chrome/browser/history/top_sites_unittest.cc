// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app/l10n_util.h"
#include "base/file_util.h"
#include "base/scoped_temp_dir.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/browser/dom_ui/most_visited_handler.h"
#include "chrome/browser/history/history_marshaling.h"
#include "chrome/browser/history/top_sites_database.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/testing_profile.h"
#include "chrome/tools/profiles/thumbnail-inl.h"
#include "gfx/codec/jpeg_codec.h"
#include "googleurl/src/gurl.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"


namespace history {

static const unsigned char kBlob[] =
    "12346102356120394751634516591348710478123649165419234519234512349134";

class TopSitesTest : public testing::Test {
 public:
  TopSitesTest() : number_of_callbacks_(0) {
  }
  ~TopSitesTest() {
  }

  TopSites& top_sites() { return *top_sites_; }
  MostVisitedURLList& urls() { return urls_; }
  Profile& profile() {return *profile_;}
  FilePath& file_name() { return file_name_; }
  RefCountedBytes* google_thumbnail() { return google_thumbnail_; }
  RefCountedBytes* random_thumbnail() { return random_thumbnail_; }
  RefCountedBytes* weewar_thumbnail() { return weewar_thumbnail_; }
  CancelableRequestConsumer* consumer() { return &consumer_; }
  size_t number_of_callbacks() {return number_of_callbacks_; }
  // Prepopulated URLs - added at the back of TopSites.
  GURL welcome_url() {
    return GURL(l10n_util::GetStringUTF8(IDS_CHROME_WELCOME_URL));
  }
  GURL themes_url() {
    return GURL(l10n_util::GetStringUTF8(IDS_THEMES_GALLERY_URL));
  }

  virtual void SetUp() {
    profile_.reset(new TestingProfile);
    top_sites_ = new TopSites(profile_.get());

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    file_name_ = temp_dir_.path().AppendASCII("TopSites.db");
    EXPECT_TRUE(file_util::Delete(file_name_, false));

    std::vector<unsigned char> random_data(kBlob, kBlob + sizeof(kBlob));
    std::vector<unsigned char> google_data(kGoogleThumbnail,
                                           kGoogleThumbnail +
                                           sizeof(kGoogleThumbnail));
    std::vector<unsigned char> weewar_data(kWeewarThumbnail,
                                           kWeewarThumbnail +
                                           sizeof(kWeewarThumbnail));
    random_thumbnail_ = new RefCountedBytes(random_data);
    google_thumbnail_ = new RefCountedBytes(google_data);
    weewar_thumbnail_ = new RefCountedBytes(weewar_data);
  }

  virtual void TearDown() {
    profile_.reset();
    TopSites::DeleteTopSites(top_sites_);
    EXPECT_TRUE(file_util::Delete(file_name_, false));
  }

  // Callback for TopSites::GetMostVisitedURLs.
  void OnTopSitesAvailable(history::MostVisitedURLList data) {
    urls_ = data;
    number_of_callbacks_++;
  }

  // Wrappers that allow private TopSites functions to be called from the
  // individual tests without making them all be friends.
  GURL GetCanonicalURL(const GURL& url) const {
    AutoLock lock(top_sites_->lock_);  // The function asserts it's in the lock.
    return top_sites_->GetCanonicalURL(url);
  }

  void StoreMostVisited(std::vector<MostVisitedURL>* urls) {
    top_sites_->StoreMostVisited(urls);
  }

  static void DiffMostVisited(const std::vector<MostVisitedURL>& old_list,
                              const std::vector<MostVisitedURL>& new_list,
                              std::vector<size_t>* added_urls,
                              std::vector<size_t>* deleted_urls,
                              std::vector<size_t>* moved_urls)  {
    TopSites::DiffMostVisited(old_list, new_list,
                              added_urls, deleted_urls, moved_urls);
  }

  Lock& lock() {
    return top_sites_->lock_;
  }

 private:
  scoped_refptr<TopSites> top_sites_;
  MostVisitedURLList urls_;
  size_t number_of_callbacks_;
  scoped_ptr<TestingProfile> profile_;
  ScopedTempDir temp_dir_;
  FilePath file_name_;  // Database filename.
  scoped_refptr<RefCountedBytes> google_thumbnail_;
  scoped_refptr<RefCountedBytes> random_thumbnail_;
  scoped_refptr<RefCountedBytes> weewar_thumbnail_;
  MessageLoop message_loop_;
  CancelableRequestConsumer consumer_;

  DISALLOW_COPY_AND_ASSIGN(TopSitesTest);
};


// A mockup of a HistoryService used for testing TopSites.
class MockHistoryServiceImpl : public TopSites::MockHistoryService {
 public:
  MockHistoryServiceImpl() : num_thumbnail_requests_(0) {}

  // Calls the callback directly with the results.
  HistoryService::Handle QueryMostVisitedURLs(
      int result_count, int days_back,
      CancelableRequestConsumerBase* consumer,
      HistoryService::QueryMostVisitedURLsCallback* callback) {
    callback->Run(CancelableRequestProvider::Handle(0),  // Handle is unused.
                  most_visited_urls_);
    delete callback;
    return 0;
  }

  // Add a page to the end of the pages list.
  void AppendMockPage(const GURL& url,
                      const string16& title) {
    MostVisitedURL page;
    page.url = url;
    page.title = title;
    page.redirects = RedirectList();
    page.redirects.push_back(url);
    most_visited_urls_.push_back(page);
  }

  // Removes the last URL in the list.
  void RemoveMostVisitedURL() {
    most_visited_urls_.pop_back();
  }

  virtual void GetPageThumbnail(
      const GURL& url,
      CancelableRequestConsumerTSimple<size_t>* consumer,
      HistoryService::ThumbnailDataCallback* callback,
      size_t index) {
    num_thumbnail_requests_++;
    MostVisitedURL mvu;
    mvu.url = url;
    MostVisitedURLList::iterator pos = std::find(most_visited_urls_.begin(),
                                                 most_visited_urls_.end(),
                                                 mvu);
    EXPECT_TRUE(pos != most_visited_urls_.end()) << url.spec();
    scoped_refptr<RefCountedBytes> thumbnail;
    callback->Run(index, thumbnail);
    delete callback;
  }

  void ResetNumberOfThumbnailRequests() {
    num_thumbnail_requests_ = 0;
  }

  int GetNumberOfThumbnailRequests() {
    return num_thumbnail_requests_;
  }

 private:
  MostVisitedURLList most_visited_urls_;
  int num_thumbnail_requests_;  // Number of calls to GetPageThumbnail.
};


// A mockup of a TopSitesDatabase used for testing TopSites.
class MockTopSitesDatabaseImpl : public TopSitesDatabase {
 public:
  virtual void GetPageThumbnails(MostVisitedURLList* urls,
                                 std::map<GURL, Images>* thumbnails) {
    // Return a copy of the vector.
    *urls = top_sites_list_;
    *thumbnails = thumbnails_map_;
  }

  virtual void SetPageThumbnail(const MostVisitedURL& url, int url_rank,
                                const Images& thumbnail) {
    SetPageRank(url, url_rank);
    // Update thubmnail
    thumbnails_map_[url.url] = thumbnail;
  }

  virtual void UpdatePageRank(const MostVisitedURL& url, int new_rank) {
    MostVisitedURLList::iterator pos = std::find(top_sites_list_.begin(),
                                                 top_sites_list_.end(),
                                                 url);
    // Is it in the right position?
    int rank = pos - top_sites_list_.begin();
    if (rank != new_rank) {
      // Move the URL to a new position.
      top_sites_list_.erase(pos);
      top_sites_list_.insert(top_sites_list_.begin() + new_rank, url);
    }
  }

  virtual void SetPageRank(const MostVisitedURL& url, int url_rank) {
    // Check if this url is in the list, and at which position.
    MostVisitedURLList::iterator pos = std::find(top_sites_list_.begin(),
                                                 top_sites_list_.end(),
                                                 url);
    if (pos == top_sites_list_.end()) {
      // Add it to the list.
      top_sites_list_.insert(top_sites_list_.begin() + url_rank, url);
    } else {
      UpdatePageRank(url, url_rank);
    }
  }

  // Get a thumbnail for a given page. Returns true iff we have the thumbnail.
  virtual bool GetPageThumbnail(const GURL& url,
                                Images* thumbnail) {
    std::map<GURL, Images>::const_iterator found =
        thumbnails_map_.find(url);
    if (found == thumbnails_map_.end())
      return false;  // No thumbnail for this URL.

    thumbnail->thumbnail = found->second.thumbnail;
    thumbnail->thumbnail_score = found->second.thumbnail_score;
    return true;
  }

  virtual bool RemoveURL(const MostVisitedURL& url) {
    // Comparison by url.
    MostVisitedURLList::iterator pos = std::find(top_sites_list_.begin(),
                                                 top_sites_list_.end(),
                                                 url);
    if (pos == top_sites_list_.end()) {
      return false;
    }
    top_sites_list_.erase(pos);
    thumbnails_map_.erase(url.url);
    return true;
  }

 private:
  MostVisitedURLList top_sites_list_;  // Keeps the URLs sorted by score (rank).
  std::map<GURL, Images> thumbnails_map_;
};


// Helper function for appending a URL to a vector of "most visited" URLs,
// using the default values for everything but the URL.
static void AppendMostVisitedURL(std::vector<MostVisitedURL>* list,
                                 const GURL& url) {
  MostVisitedURL mv;
  mv.url = url;
  mv.redirects.push_back(url);
  list->push_back(mv);
}

// Returns true if t1 and t2 contain the same data.
static bool ThumbnailsAreEqual(RefCountedBytes* t1,
                               RefCountedBytes* t2) {
  if (!t1 || !t2)
    return false;
  if (t1->data.size() != t2->data.size())
    return false;
  return std::equal(t1->data.begin(),
                    t1->data.end(),
                    t2->data.begin());
}

// Same as AppendMostVisitedURL except that it adds a redirect from the first
// URL to the second.
static void AppendMostVisitedURLWithRedirect(
    std::vector<MostVisitedURL>* list,
    const GURL& redirect_source, const GURL& redirect_dest) {
  MostVisitedURL mv;
  mv.url = redirect_dest;
  mv.redirects.push_back(redirect_source);
  mv.redirects.push_back(redirect_dest);
  list->push_back(mv);
}

TEST_F(TopSitesTest, GetCanonicalURL) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  // Have two chains:
  //   google.com -> www.google.com
  //   news.google.com (no redirects)
  GURL news("http://news.google.com/");
  GURL source("http://google.com/");
  GURL dest("http://www.google.com/");

  std::vector<MostVisitedURL> most_visited;
  AppendMostVisitedURLWithRedirect(&most_visited, source, dest);
  AppendMostVisitedURL(&most_visited, news);
  StoreMostVisited(&most_visited);

  // Random URLs not in the database are returned unchanged.
  GURL result = GetCanonicalURL(GURL("http://fark.com/"));
  EXPECT_EQ(GURL("http://fark.com/"), result);

  // Easy case, there are no redirects and the exact URL is stored.
  result = GetCanonicalURL(news);
  EXPECT_EQ(news, result);

  // The URL in question is the source URL in a redirect list.
  result = GetCanonicalURL(source);
  EXPECT_EQ(dest, result);

  // The URL in question is the destination of a redirect.
  result = GetCanonicalURL(dest);
  EXPECT_EQ(dest, result);
}

TEST_F(TopSitesTest, DiffMostVisited) {
  GURL stays_the_same("http://staysthesame/");
  GURL gets_added_1("http://getsadded1/");
  GURL gets_added_2("http://getsadded2/");
  GURL gets_deleted_1("http://getsdeleted2/");
  GURL gets_moved_1("http://getsmoved1/");

  std::vector<MostVisitedURL> old_list;
  AppendMostVisitedURL(&old_list, stays_the_same);  // 0  (unchanged)
  AppendMostVisitedURL(&old_list, gets_deleted_1);  // 1  (deleted)
  AppendMostVisitedURL(&old_list, gets_moved_1);    // 2  (moved to 3)

  std::vector<MostVisitedURL> new_list;
  AppendMostVisitedURL(&new_list, stays_the_same);  // 0  (unchanged)
  AppendMostVisitedURL(&new_list, gets_added_1);    // 1  (added)
  AppendMostVisitedURL(&new_list, gets_added_2);    // 2  (added)
  AppendMostVisitedURL(&new_list, gets_moved_1);    // 3  (moved from 2)

  std::vector<size_t> added;
  std::vector<size_t> deleted;
  std::vector<size_t> moved;
  DiffMostVisited(old_list, new_list, &added, &deleted, &moved);

  ASSERT_EQ(2u, added.size());
  ASSERT_EQ(1u, deleted.size());
  ASSERT_EQ(1u, moved.size());

  // There should be 2 URLs added, we don't assume what order they're in inside
  // the result vector.
  EXPECT_TRUE(added[0] == 1 || added[1] == 1);
  EXPECT_TRUE(added[0] == 2 || added[1] == 2);

  EXPECT_EQ(1u, deleted[0]);
  EXPECT_EQ(3u, moved[0]);
}

TEST_F(TopSitesTest, SetPageThumbnail) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  GURL url1a("http://google.com/");
  GURL url1b("http://www.google.com/");
  GURL url2("http://images.google.com/");
  GURL invalid_url("chrome://favicon/http://google.com/");

  std::vector<MostVisitedURL> list;
  AppendMostVisitedURL(&list, url2);

  MostVisitedURL mv;
  mv.url = url1b;
  mv.redirects.push_back(url1a);
  mv.redirects.push_back(url1b);
  list.push_back(mv);

  // Save our most visited data containing that one site.
  StoreMostVisited(&list);

  // Create a dummy thumbnail.
  SkBitmap thumbnail;
  thumbnail.setConfig(SkBitmap::kARGB_8888_Config, 4, 4);
  thumbnail.allocPixels();
  thumbnail.eraseRGB(0x00, 0x00, 0x00);

  base::Time now = base::Time::Now();
  ThumbnailScore low_score(1.0, true, true, now);
  ThumbnailScore medium_score(0.5, true, true, now);
  ThumbnailScore high_score(0.0, true, true, now);

  // Setting the thumbnail for invalid pages should fail.
  EXPECT_FALSE(top_sites().SetPageThumbnail(invalid_url,
                                            thumbnail, medium_score));

  // Setting the thumbnail for url2 should succeed, lower scores shouldn't
  // replace it, higher scores should.
  EXPECT_TRUE(top_sites().SetPageThumbnail(url2, thumbnail, medium_score));
  EXPECT_FALSE(top_sites().SetPageThumbnail(url2, thumbnail, low_score));
  EXPECT_TRUE(top_sites().SetPageThumbnail(url2, thumbnail, high_score));

  // Set on the redirect source should succeed. It should be replacable by
  // the same score on the redirect destination, which in turn should not
  // be replaced by the source again.
  EXPECT_TRUE(top_sites().SetPageThumbnail(url1a, thumbnail, medium_score));
  EXPECT_TRUE(top_sites().SetPageThumbnail(url1b, thumbnail, medium_score));
  EXPECT_FALSE(top_sites().SetPageThumbnail(url1a, thumbnail, medium_score));
}

TEST_F(TopSitesTest, GetPageThumbnail) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  MostVisitedURLList url_list;
  MostVisitedURL url1(GURL("http://asdf.com"), GURL(), string16());
  url1.redirects.push_back(url1.url);
  url_list.push_back(url1);

  MostVisitedURL url2(GURL("http://gmail.com"), GURL(), string16());
  url2.redirects.push_back(url2.url);
  url2.redirects.push_back(GURL("http://mail.google.com"));
  url_list.push_back(url2);

  top_sites().UpdateMostVisited(url_list);
  MessageLoop::current()->RunAllPending();

  // Create a dummy thumbnail.
  SkBitmap thumbnail;
  thumbnail.setConfig(SkBitmap::kARGB_8888_Config, 4, 4);
  thumbnail.allocPixels();
  thumbnail.eraseRGB(0x00, 0x00, 0x00);
  ThumbnailScore score(0.5, true, true, base::Time::Now());

  RefCountedBytes* result = NULL;
  EXPECT_TRUE(top_sites().SetPageThumbnail(url1.url, thumbnail, score));
  EXPECT_TRUE(top_sites().GetPageThumbnail(url1.url, &result));

  EXPECT_TRUE(top_sites().SetPageThumbnail(GURL("http://gmail.com"),
                                           thumbnail, score));
  EXPECT_TRUE(top_sites().GetPageThumbnail(GURL("http://gmail.com"),
                                           &result));
  // Get a thumbnail via a redirect.
  EXPECT_TRUE(top_sites().GetPageThumbnail(GURL("http://mail.google.com"),
                                           &result));

  EXPECT_TRUE(top_sites().SetPageThumbnail(GURL("http://mail.google.com"),
                                           thumbnail, score));
  EXPECT_TRUE(top_sites().GetPageThumbnail(url2.url, &result));

  scoped_ptr<SkBitmap> out_bitmap(gfx::JPEGCodec::Decode(result->front(),
                                                         result->size()));
  EXPECT_EQ(0, memcmp(thumbnail.getPixels(), out_bitmap->getPixels(),
                      thumbnail.getSize()));
}

TEST_F(TopSitesTest, GetMostVisited) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  GURL news("http://news.google.com/");
  GURL google("http://google.com/");

  MockHistoryServiceImpl hs;
  hs.AppendMockPage(news, ASCIIToUTF16("Google News"));
  hs.AppendMockPage(google, ASCIIToUTF16("Google"));
  top_sites().SetMockHistoryService(&hs);

  top_sites().StartQueryForMostVisited();
  MessageLoop::current()->RunAllPending();
  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  // 2 extra prepopulated URLs.
  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ(news, urls()[0].url);
  EXPECT_EQ(google, urls()[1].url);
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);
}

TEST_F(TopSitesTest, MockDatabase) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  MockTopSitesDatabaseImpl* db = new MockTopSitesDatabaseImpl;
  // |db| is destroyed when the top_sites is destroyed in TearDown.
  top_sites().db_.reset(db);
  MostVisitedURL url;
  GURL asdf_url("http://asdf.com");
  string16 asdf_title(ASCIIToUTF16("ASDF"));
  GURL google_url("http://google.com");
  string16 google_title(ASCIIToUTF16("Google"));
  GURL news_url("http://news.google.com");
  string16 news_title(ASCIIToUTF16("Google News"));

  url.url = asdf_url;
  url.title = asdf_title;
  url.redirects.push_back(url.url);
  Images thumbnail;
  db->SetPageThumbnail(url, 0, thumbnail);

  top_sites().ReadDatabase();

  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(3u, urls().size());
  EXPECT_EQ(asdf_url, urls()[0].url);
  EXPECT_EQ(asdf_title, urls()[0].title);
  EXPECT_EQ(welcome_url(), urls()[1].url);
  EXPECT_EQ(themes_url(), urls()[2].url);

  MostVisitedURL url2;
  url2.url = google_url;
  url2.title = google_title;
  url2.redirects.push_back(url2.url);

  // Add new thumbnail at rank 0 and shift the other result to 1.
  db->SetPageThumbnail(url2, 0, thumbnail);

  top_sites().ReadDatabase();

  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ(google_url, urls()[0].url);
  EXPECT_EQ(google_title, urls()[0].title);
  EXPECT_EQ(asdf_url, urls()[1].url);
  EXPECT_EQ(asdf_title, urls()[1].title);
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);

  MockHistoryServiceImpl hs;
  // Add one old, one new URL to the history.
  hs.AppendMockPage(google_url, google_title);
  hs.AppendMockPage(news_url, news_title);
  top_sites().SetMockHistoryService(&hs);

  // This writes the new data to the DB.
  top_sites().StartQueryForMostVisited();
  MessageLoop::current()->RunAllPending();

  std::map<GURL, Images> thumbnails;
  MostVisitedURLList result;
  db->GetPageThumbnails(&result, &thumbnails);
  ASSERT_EQ(4u, result.size());
  EXPECT_EQ(google_title, result[0].title);
  EXPECT_EQ(news_title, result[1].title);
}

// Test TopSitesDatabaseImpl.
TEST_F(TopSitesTest, TopSitesDB) {
  TopSitesDatabaseImpl db;

  ASSERT_TRUE(db.Init(file_name()));

  MostVisitedURL url;
  GURL asdf_url("http://asdf.com");
  string16 asdf_title(ASCIIToUTF16("ASDF"));
  GURL google_url("http://google.com");
  string16 google_title(ASCIIToUTF16("Google"));
  GURL news_url("http://news.google.com");
  string16 news_title(ASCIIToUTF16("Google News"));

  url.url = asdf_url;
  url.title = asdf_title;
  url.redirects.push_back(url.url);
  Images thumbnail;
  thumbnail.thumbnail = random_thumbnail();
  // Add asdf at rank 0.
  db.SetPageThumbnail(url, 0, thumbnail);

  Images result;
  EXPECT_TRUE(db.GetPageThumbnail(url.url, &result));
  EXPECT_EQ(thumbnail.thumbnail->data.size(), result.thumbnail->data.size());
  EXPECT_TRUE(ThumbnailsAreEqual(thumbnail.thumbnail, result.thumbnail));

  MostVisitedURLList urls;
  std::map<GURL, Images> thumbnails;
  db.GetPageThumbnails(&urls, &thumbnails);
  ASSERT_EQ(1u, urls.size());
  EXPECT_EQ(asdf_url, urls[0].url);
  EXPECT_EQ(asdf_title, urls[0].title);

  url.url = google_url;
  url.title = google_title;

  // Add google at rank 1 - no rank shifting.
  db.SetPageThumbnail(url, 1, thumbnail);
  db.GetPageThumbnails(&urls, &thumbnails);
  ASSERT_EQ(2u, urls.size());
  EXPECT_EQ(asdf_url, urls[0].url);
  EXPECT_EQ(asdf_title, urls[0].title);
  EXPECT_EQ(google_url, urls[1].url);
  EXPECT_EQ(google_title, urls[1].title);

  url.url = news_url;
  url.title = news_title;

  // Add news at rank 1 - shift google to rank 2.
  db.SetPageThumbnail(url, 1, thumbnail);
  db.GetPageThumbnails(&urls, &thumbnails);
  ASSERT_EQ(3u, urls.size());
  EXPECT_EQ(asdf_url, urls[0].url);
  EXPECT_EQ(news_url, urls[1].url);
  EXPECT_EQ(google_url, urls[2].url);

  // Move news at rank 0 - shift the rest up.
  db.SetPageThumbnail(url, 0, thumbnail);
  db.GetPageThumbnails(&urls, &thumbnails);
  ASSERT_EQ(3u, urls.size());
  EXPECT_EQ(news_url, urls[0].url);
  EXPECT_EQ(asdf_url, urls[1].url);
  EXPECT_EQ(google_url, urls[2].url);

  // Move news at rank 2 - shift the rest down.
  db.SetPageThumbnail(url, 2, thumbnail);
  db.GetPageThumbnails(&urls, &thumbnails);
  ASSERT_EQ(3u, urls.size());
  EXPECT_EQ(asdf_url, urls[0].url);
  EXPECT_EQ(google_url, urls[1].url);
  EXPECT_EQ(news_url, urls[2].url);

  // Delete asdf.
  url.url = asdf_url;
  db.RemoveURL(url);

  db.GetPageThumbnails(&urls, &thumbnails);
  ASSERT_EQ(2u, urls.size());
  EXPECT_EQ(google_url, urls[0].url);
  EXPECT_EQ(news_url, urls[1].url);
}

// Test TopSites with a real database.
TEST_F(TopSitesTest, RealDatabase) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  TopSitesDatabaseImpl* db = new TopSitesDatabaseImpl;

  ASSERT_TRUE(db->Init(file_name()));
  // |db| is destroyed when the top_sites is destroyed in TearDown.
  top_sites().db_.reset(db);
  MostVisitedURL url;
  GURL asdf_url("http://asdf.com");
  string16 asdf_title(ASCIIToUTF16("ASDF"));
  GURL google1_url("http://google.com");
  GURL google2_url("http://google.com/redirect");
  GURL google3_url("http://www.google.com");
  string16 google_title(ASCIIToUTF16("Google"));
  GURL news_url("http://news.google.com");
  string16 news_title(ASCIIToUTF16("Google News"));

  url.url = asdf_url;
  url.title = asdf_title;
  url.redirects.push_back(url.url);
  Images thumbnail;
  thumbnail.thumbnail = random_thumbnail();
  db->SetPageThumbnail(url, 0, thumbnail);

  top_sites().ReadDatabase();

  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(3u, urls().size());
  EXPECT_EQ(asdf_url, urls()[0].url);
  EXPECT_EQ(asdf_title, urls()[0].title);
  EXPECT_EQ(welcome_url(), urls()[1].url);
  EXPECT_EQ(themes_url(), urls()[2].url);

  Images img_result;
  db->GetPageThumbnail(asdf_url, &img_result);
  EXPECT_TRUE(img_result.thumbnail != NULL);
  EXPECT_TRUE(ThumbnailsAreEqual(random_thumbnail(), img_result.thumbnail));

  RefCountedBytes* thumbnail_result;
  EXPECT_TRUE(top_sites().GetPageThumbnail(asdf_url, &thumbnail_result));
  EXPECT_TRUE(thumbnail_result != NULL);
  EXPECT_TRUE(ThumbnailsAreEqual(random_thumbnail(), thumbnail_result));

  MostVisitedURL url2;
  url2.url = google1_url;
  url2.title = google_title;
  url2.redirects.push_back(google1_url);
  url2.redirects.push_back(google2_url);
  url2.redirects.push_back(google3_url);

  // Add new thumbnail at rank 0 and shift the other result to 1.
  Images g_thumbnail;
  g_thumbnail.thumbnail = google_thumbnail();
  db->SetPageThumbnail(url2, 0, g_thumbnail);

  top_sites().ReadDatabase();

  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ(google1_url, urls()[0].url);
  EXPECT_EQ(google_title, urls()[0].title);
  EXPECT_TRUE(top_sites().GetPageThumbnail(google1_url, &thumbnail_result));
  EXPECT_TRUE(ThumbnailsAreEqual(google_thumbnail(), thumbnail_result));
  ASSERT_EQ(3u, urls()[0].redirects.size());
  EXPECT_EQ(google1_url, urls()[0].redirects[0]);
  EXPECT_EQ(google2_url, urls()[0].redirects[1]);
  EXPECT_EQ(google3_url, urls()[0].redirects[2]);

  EXPECT_EQ(asdf_url, urls()[1].url);
  EXPECT_EQ(asdf_title, urls()[1].title);
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);

  MockHistoryServiceImpl hs;
  // Add one old, one new URL to the history.
  hs.AppendMockPage(google1_url, google_title);
  hs.AppendMockPage(news_url, news_title);
  top_sites().SetMockHistoryService(&hs);

  // This requests data from History Service and  writes it to the DB.
  top_sites().StartQueryForMostVisited();
  MessageLoop::current()->RunAllPending();

  std::map<GURL, Images> thumbnails;
  MostVisitedURLList results;
  db->GetPageThumbnails(&results, &thumbnails);
  ASSERT_EQ(4u, results.size());
  EXPECT_EQ(google_title, results[0].title);
  EXPECT_EQ(news_title, results[1].title);

  scoped_ptr<SkBitmap> weewar_bitmap(
      gfx::JPEGCodec::Decode(weewar_thumbnail()->front(),
                             weewar_thumbnail()->size()));

  base::Time now = base::Time::Now();
  ThumbnailScore low_score(1.0, true, true, now);
  ThumbnailScore medium_score(0.5, true, true, now);
  ThumbnailScore high_score(0.0, true, true, now);

  // 1. Set to weewar. (Writes the thumbnail to the DB.)
  EXPECT_TRUE(top_sites().SetPageThumbnail(google1_url,
                                           *weewar_bitmap,
                                           medium_score));
  RefCountedBytes* out_1;
  Images out_2;
  EXPECT_TRUE(top_sites().GetPageThumbnail(google1_url, &out_1));

  MessageLoop::current()->RunAllPending();

  db->GetPageThumbnail(url2.url, &out_2);
  EXPECT_TRUE(ThumbnailsAreEqual(out_1, out_2.thumbnail));

  scoped_ptr<SkBitmap> google_bitmap(
      gfx::JPEGCodec::Decode(google_thumbnail()->front(),
                             google_thumbnail()->size()));

  // 2. Set to google - low score.
  EXPECT_FALSE(top_sites().SetPageThumbnail(google1_url,
                                            *google_bitmap,
                                            low_score));

  // 3. Set to google - high score.
  EXPECT_TRUE(top_sites().SetPageThumbnail(google1_url,
                                           *google_bitmap,
                                           high_score));
  // Check that the thumbnail was updated.
  EXPECT_TRUE(top_sites().GetPageThumbnail(google1_url, &out_1));
  EXPECT_FALSE(ThumbnailsAreEqual(out_1, out_2.thumbnail));
  MessageLoop::current()->RunAllPending();

  // Read the new thumbnail from the DB - should match what's in TopSites.
  db->GetPageThumbnail(url2.url, &out_2);
  EXPECT_TRUE(ThumbnailsAreEqual(out_1, out_2.thumbnail));
  EXPECT_TRUE(high_score.Equals(out_2.thumbnail_score));
}

TEST_F(TopSitesTest, DeleteNotifications) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  GURL google1_url("http://google.com");
  GURL google2_url("http://google.com/redirect");
  GURL google3_url("http://www.google.com");
  string16 google_title(ASCIIToUTF16("Google"));
  GURL news_url("http://news.google.com");
  string16 news_title(ASCIIToUTF16("Google News"));

  MockHistoryServiceImpl hs;

  top_sites().Init(file_name());

  hs.AppendMockPage(google1_url, google_title);
  hs.AppendMockPage(news_url, news_title);
  top_sites().SetMockHistoryService(&hs);

  top_sites().StartQueryForMostVisited();
  MessageLoop::current()->RunAllPending();

  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  // 2 extra prepopulated URLs.
  ASSERT_EQ(4u, urls().size());

  hs.RemoveMostVisitedURL();

  history::URLsDeletedDetails history_details;
  history_details.all_history = false;
  Details<URLsDeletedDetails> details(&history_details);
  top_sites().Observe(NotificationType::HISTORY_URLS_DELETED,
                      Source<Profile> (&profile()),
                      details);
  MessageLoop::current()->RunAllPending();

  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(3u, urls().size());
  EXPECT_EQ(google_title, urls()[0].title);
  EXPECT_EQ(welcome_url(), urls()[1].url);
  EXPECT_EQ(themes_url(), urls()[2].url);

  hs.RemoveMostVisitedURL();
  history_details.all_history = true;
  details = Details<HistoryDetails>(&history_details);
  top_sites().Observe(NotificationType::HISTORY_URLS_DELETED,
                      Source<Profile> (&profile()),
                      details);
  MessageLoop::current()->RunAllPending();
  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(2u, urls().size());
  EXPECT_EQ(welcome_url(), urls()[0].url);
  EXPECT_EQ(themes_url(), urls()[1].url);
}

TEST_F(TopSitesTest, PinnedURLsDeleted) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  GURL google1_url("http://google.com");
  GURL google2_url("http://google.com/redirect");
  GURL google3_url("http://www.google.com");
  string16 google_title(ASCIIToUTF16("Google"));
  GURL news_url("http://news.google.com");
  string16 news_title(ASCIIToUTF16("Google News"));

  MockHistoryServiceImpl hs;

  top_sites().Init(file_name());

  hs.AppendMockPage(google1_url, google_title);
  hs.AppendMockPage(news_url, news_title);
  top_sites().SetMockHistoryService(&hs);

  top_sites().StartQueryForMostVisited();
  MessageLoop::current()->RunAllPending();
  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(1u, number_of_callbacks());
  // 2 extra prepopulated URLs.
  ASSERT_EQ(4u, urls().size());

  top_sites().AddPinnedURL(news_url, 3);
  EXPECT_TRUE(top_sites().IsURLPinned(news_url));

  hs.RemoveMostVisitedURL();
  history::URLsDeletedDetails history_details;
  history_details.all_history = false;
  history_details.urls.insert(news_url);
  Details<URLsDeletedDetails> details(&history_details);
  top_sites().Observe(NotificationType::HISTORY_URLS_DELETED,
                      Source<Profile> (&profile()),
                      details);
  MessageLoop::current()->RunAllPending();
  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(2u, number_of_callbacks());
  ASSERT_EQ(3u, urls().size());
  EXPECT_FALSE(top_sites().IsURLPinned(news_url));

  hs.RemoveMostVisitedURL();
  history_details.all_history = true;
  details = Details<HistoryDetails>(&history_details);
  top_sites().Observe(NotificationType::HISTORY_URLS_DELETED,
                      Source<Profile> (&profile()),
                      details);
  MessageLoop::current()->RunAllPending();
  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(2u, urls().size());
  MessageLoop::current()->RunAllPending();

  top_sites().StartQueryForMostVisited();
  MessageLoop::current()->RunAllPending();
  top_sites().GetMostVisitedURLs(
      consumer(),
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(2u, urls().size());
  EXPECT_EQ(welcome_url(), urls()[0].url);
  EXPECT_EQ(themes_url(), urls()[1].url);
}

TEST_F(TopSitesTest, GetUpdateDelay) {
  top_sites().last_num_urls_changed_ = 0;
  EXPECT_EQ(30, top_sites().GetUpdateDelay().InSeconds());

  top_sites().top_sites_.resize(20);
  top_sites().last_num_urls_changed_ = 0;
  EXPECT_EQ(60, top_sites().GetUpdateDelay().InMinutes());

  top_sites().last_num_urls_changed_ = 3;
  EXPECT_EQ(52, top_sites().GetUpdateDelay().InMinutes());

  top_sites().last_num_urls_changed_ = 20;
  EXPECT_EQ(1, top_sites().GetUpdateDelay().InMinutes());
}

TEST_F(TopSitesTest, Migration) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  GURL google1_url("http://google.com");
  string16 google_title(ASCIIToUTF16("Google"));
  GURL news_url("http://news.google.com");
  string16 news_title(ASCIIToUTF16("Google News"));

  MockHistoryServiceImpl hs;

  top_sites().Init(file_name());

  hs.AppendMockPage(google1_url, google_title);
  hs.AppendMockPage(news_url, news_title);
  top_sites().SetMockHistoryService(&hs);
  MessageLoop::current()->RunAllPending();

  top_sites().StartMigration();
  EXPECT_TRUE(top_sites().migration_in_progress_);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(2, hs.GetNumberOfThumbnailRequests());
  EXPECT_FALSE(top_sites().migration_in_progress_);
}

TEST_F(TopSitesTest, QueueingRequestsForTopSites) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  CancelableRequestConsumer c1;
  CancelableRequestConsumer c2;
  CancelableRequestConsumer c3;
  top_sites().GetMostVisitedURLs(
      &c1,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  top_sites().GetMostVisitedURLs(
      &c2,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  top_sites().GetMostVisitedURLs(
      &c3,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  EXPECT_EQ(0u, number_of_callbacks());
  EXPECT_EQ(0u, urls().size());

  MostVisitedURLList pages;
  MostVisitedURL url;
  url.url = GURL("http://1.com/");
  url.redirects.push_back(url.url);
  pages.push_back(url);
  url.url = GURL("http://2.com/");
  url.redirects.push_back(url.url);
  pages.push_back(url);
  top_sites().OnTopSitesAvailable(0, pages);
  MessageLoop::current()->RunAllPending();

  EXPECT_EQ(3u, number_of_callbacks());

  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://1.com/", urls()[0].url.spec());
  EXPECT_EQ("http://2.com/", urls()[1].url.spec());
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);


  url.url = GURL("http://3.com/");
  url.redirects.push_back(url.url);
  pages.push_back(url);
  top_sites().OnTopSitesAvailable(0, pages);
  MessageLoop::current()->RunAllPending();

  top_sites().GetMostVisitedURLs(
      &c3,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  EXPECT_EQ(4u, number_of_callbacks());

  ASSERT_EQ(5u, urls().size());
  EXPECT_EQ("http://1.com/", urls()[0].url.spec());
  EXPECT_EQ("http://2.com/", urls()[1].url.spec());
  EXPECT_EQ("http://3.com/", urls()[2].url.spec());
  EXPECT_EQ(welcome_url(), urls()[3].url);
  EXPECT_EQ(themes_url(), urls()[4].url);

}

TEST_F(TopSitesTest, CancelingRequestsForTopSites) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  CancelableRequestConsumer c1;
  CancelableRequestConsumer c2;
  top_sites().GetMostVisitedURLs(
      &c1,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  top_sites().GetMostVisitedURLs(
      &c2,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  {
    CancelableRequestConsumer c3;
    top_sites().GetMostVisitedURLs(
        &c3,
        NewCallback(static_cast<TopSitesTest*>(this),
                    &TopSitesTest::OnTopSitesAvailable));
    // c3 is out of scope, and the request should be cancelled.
  }

  // No requests until OnTopSitesAvailable is called.
  EXPECT_EQ(0u, number_of_callbacks());
  EXPECT_EQ(0u, urls().size());

  MostVisitedURLList pages;
  MostVisitedURL url;
  url.url = GURL("http://1.com/");
  url.redirects.push_back(url.url);
  pages.push_back(url);
  url.url = GURL("http://2.com/");
  pages.push_back(url);

  top_sites().OnTopSitesAvailable(0, pages);
  MessageLoop::current()->RunAllPending();

  // 1 request was canceled.
  EXPECT_EQ(2u, number_of_callbacks());

  // 2 extra prepopulated URLs.
  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://1.com/", urls()[0].url.spec());
  EXPECT_EQ("http://2.com/", urls()[1].url.spec());
}

TEST_F(TopSitesTest, AddTemporaryThumbnail) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  GURL unknown_url("http://news.google.com/");
  GURL invalid_url("chrome://thumb/http://google.com/");
  GURL url1a("http://google.com/");
  GURL url1b("http://www.google.com/");

  // Create a dummy thumbnail.
  SkBitmap thumbnail;
  thumbnail.setConfig(SkBitmap::kARGB_8888_Config, 4, 4);
  thumbnail.allocPixels();
  thumbnail.eraseRGB(0x00, 0x00, 0x00);

  ThumbnailScore medium_score(0.5, true, true, base::Time::Now());

  // Don't store thumbnails for Javascript URLs.
  EXPECT_FALSE(top_sites().SetPageThumbnail(invalid_url,
                                            thumbnail, medium_score));
  // Store thumbnails for unknown (but valid) URLs temporarily - calls
  // AddTemporaryThumbnail.
  EXPECT_TRUE(top_sites().SetPageThumbnail(unknown_url,
                                           thumbnail, medium_score));

  std::vector<MostVisitedURL> list;

  MostVisitedURL mv;
  mv.url = unknown_url;
  mv.redirects.push_back(mv.url);
  mv.redirects.push_back(url1a);
  mv.redirects.push_back(url1b);
  list.push_back(mv);

  // Update URLs - use temporary thumbnails.
  top_sites().UpdateMostVisited(list);

  RefCountedBytes* out = NULL;
  ASSERT_TRUE(top_sites().GetPageThumbnail(unknown_url, &out));
  scoped_ptr<SkBitmap> out_bitmap(gfx::JPEGCodec::Decode(out->front(),
                                                         out->size()));
  EXPECT_EQ(0, memcmp(thumbnail.getPixels(), out_bitmap->getPixels(),
                      thumbnail.getSize()));
}

TEST_F(TopSitesTest, Blacklisting) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  MostVisitedURLList pages;
  MostVisitedURL url, url1;
  url.url = GURL("http://bbc.com/");
  url.redirects.push_back(url.url);
  pages.push_back(url);
  url1.url = GURL("http://google.com/");
  url1.redirects.push_back(url1.url);
  pages.push_back(url1);

  CancelableRequestConsumer c;
  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  top_sites().OnTopSitesAvailable(0, pages);
  MessageLoop::current()->RunAllPending();
  {
    Lock& l = lock();
    AutoLock lock(l);  // IsBlacklisted must be in a lock.
    EXPECT_FALSE(top_sites().IsBlacklisted(GURL("http://bbc.com/")));
  }

  EXPECT_EQ(1u, number_of_callbacks());

  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ("http://google.com/", urls()[1].url.spec());
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);
  EXPECT_FALSE(top_sites().HasBlacklistedItems());

  top_sites().AddBlacklistedURL(GURL("http://google.com/"));
  EXPECT_TRUE(top_sites().HasBlacklistedItems());
  {
    Lock& l = lock();
    AutoLock lock(l);  // IsBlacklisted must be in a lock.
    EXPECT_TRUE(top_sites().IsBlacklisted(GURL("http://google.com/")));
    EXPECT_FALSE(top_sites().IsBlacklisted(GURL("http://bbc.com/")));
    EXPECT_FALSE(top_sites().IsBlacklisted(welcome_url()));
  }

  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(2u, number_of_callbacks());
  ASSERT_EQ(3u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ(welcome_url(), urls()[1].url);
  EXPECT_EQ(themes_url(), urls()[2].url);

  top_sites().AddBlacklistedURL(welcome_url());
  EXPECT_TRUE(top_sites().HasBlacklistedItems());
  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(2u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ(themes_url(), urls()[1].url);

  top_sites().RemoveBlacklistedURL(GURL("http://google.com/"));
  EXPECT_TRUE(top_sites().HasBlacklistedItems());
  {
    Lock& l = lock();
    AutoLock lock(l);  // IsBlacklisted must be in a lock.
    EXPECT_FALSE(top_sites().IsBlacklisted(GURL("http://google.com/")));
  }

  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(3u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ("http://google.com/", urls()[1].url.spec());
  EXPECT_EQ(themes_url(), urls()[2].url);

  top_sites().ClearBlacklistedURLs();
  EXPECT_FALSE(top_sites().HasBlacklistedItems());
  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ("http://google.com/", urls()[1].url.spec());
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);
}

TEST_F(TopSitesTest, PinnedURLs) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  MostVisitedURLList pages;
  MostVisitedURL url, url1;
  url.url = GURL("http://bbc.com/");
  url.redirects.push_back(url.url);
  pages.push_back(url);
  url1.url = GURL("http://google.com/");
  url1.redirects.push_back(url1.url);
  pages.push_back(url1);

  CancelableRequestConsumer c;
  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  top_sites().OnTopSitesAvailable(0, pages);
  MessageLoop::current()->RunAllPending();
  EXPECT_FALSE(top_sites().IsURLPinned(GURL("http://bbc.com/")));

  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ("http://google.com/", urls()[1].url.spec());
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);

  top_sites().AddPinnedURL(GURL("http://google.com/"), 3);
  EXPECT_FALSE(top_sites().IsURLPinned(GURL("http://bbc.com/")));
  EXPECT_FALSE(top_sites().IsURLPinned(welcome_url()));

  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  EXPECT_EQ(2u, number_of_callbacks());
  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ(welcome_url(), urls()[1].url);
  EXPECT_EQ(themes_url(), urls()[2].url);
  EXPECT_EQ("http://google.com/", urls()[3].url.spec());

  top_sites().RemovePinnedURL(GURL("http://google.com/"));
  EXPECT_FALSE(top_sites().IsURLPinned(GURL("http://google.com/")));
  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://bbc.com/", urls()[0].url.spec());
  EXPECT_EQ("http://google.com/", urls()[1].url.spec());
  EXPECT_EQ(welcome_url(), urls()[2].url);
  EXPECT_EQ(themes_url(), urls()[3].url);

  top_sites().AddPinnedURL(GURL("http://bbc.com"), 1);
  top_sites().AddPinnedURL(themes_url(), 0);
  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ(themes_url(), urls()[0].url);
  EXPECT_EQ("http://bbc.com/", urls()[1].url.spec());
  EXPECT_EQ("http://google.com/", urls()[2].url.spec());
  EXPECT_EQ(welcome_url(), urls()[3].url);

  top_sites().RemovePinnedURL(GURL("http://bbc.com"));
  top_sites().RemovePinnedURL(themes_url());

  top_sites().AddPinnedURL(welcome_url(), 1);
  top_sites().AddPinnedURL(GURL("http://bbc.com"), 3);

  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  ASSERT_EQ(4u, urls().size());
  EXPECT_EQ("http://google.com/", urls()[0].url.spec());
  EXPECT_EQ(welcome_url(), urls()[1].url);
  EXPECT_EQ(themes_url(), urls()[2].url);
  EXPECT_EQ("http://bbc.com/", urls()[3].url.spec());
}

TEST_F(TopSitesTest, BlacklistingAndPinnedURLs) {
  BrowserThread db_loop(BrowserThread::DB, MessageLoop::current());
  MostVisitedURLList pages;
  CancelableRequestConsumer c;
  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));
  top_sites().OnTopSitesAvailable(0, pages);
  MessageLoop::current()->RunAllPending();

  ASSERT_EQ(2u, urls().size());
  EXPECT_EQ(welcome_url(), urls()[0].url);
  EXPECT_EQ(themes_url(), urls()[1].url);

  top_sites().AddPinnedURL(themes_url(), 1);
  top_sites().AddBlacklistedURL(welcome_url());

  top_sites().GetMostVisitedURLs(
      &c,
      NewCallback(static_cast<TopSitesTest*>(this),
                  &TopSitesTest::OnTopSitesAvailable));

  ASSERT_EQ(2u, urls().size());
  EXPECT_EQ(GURL(), urls()[0].url);
  EXPECT_EQ(themes_url(), urls()[1].url);

}

TEST_F(TopSitesTest, AddPrepopulatedPages) {
  MostVisitedURLList pages;
  top_sites().AddPrepopulatedPages(&pages);
  ASSERT_EQ(2u, pages.size());
  EXPECT_EQ(welcome_url(), pages[0].url);
  EXPECT_EQ(themes_url(), pages[1].url);

  pages.clear();

  MostVisitedURL url(themes_url(), GURL(), string16());
  pages.push_back(url);

  top_sites().AddPrepopulatedPages(&pages);

  // Themes URL is already in pages; should not be added twice.
  ASSERT_EQ(2u, pages.size());
  EXPECT_EQ(themes_url(), pages[0].url);
  EXPECT_EQ(welcome_url(), pages[1].url);
}

}  // namespace history
