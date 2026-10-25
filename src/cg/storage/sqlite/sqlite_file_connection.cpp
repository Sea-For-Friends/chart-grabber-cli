// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_file_connection.h"

#include "cg/core/content_detector.h"
#include "cg/core/log.h"
#include "cg/core/platform/file_times.h"

#include <QDir>
#include <QFileInfo>
#include <QLatin1StringView>

#include <array>
#include <tuple>
#include <utility>
#include <vector>

namespace cg::storage::sqlite {

namespace {

// Ports the per-format default extension table of PrepareFileName
// (u_TileStorageSQLiteFileConnectionBuilder.pas:184-195).
[[nodiscard]] QLatin1StringView defaultExtensionFor(SqliteFileFormat format) {
    switch (format) {
    case SqliteFileFormat::MbTiles:
        return QLatin1StringView(".mbtiles");
    case SqliteFileFormat::OsmAnd:
    case SqliteFileFormat::Locus:
    case SqliteFileFormat::RMaps:
        return QLatin1StringView(".sqlitedb");
    case SqliteFileFormat::OruxMaps:
        return QLatin1StringView(".db");
    }
    Q_UNREACHABLE();
}

// Ports TTileRectInfoShort.TileInRectToIndex (tile_rect_info.h's own doc comment): row-major,
// (x - left) + (y - top) * width; -1 when (x, y) is outside `rect`.
[[nodiscard]] qint64 indexInRect(const geo::RectI& rect, int x, int y) {
    if (x < rect.left || x >= rect.right || y < rect.top || y >= rect.bottom) {
        return -1;
    }
    return (x - rect.left) + static_cast<qint64>(y - rect.top) * rect.width();
}

} // namespace

// --- prepareFileName -----------------------------------------------------------

QString prepareFileName(const QString& path, SqliteFileFormat format) {
    QString result = path;
    if (result.isEmpty()) {
        return result;
    }

    // "if Result[High(Result)] = PathDelim then SetLength(Result, Length(Result) - 1)"
    // (u_TileStorageSQLiteFileConnectionBuilder.pas:210-212): strip exactly one trailing
    // separator, PathDelim == '/' on this platform.
    if (result.endsWith(QLatin1Char('/'))) {
        result.chop(1);
    }

    // "if ExtractFileExt(Result) = '' then ..." (:214): ExtractFileExt looks for a '.' after the
    // last path delimiter only.
    const qsizetype lastSlash = result.lastIndexOf(QLatin1Char('/'));
    const QString lastComponent = result.mid(lastSlash + 1);
    const bool hasExtension = lastComponent.lastIndexOf(QLatin1Char('.')) >= 0;

    if (!hasExtension) {
        // "input is a path: create a file inside it with the same name as its last directory"
        // (:215-221). lastComponent IS ExtractFileName(Result) here (no dot to strip).
        if (!lastComponent.isEmpty()) {
            result += QLatin1Char('/') + lastComponent + defaultExtensionFor(format);
        } else {
            result += defaultExtensionFor(format);
        }
    }
    // else: "input is a file: nothing to do anymore" (:222-224).

    return result;
}

// --- SqliteFileInfo --------------------------------------------------------------

SqliteFileInfo::SqliteFileInfo(QString fileName, TileDate fileDate)
    : fileName_(std::move(fileName)), fileDate_(std::move(fileDate)) {}

void SqliteFileInfo::addOrSetMetadataValue(const QString& key, const QString& value) {
    metadata_.insert(key, value);
}

std::optional<QString> SqliteFileInfo::metadataValue(const QString& key) const {
    const auto it = metadata_.constFind(key);
    if (it == metadata_.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

// --- ConnectionStatement -----------------------------------------------------------

core::Result<bool> ConnectionStatement::ensurePrepared(sqlite::SqliteDb& db) {
    if (state_ == State::Prepared) {
        return true;
    }
    if (state_ == State::Error) {
        // Cached failure: ports CheckPrepared's `Result := (FState = ssPrepared)` fallthrough
        // (:642) for a statement whose ONE prepare attempt already failed -- no raise this time.
        return false;
    }

    auto prepared = db.prepare(QByteArrayView(sqlText()));
    if (!prepared) {
        // Fresh failure: ports `ASQLite3DB.RaiseSQLite3Error` (:639), uncaught in this unit.
        state_ = State::Error;
        return core::Unexpected(prepared.error());
    }
    stmt_ = std::make_unique<sqlite::SqliteStmt>(std::move(*prepared));
    state_ = State::Prepared;
    return true;
}

QByteArray ConnectionStatement::blobColumn(int index) const {
    if (!stmt_ || stmt_->isNull(index)) {
        return {};
    }
    return stmt_->columnBlob(index);
}

// --- SqliteFileConnection ------------------------------------------------------

SqliteFileConnection::SqliteFileConnection(bool isReadOnly, QString fileName,
                                           core::ContentTypeInfo mainContentType,
                                           const core::ContentTypeRegistry& contentTypeManager,
                                           geo::ProjectionSetPtr projectionSet)
    : isReadOnly_(isReadOnly), fileName_(std::move(fileName)), mainContentType_(std::move(mainContentType)),
      contentTypeManager_(&contentTypeManager), projectionSet_(std::move(projectionSet)) {}

core::Result<void> SqliteFileConnection::initialize(SqliteFileInfoPtr existingInfo) {
    bool needCreateTables = false;

    if (existingInfo == nullptr) {
        // "this is our first attempt to open db" (:203-214): only the first connection to a file
        // checks it exists / creates its parent directory.
        if (!QFileInfo::exists(fileName_)) {
            if (isReadOnly_) {
                return core::fail(core::ErrorCode::IoError,
                                  QStringLiteral("File not found: \"%1\"").arg(fileName_));
            }
            needCreateTables = true;
            const QString dirPath = QFileInfo(fileName_).absolutePath();
            if (!QDir(dirPath).exists() && !QDir().mkpath(dirPath)) {
                return core::fail(core::ErrorCode::IoError,
                                  QStringLiteral("cannot create directory %1").arg(dirPath));
            }
        }
    }

    QString openPath = fileName_;
    sqlite::SqliteDb::OpenMode mode = sqlite::SqliteDb::OpenMode::ReadWriteCreate;
    if (isReadOnly_) {
        mode = sqlite::SqliteDb::OpenMode::ReadOnly;
        // ports ":218-222" -- 'file:///' + AFileName + '?immutable=1', verbatim (no escaping;
        // matches legacy, which does none either). legacy-defects.md L-39: a literal '#' or '?' in
        // the path truncates it (and '?' also drops "immutable"), and '%HH' is percent-decoded --
        // reproduced verbatim, not sanitised.
        openPath = QStringLiteral("file:///") + fileName_ + QStringLiteral("?immutable=1");
    }

    auto opened = sqlite::SqliteDb::open(openPath, mode);
    if (!opened) {
        return core::Unexpected(opened.error());
    }
    db_ = std::make_unique<sqlite::SqliteDb>(std::move(*opened));

    if (!isReadOnly_) {
        // ports ":225-227", same three PRAGMAs, same order.
        static constexpr std::array<QByteArrayView, 3> kPragmas = {
            QByteArrayView("PRAGMA encoding = \"UTF-8\""),
            QByteArrayView("PRAGMA synchronous = NORMAL"),
            QByteArrayView("PRAGMA journal_mode = WAL"),
        };
        for (const QByteArrayView pragma : kPragmas) {
            if (auto r = db_->exec(pragma); !r) {
                return core::Unexpected(r.error());
            }
        }
    }

    if (existingInfo == nullptr) {
        // ports ":232-240". Legacy reads TFile.GetCreationTime; POSIX has no creation time and
        // this project already has one approved substitute for it (ADR-0005, docs/specs/storage.md
        // §8 D-6's neighbouring row; core::platform::file_times.h's own doc comment: "no POSIX
        // analogue of the ftCreationTime fallback (mtime-only, already an approved ADR-0005 row)").
        auto mtimeSecs = core::platform::fileMTime(fileName_);
        if (!mtimeSecs) {
            return core::Unexpected(mtimeSecs.error());
        }
        fileDate_ = fromPosixSeconds(*mtimeSecs);
        fileInfo_ = std::make_shared<SqliteFileInfo>(fileName_, fileDate_);

        if (needCreateTables) {
            if (auto r = createTables(); !r) {
                return r;
            }
        }
        if (auto r = fetchMetadata(); !r) {
            return r;
        }
    } else {
        fileInfo_ = std::move(existingInfo);
        fileDate_ = fileInfo_->fileDate();
    }

    // Ports `FEnabled := FTileDataStmt.CheckPrepared(FSQLite3DB) and
    // FTileInfoStmt.CheckPrepared(FSQLite3DB)` (MBTiles :226-228, same shape in RMaps :283-285 and
    // OruxMaps :191-193) -- unconditional, on every connection, not only the first one to a file.
    // Short-circuit `and`: TileInfoStmt is never touched once TileDataStmt comes back cached-false
    // (F3, legacy-defects L-40). A fresh failure of either raises out of the flavour's constructor
    // in legacy (uncaught, all the way out through Pool.Acquire -- F1's "Create reached through
    // Acquire"); ensurePrepared's error Result reproduces that by failing initialize() itself.
    if (tileDataStmt_ == nullptr || tileInfoStmt_ == nullptr) {
        enabled_ = false;
        return {};
    }
    auto dataPrepared = tileDataStmt_->ensurePrepared(*db_);
    if (!dataPrepared) {
        return core::Unexpected(dataPrepared.error());
    }
    bool bothPrepared = *dataPrepared;
    if (bothPrepared) {
        auto infoPrepared = tileInfoStmt_->ensurePrepared(*db_);
        if (!infoPrepared) {
            return core::Unexpected(infoPrepared.error());
        }
        bothPrepared = *infoPrepared;
    }
    enabled_ = bothPrepared;

    return {};
}

QByteArray SqliteFileConnection::tileContentType(const QByteArray& data) const {
    // Ports GetTileContentTypeInfo (:264-280), wired in by T-0022C2: sniff the tile's magic bytes
    // (cg::core::detectContentType, moved from cg::net to cg::core by T-0033), map a match
    // through contentTypeManager_ (nullptr from the registry counts as "unknown", same as legacy's
    // FContentTypeManager.GetInfo returning nil), and fall back to the connection's main content
    // type when the data is empty/null, nothing is sniffed, or the sniffed type is unknown to the
    // registry -- matches legacy's `if Result = nil then Result := FMainContentType` exactly.
    if (!data.isEmpty()) {
        const QByteArray detected = core::detectContentType(data);
        if (!detected.isEmpty()) {
            if (const core::ContentTypeInfo* info = contentTypeManager_->infoByType(detected);
                info != nullptr) {
                return info->type;
            }
        }
    }
    return mainContentType_.type;
}

core::Result<TileInfo> SqliteFileConnection::fetchOne(geo::PointI xy, quint8 zoom, LoadMode mode) {
    TileInfo notFound;
    notFound.type = TileInfoType::NotExists;

    if (!enabled_) {
        return notFound;
    }

    const bool withData = mode == LoadMode::WithData;
    ConnectionStatement* base = withData ? static_cast<ConnectionStatement*>(tileDataStmt_.get())
                                         : static_cast<ConnectionStatement*>(tileInfoStmt_.get());
    if (base == nullptr) {
        return notFound;
    }
    // Legacy asserts FTileDataStmt/FTileInfoStmt already prepared here (:301, :305) instead of
    // calling CheckPrepared -- guaranteed by initialize()'s F3 step whenever enabled_ is true.

    core::Result<void> bound =
        withData ? tileDataStmt_->bindParams(xy, zoom) : tileInfoStmt_->bindParams(xy, zoom);
    if (!bound) {
        // Ports ":310-312": `FSQLite3DB.RaiseSQLite3Error`, uncaught in this unit (F1).
        return core::Unexpected(bound.error());
    }

    sqlite::SqliteStmt& stmt = base->stmt();
    auto step = stmt.step();
    if (!step) {
        qCWarning(core::logStorage) << "SqliteFileConnection::fetchOne: step failed:" << step.error().what();
        std::ignore = stmt.reset();
        return notFound;
    }
    if (*step != sqlite::SqliteStmt::StepResult::Row) {
        std::ignore = stmt.reset();
        return notFound;
    }

    TileInfo info;
    info.loadDate = fileDate_;
    if (withData) {
        const QByteArray blob = tileDataStmt_->result();
        info.data = blob;
        info.size = static_cast<quint32>(blob.size());
        info.contentType = tileContentType(blob);
        info.type = TileInfoType::Exists;
    } else {
        info.size = tileInfoStmt_->result();
        info.contentType = mainContentType_.type;
        info.type = TileInfoType::Exists;
    }
    std::ignore = stmt.reset();
    return info;
}

core::Result<TileRectInfoPtr> SqliteFileConnection::fetchRectInfo(geo::RectI rect, quint8 zoom,
                                                                  const std::stop_token& stop) {
    if (!enabled_ || stop.stop_requested()) {
        return TileRectInfoPtr{}; // ports ":349-351" -- FEnabled or the upfront cancel check
    }
    if (rectInfoStmt_ == nullptr) {
        return TileRectInfoPtr{};
    }
    auto prep = rectInfoStmt_->ensurePrepared(*db_);
    if (!prep) {
        // Fresh failure: ports ":353" -> CheckPrepared's raise (:639), uncaught (F1).
        return core::Unexpected(prep.error());
    }
    if (!*prep) {
        // Cached failure: ports ":353-356" -- `FEnabled := False; Exit` (legacy-defects L-40).
        enabled_ = false;
        return TileRectInfoPtr{};
    }
    if (auto bound = rectInfoStmt_->bindParams(rect, zoom); !bound) {
        // Ports ":358-360": `FSQLite3DB.RaiseSQLite3Error`, uncaught in this unit (F1).
        return core::Unexpected(bound.error());
    }

    const qint64 width = rect.width();
    const qint64 height = rect.height();
    std::vector<TileRectInfoEntry> items(
        static_cast<std::size_t>(width > 0 && height > 0 ? width * height : 0));

    sqlite::SqliteStmt& stmt = rectInfoStmt_->stmt();
    qint64 count = 0;
    for (;;) {
        auto step = stmt.step();
        if (!step || *step != sqlite::SqliteStmt::StepResult::Row) {
            // Ports the loop condition itself: `while StepPrepared = SQLITE_ROW` (:368) ends on
            // DONE or on any step error alike -- legacy never distinguishes them here. Falls
            // through to the (possibly partial) result below instead of failing the whole call
            // (legacy-defects L-41, F4).
            if (!step) {
                qCWarning(core::logStorage)
                    << "SqliteFileConnection::fetchRectInfo: step failed:" << step.error().what();
            }
            break;
        }

        ++count;
        if (count % 1024 == 0 && stop.stop_requested()) { // ports ":371" (same 1024 constant)
            std::ignore = stmt.reset();
            return TileRectInfoPtr{};
        }

        int x = 0;
        int y = 0;
        quint32 size = 0;
        rectInfoStmt_->result(x, y, size);

        const qint64 index = indexInRect(rect, x, y);
        Q_ASSERT(index >= 0);
        if (index >= 0 && static_cast<std::size_t>(index) < items.size()) {
            items[static_cast<std::size_t>(index)] = TileRectInfoEntry{fileDate_, size, TileInfoType::Exists};
        }
    }
    std::ignore = stmt.reset();

    if (stop.stop_requested()) { // ports ":390-392", the post-loop check
        return TileRectInfoPtr{};
    }

    return std::make_shared<const TileRectInfo>(rect, zoom, std::move(items));
}

core::Result<bool> SqliteFileConnection::fetchNext(TileInfo& out, geo::PointI& xy, quint8& zoom) {
    if (!enabled_ || enumTilesStmt_ == nullptr) {
        return false;
    }

    if (fetchNextState_ == FetchNextState::None) {
        auto prep = enumTilesStmt_->ensurePrepared(*db_);
        if (!prep) {
            // Fresh failure: ports ":413" -> CheckPrepared's raise (:639), uncaught (F1). Legacy's
            // raise happens before the `FFetchNextState := fnsError` line, so the state stays
            // fnsNone here too -- the next call re-enters this branch and gets the cached result.
            return core::Unexpected(prep.error());
        }
        if (!*prep) {
            // Cached failure: ports ":413-419" (legacy-defects L-40).
            fetchNextState_ = FetchNextState::Error;
            enabled_ = false;
            return false;
        }
        fetchNextState_ = FetchNextState::Next;
    }
    if (fetchNextState_ != FetchNextState::Next) {
        return false;
    }

    sqlite::SqliteStmt& stmt = enumTilesStmt_->stmt();
    auto step = stmt.step();
    if (!step) {
        qCWarning(core::logStorage) << "SqliteFileConnection::fetchNext: step failed:" << step.error().what();
        fetchNextState_ = FetchNextState::Error;
        std::ignore = stmt.reset();
        return false;
    }
    if (*step == sqlite::SqliteStmt::StepResult::Done) {
        fetchNextState_ = FetchNextState::Done;
        std::ignore = stmt.reset();
        return false;
    }

    int x = 0;
    int y = 0;
    quint8 z = 0;
    QByteArray data;
    enumTilesStmt_->result(x, y, z, data);

    xy = geo::PointI(x, y);
    zoom = z;

    out = TileInfo{};
    out.loadDate = fileDate_;
    out.contentType = tileContentType(data);
    if (!data.isEmpty()) {
        out.data = data;
        out.size = static_cast<quint32>(data.size());
        out.type = TileInfoType::Exists;
    } else {
        out.size = 0;
        out.type = TileInfoType::NotExists;
    }

    // Deliberately no stmt.reset() here: the statement stays open across calls while
    // fetchNextState_ == Next, exactly like legacy's `finally if FFetchNextState <> fnsNext then
    // VStmt.Reset` (:455-458) -- it is only reset once, on the terminal Done/Error transition
    // above.
    return true;
}

core::Result<bool> SqliteFileConnection::tileExistsForWrite(geo::PointI xy, quint8 zoom) {
    // Legacy asserts FTileInfoStmt already prepared here (:474) -- guaranteed by initialize()'s F3
    // step, same as fetchOne().
    if (auto bound = tileInfoStmt_->bindParams(xy, zoom); !bound) {
        // Ports ":476-478": `FSQLite3DB.RaiseSQLite3Error`. Uncaught here, but the ONE caller
        // (insertOrUpdate) invokes this from inside Insert's own try, so it is caught there.
        return core::Unexpected(bound.error());
    }
    sqlite::SqliteStmt& stmt = tileInfoStmt_->stmt();
    auto step = stmt.step();
    std::ignore = stmt.reset();
    if (!step) {
        // Ports ":482": `Result := StepPrepared(VStmt) = SQLITE_ROW` -- a non-ROW step outcome,
        // error or not, just makes the comparison False; StepPrepared's return value is never
        // itself raised here.
        return false;
    }
    return *step == sqlite::SqliteStmt::StepResult::Row;
}

core::Result<bool> SqliteFileConnection::insertOrUpdate(geo::PointI xy, quint8 zoom, const QByteArray& data,
                                                        bool overwrite) {
    if (!enabled_ || insertStmt_ == nullptr || updateStmt_ == nullptr || tileInfoStmt_ == nullptr) {
        return false; // ports ":537-539"
    }

    // Ports ":541", OUTSIDE legacy's try/except: a BEGIN failure raises all the way out of Insert,
    // uncaught (F1) -- nothing to roll back, since no transaction was ever opened.
    if (auto begin = db_->exec(QByteArrayView("BEGIN")); !begin) {
        return core::Unexpected(begin.error());
    }
    // Ports the except handler (BASE:559-561): `Result := False; FSQLite3DB.RollbackTransaction`.
    // legacy sets Result False FIRST, so if RollbackTransaction itself then raises -- reachable
    // when SQLite already auto-rolled back the whole transaction (FULL/IOERR/NOMEM on the write
    // step, then a fall-through COMMIT also fails, H:535-543) -- that new exception replaces the
    // assignment and escapes Insert uncaught (review finding 3, legacy-defects.md L-46). Same
    // convention as the BEGIN failure above (:479-481): propagate as an error Result instead of
    // swallowing it to False; every call site below is a use of this one shared except handler,
    // exactly as legacy shares the one except block across every raise path in the try.
    const auto rollbackAndFail = [this]() -> core::Result<bool> {
        if (auto r = db_->exec(QByteArrayView("ROLLBACK")); !r) {
            return core::Unexpected(r.error());
        }
        return false;
    };
    // `FSQLite3DB.CommitTransaction` (:551, :561) is the unconditional LAST statement inside
    // legacy's try, for every branch that reaches it (including the two N1 branches below) -- so a
    // COMMIT failure itself is still caught by the except and rolls back, exactly like every other
    // in-try failure.
    const auto commit = [this]() -> bool { return db_->exec(QByteArrayView("COMMIT")).has_value(); };

    // Everything from here to CommitTransaction sits inside legacy's try/except (:542-562): a
    // RAISING failure (BindParams, or a statement's FIRST failed prepare) is caught, rolls back,
    // and Insert returns False. A write statement that fails WITHOUT raising -- a CACHED prepare
    // failure, or `StepPrepared <> SQLITE_DONE` for any reason, error or not -- instead falls
    // through to legacy's own unconditional CommitTransaction call, unchanged, and Insert still
    // returns False (T-0022C1-review.md confirmation review, note N1). Reproduced at both the
    // update and insert call sites below, not rolled back.
    auto exists = tileExistsForWrite(xy, zoom);
    if (!exists) {
        return rollbackAndFail();
    }

    if (*exists) {
        if (!overwrite) {
            // Ports the "// Ignore" Exit (:547-550): no COMMIT, no ROLLBACK -- the transaction
            // stays open on this connection until its next statement. legacy-defects.md L-38,
            // reproduced verbatim (Rule 3: data-destroying but well-defined, not the Rule 2
            // crash/UB exception -- see docs/handoffs/T-0022C1-review.md's "open transaction on
            // Insert-ignore" recommendation).
            return false;
        }

        auto prep = updateStmt_->ensurePrepared(*db_);
        if (!prep) {
            // Fresh failure: ports ":517" -> CheckPrepared's raise, caught by Insert's except
            // (enabled_ untouched -- the raise skips `FEnabled := False` inside UpdateTile too).
            return rollbackAndFail();
        }
        if (!*prep) {
            // Cached failure: ports ":517-520" -- `FEnabled := False; Exit` (no raise): UpdateTile
            // returns False and Insert falls through to CommitTransaction (N1), not the except.
            // legacy-defects L-40.
            enabled_ = false;
            if (!commit()) {
                return rollbackAndFail();
            }
            return false;
        }
        if (auto bound = updateStmt_->bindParams(xy, zoom, data); !bound) {
            // BindParams failure: caught by Insert's except -> ROLLBACK (same shape as :476-478).
            return rollbackAndFail();
        }
        sqlite::SqliteStmt& updateRaw = updateStmt_->stmt();
        auto updateStep = updateRaw.step();
        std::ignore = updateRaw.reset();
        if (!updateStep) {
            qCWarning(core::logStorage)
                << "SqliteFileConnection::insertOrUpdate: update step failed:" << updateStep.error().what();
        }
        if (!updateStep || *updateStep != sqlite::SqliteStmt::StepResult::Done) {
            // `StepPrepared <> SQLITE_DONE` (:562-equivalent), error or not: UpdateTile returns
            // False without raising either way (N1) -> falls through to CommitTransaction.
            if (!commit()) {
                return rollbackAndFail();
            }
            return false;
        }
        if (!commit()) {
            return rollbackAndFail();
        }
        return true;
    }

    auto prep = insertStmt_->ensurePrepared(*db_);
    if (!prep) {
        // Fresh failure: ports ":494" -> CheckPrepared's raise, caught by Insert's except.
        return rollbackAndFail();
    }
    if (!*prep) {
        // Cached failure: ports ":494-497" (N1, same shape as the update branch above).
        enabled_ = false;
        if (!commit()) {
            return rollbackAndFail();
        }
        return false;
    }
    if (auto bound = insertStmt_->bindParams(xy, zoom, data); !bound) {
        // BindParams failure: caught by Insert's except -> ROLLBACK.
        return rollbackAndFail();
    }
    sqlite::SqliteStmt& insertRaw = insertStmt_->stmt();
    auto insertStep = insertRaw.step();
    std::ignore = insertRaw.reset();
    if (!insertStep) {
        qCWarning(core::logStorage) << "SqliteFileConnection::insertOrUpdate: insert step failed:"
                                    << insertStep.error().what();
    }
    const bool insertDone = insertStep && *insertStep == sqlite::SqliteStmt::StepResult::Done;
    if (insertDone) {
        // Ports "if Result then Self.UpdateMetadata(AXY, AZoom)" (:555). A failure here raises in
        // both flavours (MBTiles' invalid zoom, RMaps' RaiseSQLite3Error), caught by the same
        // except -- Insert rolls back and returns False, discarding the tile just inserted (F2).
        auto meta = updateMetadata(xy, zoom);
        if (!meta) {
            return rollbackAndFail();
        }
    }
    // `StepPrepared <> SQLITE_DONE` falls through here too (N1): no rollback, straight to commit.
    if (!commit()) {
        return rollbackAndFail();
    }
    return insertDone;
}

core::Result<bool> SqliteFileConnection::deleteTile(geo::PointI xy, quint8 zoom) {
    if (!enabled_ || deleteStmt_ == nullptr) {
        return false;
    }
    auto prep = deleteStmt_->ensurePrepared(*db_);
    if (!prep) {
        // Fresh failure: ports ":578" -> CheckPrepared's raise (:639), uncaught (F1).
        return core::Unexpected(prep.error());
    }
    if (!*prep) {
        // Cached failure: ports ":578-581" (legacy-defects L-40).
        enabled_ = false;
        return false;
    }
    if (auto bound = deleteStmt_->bindParams(xy, zoom); !bound) {
        // Ports ":583-585": `FSQLite3DB.RaiseSQLite3Error`, uncaught in this unit (F1).
        return core::Unexpected(bound.error());
    }

    sqlite::SqliteStmt& stmt = deleteStmt_->stmt();
    auto step = stmt.step();
    std::ignore = stmt.reset();
    if (!step) {
        qCWarning(core::logStorage) << "SqliteFileConnection::deleteTile: step failed:"
                                    << step.error().what();
        return false;
    }
    return *step == sqlite::SqliteStmt::StepResult::Done;
}

} // namespace cg::storage::sqlite
