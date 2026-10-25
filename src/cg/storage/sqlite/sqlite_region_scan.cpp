// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_region_scan.h"

#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/sqlite/sqlite_region_sql.h"
#include "cg/storage/tile_date.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <deque>
#include <optional>
#include <utility>

namespace cg::storage::sqlite {

namespace {

constexpr int kMaxFolderDepth = 10; // u_TileStorageSQLite.pas:508 cMaxFolderDepth

// SysUtils's 2-arg SameText folds only ASCII 'A'-'Z' (same helper as sqlite_region_handler_read.cpp).
bool asciiSameText(const QString& a, const QString& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        ushort ca = a[i].unicode();
        ushort cb = b[i].unicode();
        if (ca >= u'a' && ca <= u'z') {
            ca -= (u'a' - u'A');
        }
        if (cb >= u'a' && cb <= u'z') {
            cb -= (u'a' - u'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

// Ports TEnumTileInfoBySQLite + TTileStorageSQLiteFetcherComplex, collapsed into one class (the
// ITileStorageSQLiteFetcher interface has this single implementation and one user).
class SqliteRegionScanEnumerator final : public IEnumTileInfo {
public:
    SqliteRegionScanEnumerator(std::shared_ptr<SqliteRegionPool> pool, QString storagePath,
                               ITileNameCodecPtr codec, bool ignoreTne)
        : pool_(std::move(pool)), storagePath_(std::move(storagePath)), codec_(std::move(codec)),
          ignoreTne_(ignoreTne) {
        pending_.emplace_back(QString(), 0);
    }

    ~SqliteRegionScanEnumerator() override { closeFile(); }

    // Ports TEnumTileInfoBySQLite.Next (:107-189).
    bool next(TileInfo& out, geo::PointI& tile, quint8& zoom) override {
        if (state_ == State::Finish) {
            return false;
        }
        for (;;) {
            while (state_ == State::Start) {
                QString rel;
                if (!nextFile(rel)) {
                    closeFile();
                    state_ = State::Finish;
                    return false;
                }
                bool ascii = true;
                for (const QChar ch : rel) {
                    if (ch.unicode() > 0x7F) {
                        ascii = false;
                        break;
                    }
                }
                geo::PointI dummy;
                quint8 z = 0;
                if (!ascii || !codec_->parse(rel, dummy, z)) {
                    closeFile();
                    continue;
                }
                zoom_ = z;
                openFile(storagePath_ + rel);
                if (lease_ != nullptr && fetch(out)) {
                    state_ = State::Fetched;
                    tile = tile_;
                    zoom = zoom_;
                    return true;
                }
                closeFile(); // not opened, or no row
            }
            if (state_ == State::Fetched) {
                if (fetch(out)) {
                    tile = tile_;
                    zoom = zoom_;
                    return true;
                }
                closeFile();
                state_ = State::Start;
            }
        }
    }

private:
    enum class State : std::uint8_t { Start, Fetched, Finish };

    // The files walk: ports TFileNameIteratorFolderWithSubfolders over
    // TFoldersIteratorRecursiveByLevels + TFileNameIteratorInFolderByMaskList('*.sqlitedb'); same
    // structure and ordering as the filesystem enumerator in file_system_tile_storage.cpp.
    bool nextFile(QString& rel) {
        for (;;) {
            if (fileIndex_ < files_.size()) {
                rel = files_[fileIndex_++];
                return true;
            }
            if (!advanceFolder()) {
                return false;
            }
        }
    }

    bool advanceFolder() {
        while (!pending_.empty()) {
            const auto [folderRel, depth] = pending_.front();
            pending_.pop_front();
            QDir dir(storagePath_ + folderRel);
            if (!dir.exists()) {
                continue;
            }
            if (depth < kMaxFolderDepth) {
                const QFileInfoList subdirs = dir.entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::IgnoreCase);
                for (const QFileInfo& fi : subdirs) {
                    pending_.emplace_back(folderRel + fi.fileName() + QLatin1Char('/'), depth + 1);
                }
            }
            files_.clear();
            fileIndex_ = 0;
            const QStringList names = dir.entryList(
                {QStringLiteral("*.sqlitedb")}, QDir::Files | QDir::Hidden, QDir::Name | QDir::IgnoreCase);
            for (const QString& name : names) {
                files_.push_back(folderRel + name);
            }
            if (!files_.isEmpty()) {
                return true;
            }
        }
        return false;
    }

    // Ports the fetcher constructor (Fetcher.pas:118-153, InitObjIface :408-440) -- the part after
    // the handler exists: the version list, the column info, the first InternalOpen.
    void openFile(const QString& absPath) {
        closeFile();
        plainSelectDone_ = false;
        lease_ = pool_->lease(absPath);
        if (lease_ == nullptr) {
            return;
        }
        lease_->run([this](SqliteRegionHandler& handler) {
            columnInfo_ = handler.columnInfo();
            auto versions = handler.listVersions();
            allVersions_ = versions ? *versions : MapVersionList{}; // GetListOfVersions swallows errors
            enumVersionIndex_ = 0;
            internalOpen(handler);
        });
    }

    // Ports the destructor / InternalClose: the statement must be finalized under the file's mutex.
    void closeFile() {
        if (lease_ != nullptr) {
            lease_->run([this](SqliteRegionHandler&) { stmt_.reset(); });
            lease_.reset();
        }
        stmt_.reset();
        prepared_ = false;
        allVersions_.reset();
        lastSelectedVersion_.reset();
    }

    // Ports InternalOpen + GetSQL_SelectEntire (Fetcher.pas:335-387, :158-170).
    void internalOpen(SqliteRegionHandler& handler) {
        stmt_.reset();
        prepared_ = false;
        const QByteArray sql = selectEntire();
        if (sql.isEmpty()) {
            return;
        }
        auto prepared = handler.db()->prepare(sql);
        if (prepared) {
            stmt_.emplace(std::move(*prepared));
            prepared_ = true;
        }
    }

    [[nodiscard]] QByteArray selectEntire() {
        QByteArray where;
        if (ignoreTne_) {
            where = "s>0 and b is not null";
        }
        QByteArray sql = "SELECT x,y,s,d";
        if (pool_->useVersionFieldInDb() && columnInfo_.hasVersion) {
            sql += ",v";
            if (allVersions_ && enumVersionIndex_ < allVersions_->size()) {
                lastSelectedVersion_ = (*allVersions_)[enumVersionIndex_++];
            } else {
                return QByteArray(); // no more versions
            }
            if (!where.isEmpty()) {
                where += " AND ";
            }
            where +=
                versionFieldIsEqual(columnInfo_.versionMode, lastSelectedVersion_->storeString().toUtf8());
        } else {
            // Deviation D-44 (legacy L-92): legacy re-issues this SELECT for ever; here once per file.
            if (plainSelectDone_) {
                return QByteArray();
            }
            plainSelectDone_ = true;
        }
        if (columnInfo_.hasContentType) {
            sql += ",c";
        }
        sql += ",b FROM t";
        if (!where.isEmpty()) {
            sql += " WHERE " + where;
        }
        return sql;
    }

    // Ports TTileStorageSQLiteFetcherComplex.Fetch (:390-406) and the base Fetch (:154-157):
    // one step under the file's mutex. The re-open loop ends because selectEntire() returns an
    // empty string once the file's SELECTs are used up (D-44); it never spins under the mutex.
    bool fetch(TileInfo& out) {
        bool result = false;
        lease_->run([&](SqliteRegionHandler& handler) {
            if (prepared_ && stepRow(out)) {
                result = true;
                return;
            }
            for (;;) {
                internalOpen(handler);
                if (!prepared_) {
                    return;
                }
                if (handler.isOpened() && stepRow(out)) {
                    result = true;
                    return;
                }
            }
        });
        return result;
    }

    bool stepRow(TileInfo& out) {
        if (!stmt_.has_value()) {
            return false;
        }
        auto step = stmt_->step();
        if (!step || *step != SqliteStmt::StepResult::Row) {
            return false;
        }
        fillRow(*stmt_, out);
        return true;
    }

    // Ports CallbackEnum (:187-327). Column positions follow columnInfo_ (legacy's FTBColInfoPtr),
    // not the SELECT actually issued -- when `v` is not selected (UseVersionFieldInDB off) the
    // indices are off by one and legacy reads past the row; sqlite returns NULL/0 for those.
    void fillRow(const SqliteStmt& stmt, TileInfo& out) {
        const SqliteRegionSchema& schema = *pool_->schema();
        out = TileInfo{};
        tile_ = geo::PointI(stmt.columnInt(0), stmt.columnInt(1));
        const int origSize = stmt.columnInt(2);
        out.loadDate = fromLegacyUnixSeconds(stmt.columnInt64(3));

        QString versionStr;
        if (columnInfo_.hasVersion) {
            if (columnInfo_.versionMode != VersionColMode::Text && stmt.isInteger(4)) {
                const qint64 value = stmt.columnInt64(4);
                versionStr = value == 0 ? QString() : QString::number(value);
            } else {
                versionStr = stmt.columnText(4); // NULL reads as the empty string
            }
        }
        if (lastSelectedVersion_ && asciiSameText(lastSelectedVersion_->storeString(), versionStr)) {
            out.version = lastSelectedVersion_;
        } else {
            out.version = schema.versionInfo(versionStr);
        }

        if (origSize <= 0) {
            out.type = TileInfoType::TneExists;
            out.size = 0;
            return;
        }
        out.type = TileInfoType::Exists;

        const int hasV = columnInfo_.hasVersion ? 1 : 0;
        QByteArray contentTypeDb;
        if (columnInfo_.hasContentType) {
            contentTypeDb = stmt.columnUtf8(4 + hasV);
        }
        out.contentType = schema.contentTypeFromDb(contentTypeDb);
        out.size = static_cast<quint32>(origSize);

        const int blobCol = 4 + hasV + (columnInfo_.hasContentType ? 1 : 0);
        QByteArray blob = stmt.columnBlob(blobCol);
        if (blob.isEmpty()) {
            out.type = TileInfoType::TneExists; // "if no BLOB - treat as TNE"
        } else {
            out.data = std::move(blob);
        }
    }

    std::shared_ptr<SqliteRegionPool> pool_;
    QString storagePath_;
    ITileNameCodecPtr codec_;
    bool ignoreTne_;

    State state_ = State::Start;
    std::deque<std::pair<QString, int>> pending_;
    QStringList files_;
    qsizetype fileIndex_ = 0;

    quint8 zoom_ = 0;
    geo::PointI tile_;
    std::unique_ptr<SqliteRegionPool::Lease> lease_;
    SqliteRegionHandler::ColumnInfo columnInfo_;
    MapVersionList allVersions_;
    std::size_t enumVersionIndex_ = 0;
    MapVersionInfoPtr lastSelectedVersion_;
    std::optional<SqliteStmt> stmt_;
    bool prepared_ = false;
    bool plainSelectDone_ = false; // non-versioned branch: its single SELECT was issued
};

} // namespace

EnumTileInfoPtr makeSqliteRegionScan(std::shared_ptr<SqliteRegionPool> pool, QString storagePath,
                                     ITileNameCodecPtr codec, bool ignoreTne) {
    return std::make_unique<SqliteRegionScanEnumerator>(std::move(pool), std::move(storagePath),
                                                        std::move(codec), ignoreTne);
}

} // namespace cg::storage::sqlite
