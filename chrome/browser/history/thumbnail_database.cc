// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/thumbnail_database.h"

#include "app/sql/statement.h"
#include "app/sql/transaction.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/ref_counted_memory.h"
#include "base/time.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/diagnostics/sqlite_diagnostics.h"
#include "chrome/browser/history/history_publisher.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/browser/history/url_database.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/thumbnail_score.h"
#include "gfx/codec/jpeg_codec.h"
#include "third_party/skia/include/core/SkBitmap.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#endif

namespace history {

// Version number of the database.
static const int kCurrentVersionNumber = 3;
static const int kCompatibleVersionNumber = 3;

ThumbnailDatabase::ThumbnailDatabase() : history_publisher_(NULL),
                                         use_top_sites_(false) {
}

ThumbnailDatabase::~ThumbnailDatabase() {
  // The DBCloseScoper will delete the DB and the cache.
}

sql::InitStatus ThumbnailDatabase::Init(
    const FilePath& db_name,
    const HistoryPublisher* history_publisher) {
  history_publisher_ = history_publisher;
  sql::InitStatus status = OpenDatabase(&db_, db_name);
  if (status != sql::INIT_OK)
    return status;

  // Scope initialization in a transaction so we can't be partially initialized.
  sql::Transaction transaction(&db_);
  transaction.Begin();

#if defined(OS_MACOSX)
  // Exclude the thumbnails file and its journal from backups.
  base::mac::SetFileBackupExclusion(db_name, true);
  FilePath::StringType db_name_string(db_name.value());
  db_name_string += "-journal";
  FilePath db_journal_name(db_name_string);
  base::mac::SetFileBackupExclusion(db_journal_name, true);
#endif

  // Create the tables.
  if (!meta_table_.Init(&db_, kCurrentVersionNumber,
                        kCompatibleVersionNumber) ||
      !InitThumbnailTable() ||
      !InitFavIconsTable(&db_, false)) {
    db_.Close();
    return sql::INIT_FAILURE;
  }
  InitFavIconsIndex();

  // Version check. We should not encounter a database too old for us to handle
  // in the wild, so we try to continue in that case.
  if (meta_table_.GetCompatibleVersionNumber() > kCurrentVersionNumber) {
    LOG(WARNING) << "Thumbnail database is too new.";
    return sql::INIT_TOO_NEW;
  }

  int cur_version = meta_table_.GetVersionNumber();
  if (cur_version == 2) {
    if (!UpgradeToVersion3()) {
      LOG(WARNING) << "Unable to update to thumbnail database to version 3.";
      db_.Close();
      return sql::INIT_FAILURE;
    }
    ++cur_version;
  }

  LOG_IF(WARNING, cur_version < kCurrentVersionNumber) <<
      "Thumbnail database version " << cur_version << " is too old to handle.";

  // Initialization is complete.
  if (!transaction.Commit()) {
    db_.Close();
    return sql::INIT_FAILURE;
  }

  return sql::INIT_OK;
}

sql::InitStatus ThumbnailDatabase::OpenDatabase(sql::Connection* db,
                                                const FilePath& db_name) {
  // Set the exceptional sqlite error handler.
  db->set_error_delegate(GetErrorHandlerForThumbnailDb());

  // Thumbnails db now only stores favicons, so we don't need that big a page
  // size or cache.
  db->set_page_size(2048);
  db->set_cache_size(32);

  // Run the database in exclusive mode. Nobody else should be accessing the
  // database while we're running, and this will give somewhat improved perf.
  db->set_exclusive_locking();

  if (!db->Open(db_name))
    return sql::INIT_FAILURE;

  return sql::INIT_OK;
}

bool ThumbnailDatabase::InitThumbnailTable() {
  if (!db_.DoesTableExist("thumbnails")) {
    if (history::TopSites::IsEnabled()) {
      use_top_sites_ = true;
      return true;
    }
    if (!db_.Execute("CREATE TABLE thumbnails ("
        "url_id INTEGER PRIMARY KEY,"
        "boring_score DOUBLE DEFAULT 1.0,"
        "good_clipping INTEGER DEFAULT 0,"
        "at_top INTEGER DEFAULT 0,"
        "last_updated INTEGER DEFAULT 0,"
        "data BLOB)"))
      return false;
  }
  return true;
}

bool ThumbnailDatabase::UpgradeToVersion3() {
  if (use_top_sites_) {
    meta_table_.SetVersionNumber(3);
    meta_table_.SetCompatibleVersionNumber(
        std::min(3, kCompatibleVersionNumber));
    return true;  // Not needed after migration to TopSites.
  }

  // sqlite doesn't like the "ALTER TABLE xxx ADD (column_one, two,
  // three)" syntax, so list out the commands we need to execute:
  const char* alterations[] = {
    "ALTER TABLE thumbnails ADD boring_score DOUBLE DEFAULT 1.0",
    "ALTER TABLE thumbnails ADD good_clipping INTEGER DEFAULT 0",
    "ALTER TABLE thumbnails ADD at_top INTEGER DEFAULT 0",
    "ALTER TABLE thumbnails ADD last_updated INTEGER DEFAULT 0",
    NULL
  };

  for (int i = 0; alterations[i] != NULL; ++i) {
    if (!db_.Execute(alterations[i])) {
      NOTREACHED();
      return false;
    }
  }

  meta_table_.SetVersionNumber(3);
  meta_table_.SetCompatibleVersionNumber(std::min(3, kCompatibleVersionNumber));
  return true;
}

bool ThumbnailDatabase::RecreateThumbnailTable() {
  if (use_top_sites_)
    return true;  // Not needed after migration to TopSites.

  if (!db_.Execute("DROP TABLE thumbnails"))
    return false;
  return InitThumbnailTable();
}

bool ThumbnailDatabase::InitFavIconsTable(sql::Connection* db,
                                          bool is_temporary) {
  // Note: if you update the schema, don't forget to update
  // CopyToTemporaryFaviconTable as well.
  const char* name = is_temporary ? "temp_favicons" : "favicons";
  if (!db->DoesTableExist(name)) {
    std::string sql;
    sql.append("CREATE TABLE ");
    sql.append(name);
    sql.append("("
               "id INTEGER PRIMARY KEY,"
               "url LONGVARCHAR NOT NULL,"
               "last_updated INTEGER DEFAULT 0,"
               "image_data BLOB)");
    if (!db->Execute(sql.c_str()))
      return false;
  }
  return true;
}

void ThumbnailDatabase::InitFavIconsIndex() {
  // Add an index on the url column. We ignore errors. Since this is always
  // called during startup, the index will normally already exist.
  db_.Execute("CREATE INDEX favicons_url ON favicons(url)");
}

void ThumbnailDatabase::BeginTransaction() {
  db_.BeginTransaction();
}

void ThumbnailDatabase::CommitTransaction() {
  db_.CommitTransaction();
}

void ThumbnailDatabase::Vacuum() {
  DCHECK(db_.transaction_nesting() == 0) <<
      "Can not have a transaction when vacuuming.";
  db_.Execute("VACUUM");
}

void ThumbnailDatabase::SetPageThumbnail(
    const GURL& url,
    URLID id,
    const SkBitmap& thumbnail,
    const ThumbnailScore& score,
    base::Time time) {
  if (use_top_sites_) {
    LOG(WARNING) << "Use TopSites instead.";
    return;  // Not possible after migration to TopSites.
  }

  if (!thumbnail.isNull()) {
    bool add_thumbnail = true;
    ThumbnailScore current_score;
    if (ThumbnailScoreForId(id, &current_score)) {
      add_thumbnail = ShouldReplaceThumbnailWith(current_score, score);
    }

    if (add_thumbnail) {
      sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
          "INSERT OR REPLACE INTO thumbnails "
          "(url_id, boring_score, good_clipping, at_top, last_updated, data) "
          "VALUES (?,?,?,?,?,?)"));
      if (!statement)
        return;

      // We use 90 quality (out of 100) which is pretty high, because
      // we're very sensitive to artifacts for these small sized,
      // highly detailed images.
      std::vector<unsigned char> jpeg_data;
      SkAutoLockPixels thumbnail_lock(thumbnail);
      bool encoded = gfx::JPEGCodec::Encode(
          reinterpret_cast<unsigned char*>(thumbnail.getAddr32(0, 0)),
          gfx::JPEGCodec::FORMAT_SkBitmap, thumbnail.width(),
          thumbnail.height(),
          static_cast<int>(thumbnail.rowBytes()), 90,
          &jpeg_data);

      if (encoded) {
        statement.BindInt64(0, id);
        statement.BindDouble(1, score.boring_score);
        statement.BindBool(2, score.good_clipping);
        statement.BindBool(3, score.at_top);
        statement.BindInt64(4, score.time_at_snapshot.ToTimeT());
        statement.BindBlob(5, &jpeg_data[0],
                           static_cast<int>(jpeg_data.size()));
        if (!statement.Run())
          NOTREACHED() << db_.GetErrorMessage();
      }

      // Publish the thumbnail to any indexers listening to us.
      // The tests may send an invalid url. Hence avoid publishing those.
      if (url.is_valid() && history_publisher_ != NULL)
        history_publisher_->PublishPageThumbnail(jpeg_data, url, time);
    }
  } else {
    if (!DeleteThumbnail(id) )
      DLOG(WARNING) << "Unable to delete thumbnail";
  }
}

bool ThumbnailDatabase::GetPageThumbnail(URLID id,
                                         std::vector<unsigned char>* data) {
  if (use_top_sites_) {
    LOG(WARNING) << "Use TopSites instead.";
    return false;  // Not possible after migration to TopSites.
  }

  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT data FROM thumbnails WHERE url_id=?"));
  if (!statement)
    return false;

  statement.BindInt64(0, id);
  if (!statement.Step())
    return false;  // don't have a thumbnail for this ID

  statement.ColumnBlobAsVector(0, data);
  return true;
}

bool ThumbnailDatabase::DeleteThumbnail(URLID id) {
  if (use_top_sites_) {
    return true;  // Not possible after migration to TopSites.
  }

  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM thumbnails WHERE url_id = ?"));
  if (!statement)
    return false;

  statement.BindInt64(0, id);
  return statement.Run();
}

bool ThumbnailDatabase::ThumbnailScoreForId(URLID id,
                                            ThumbnailScore* score) {
  if (use_top_sites_) {
    LOG(WARNING) << "Use TopSites instead.";
    return false;  // Not possible after migration to TopSites.
  }

  // Fetch the current thumbnail's information to make sure we
  // aren't replacing a good thumbnail with one that's worse.
  sql::Statement select_statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT boring_score, good_clipping, at_top, last_updated "
      "FROM thumbnails WHERE url_id=?"));
  if (!select_statement) {
    NOTREACHED() << "Couldn't build select statement!";
  } else {
    select_statement.BindInt64(0, id);
    if (select_statement.Step()) {
      double current_boring_score = select_statement.ColumnDouble(0);
      bool current_clipping = select_statement.ColumnBool(1);
      bool current_at_top = select_statement.ColumnBool(2);
      base::Time last_updated =
          base::Time::FromTimeT(select_statement.ColumnInt64(3));
      *score = ThumbnailScore(current_boring_score, current_clipping,
                              current_at_top, last_updated);
      return true;
    }
  }

  return false;
}

bool ThumbnailDatabase::SetFavIcon(URLID icon_id,
                                   scoped_refptr<RefCountedMemory> icon_data,
                                   base::Time time) {
  DCHECK(icon_id);
  if (icon_data->size()) {
    sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
        "UPDATE favicons SET image_data=?, last_updated=? WHERE id=?"));
    if (!statement)
      return 0;

    statement.BindBlob(0, icon_data->front(),
                       static_cast<int>(icon_data->size()));
    statement.BindInt64(1, time.ToTimeT());
    statement.BindInt64(2, icon_id);
    return statement.Run();
  } else {
    sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
        "UPDATE favicons SET image_data=NULL, last_updated=? WHERE id=?"));
    if (!statement)
      return 0;

    statement.BindInt64(0, time.ToTimeT());
    statement.BindInt64(1, icon_id);
    return statement.Run();
  }
}

bool ThumbnailDatabase::SetFavIconLastUpdateTime(FavIconID icon_id,
                                                 base::Time time) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "UPDATE favicons SET last_updated=? WHERE id=?"));
  if (!statement)
    return 0;

  statement.BindInt64(0, time.ToTimeT());
  statement.BindInt64(1, icon_id);
  return statement.Run();
}

FavIconID ThumbnailDatabase::GetFavIconIDForFavIconURL(const GURL& icon_url) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT id FROM favicons WHERE url=?"));
  if (!statement)
    return 0;

  statement.BindString(0, URLDatabase::GURLToDatabaseURL(icon_url));
  if (!statement.Step())
    return 0;  // not cached

  return statement.ColumnInt64(0);
}

bool ThumbnailDatabase::GetFavIcon(
    FavIconID icon_id,
    base::Time* last_updated,
    std::vector<unsigned char>* png_icon_data,
    GURL* icon_url) {
  DCHECK(icon_id);

  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT last_updated, image_data, url FROM favicons WHERE id=?"));
  if (!statement)
    return 0;

  statement.BindInt64(0, icon_id);

  if (!statement.Step())
    return false;  // No entry for the id.

  *last_updated = base::Time::FromTimeT(statement.ColumnInt64(0));
  if (statement.ColumnByteLength(1) > 0)
    statement.ColumnBlobAsVector(1, png_icon_data);
  if (icon_url)
    *icon_url = GURL(statement.ColumnString(2));

  return true;
}

FavIconID ThumbnailDatabase::AddFavIcon(const GURL& icon_url) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO favicons (url) VALUES (?)"));
  if (!statement)
    return 0;

  statement.BindString(0, URLDatabase::GURLToDatabaseURL(icon_url));
  if (!statement.Run())
    return 0;
  return db_.GetLastInsertRowId();
}

bool ThumbnailDatabase::DeleteFavIcon(FavIconID id) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM favicons WHERE id = ?"));
  if (!statement)
    return false;
  statement.BindInt64(0, id);
  return statement.Run();
}

FavIconID ThumbnailDatabase::CopyToTemporaryFavIconTable(FavIconID source) {
  sql::Statement statement(db_.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO temp_favicons (url, last_updated, image_data)"
      "SELECT url, last_updated, image_data "
      "FROM favicons WHERE id = ?"));
  if (!statement)
    return 0;
  statement.BindInt64(0, source);
  if (!statement.Run())
    return 0;

  // We return the ID of the newly inserted favicon.
  return db_.GetLastInsertRowId();
}

bool ThumbnailDatabase::CommitTemporaryFavIconTable() {
  // Delete the old favicons table.
  if (!db_.Execute("DROP TABLE favicons"))
    return false;

  // Rename the temporary one.
  if (!db_.Execute("ALTER TABLE temp_favicons RENAME TO favicons"))
    return false;

  // The renamed table needs the index (the temporary table doesn't have one).
  InitFavIconsIndex();
  return true;
}

bool ThumbnailDatabase::NeedsMigrationToTopSites() {
  return !use_top_sites_;
}

bool ThumbnailDatabase::RenameAndDropThumbnails(const FilePath& old_db_file,
                                                const FilePath& new_db_file) {
  // Init favicons table - same schema as the thumbnails.
  sql::Connection favicons;
  if (OpenDatabase(&favicons, new_db_file) != sql::INIT_OK)
    return false;

  if (!InitFavIconsTable(&favicons, false)) {
    NOTREACHED() << "Couldn't init favicons table.";
    favicons.Close();
    return false;
  }
  favicons.Close();

  // Can't attach within a transaction.
  if (transaction_nesting())
    CommitTransaction();

  // Attach new DB.
  {
    // This block is needed because otherwise the attach statement is
    // never cleared from cache and we can't close the DB :P
    sql::Statement attach(db_.GetUniqueStatement("ATTACH ? AS new_favicons"));
    if (!attach) {
      NOTREACHED() << "Unable to attach database.";
      // Keep the transaction open, even though we failed.
      BeginTransaction();
      return false;
    }

#if defined(OS_POSIX)
    attach.BindString(0, new_db_file.value());
#else
    attach.BindString(0, WideToUTF8(new_db_file.value()));
#endif

    if (!attach.Run()) {
      NOTREACHED() << db_.GetErrorMessage();
      BeginTransaction();
      return false;
    }
  }

  // Move favicons to the new DB.
  if (!db_.Execute("INSERT OR REPLACE INTO new_favicons.favicons "
                   "SELECT * FROM favicons")) {
    NOTREACHED() << "Unable to copy favicons.";
    BeginTransaction();
    return false;
  }

  if (!db_.Execute("DETACH new_favicons")) {
    NOTREACHED() << "Unable to detach database.";
    BeginTransaction();
    return false;
  }

  db_.Close();

  // Reset the DB to point to new file.
  if (OpenDatabase(&db_, new_db_file) != sql::INIT_OK)
    return false;

  file_util::Delete(old_db_file, false);

  InitFavIconsIndex();

  // Reopen the transaction.
  BeginTransaction();
  use_top_sites_ = true;
  return true;
}

}  // namespace history
