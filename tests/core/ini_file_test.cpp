// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-200; docs/specs/core.md 2.12 (quirks Q1-Q24), test plan 7.5
#include "cg/core/ini_file.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

#include "fixture_path.h"

using cg::core::ErrorCode;
using cg::core::IniFile;

namespace {

QByteArray readFixtureBytes(const QString& relativePath) {
    QFile file(cgFixture(relativePath));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << relativePath.toStdString();
    return file.readAll();
}

IniFile loadQuirks() {
    auto parsed = IniFile::fromBytes(readFixtureBytes(QStringLiteral("ini/quirks.ini")));
    EXPECT_TRUE(parsed.has_value());
    return parsed.value_or(IniFile{});
}

IniFile parse(const QByteArray& raw) {
    auto parsed = IniFile::fromBytes(raw);
    EXPECT_TRUE(parsed.has_value());
    return parsed.value_or(IniFile{});
}

} // namespace

TEST(IniFile, Q1_LineIsTrimmedBeforeClassification) {
    const IniFile ini = loadQuirks();
    // "  Spaced  =   padded value" is a key line despite the indentation.
    EXPECT_TRUE(ini.hasKey(u"Plain", u"Spaced"));
    // "   ; indented comment (Q1)" stays a comment, not a key.
    EXPECT_EQ(ini.keyNames(u"Plain").filter(QStringLiteral(";"), Qt::CaseInsensitive).size(), 0);
    const IniFile padded = parse("  [Padded]  \r\nA=1\r\n");
    EXPECT_TRUE(padded.hasSection(u"Padded"));
    EXPECT_EQ(padded.readString(u"Padded", u"A"), QStringLiteral("1"));
}

TEST(IniFile, Q2_SemicolonIsCommentHashIsNot) {
    const IniFile ini = loadQuirks();
    EXPECT_TRUE(ini.hasKey(u"Plain", u"#HashKey"));
    EXPECT_EQ(ini.readString(u"Plain", u"#HashKey"), QStringLiteral("hashvalue"));
    EXPECT_FALSE(ini.hasKey(u"Plain", u"leading comment, preserved verbatim"));
}

TEST(IniFile, Q3_SectionNeedsBracketsAtBothEnds) {
    const IniFile ini = parse("[Good]\r\nA=1\r\n[Bad] ; note\r\nB=2\r\n");
    EXPECT_TRUE(ini.hasSection(u"Good"));
    EXPECT_FALSE(ini.hasSection(u"Bad"));
    // The orphan header line does not open a section, so B belongs to [Good].
    EXPECT_EQ(ini.readString(u"Good", u"B"), QStringLiteral("2"));
    EXPECT_EQ(ini.sectionNames(), QStringList{QStringLiteral("Good")});
}

TEST(IniFile, Q3_SectionNameMayContainBackslashAndDollar) {
    const IniFile ini = loadQuirks();
    EXPECT_TRUE(ini.hasSection(u"SASGIS_DBMS\\$"));
    EXPECT_EQ(ini.readInt(u"SASGIS_DBMS\\$", u"$DollarKey", -1), 1);

    const IniFile real = parse(readFixtureBytes(QStringLiteral("ini/TileStorage_DBMS.Default.ini")));
    EXPECT_TRUE(real.hasSection(u"SASGIS_DBMS\\$"));
    EXPECT_EQ(real.readString(u"SASGIS_DBMS\\$", u"DSN"), QStringLiteral("SASGIS_DBMS"));
}

TEST(IniFile, Q4_SplitAtFirstEqualsOnly) {
    const IniFile ini = loadQuirks();
    EXPECT_TRUE(ini.hasKey(u"Plain", u"TwoEquals"));
    EXPECT_EQ(ini.readString(u"Plain", u"TwoEquals"), QStringLiteral("a=b=c"));
}

TEST(IniFile, Q5_KeyAndValueAreTrimmed) {
    const IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readString(u"Plain", u"Spaced"), QStringLiteral("padded value"));
    EXPECT_TRUE(ini.keyNames(u"Plain").contains(QStringLiteral("Spaced")));
}

TEST(IniFile, Q6_QuotesAreNotStripped) {
    const IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readString(u"Plain", u"Quoted"), QStringLiteral("\"quoted value\""));
}

TEST(IniFile, Q7_LineWithoutEqualsInsideSectionIsNotAKey) {
    const IniFile ini = loadQuirks();
    EXPECT_FALSE(ini.keyNames(u"Plain").contains(QStringLiteral("NoEqualsLineHere")));
    EXPECT_FALSE(ini.hasKey(u"Plain", u"NoEqualsLineHere"));
    // ... but the line survives serialisation.
    EXPECT_TRUE(ini.toBytes().contains("NoEqualsLineHere"));
}

TEST(IniFile, Q8_KeyBeforeFirstSectionIsIgnored) {
    const IniFile ini = loadQuirks();
    EXPECT_FALSE(ini.hasKey(u"", u"KeyBeforeAnySection"));
    EXPECT_FALSE(ini.hasKey(u"Plain", u"KeyBeforeAnySection"));
    EXPECT_TRUE(ini.toBytes().contains("KeyBeforeAnySection=ignored"));
}

TEST(IniFile, Q9_DuplicateKeyFirstWins) {
    const IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readString(u"Dup", u"Key"), QStringLiteral("first"));
    EXPECT_EQ(ini.keyNames(u"Dup"), (QStringList{QStringLiteral("Key"), QStringLiteral("Key")}));
}

TEST(IniFile, Q9_DeleteKeyRemovesAllDuplicates) {
    IniFile ini = loadQuirks();
    ini.deleteKey(u"Dup", u"Key");
    EXPECT_FALSE(ini.hasKey(u"Dup", u"Key"));
    EXPECT_TRUE(ini.keyNames(u"Dup").isEmpty());
    EXPECT_FALSE(ini.toBytes().contains("Key=second"));
    EXPECT_TRUE(ini.isDirty());
}

TEST(IniFile, Q10_DuplicateSectionFirstWins) {
    const IniFile ini = loadQuirks();
    EXPECT_FALSE(ini.hasKey(u"Plain", u"Shadowed"));
    EXPECT_EQ(ini.readString(u"plain", u"Shadowed", QStringLiteral("absent")), QStringLiteral("absent"));
    EXPECT_EQ(ini.sectionNames().count(QStringLiteral("Plain")), 1);
    EXPECT_EQ(ini.sectionNames().count(QStringLiteral("plain")), 1);
}

TEST(IniFile, Q11_SectionAndKeyLookupIsCaseInsensitive) {
    const IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readString(u"PLAIN", u"simple"), QStringLiteral("value"));
    EXPECT_TRUE(ini.hasSection(u"tYpEs"));
    EXPECT_TRUE(ini.hasKey(u"types", u"INTDEC"));
}

TEST(IniFile, Q11_WriteKeepsOriginalKeyCasing) {
    IniFile ini = loadQuirks();
    ini.writeString(u"PLAIN", u"SIMPLE", QStringLiteral("rewritten"));
    EXPECT_TRUE(ini.toBytes().contains("Simple=rewritten"));
    EXPECT_FALSE(ini.toBytes().contains("SIMPLE=rewritten"));
}

TEST(IniFile, Q12_EmptyValueIsNotTheDefault) {
    const IniFile ini = loadQuirks();
    EXPECT_TRUE(ini.hasKey(u"Plain", u"Empty"));
    EXPECT_EQ(ini.readString(u"Plain", u"Empty", QStringLiteral("fallback")), QString());
    EXPECT_EQ(ini.readString(u"Plain", u"Missing", QStringLiteral("fallback")), QStringLiteral("fallback"));
}

TEST(IniFile, Q13_ReadIntDecimalHexAndFailure) {
    const IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readInt(u"Types", u"IntDec", -1), 42);
    EXPECT_EQ(ini.readInt(u"Types", u"IntNeg", -1), -7);
    EXPECT_EQ(ini.readInt(u"Types", u"IntHex", -1), 31);
    EXPECT_EQ(ini.readInt(u"Types", u"IntDollar", -1), 31);
    EXPECT_EQ(ini.readInt(u"Types", u"IntBad", -1), -1);
    EXPECT_EQ(ini.readInt(u"Types", u"Absent", 7), 7);
}

TEST(IniFile, Q13_ReadIntUppercaseHexPrefix) {
    // Minor 10 (T-0002A rework): only the lower-case "0x" branch was pinned; "0X" is implemented
    // identically (ini_file.cpp: case-insensitive check) but was unexercised.
    const IniFile ini = parse("[T]\r\nA=0X1F\r\n");
    EXPECT_EQ(ini.readInt(u"T", u"A", -1), 31);
}

TEST(IniFile, Q13_ReadIntOverflowReturnsDefault) {
    const IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readInt(u"Types", u"IntHuge", -1), -1);
    const IniFile wide = parse("[T]\r\nA=$FFFFFFFF\r\nB=$1FFFFFFFF\r\n");
    EXPECT_EQ(wide.readInt(u"T", u"A", 0), -1); // Delphi StrToInt('$FFFFFFFF') = -1
    EXPECT_EQ(wide.readInt(u"T", u"B", 5), 5);
}

TEST(IniFile, Q14_ReadBoolOnlyUnderstandsIntegers) {
    const IniFile ini = loadQuirks();
    EXPECT_TRUE(ini.readBool(u"Types", u"BoolOne", false));
    EXPECT_FALSE(ini.readBool(u"Types", u"BoolZero", true));
    EXPECT_TRUE(ini.readBool(u"Types", u"BoolTwo", false));
    // "True"/"False" do not parse as integers, so the default survives - exactly as legacy.
    EXPECT_FALSE(ini.readBool(u"Types", u"BoolTrue", false));
    EXPECT_TRUE(ini.readBool(u"Types", u"BoolTrue", true));
    EXPECT_TRUE(ini.readBool(u"Types", u"BoolFalse", true));
}

TEST(IniFile, Q15_WriteBoolEmitsZeroOne) {
    IniFile ini = loadQuirks();
    ini.writeBool(u"Types", u"BoolOne", false);
    ini.writeBool(u"Types", u"BoolZero", true);
    EXPECT_TRUE(ini.toBytes().contains("BoolOne=0"));
    EXPECT_TRUE(ini.toBytes().contains("BoolZero=1"));
}

TEST(IniFile, Q16_ReadFloatCommaNanAndFailure) {
    const IniFile ini = loadQuirks();
    EXPECT_DOUBLE_EQ(ini.readFloat(u"Types", u"FloatDot", -1.0), 3.25);
    EXPECT_DOUBLE_EQ(ini.readFloat(u"Types", u"FloatComma", -1.0), 3.25);
    EXPECT_TRUE(std::isnan(ini.readFloat(u"Types", u"FloatNan", -1.0)));
    EXPECT_DOUBLE_EQ(ini.readFloat(u"Types", u"FloatBad", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(ini.readFloat(u"Types", u"Absent", 2.5), 2.5);
}

TEST(IniFile, Q16_ReadFloatRejectsInfLikeDelphi) {
    // Finding 5 (T-0002A rework): Delphi StrToFloat has no "Inf"/"Infinity" spelling and raises
    // EConvertError (-> default) for them; QString::toDouble() would otherwise accept them.
    const IniFile ini = parse("[F]\r\nA=inf\r\nB=-INF\r\nC=Infinity\r\nD=nan\r\nE=INF\r\n");
    EXPECT_DOUBLE_EQ(ini.readFloat(u"F", u"A", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(ini.readFloat(u"F", u"B", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(ini.readFloat(u"F", u"C", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(ini.readFloat(u"F", u"D", -1.0), -1.0);
    // Q17 writes non-finite values as exactly "INF"/"-INF"/"NAN"; only the exact-text "NAN" is a
    // legacy-recognised read spelling, so a value the port itself wrote as "INF" still reads back
    // as the default, matching legacy (spec §4 divergence, documented as such, not "improved").
    EXPECT_DOUBLE_EQ(ini.readFloat(u"F", u"E", -1.0), -1.0);
}

TEST(IniFile, Q16_ReadFloatOnlyReplacesFirstComma) {
    // Minor 10 (T-0002A rework): a second comma was never exercised. Q16 replaces only the
    // *first* comma with a dot; "1,2,3" still has a comma left over and must fail to parse.
    const IniFile ini = parse("[F]\r\nA=1,2,3\r\n");
    EXPECT_DOUBLE_EQ(ini.readFloat(u"F", u"A", -1.0), -1.0);
}

TEST(IniFile, Q17_WriteFloatIsLocaleFree) {
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::German));
    IniFile ini = loadQuirks();
    ini.writeFloat(u"Types", u"FloatDot", 1234.5);
    EXPECT_TRUE(ini.toBytes().contains("FloatDot=1234.5"));
    ini.writeFloat(u"Types", u"FloatNan", std::nan(""));
    EXPECT_TRUE(ini.toBytes().contains("FloatNan=NAN"));
    QLocale::setDefault(previous);
}

TEST(IniFile, Q18_DateTimeFormats) {
    IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readDate(u"Types", u"DateFull", QDate()), QDate(2024, 12, 31));
    EXPECT_EQ(ini.readTime(u"Types", u"TimeVal", QTime()), QTime(23, 59, 58));
    EXPECT_EQ(ini.readDateTime(u"Types", u"DateTimeVal", QDateTime()),
              QDateTime(QDate(2024, 12, 31), QTime(23, 59, 58)));

    ini.writeDate(u"Types", u"DateFull", QDate(2001, 2, 3));
    ini.writeTime(u"Types", u"TimeVal", QTime(4, 5, 6));
    ini.writeDateTime(u"Types", u"DateTimeVal", QDateTime(QDate(2001, 2, 3), QTime(4, 5, 6)));
    const QByteArray bytes = ini.toBytes();
    EXPECT_TRUE(bytes.contains("DateFull=03.02.2001"));
    EXPECT_TRUE(bytes.contains("TimeVal=04:05:06"));
    EXPECT_TRUE(bytes.contains("DateTimeVal=03.02.2001 04:05:06"));
}

TEST(IniFile, Q19_DateParsingIsLenient) {
    const IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readDate(u"Types", u"DateShort", QDate()), QDate(2024, 2, 1));
    const IniFile more = parse("[D]\r\nA=01.02.2024\r\nB=\r\nC=nonsense\r\nD=31.02.2024\r\n");
    EXPECT_EQ(more.readDate(u"D", u"A", QDate()), QDate(2024, 2, 1));
    EXPECT_EQ(more.readDate(u"D", u"B", QDate(1999, 1, 1)), QDate(1999, 1, 1));
    EXPECT_EQ(more.readDate(u"D", u"C", QDate(1999, 1, 1)), QDate(1999, 1, 1));
    EXPECT_EQ(more.readDate(u"D", u"D", QDate(1999, 1, 1)), QDate(1999, 1, 1));
}

TEST(IniFile, Q20_TwoDigitYearWindowFifty) {
    const IniFile ini = loadQuirks();
    const int base = QDate::currentDate().year() - 50;
    const auto expand = [base](int twoDigit) {
        int year = twoDigit + (base / 100) * 100;
        if (year < base) {
            year += 100;
        }
        return year;
    };
    EXPECT_EQ(ini.readDate(u"Types", u"DateTwoDigit", QDate()), QDate(expand(75), 2, 1));

    const IniFile other = parse("[D]\r\nA=1.2.76\r\n");
    EXPECT_EQ(other.readDate(u"D", u"A", QDate()), QDate(expand(76), 2, 1));
    EXPECT_NE(expand(75), expand(76));
}

TEST(IniFile, Q21_BinaryIsUpperCaseHex) {
    IniFile ini = loadQuirks();
    EXPECT_EQ(ini.readBinary(u"Types", u"BinHex"), QByteArray::fromHex("DEADBEEF"));
    EXPECT_TRUE(ini.readBinary(u"Types", u"BinOdd").isEmpty());
    EXPECT_TRUE(ini.readBinary(u"Types", u"BinBad").isEmpty());
    EXPECT_TRUE(ini.readBinary(u"Types", u"Absent").isEmpty());

    ini.writeBinary(u"Types", u"BinHex", QByteArray::fromHex("0a1b2c"));
    EXPECT_TRUE(ini.toBytes().contains("BinHex=0A1B2C"));
}

TEST(IniFile, Q22_BomOnlyFileRoundTripsToThreeBytes) {
    const QByteArray raw = readFixtureBytes(QStringLiteral("ini/FormsState.ini"));
    ASSERT_EQ(raw.size(), 3);
    const IniFile ini = parse(raw);
    EXPECT_TRUE(ini.sectionNames().isEmpty());
    EXPECT_EQ(ini.toBytes(), raw);
}

TEST(IniFile, Q23_NewKeyAppendsToItsSection) {
    IniFile ini = loadQuirks();
    ini.writeString(u"Plain", u"Added", QStringLiteral("yes"));
    const QByteArray bytes = ini.toBytes();
    EXPECT_TRUE(bytes.contains("#HashKey=hashvalue\r\nAdded=yes\r\n\r\n[plain]"));
    EXPECT_EQ(ini.readString(u"Plain", u"Added"), QStringLiteral("yes"));
    EXPECT_TRUE(ini.isDirty());
}

TEST(IniFile, Q23_NewSectionAppendsWithLeadingBlankLine) {
    IniFile ini = loadQuirks();
    ini.writeString(u"Brand", u"New", QStringLiteral("1"));
    const QByteArray bytes = ini.toBytes();
    EXPECT_TRUE(bytes.endsWith("$DollarKey=1\r\n\r\n[Brand]\r\nNew=1\r\n")) << bytes.right(60).toStdString();

    IniFile fresh;
    fresh.writeInt(u"First", u"A", 1);
    EXPECT_EQ(fresh.toBytes(), QByteArray("\xEF\xBB\xBF") + QByteArray("[First]\r\nA=1\r\n"));
}

TEST(IniFile, Q24_UnmodifiedFileRoundTripsByteIdentical) {
    const QStringList fixtures = {
        QStringLiteral("ini/FormsState.ini"),
        QStringLiteral("ini/TileStorage_DBMS.Default.ini"),
        QStringLiteral("ini/TerrainData/SASTerrain.Default.ini"),
        QStringLiteral("ini/quirks.ini"),
    };
    for (const QString& fixture : fixtures) {
        const QByteArray raw = readFixtureBytes(fixture);
        const IniFile ini = parse(raw);
        EXPECT_FALSE(ini.isDirty());
        EXPECT_EQ(ini.toBytes(), raw) << fixture.toStdString();
    }
}

TEST(IniFile, Q24_ModifiedFileKeepsCommentsAndOrder) {
    const QByteArray raw = readFixtureBytes(QStringLiteral("ini/quirks.ini"));
    IniFile ini = parse(raw);
    ini.writeString(u"Plain", u"Simple", QStringLiteral("changed"));
    const QByteArray bytes = ini.toBytes();

    QByteArray expected = raw;
    expected.replace("Simple=value", "Simple=changed");
    EXPECT_EQ(bytes, expected);
    EXPECT_TRUE(bytes.contains("; leading comment, preserved verbatim"));
    EXPECT_TRUE(bytes.contains("   ; indented comment (Q1)"));
    EXPECT_TRUE(bytes.contains("NoEqualsLineHere"));
}

// --- Finding 3 (T-0002A rework): eraseSection() / save() / saveAs() / setSourcePath() were
// entirely uncovered. All persistence tests use a QTemporaryDir so nothing touches the repo.

TEST(IniFile, EraseSectionRemovesOnlyFirstOfDuplicateSections) {
    // Q10: quirks.ini has both "[Plain]" and "[plain]". Legacy TMemIniFile.EraseSection uses
    // FSections.IndexOf, so it removes only the first match -- the port must do the same.
    IniFile ini = loadQuirks();
    ASSERT_TRUE(ini.hasKey(u"Plain", u"Simple"));
    ini.eraseSection(u"Plain");
    EXPECT_TRUE(ini.isDirty());
    EXPECT_FALSE(ini.hasKey(u"Plain", u"Simple"));
    // The surviving duplicate now resolves first for a case-insensitive "Plain" lookup.
    EXPECT_EQ(ini.readString(u"Plain", u"Shadowed"), QStringLiteral("second section, unreachable"));
    const QByteArray bytes = ini.toBytes();
    EXPECT_FALSE(bytes.contains("Simple=value"));
    EXPECT_TRUE(bytes.contains("[plain]\r\nShadowed=second section, unreachable\r\n"));
}

TEST(IniFile, SetSourcePathIsReportedBySourcePath) {
    IniFile ini;
    EXPECT_TRUE(ini.sourcePath().isEmpty());
    ini.setSourcePath(QStringLiteral("/tmp/does-not-need-to-exist.ini"));
    EXPECT_EQ(ini.sourcePath(), QStringLiteral("/tmp/does-not-need-to-exist.ini"));
}

TEST(IniFile, SaveWithoutSourcePathReturnsIoError) {
    IniFile ini;
    ini.writeInt(u"S", u"A", 1);
    const auto result = ini.save();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::IoError);
}

TEST(IniFile, SaveUsesThePathSetBySetSourcePath) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("nested.ini"));
    IniFile ini;
    ini.writeInt(u"S", u"A", 7);
    ini.setSourcePath(path);
    ASSERT_TRUE(ini.save().has_value());
    EXPECT_FALSE(ini.isDirty());

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), ini.toBytes());
}

TEST(IniFile, SaveAsReloadRoundTripsByteIdenticalAndReplacesAtomically) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    {
        QFile seed(path);
        ASSERT_TRUE(seed.open(QIODevice::WriteOnly));
        ASSERT_NE(seed.write("stale content that must be fully replaced, not appended to"), -1);
    }

    IniFile ini = loadQuirks();
    ini.writeString(u"Plain", u"Simple", QStringLiteral("changed"));
    ASSERT_TRUE(ini.saveAs(path).has_value());
    EXPECT_FALSE(ini.isDirty());

    QFile onDisk(path);
    ASSERT_TRUE(onDisk.open(QIODevice::ReadOnly));
    const QByteArray bytes = onDisk.readAll();
    EXPECT_EQ(bytes, ini.toBytes());
    EXPECT_FALSE(bytes.contains("stale content"));
    // QSaveFile::commit() renames its staging file over the target -- nothing else should be
    // left beside it in the directory (ADR-0010 atomic write via QSaveFile, spec §2.12.3).
    EXPECT_EQ(QDir(dir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 1);

    auto reloaded = IniFile::fromFile(path);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_FALSE(reloaded->isDirty());
    EXPECT_EQ(reloaded->readString(u"Plain", u"Simple"), QStringLiteral("changed"));
    EXPECT_EQ(reloaded->toBytes(), bytes);
}

TEST(IniFile, SaveAsFailsAndLeavesFileUntouchedWhenValueNotRepresentableInCp1251) {
    CG_SKIP_IF_NO_CORPUS();
    // Finding 2 (T-0002A rework): a Cp1251 settings file that later receives a character with no
    // Cp1251 representation must fail loudly (spec §4 "nothing is ever swallowed") and must not
    // corrupt the file with a '?' substitution.
    const QByteArray original =
        readFixtureBytes(QStringLiteral("zmp/sas.maps/_Coverage/Beeline/Beeline_IoT.zmp/params.txt"));
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("params.txt"));
    {
        QFile seed(path);
        ASSERT_TRUE(seed.open(QIODevice::WriteOnly));
        ASSERT_EQ(seed.write(original), original.size());
    }

    auto loaded = IniFile::fromFile(path);
    ASSERT_TRUE(loaded.has_value());
    IniFile ini = std::move(loaded.value());
    ASSERT_TRUE(ini.hasSection(u"PARAMS")); // sanity: real Cp1251 fixture (text_codec_test).

    ini.writeString(u"PARAMS", u"name", QString::fromUtf8("\xE4\xB8\xAD")); // U+4E2D, absent from Cp1251
    const auto result = ini.saveAs(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Unsupported);

    QFile after(path);
    ASSERT_TRUE(after.open(QIODevice::ReadOnly));
    EXPECT_EQ(after.readAll(), original) << "saveAs() must leave the file untouched on failure";
}
