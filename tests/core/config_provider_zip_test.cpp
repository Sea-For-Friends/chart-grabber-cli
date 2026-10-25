// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: prerequisite of FEAT-NET-001 (zip ZMP container, maptype spec 2.2, LD-1, D-48; T-0061)
#include "cg/core/archive_reader_zip.h"
#include "cg/core/config_provider_folder.h"
#include "cg/core/config_provider_zip.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <clocale>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fixture_path.h"

using cg::core::ConfigProviderFolder;
using cg::core::ConfigProviderZip;
using cg::core::IConfigProviderPtr;

namespace {

QString zippedZmp(const char* name) {
    return cgFixture(QStringLiteral("zmp/zipped/") + QLatin1String(name));
}

QString folderZmp() {
    return cgFixture(QStringLiteral("zmp/sas.maps/_osm/Gps_tiles.zmp"));
}

IConfigProviderPtr openZip(const QString& path) {
    auto opened = ConfigProviderZip::open(path);
    EXPECT_TRUE(opened.has_value()) << path.toStdString();
    return opened ? *opened : nullptr;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    return file.readAll();
}

QStringList sorted(QStringList list) {
    list.sort();
    return list;
}

// --- a hand-made stored zip, for what no real fixture can show (names, case, damage) ---
struct ZipMember {
    QString name;
    QByteArray data; // ignored for directories (name ends in '/')
    bool encrypted = false;
    bool utf8Flag = false; // general purpose bit 11: the name is UTF-8
};

quint32 crc32Of(const QByteArray& bytes) {
    quint32 crc = 0xFFFFFFFFu;
    for (const char c : bytes) {
        crc ^= static_cast<quint8>(c);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) != 0 ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

void put(QByteArray& out, quint32 value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFFu));
    }
}

QByteArray makeStoredZip(const std::vector<ZipMember>& members) {
    QByteArray body;
    QByteArray central;
    for (const ZipMember& m : members) {
        const bool dir = m.name.endsWith(QLatin1Char('/'));
        const QByteArray name = m.name.toUtf8();
        const QByteArray data = dir ? QByteArray() : m.data;
        const quint32 flags = (m.encrypted ? 1u : 0u) | (m.utf8Flag ? 0x800u : 0u);
        const quint32 crc = crc32Of(data);
        const auto offset = static_cast<quint32>(body.size());
        put(body, 0x04034b50u, 4);
        put(body, 20, 2);
        put(body, flags, 2);
        put(body, 0, 2);
        put(body, 0, 2);
        put(body, 0x21, 2);
        put(body, crc, 4);
        put(body, static_cast<quint32>(data.size()), 4);
        put(body, static_cast<quint32>(data.size()), 4);
        put(body, static_cast<quint32>(name.size()), 2);
        put(body, 0, 2);
        body += name;
        body += data;
        put(central, 0x02014b50u, 4);
        put(central, 20, 2);
        put(central, 20, 2);
        put(central, flags, 2);
        put(central, 0, 2);
        put(central, 0, 2);
        put(central, 0x21, 2);
        put(central, crc, 4);
        put(central, static_cast<quint32>(data.size()), 4);
        put(central, static_cast<quint32>(data.size()), 4);
        put(central, static_cast<quint32>(name.size()), 2);
        put(central, 0, 2);
        put(central, 0, 2);
        put(central, 0, 2);
        put(central, 0, 2);
        put(central, dir ? ((0040755u << 16) | 0x10u) : (0100644u << 16), 4);
        put(central, offset, 4);
        central += name;
    }
    QByteArray out = body;
    const auto cdOffset = static_cast<quint32>(body.size());
    out += central;
    put(out, 0x06054b50u, 4);
    put(out, 0, 2);
    put(out, 0, 2);
    put(out, static_cast<quint32>(members.size()), 2);
    put(out, static_cast<quint32>(members.size()), 2);
    put(out, static_cast<quint32>(central.size()), 4);
    put(out, cdOffset, 4);
    put(out, 0, 2);
    return out;
}

// Writes the bytes to a file in `dir` and opens it as a zip provider.
cg::core::Result<std::shared_ptr<ConfigProviderZip>> openBytes(QTemporaryDir& dir, const QByteArray& bytes) {
    const QString path = dir.filePath(QStringLiteral("x.zmp"));
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    return ConfigProviderZip::open(path);
}

} // namespace

// The ticket's proof at provider level: every member of the zipped fixture, read through the zip
// provider, equals the same member of the original folder.
class ZippedZmp : public testing::TestWithParam<const char*> {};

TEST_P(ZippedZmp, EveryMemberEqualsTheFolderProvider) {
    CG_SKIP_IF_NO_CORPUS();
    const IConfigProviderPtr zip = openZip(zippedZmp(GetParam()));
    const IConfigProviderPtr folder = ConfigProviderFolder::create(folderZmp());
    ASSERT_TRUE(zip && folder);

    QStringList zipNames = zip->valueNames();
    EXPECT_TRUE(zipNames.removeOne(QStringLiteral("EmptyTiles"))) << "L-155: the directory entry is listed";
    EXPECT_EQ(sorted(zipNames),
              sorted(folder->valueNames())); // .ini is the only filtered extension: none here
    for (const QString& name : folder->valueNames()) {
        EXPECT_EQ(zip->binary(name), folder->binary(name)) << name.toStdString();
        EXPECT_EQ(zip->value(name), folder->value(name)) << name.toStdString();
    }
    EXPECT_EQ(sorted(zip->subItemNames()), sorted(folder->subItemNames()));

    // T-0273 (L-155): the EmptyTiles/ sub-folder is found through subItem() like the folder's, and its
    // members equal the folder's; the files are still readable by their full path.
    const IConfigProviderPtr zipEmpty = zip->subItem(u"EmptyTiles");
    const IConfigProviderPtr folderEmpty = folder->subItem(u"EmptyTiles");
    ASSERT_TRUE(zipEmpty && folderEmpty);
    EXPECT_FALSE(folderEmpty->valueNames().isEmpty());
    EXPECT_EQ(sorted(zipEmpty->valueNames()), sorted(folderEmpty->valueNames()));
    for (const QString& name : folderEmpty->valueNames()) {
        EXPECT_EQ(zipEmpty->binary(name), folderEmpty->binary(name)) << name.toStdString();
        EXPECT_EQ(zip->binary(QStringLiteral("EmptyTiles/") + name), folderEmpty->binary(name))
            << name.toStdString();
        EXPECT_EQ(zip->binary(QStringLiteral("EmptyTiles\\") + name), folderEmpty->binary(name));
        EXPECT_FALSE(zip->value(QStringLiteral("EmptyTiles/") + name).isValid())
            << "a .png is not a text member";
    }

    const IConfigProviderPtr zipParams = zip->subItem(u"params.txt");
    const IConfigProviderPtr folderParams = folder->subItem(u"params.txt");
    ASSERT_TRUE(zipParams && folderParams);
    EXPECT_EQ(zipParams->subItemNames(), folderParams->subItemNames());
    const IConfigProviderPtr zipSection = zipParams->subItem(u"PARAMS");
    const IConfigProviderPtr folderSection = folderParams->subItem(u"PARAMS");
    ASSERT_TRUE(zipSection && folderSection);
    EXPECT_EQ(zipSection->valueNames(), folderSection->valueNames());
    for (const QString& key : folderSection->valueNames()) {
        EXPECT_EQ(zipSection->value(key), folderSection->value(key)) << key.toStdString();
    }
}

TEST_P(ZippedZmp, FileNameSemantics) {
    CG_SKIP_IF_NO_FIXTURE(zippedZmp("Gps_tiles_deflated.zmp"));
    const QString path = zippedZmp(GetParam());
    const IConfigProviderPtr zip = openZip(path);
    ASSERT_TRUE(zip);
    EXPECT_EQ(zip->value(u"::FileName").toString(), path);
    // A sub-provider's "::FileName" is <file>/<folder> (T-0273: subItem() finds the folder, L-155).
    const IConfigProviderPtr sub = zip->subItem(u"EmptyTiles");
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->value(u"::FileName").toString(), path + QStringLiteral("/EmptyTiles"));
}

INSTANTIATE_TEST_SUITE_P(StoredAndDeflated, ZippedZmp,
                         testing::Values("Gps_tiles_stored.zmp", "Gps_tiles_deflated.zmp"));

TEST(ZippedZmpLookup, MissingMembers) {
    CG_SKIP_IF_NO_FIXTURE(zippedZmp("Gps_tiles_deflated.zmp"));
    const IConfigProviderPtr zip = openZip(zippedZmp("Gps_tiles_deflated.zmp"));
    ASSERT_TRUE(zip);
    EXPECT_EQ(zip->subItem(u"nope.txt"), nullptr);
    EXPECT_EQ(zip->subItem(u"nope"), nullptr);
    EXPECT_EQ(zip->subItem(u"24.bmp"), nullptr); // a file is not a folder
    EXPECT_EQ(zip->subItem(u"EmptyTiles/NA.png"), nullptr);
    EXPECT_TRUE(zip->binary(u"nope.bin").isEmpty());
    EXPECT_TRUE(zip->binary(u"EmptyTiles").isEmpty()); // a directory item has no data
    EXPECT_FALSE(zip->value(u"nope.bmp").isValid());   // not a text extension: the caller's default
}

// LD-1 / L-154: the folder provider answers "absent" for a missing text member, the zip one "".
TEST(ZippedZmpLookup, MissingTextMemberReadsAsEmptyString) {
    CG_SKIP_IF_NO_CORPUS();
    const IConfigProviderPtr zip = openZip(zippedZmp("Gps_tiles_deflated.zmp"));
    const IConfigProviderPtr folder = ConfigProviderFolder::create(folderZmp());
    ASSERT_TRUE(zip && folder);
    for (const QString& name :
         {QStringLiteral("index.html"), QStringLiteral("nope.txt"), QStringLiteral("nope.ini")}) {
        ASSERT_TRUE(zip->value(name).isValid()) << name.toStdString();
        EXPECT_EQ(zip->value(name).toString(), QString());
        EXPECT_EQ(zip->readString(name, QStringLiteral("def")), QString());
        EXPECT_FALSE(folder->value(name).isValid());
    }
}

// D-48 for the zip provider: exact name first, then case-insensitive.
TEST(ZippedZmpLookup, CaseRule) {
    CG_SKIP_IF_NO_CORPUS();
    const IConfigProviderPtr zip = openZip(zippedZmp("Gps_tiles_stored.zmp"));
    const IConfigProviderPtr folder = ConfigProviderFolder::create(folderZmp());
    ASSERT_TRUE(zip && folder);
    EXPECT_EQ(zip->binary(u"getURLscript.TXT"), folder->binary(u"GetUrlScript.txt"));
    EXPECT_EQ(zip->value(u"INFO.txt"), folder->value(u"info.txt"));
    ASSERT_NE(zip->subItem(u"PARAMS.TXT"), nullptr);
    EXPECT_EQ(zip->binary(u"emptytiles/NA.PNG"), folder->subItem(u"EmptyTiles")->binary(u"NA.png"));

    QTemporaryDir dir;
    auto both = openBytes(
        dir, makeStoredZip({{QStringLiteral("a.txt"), "lower"}, {QStringLiteral("A.txt"), "UPPER"}}));
    ASSERT_TRUE(both.has_value());
    EXPECT_EQ((*both)->value(u"A.txt").toString(), QStringLiteral("UPPER")); // the exact name wins ...
    EXPECT_EQ((*both)->value(u"a.txt").toString(), QStringLiteral("lower"));
    EXPECT_EQ((*both)->value(u"a.TXT").toString(), QStringLiteral("lower")); // ... else the first one
}

TEST(ZippedZmpLookup, BackslashSeparatorsAndSubfolderListing) {
    const auto reader =
        cg::core::openArchiveReaderZip(makeStoredZip({{QStringLiteral("Sub\\"), {}},
                                                      {QStringLiteral("Sub\\b.INI"), "[S]\r\nk=v\r\n"},
                                                      {QStringLiteral("Sub\\c.bin"), "xyz"},
                                                      {QStringLiteral("top.txt"), "t"}}));
    ASSERT_TRUE(reader.has_value());
    const auto zip = std::make_shared<ConfigProviderZip>(QStringLiteral("x"), *reader);
    // L-155: the directory entry is listed by its name, .ini members are not filtered.
    EXPECT_EQ(zip->valueNames(), (QStringList{QStringLiteral("Sub"), QStringLiteral("top.txt")}));
    EXPECT_EQ(zip->subItemNames(), QStringList{QStringLiteral("top.txt")});
    // T-0273 (L-155): the directory entry "Sub\" is found as the folder, with its members.
    ASSERT_NE(zip->subItem(u"Sub"), nullptr);
    EXPECT_EQ(zip->subItem(u"Sub")->valueNames(),
              (QStringList{QStringLiteral("b.INI"), QStringLiteral("c.bin")}));
    EXPECT_EQ(zip->binary(u"Sub/c.bin"), QByteArray("xyz"));
    EXPECT_EQ(zip->binary(u"sub\\C.BIN"), QByteArray("xyz"));
    // The sub-provider legacy's ctor would build (never reached through subItem()):
    ConfigProviderZip sub(QStringLiteral("x"), *reader, QStringLiteral("Sub"));
    EXPECT_EQ(sub.valueNames(), (QStringList{QStringLiteral("b.INI"), QStringLiteral("c.bin")}));
    EXPECT_EQ(sub.subItemNames(), QStringList{QStringLiteral("b.INI")});
    EXPECT_EQ(sub.binary(u"c.bin"), QByteArray("xyz"));
    ASSERT_TRUE(sub.subItem(u"b.ini"));
    EXPECT_EQ(sub.subItem(u"b.ini")->subItem(u"S")->value(u"k").toString(), QStringLiteral("v"));
}

// T-0273 (L-155, D row): a zipped sub-folder is found with its directory entry, without it, or by its
// directory entry alone; the files inside are read through the sub-provider and by their full path.
TEST(ZippedZmpLookup, SubFolderIsFoundWithDirectoryEntry) {
    QTemporaryDir dir;
    auto zip = openBytes(dir, makeStoredZip({{QStringLiteral("EmptyTiles/"), {}},
                                             {QStringLiteral("EmptyTiles/NA.png"), "png"}}));
    ASSERT_TRUE(zip.has_value());
    const IConfigProviderPtr sub = (*zip)->subItem(u"EmptyTiles");
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->binary(u"NA.png"), QByteArray("png"));
    EXPECT_EQ(sub->valueNames(), QStringList{QStringLiteral("NA.png")});
    EXPECT_EQ((*zip)->binary(u"EmptyTiles/NA.png"), QByteArray("png"));
    EXPECT_EQ((*zip)->valueNames(), QStringList{QStringLiteral("EmptyTiles")});
}

TEST(ZippedZmpLookup, SubFolderIsFoundWithoutDirectoryEntry) {
    QTemporaryDir dir;
    auto zip = openBytes(dir, makeStoredZip({{QStringLiteral("EmptyTiles/NA.png"), "png"}}));
    ASSERT_TRUE(zip.has_value());
    const IConfigProviderPtr sub = (*zip)->subItem(u"emptytiles"); // case rule
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->binary(u"NA.png"), QByteArray("png"));
    EXPECT_EQ(sub->valueNames(), QStringList{QStringLiteral("NA.png")});
    EXPECT_EQ((*zip)->binary(u"EmptyTiles/NA.png"), QByteArray("png"));
    EXPECT_TRUE((*zip)->valueNames().isEmpty()); // the member is not a direct child of the root
}

TEST(ZippedZmpLookup, SubFolderIsFoundByItsDirectoryEntryAlone) {
    QTemporaryDir dir;
    auto zip = openBytes(dir, makeStoredZip({{QStringLiteral("BanTiles/"), {}}}));
    ASSERT_TRUE(zip.has_value());
    const IConfigProviderPtr sub = (*zip)->subItem(u"BanTiles");
    ASSERT_NE(sub, nullptr);
    EXPECT_TRUE(sub->valueNames().isEmpty());
    EXPECT_TRUE(sub->subItemNames().isEmpty());
}

// A prefix of a folder name is not the folder: only "<folder>" or "<folder>/..." counts.
TEST(ZippedZmpLookup, PrefixOfAFolderNameIsNotAFolder) {
    QTemporaryDir dir;
    auto zip = openBytes(dir, makeStoredZip({{QStringLiteral("EmptyTiles/NA.png"), "png"}}));
    ASSERT_TRUE(zip.has_value());
    EXPECT_EQ((*zip)->subItem(u"Empty"), nullptr);
    EXPECT_EQ((*zip)->subItem(u"EmptyTiles/NA"), nullptr);
}

// Names: libarchive hands over UTF-8 when the entry says so (bit 11); without the bit it converts
// from the locale charset, and the port then reads the raw bytes as UTF-8. 7-Zip would use the
// OEM code page for an unflagged name; no stock ZMP has a non-ASCII member name.
TEST(ZippedZmpLookup, NonAsciiMemberName) {
    // libarchive converts names with the process LC_CTYPE; the test binary has no QCoreApplication.
    struct LocaleGuard {
        std::string old = std::setlocale(LC_CTYPE, nullptr);
        ~LocaleGuard() { std::setlocale(LC_CTYPE, old.c_str()); }
    } localeGuard;
    if (std::setlocale(LC_CTYPE, "C.UTF-8") == nullptr) {
        GTEST_SKIP() << "no C.UTF-8 locale";
    }
    QTemporaryDir dir;
    const QString name = QString::fromUtf8("\xD0\x98\xD0\xBD\xD1\x84\xD0\xBE.txt"); // "Инфо.txt"
    auto flagged = openBytes(dir, makeStoredZip({{name, "text", false, true}}));
    ASSERT_TRUE(flagged.has_value());
    EXPECT_EQ((*flagged)->valueNames(), QStringList{name});
    EXPECT_EQ((*flagged)->value(name.toUpper()).toString(), QStringLiteral("text")); // case rule on Cyrillic
    auto plain = openBytes(dir, makeStoredZip({{name, "text"}}));
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ((*plain)->value(name).toString(), QStringLiteral("text"));
}

TEST(ZippedZmpCorrupt, UnreadableArchivesAreRefused) {
    CG_SKIP_IF_NO_FIXTURE(zippedZmp("Gps_tiles_deflated.zmp"));
    QTemporaryDir dir;
    EXPECT_FALSE(ConfigProviderZip::open(dir.filePath(QStringLiteral("missing.zmp"))).has_value());
    EXPECT_FALSE(openBytes(dir, QByteArray()).has_value());
    EXPECT_FALSE(openBytes(dir, QByteArray("PK this is not a zip file at all, just text")).has_value());
    const QByteArray good = readFile(zippedZmp("Gps_tiles_deflated.zmp"));
    EXPECT_FALSE(openBytes(dir, good.left(good.size() / 2)).has_value()); // no central directory
}

// End record cut: libarchive falls back to the local headers (7-Zip would refuse), D-62.
TEST(ZippedZmpCorrupt, CutEndRecordStillOpens) {
    CG_SKIP_IF_NO_CORPUS();
    QTemporaryDir dir;
    const QByteArray good = readFile(zippedZmp("Gps_tiles_deflated.zmp"));
    auto cut = openBytes(dir, good.left(good.size() - 10));
    ASSERT_TRUE(cut.has_value());
    EXPECT_EQ((*cut)->binary(u"params.txt"), readFile(folderZmp() + QStringLiteral("/params.txt")));
}

// A damaged member reads as absent, is logged, and is kept in status() (sticky, shared with the
// sub-providers) so that the ZMP set loader can drop the ZMP as legacy does (D-63).
TEST(ZippedZmpCorrupt, DamagedMemberIsEmptyAndStatusCarriesTheError) {
    CG_SKIP_IF_NO_CORPUS();
    QByteArray damaged = readFile(zippedZmp("Gps_tiles_deflated.zmp"));
    for (qsizetype i = 40; i < 90; ++i) { // inside the data of the first member, 24.bmp
        damaged[i] = static_cast<char>(damaged[i] ^ 0x5A);
    }
    const auto reader = cg::core::openArchiveReaderZip(damaged);
    ASSERT_TRUE(reader.has_value());
    const auto zip = std::make_shared<ConfigProviderZip>(QStringLiteral("x.zmp"), *reader);
    EXPECT_TRUE(zip->status().has_value());
    const ConfigProviderZip sub(QStringLiteral("x.zmp/EmptyTiles"), *reader, QStringLiteral("EmptyTiles"));
    EXPECT_TRUE(zip->binary(u"24.bmp").isEmpty());
    const auto status = zip->status();
    ASSERT_FALSE(status.has_value());
    EXPECT_TRUE(status.error().what().contains(QStringLiteral("24.bmp")));
    EXPECT_EQ(zip->binary(u"params.txt"), readFile(folderZmp() + QStringLiteral("/params.txt")));
    EXPECT_FALSE(sub.status().has_value());  // shared with the sub-provider
    EXPECT_FALSE(zip->status().has_value()); // sticky: a later good read does not clear it
    EXPECT_EQ(zip->status().error().what(), status.error().what());
}

TEST(ZippedZmpCorrupt, IntactArchiveStatusIsOk) {
    CG_SKIP_IF_NO_FIXTURE(zippedZmp("Gps_tiles_deflated.zmp"));
    const auto zip = ConfigProviderZip::open(zippedZmp("Gps_tiles_deflated.zmp"));
    ASSERT_TRUE(zip.has_value());
    EXPECT_FALSE((*zip)->binary(u"24.bmp").isEmpty());
    EXPECT_TRUE((*zip)->binary(u"missing.bin").isEmpty()); // absent is not a failure
    EXPECT_TRUE((*zip)->status().has_value());
}

TEST(ZippedZmpCorrupt, EncryptedMemberReadsAsEmptyAndFailsStatus) {
    QTemporaryDir dir;
    auto zip = openBytes(dir, makeStoredZip({{QStringLiteral("secret.bin"), "data", true},
                                             {QStringLiteral("ok.bin"), "fine"}}));
    ASSERT_TRUE(zip.has_value());
    EXPECT_TRUE((*zip)->binary(u"secret.bin").isEmpty());
    EXPECT_FALSE((*zip)->status().has_value());
    EXPECT_EQ((*zip)->binary(u"ok.bin"), QByteArray("fine"));
}

// The per-member size cap (a port invention, D-65), pinned with a small limit.
TEST(ZippedZmpCorrupt, MemberOverTheSizeCapIsRefused) {
    const QByteArray bytes =
        makeStoredZip({{QStringLiteral("small.bin"), "ab"}, {QStringLiteral("big.bin"), "abcd"}});
    const auto reader = cg::core::openArchiveReaderZip(bytes, QStringLiteral("x.zmp"), 3);
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ((*reader)->itemByName(QStringLiteral("small.bin")), QByteArray("ab"));
    EXPECT_TRUE((*reader)->status().has_value());
    EXPECT_FALSE((*reader)->itemByName(QStringLiteral("big.bin")).has_value());
    EXPECT_FALSE((*reader)->status().has_value());
}
