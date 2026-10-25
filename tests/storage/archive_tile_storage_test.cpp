// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-027. Ports u_TileStorageArchive.pas / u_TileStorageTypeArchive.pas;
// docs/specs/storage.md §2.19/§3.5; docs/backlog/T-0024-storage-archive.md.
//
// Test-count ruling (tests-port-dont-multiply): legacy has no DUnit suite for this class, so this
// is the ticket's own minimal list -- write a tar and a zip with two tiles and one TNE, scan each
// back (names through one folder codec, the zero-byte .tne entry reported as TNE, mtime
// preserved), the OsmAnd codec's D-7 fix, a duplicate tile (no dedup, matching legacy), and the
// "nothing" methods on both a write-only and a scan-only instance. The exclusivity assert
// (§3.5) is deliberately left untested (ticket: "unless the project already uses death tests" --
// it does not).
//
// Rework (review findings, one test per finding at most): ScanAcceptsLegacyBackslashSeparatedEntryNames
// (F1), EntryMtimeUsesLegacyUnixSecondsBothDirectionsAndPre1970Clamp (F3, extended with a pre-1970
// vector for F9), ZipEntryPermissionsAreReadable (F4).
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/archive_tile_storage.h"
#include "cg/storage/codecs/osmand_tile_name_codec.h"
#include "cg/storage/codecs/sas_tile_name_codec.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTime>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "storage_state_changeable_check.h"

using cg::core::ErrorCode;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::storage::ArchiveTileStorage;
using cg::storage::EnumTileInfoPtr;
using cg::storage::fromLegacyUnixSeconds;
using cg::storage::fromPosixSeconds;
using cg::storage::MapVersionFactory;
using cg::storage::OsmandTileNameCodec;
using cg::storage::SasTileNameCodec;
using cg::storage::TileDate;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::TilePyramidNotifier;
using cg::storage::toLegacyUnixSeconds;

namespace {

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256);
}

std::shared_ptr<ArchiveTileStorage> openWrite(const QString& file, ArchiveTileStorage::Container container,
                                              int zipLevel, cg::storage::ITileNameCodecPtr codec) {
    auto opened = ArchiveTileStorage::openForWrite(
        file, container, zipLevel, std::move(codec),
        cg::core::ContentTypeInfo{"image/jpg", ".jpg", cg::core::ContentKind::Bitmap}, makeProjectionSet(),
        std::make_shared<MapVersionFactory>(), std::make_shared<TilePyramidNotifier>());
    EXPECT_TRUE(opened.has_value());
    return opened.has_value() ? *opened : nullptr;
}

std::shared_ptr<ArchiveTileStorage> openScan(const QString& file, cg::storage::ITileNameCodecPtr codec) {
    auto opened = ArchiveTileStorage::openForScan(file, std::move(codec), makeProjectionSet(),
                                                  std::make_shared<MapVersionFactory>(),
                                                  std::make_shared<TilePyramidNotifier>());
    EXPECT_TRUE(opened.has_value());
    return opened.has_value() ? *opened : nullptr;
}

// Writes two tiles ("11-22" jpg, "33-44" jpg) and one TNE ("55-66") through `writer`.
void writeSample(ArchiveTileStorage& writer, TileDate loadDate) {
    auto s1 = writer.saveTile(PointI{2, 2}, 1, nullptr, loadDate, QByteArrayView("image/jpg"),
                              QByteArray("tile-a-bytes"), false);
    ASSERT_TRUE(s1.has_value());
    EXPECT_TRUE(*s1);
    auto s2 = writer.saveTile(PointI{5, 9}, 3, nullptr, loadDate, QByteArrayView("image/jpg"),
                              QByteArray("tile-b-bytes"), false);
    ASSERT_TRUE(s2.has_value());
    EXPECT_TRUE(*s2);
    // Empty contentType + null data => TNE (ITileStorage::saveTile contract).
    auto s3 = writer.saveTile(PointI{7, 1}, 4, nullptr, loadDate, QByteArrayView(), QByteArray(), false);
    ASSERT_TRUE(s3.has_value());
    EXPECT_TRUE(*s3);
}

struct ScannedTiles {
    std::vector<TileInfo> infos;
    std::vector<PointI> tiles;
    std::vector<quint8> zooms;
};

ScannedTiles drain(EnumTileInfoPtr enumerator) {
    ScannedTiles result;
    TileInfo info;
    PointI xy;
    quint8 zoom = 0;
    while (enumerator->next(info, xy, zoom)) {
        result.infos.push_back(info);
        result.tiles.push_back(xy);
        result.zooms.push_back(zoom);
    }
    return result;
}

// --- F1 fixture: a from-scratch, minimal POSIX ustar writer -----------------------------------
// ArchiveTileStorage never writes '\' entry names (F2 ruling), so the F1 regression needs a tar
// built independently of it -- a legacy-Windows-style tar with '\'-separated entry names. This is
// a formula-defined layout (the plain POSIX ustar header, unrelated to libarchive or to this
// module's own writer), generated by a script independent of the implementation under test
// (tests-are-real-fixtures ruling 2026-09-18 covers exactly this case).
void ustarPutField(char* header, int offset, int len, const QByteArray& value) {
    std::memset(header + offset, 0, static_cast<std::size_t>(len));
    std::memcpy(header + offset, value.constData(),
                static_cast<std::size_t>(std::min<int>(len, static_cast<int>(value.size()))));
}

void ustarPutOctal(char* header, int offset, int len, quint64 value) {
    // `len` includes the trailing NUL: len-1 octal digits, left-padded with '0'.
    QByteArray digits = QByteArray::number(value, 8);
    while (digits.size() < len - 1) {
        digits.prepend('0');
    }
    ustarPutField(header, offset, len - 1, digits);
    header[offset + len - 1] = '\0';
}

QByteArray ustarHeader(const QByteArray& name, quint64 size, qint64 mtime) {
    QByteArray header(512, '\0');
    char* h = header.data();
    ustarPutField(h, 0, 100, name);                         // name
    ustarPutOctal(h, 100, 8, 0644);                         // mode
    ustarPutOctal(h, 108, 8, 0);                            // uid
    ustarPutOctal(h, 116, 8, 0);                            // gid
    ustarPutOctal(h, 124, 12, size);                        // size
    ustarPutOctal(h, 136, 12, static_cast<quint64>(mtime)); // mtime
    std::memset(h + 148, ' ', 8);                           // chksum placeholder while summing
    h[156] = '0';                                           // typeflag: regular file
    ustarPutField(h, 257, 6, QByteArrayLiteral("ustar") + QByteArray(1, '\0')); // magic
    ustarPutField(h, 263, 2, QByteArrayLiteral("00"));                          // version
    quint32 sum = 0;
    for (int i = 0; i < 512; ++i) {
        sum += static_cast<unsigned char>(h[i]);
    }
    QByteArray chksum = QByteArray::number(sum, 8);
    while (chksum.size() < 6) {
        chksum.prepend('0');
    }
    ustarPutField(h, 148, 6, chksum);
    h[154] = '\0';
    h[155] = ' ';
    return header;
}

QByteArray ustarPad512(const QByteArray& data) {
    QByteArray out = data;
    const int rem = out.size() % 512;
    if (rem != 0) {
        out.append(QByteArray(512 - rem, '\0'));
    }
    return out;
}

// One entry per {name, data} pair, terminated by the standard two zero blocks.
QByteArray makeMinimalUstarTar(const std::vector<std::pair<QByteArray, QByteArray>>& entries, qint64 mtime) {
    QByteArray out;
    for (const auto& [name, data] : entries) {
        out += ustarHeader(name, static_cast<quint64>(data.size()), mtime);
        out += ustarPad512(data);
    }
    out += QByteArray(1024, '\0'); // two zero blocks = end of archive
    return out;
}

} // namespace

TEST(ArchiveTileStorage, TarWriteAndScanRoundTrip) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("cache.tar"));
    const TileDate loadDate = fromPosixSeconds(1700000000);

    {
        auto writer = openWrite(file, ArchiveTileStorage::Container::Tar, /*zipLevel=*/0,
                                std::make_shared<SasTileNameCodec>());
        ASSERT_NE(writer, nullptr);
        writeSample(*writer, loadDate);
    } // destructor closes/finalizes the tar

    auto reader = openScan(file, std::make_shared<SasTileNameCodec>());
    ASSERT_NE(reader, nullptr);
    auto scanned = reader->scanTiles(/*ignoreTne=*/false, /*ignoreMultiVersionTiles=*/false);
    ASSERT_TRUE(scanned.has_value());
    ASSERT_NE(*scanned, nullptr);

    ScannedTiles result = drain(std::move(*scanned));
    ASSERT_EQ(result.infos.size(), 3u);

    int existsCount = 0;
    int tneCount = 0;
    for (std::size_t i = 0; i < result.infos.size(); ++i) {
        const TileInfo& info = result.infos[i];
        EXPECT_EQ(info.loadDate, loadDate);
        if (info.type == TileInfoType::Exists) {
            ++existsCount;
            EXPECT_EQ(info.contentType, QByteArray("image/jpg"));
            EXPECT_TRUE(info.data == QByteArray("tile-a-bytes") || info.data == QByteArray("tile-b-bytes"));
        } else if (info.type == TileInfoType::TneExists) {
            ++tneCount;
            EXPECT_EQ(info.size, 0u);
            EXPECT_EQ(result.tiles[i], (PointI{7, 1}));
            EXPECT_EQ(result.zooms[i], 4);
        }
    }
    EXPECT_EQ(existsCount, 2);
    EXPECT_EQ(tneCount, 1);
}

// T-0335: libarchive opens the file by a wide name on Windows; a Cyrillic name with a space must round-trip.
TEST(ArchiveTileStorage, TarRoundTripOnANonAsciiFileName) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(
        QString::fromUtf8("\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD1\x8B \xD1\x82\xD0\xB5\xD1\x81\xD1\x82.tar"));
    {
        auto writer = openWrite(file, ArchiveTileStorage::Container::Tar, /*zipLevel=*/0,
                                std::make_shared<SasTileNameCodec>());
        ASSERT_NE(writer, nullptr);
        writeSample(*writer, fromPosixSeconds(1700000000));
    }
    auto reader = openScan(file, std::make_shared<SasTileNameCodec>());
    ASSERT_NE(reader, nullptr);
    auto scanned = reader->scanTiles(/*ignoreTne=*/false, /*ignoreMultiVersionTiles=*/false);
    ASSERT_TRUE(scanned.has_value());
    ASSERT_NE(*scanned, nullptr);
    EXPECT_EQ(drain(std::move(*scanned)).infos.size(), 3u);
}

TEST(ArchiveTileStorage, ZipWriteAndScanRoundTripStoreAndDeflate) {
    for (const int zipLevel : {0, 6}) {
        SCOPED_TRACE(zipLevel);
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("cache.zip"));
        const TileDate loadDate = fromPosixSeconds(1700000500);

        {
            auto writer = openWrite(file, ArchiveTileStorage::Container::Zip, zipLevel,
                                    std::make_shared<SasTileNameCodec>());
            ASSERT_NE(writer, nullptr);
            writeSample(*writer, loadDate);
        }

        auto reader = openScan(file, std::make_shared<SasTileNameCodec>());
        ASSERT_NE(reader, nullptr);
        auto scanned = reader->scanTiles(false, false);
        ASSERT_TRUE(scanned.has_value());
        ScannedTiles result = drain(std::move(*scanned));
        ASSERT_EQ(result.infos.size(), 3u);
        for (const TileInfo& info : result.infos) {
            EXPECT_EQ(info.loadDate, loadDate);
        }
    }
}

TEST(ArchiveTileStorage, OsmandCodecSingleExtensionNotDoubled) {
    // D-7: legacy's buggy double AddExt yields ".../678.jpg.jpg.tile" for the OsmAnd codec; the
    // port's single addExt() call yields ".../678.jpg.tile".
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("osmand.tar"));
    const TileDate loadDate = fromPosixSeconds(1700000000);

    {
        auto writer =
            openWrite(file, ArchiveTileStorage::Container::Tar, 0, std::make_shared<OsmandTileNameCodec>());
        ASSERT_NE(writer, nullptr);
        auto saved = writer->saveTile(PointI{678, 345}, 12, nullptr, loadDate, QByteArrayView("image/jpg"),
                                      QByteArray("bytes"), false);
        ASSERT_TRUE(saved.has_value());
        EXPECT_TRUE(*saved);
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
        QProcess proc;
        proc.start(QStringLiteral("bsdtar"), {QStringLiteral("-tf"), file});
        ASSERT_TRUE(proc.waitForFinished(5000));
        const QString listing = QString::fromUtf8(proc.readAllStandardOutput());
        EXPECT_TRUE(listing.contains(QStringLiteral("12/678/345.jpg.tile")));
        EXPECT_FALSE(listing.contains(QStringLiteral("12/678/345.jpg.jpg.tile")));
    }

    auto reader = openScan(file, std::make_shared<OsmandTileNameCodec>());
    ASSERT_NE(reader, nullptr);
    auto scanned = reader->scanTiles(false, false);
    ASSERT_TRUE(scanned.has_value());
    ScannedTiles result = drain(std::move(*scanned));
    ASSERT_EQ(result.infos.size(), 1u);
    EXPECT_EQ(result.tiles[0], (PointI{678, 345}));
    EXPECT_EQ(result.zooms[0], 12);
    EXPECT_TRUE(result.infos[0].isExists());
}

TEST(ArchiveTileStorage, DuplicateTileAppendsBothEntriesNoDedup) {
    // u_TileStorageArchive.pas:256-294 never checks for an existing entry (no overwrite logic at
    // all): saving the same tile twice appends two entries, unconditionally.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("dup.tar"));
    const TileDate loadDate = fromPosixSeconds(1700000000);

    {
        auto writer =
            openWrite(file, ArchiveTileStorage::Container::Tar, 0, std::make_shared<SasTileNameCodec>());
        ASSERT_NE(writer, nullptr);
        for (int i = 0; i < 2; ++i) {
            auto saved = writer->saveTile(PointI{1, 1}, 0, nullptr, loadDate, QByteArrayView("image/jpg"),
                                          QByteArray("bytes"), /*overwrite=*/false);
            ASSERT_TRUE(saved.has_value());
            EXPECT_TRUE(*saved);
        }
    }

    auto reader = openScan(file, std::make_shared<SasTileNameCodec>());
    ASSERT_NE(reader, nullptr);
    auto scanned = reader->scanTiles(false, false);
    ASSERT_TRUE(scanned.has_value());
    ScannedTiles result = drain(std::move(*scanned));
    EXPECT_EQ(result.infos.size(), 2u); // both entries present, no de-dup
}

TEST(ArchiveTileStorage, EntryNamesMatchSystemToolListing) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString tarFile = dir.filePath(QStringLiteral("names.tar"));
    const QString zipFile = dir.filePath(QStringLiteral("names.zip"));
    const TileDate loadDate = fromPosixSeconds(1700000000);

    for (const auto& [file, container] : {std::pair{tarFile, ArchiveTileStorage::Container::Tar},
                                          std::pair{zipFile, ArchiveTileStorage::Container::Zip}}) {
        auto writer = openWrite(file, container, 6, std::make_shared<SasTileNameCodec>());
        ASSERT_NE(writer, nullptr);
        auto saved = writer->saveTile(PointI{5, 9}, 3, nullptr, loadDate, QByteArrayView("image/jpg"),
                                      QByteArray("tile-b-bytes"), false);
        ASSERT_TRUE(saved.has_value());
    }

    const QString expectedName = std::make_shared<SasTileNameCodec>()->tilePath(PointI{5, 9}, 3) +
                                 QStringLiteral(".jpg"); // "z4/0/x5/0/y9.jpg"

    if (!QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
        QProcess proc;
        proc.start(QStringLiteral("bsdtar"), {QStringLiteral("-tf"), tarFile});
        ASSERT_TRUE(proc.waitForFinished(5000));
        EXPECT_TRUE(QString::fromUtf8(proc.readAllStandardOutput()).contains(expectedName));
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty()) {
        QProcess proc;
        proc.start(QStringLiteral("unzip"), {QStringLiteral("-l"), zipFile});
        ASSERT_TRUE(proc.waitForFinished(5000));
        EXPECT_TRUE(QString::fromUtf8(proc.readAllStandardOutput()).contains(expectedName));
    }
}

TEST(ArchiveTileStorage, WriteOnlyInstanceReadMethodsReturnNothing) {
    // u_TileStorageArchive.pas:227-332: GetTileInfo/GetTileInfoEx/DeleteTile/
    // GetListOfTileVersions/GetTileRectInfo are unconditional "nothing", regardless of mode.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("nothing.tar"));
    auto writer =
        openWrite(file, ArchiveTileStorage::Container::Tar, 0, std::make_shared<SasTileNameCodec>());
    ASSERT_NE(writer, nullptr);

    auto info = writer->tileInfo(PointI{0, 0}, 0, nullptr, cg::storage::LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::Unknown);

    cg::storage::MapVersionRequest request;
    auto infoEx = writer->tileInfoEx(PointI{0, 0}, 0, request, cg::storage::LoadMode::WithData);
    ASSERT_TRUE(infoEx.has_value());
    EXPECT_EQ(infoEx->type, TileInfoType::Unknown);

    auto deleted = writer->deleteTile(PointI{0, 0}, 0, nullptr);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_FALSE(*deleted);

    auto versions = writer->listVersions(PointI{0, 0}, 0, request);
    ASSERT_TRUE(versions.has_value());
    EXPECT_EQ(*versions, nullptr);

    auto rect = writer->tileRectInfo(cg::geo::RectI{0, 0, 1, 1}, 0, request, std::stop_token{});
    ASSERT_TRUE(rect.has_value());
    EXPECT_EQ(*rect, nullptr);

    EXPECT_EQ(writer->tileFileName(PointI{5, 9}, 3, nullptr), file + QStringLiteral("/z4/0/x5/0/y9.jpg"));
}

TEST(ArchiveTileStorage, ScanOnlyInstanceScansAnEmptyArchiveCleanly) {
    // Confirms a scan-only instance is a valid, working ITileStorage for its one supported method,
    // even over an archive with zero tile entries.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("empty.tar"));
    {
        auto writer =
            openWrite(file, ArchiveTileStorage::Container::Tar, 0, std::make_shared<SasTileNameCodec>());
        ASSERT_NE(writer, nullptr);
    }
    auto reader = openScan(file, std::make_shared<SasTileNameCodec>());
    ASSERT_NE(reader, nullptr);
    auto scanned = reader->scanTiles(false, false);
    ASSERT_TRUE(scanned.has_value());
    ScannedTiles result = drain(std::move(*scanned));
    EXPECT_TRUE(result.infos.empty());
}

TEST(ArchiveTileStorage, ScanAcceptsLegacyBackslashSeparatedEntryNames) {
    // F1 (review, major): a legacy-written tar uses PathDelim = '\' in every tile entry name
    // (u_TileFileNameOsmAnd.pas:67 and every other folder-name generator; TTarWriter.AddStream
    // never calls ConvertFilename, Includes/LibTar.pas:840-878). Before this fix the port's '/'-only
    // codec regex silently skipped every such entry. This tar cannot come from ArchiveTileStorage
    // itself (it only ever writes '/', F2 ruling), so it is hand-built via the from-scratch ustar
    // writer above -- a minimal, independently generated legacy-Windows-style fixture, mirroring
    // the review's own repro: two '\'-separated entries and one already-'/'-separated entry.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("legacy-backslash.tar"));
    const qint64 mtime = 1700000000;

    // SasTileNameCodec::tilePath: "z{zoom+1}/{x>>10}/x{x}/{y>>10}/y{y}".
    const QByteArray backslash1 = QByteArrayLiteral("z4\\0\\x5\\0\\y9.jpg"); // PointI{5,9}, zoom 3
    const QByteArray backslash2 = QByteArrayLiteral("z2\\0\\x1\\0\\y1.jpg"); // PointI{1,1}, zoom 1
    const QByteArray forwardSlash = QByteArrayLiteral("z3/0/x2/0/y2.jpg");   // PointI{2,2}, zoom 2
    const QByteArray tar = makeMinimalUstarTar({{backslash1, QByteArrayLiteral("tile-a")},
                                                {backslash2, QByteArrayLiteral("tile-b")},
                                                {forwardSlash, QByteArrayLiteral("tile-c")}},
                                               mtime);
    {
        QFile out(file);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        ASSERT_EQ(out.write(tar), tar.size());
    }

    auto reader = openScan(file, std::make_shared<SasTileNameCodec>());
    ASSERT_NE(reader, nullptr);
    auto scanned = reader->scanTiles(false, false);
    ASSERT_TRUE(scanned.has_value());
    ScannedTiles result = drain(std::move(*scanned));
    ASSERT_EQ(result.infos.size(), 3u);

    std::set<std::pair<int, int>> tiles;
    for (std::size_t i = 0; i < result.tiles.size(); ++i) {
        tiles.insert({result.tiles[i].x(), result.tiles[i].y()});
        EXPECT_EQ(result.infos[i].type, TileInfoType::Exists);
    }
    EXPECT_TRUE(tiles.contains({5, 9}));
    EXPECT_TRUE(tiles.contains({1, 1}));
    EXPECT_TRUE(tiles.contains({2, 2}));
}

TEST(ArchiveTileStorage, EntryMtimeUsesLegacyUnixSecondsBothDirectionsAndPre1970Clamp) {
    // F3 (review, major): legacy treats a tile's naive-local loadDate as UTC when writing/reading
    // BOTH the tar mtime (Includes/LibTar.pas WriteTarHeader/FindNext,
    // `Trunc((DateTime-NullDate)*86400)`) and the zip date (u_ArchiveReadWriteSequentialZip.pas
    // Add/Next, `DateTimeToUnix`/`UnixToDateTime`, Delphi's default AInputIsUTC=True) -- i.e.
    // toLegacyUnixSeconds/fromLegacyUnixSeconds (tile_date.h), not toPosixSeconds/fromPosixSeconds.
    // The port's own round trip is symmetric either way (self-consistent), so the real assertion
    // compares against the RAW stored timestamp, extracted to disk via bsdtar/unzip: a file's mtime
    // is a true POSIX instant, so QFileInfo::lastModified().toSecsSinceEpoch() reads back exactly
    // what was written, independent of this test host's own timezone.
    const TileDate loadDate(QDate(2021, 6, 15), QTime(12, 0, 0));
    const qint64 expectedRawSecs = toLegacyUnixSeconds(loadDate);
    const QString expectedName =
        std::make_shared<SasTileNameCodec>()->tilePath(PointI{5, 9}, 3) + QStringLiteral(".jpg");

    for (const auto container : {ArchiveTileStorage::Container::Tar, ArchiveTileStorage::Container::Zip}) {
        SCOPED_TRACE(container == ArchiveTileStorage::Container::Tar ? "tar" : "zip");
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString file =
            dir.filePath(container == ArchiveTileStorage::Container::Tar ? QStringLiteral("mtime.tar")
                                                                         : QStringLiteral("mtime.zip"));
        {
            auto writer = openWrite(file, container, 0, std::make_shared<SasTileNameCodec>());
            ASSERT_NE(writer, nullptr);
            auto saved = writer->saveTile(PointI{5, 9}, 3, nullptr, loadDate, QByteArrayView("image/jpg"),
                                          QByteArray("bytes"), false);
            ASSERT_TRUE(saved.has_value());
        }

        auto reader = openScan(file, std::make_shared<SasTileNameCodec>());
        ASSERT_NE(reader, nullptr);
        auto scanned = reader->scanTiles(false, false);
        ASSERT_TRUE(scanned.has_value());
        ScannedTiles result = drain(std::move(*scanned));
        ASSERT_EQ(result.infos.size(), 1u);
        EXPECT_EQ(result.infos[0].loadDate, loadDate); // symmetric either way -- not the real check

        const QString tool = container == ArchiveTileStorage::Container::Tar ? QStringLiteral("bsdtar")
                                                                             : QStringLiteral("unzip");
        if (QStandardPaths::findExecutable(tool).isEmpty()) {
            continue;
        }
        QTemporaryDir extractDir;
        ASSERT_TRUE(extractDir.isValid());
        QProcess proc;
        if (container == ArchiveTileStorage::Container::Tar) {
            proc.setWorkingDirectory(extractDir.path());
            proc.start(tool, {QStringLiteral("-xf"), file});
        } else {
            proc.start(tool, {QStringLiteral("-o"), file, QStringLiteral("-d"), extractDir.path()});
        }
        ASSERT_TRUE(proc.waitForFinished(5000));
        const QString extractedPath = extractDir.filePath(expectedName);
        ASSERT_TRUE(QFileInfo::exists(extractedPath)) << extractedPath.toStdString();
        EXPECT_EQ(QFileInfo(extractedPath).lastModified().toSecsSinceEpoch(), expectedRawSecs);
    }

    // F9 (review, minor): legacy clamps a pre-1970 tar mtime to `Trunc(NullDate*86400.0)` =
    // 2209161600 (year 2040), not the actual (negative) value (Includes/LibTar.pas:539-542). Zip's
    // `DateTimeToUnix` has no equivalent clamp -- tar only.
    {
        const TileDate pre1970(QDate(1960, 3, 1), QTime(0, 0, 0));
        ASSERT_LT(toLegacyUnixSeconds(pre1970), 0);
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("pre1970.tar"));
        {
            auto writer =
                openWrite(file, ArchiveTileStorage::Container::Tar, 0, std::make_shared<SasTileNameCodec>());
            ASSERT_NE(writer, nullptr);
            auto saved = writer->saveTile(PointI{5, 9}, 3, nullptr, pre1970, QByteArrayView("image/jpg"),
                                          QByteArray("bytes"), false);
            ASSERT_TRUE(saved.has_value());
        }
        if (!QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
            QTemporaryDir extractDir;
            ASSERT_TRUE(extractDir.isValid());
            QProcess proc;
            proc.setWorkingDirectory(extractDir.path());
            proc.start(QStringLiteral("bsdtar"), {QStringLiteral("-xf"), file});
            ASSERT_TRUE(proc.waitForFinished(5000));
            const QString extractedPath = extractDir.filePath(expectedName);
            ASSERT_TRUE(QFileInfo::exists(extractedPath)) << extractedPath.toStdString();
            EXPECT_EQ(QFileInfo(extractedPath).lastModified().toSecsSinceEpoch(), 2209161600LL);
        }
    }
}

TEST(ArchiveTileStorage, ZipEntryPermissionsAreReadable) {
    // F4 (review, major): libarchive's zip writer always writes "made by" = Unix; an entry with no
    // perm set therefore extracts as external attributes 0100000 (mode 000, unreadable -- verified
    // by the reviewer with `zipinfo -v` / `unzip -d`). The port now sets an explicit rw-r--r--
    // (0644) permission on every zip entry, so it extracts as a normal, world-readable file.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("perm.zip"));
    const TileDate loadDate = fromPosixSeconds(1700000000);
    {
        auto writer =
            openWrite(file, ArchiveTileStorage::Container::Zip, 0, std::make_shared<SasTileNameCodec>());
        ASSERT_NE(writer, nullptr);
        auto saved = writer->saveTile(PointI{5, 9}, 3, nullptr, loadDate, QByteArrayView("image/jpg"),
                                      QByteArray("bytes"), false);
        ASSERT_TRUE(saved.has_value());
    }

    if (QStandardPaths::findExecutable(QStringLiteral("zipinfo")).isEmpty()) {
        GTEST_SKIP() << "zipinfo not installed -- cannot verify extracted permission bits";
    }
    QProcess proc;
    proc.start(QStringLiteral("zipinfo"), {file});
    ASSERT_TRUE(proc.waitForFinished(5000));
    const QString listing = QString::fromUtf8(proc.readAllStandardOutput());
    EXPECT_TRUE(listing.contains(QStringLiteral("-rw-r--r--")))
        << "expected a world-readable regular-file entry, got:\n"
        << listing.toStdString();
    EXPECT_FALSE(listing.contains(QStringLiteral("----------")))
        << "mode 000 (the F4 defect) is still present:\n"
        << listing.toStdString();
}

// T-0117: stateChangeable() is the archive writer's own state holder (write-only: read starts false).
TEST(ArchiveTileStorage, StateChangeableIsOwnHolder) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto storage = openWrite(dir.path() + QStringLiteral("/a.tar"), ArchiveTileStorage::Container::Tar,
                                   0, std::make_shared<cg::storage::SasTileNameCodec>());
    ASSERT_NE(storage, nullptr);
    cg::storage::test::expectStateChangeableIsOwn(*storage, false);
}
