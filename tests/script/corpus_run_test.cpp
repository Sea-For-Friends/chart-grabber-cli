// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT of its own (structural-only, spec 11.5 preamble: "no Delphi oracle" for these
// 350 maps). docs/specs/script.md 11.4/11.5/11.8. Runs every one of the 350 real
// `GetUrlScript.txt` fixtures through the landed `UrlScript` (T-0015A/B/C) at the four tile points
// of §11.5.2, with the fake host of T-0015A/B, and checks STRUCTURE only (ticket T-0015E):
// finishes within budget, no crash/TSan/ASan report, a runtime error is accepted only when
// justified by a row of `corpus_run_expected_errors.csv`, a produced ResultURL is well-formed, and
// a deterministic script gives byte-identical output on two runs without touching Random/
// GetUnixTime/Downloader. `corpus_parse_test.cpp` (§11.4) proves compilation only; this proves
// execution. No URL parity with legacy is claimed here (T-0015D, 21 maps, does that).
//
// One gtest TEST_P per (script, tile point) - not a for-loop in one TEST like corpus_parse_test -
// so a single known port defect (see kPortDefects below) can GTEST_SKIP() just that script's four
// cases while every other one of the corpus still runs and is individually reported. The corpus
// file discovery itself still follows corpus_parse_test exactly (QDirIterator, never a shell glob
// - fixture paths contain spaces).
#include "cg/core/ini_file.h"
#include "cg/script/compiled_script.h"
#include "cg/script/url_script.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "fixture_path.h"
#include "script_host_fake.h"

using namespace cg::script;
using cg::script::test::FakeHttpDownloader;
using cg::script::test::FakeScriptGlobal;
using cg::script::test::FakeScriptHost;
using cg::script::test::FakeScriptLogger;
using cg::script::test::FakeScriptTileCache;

namespace {

// --- the four tile points, spec 11.5.2 table (values copied verbatim, full precision) -----------
struct TilePoint {
    const char* name;
    qint32 x, y, z; // GetZ is 1-based (internal zoom + 1)
    double llon, tlat, rlon, blat;
    double lmetr, tmetr, rmetr, bmetr;
};

constexpr std::array<TilePoint, 4> kPoints{{
    {"P1", 0, 0, 1, -180.0, 85.0511287798066, 180.0, -85.0511287798066, -20037508.342789244,
     20037508.342789244, 20037508.342789244, -20037508.342789255},
    {"P2", 1, 1, 2, 0.0, 0.0, 180.0, -85.0511287798066, 0.0, -7.081154551613622e-10, 20037508.342789244,
     -20037508.342789255},
    {"P3", 1234, 5678, 14, -125.771484375, -56.897003921272606, -125.7275390625, -56.920996758391055,
     -14000817.596939163, -7739096.239817527, -13995925.62712891, -7743988.209627776},
    {"P4", 131071, 87654, 18, 179.99725341796875, -51.79162970442692, 180.0, -51.79332849712256,
     20037202.594676103, -6762536.766446113, 20037508.342789244, -6762842.514559253},
}};

QString zmpRoot() {
    return QDir(cgScriptFixturesDir()).filePath(QStringLiteral("zmp/sas.maps"));
}

// Same exclusion as corpus_parse_test.cpp: two _broken_maps scripts are genuinely malformed
// Pascal (a missing ';' - legacy defect, not a parser gap, see docs/handoffs/T-0013.md) and never
// reach CompiledScript::compile successfully, so there is nothing here to run.
bool isKnownMalformed(const QString& path) {
    return path.endsWith(QStringLiteral("_broken_maps/Balticsmap/balticmaps.eu.map.zmp/GetUrlScript.txt")) ||
           path.endsWith(QStringLiteral("_broken_maps/Balticsmap/balticmaps.eu.topo.zmp/GetUrlScript.txt"));
}

QByteArray readRawBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// --- port defects found by this ticket (category (a): T-0015E's own instructions - 0 production
// LOC, do not fix here). Each entry GTEST_SKIP()s the named script; `onlyPoints` empty means all
// four points, otherwise just the listed ones (a defect only reachable through a script's own
// guard, e.g. PD-4/Fonecta's `6<=GetZ<=19` branch, must not skip the points that guard excludes -
// those points pass cleanly and must stay counted that way). See docs/handoffs/T-0015E.md "Port
// defects found" and docs/handoffs/T-0015E-review.md for the full table (construct, corpus
// impact, evidence). Review finding 5: a PD fixed later stays skipped silently here - the PD
// ticket (T-0031/T-0032, see docs/handoffs/T-0015E.md) must delete its entries.
struct PortDefect {
    QString id;
    QString note;
    QSet<QString> onlyPoints; // empty = skip all 4 points
};

const QMap<QString, PortDefect>& portDefectsByRelPath() {
    static const QMap<QString, PortDefect> table{
        // PD-1 (belgeodesy/odessa.ua/Rambler_maps/ProGorod1/eatlas_new/megafonpro - static
        // arrays), PD-2 (MAmet.zmp), PD-3 (Atlogis x2) and PD-4 (Fonecta x3) are all fixed - see
        // docs/handoffs/T-0031.md and docs/handoffs/T-0032.md for the before/after. Table kept
        // empty (not removed) so the next port defect found by a corpus run has somewhere to go.
    };
    return table;
}

// --- corpus_run_expected_errors.csv: path,point,line,expected,reason (RFC4180-ish: a field may be
// double-quoted to carry commas). Rule 2/3 of the ticket's Scope (in): a runtime error or a
// malformed ResultURL is accepted only when a row here names it AND its actual error text /
// ResultURL contains the row's pinned `expected` substring (review finding 3 - a CSV row that
// merely names the (path,point) but excuses *any* error hides a regression). `expected` is
// matched against `Error::what()` for the error path, or against the raw ResultURL bytes for the
// malformed-URL path. `line` stays documentation only (not cross-checked against the script
// text). A row whose (path,point) never actually produces its pinned error/URL text - because the
// case now passes structurally, the text changed, or the script/point no longer exists - is a
// "stale row" and must fail (see CorpusRunCsv.EveryRowFiresWithItsExpectedError below), so the CSV
// cannot silently rot.
struct ExpectedRow {
    QString path;
    QString point;
    int line = 0;
    QString expected;
    QString reason;
};

QStringList splitCsvLine(const QString& line) {
    QStringList out;
    QString cur;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    cur += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cur += c;
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == QLatin1Char(',')) {
            out << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    out << cur;
    return out;
}

const QList<ExpectedRow>& expectedErrorRows() {
    static const QList<ExpectedRow> rows = [] {
        QList<ExpectedRow> parsed;
        QFile file(
            QDir(cgScriptFixturesDir()).filePath(QStringLiteral("zmp/corpus_run_expected_errors.csv")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return parsed;
        }
        QTextStream in(&file);
        bool header = true;
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (header) {
                header = false;
                continue;
            }
            if (line.trimmed().isEmpty()) {
                continue;
            }
            const QStringList fields = splitCsvLine(line);
            if (fields.size() < 5) {
                continue;
            }
            parsed.append(ExpectedRow{fields[0], fields[1], fields[2].toInt(), fields[3], fields[4]});
        }
        return parsed;
    }();
    return rows;
}

enum class RowMatch { NoRow, Matched, Mismatched };

struct RowLookupResult {
    RowMatch match = RowMatch::NoRow;
    QString expected;
    QString reason;
};

// Looks up the CSV row for (relPath, point) and compares `actualText` against its pinned
// `expected` substring. A text mismatch is reported by the caller as an immediate failure (review
// finding 3: a different regression at the same point must not be silently absorbed). The
// stale-row guarantee (every row must actually fire) is checked independently, and
// process-independently, by CorpusRunCsv.EveryRowFiresWithItsExpectedError below - this lookup no
// longer tracks "consumed" rows, since that bookkeeping only made sense when every case shared one
// process (it does not under `ctest`'s one-case-per-process discovery).
RowLookupResult matchExpectedRow(const QString& relPath, const QString& point, const QString& actualText) {
    const auto& rows = expectedErrorRows();
    for (const ExpectedRow& row : rows) {
        if (row.path == relPath && row.point == point) {
            const bool ok = actualText.contains(row.expected);
            return RowLookupResult{ok ? RowMatch::Matched : RowMatch::Mismatched, row.expected, row.reason};
        }
    }
    return RowLookupResult{};
}

// --- corpus discovery (QDirIterator, never a shell glob - fixture paths contain spaces) ---------
QStringList allScriptPaths() {
    QStringList paths;
    QDirIterator it(zmpRoot(), QStringList{QStringLiteral("GetUrlScript.txt")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        paths.append(it.next());
    }
    paths.sort();
    return paths;
}

QString relativeToZmpRoot(const QString& absPath) {
    return QDir(zmpRoot()).relativeFilePath(absPath);
}

struct CorpusCase {
    QString absPath;
    QString relPath; // relative to zmp/sas.maps/, e.g. "Kosmosnimki.ru/KShyb.zmp/GetUrlScript.txt"
    int pointIndex;  // index into kPoints
};

std::vector<CorpusCase> allCorpusCases() {
    std::vector<CorpusCase> cases;
    for (const QString& path : allScriptPaths()) {
        if (isKnownMalformed(path)) {
            continue; // already a compile failure, nothing to run (corpus_parse_test covers it)
        }
        const QString relPath = relativeToZmpRoot(path);
        for (int p = 0; p < static_cast<int>(kPoints.size()); ++p) {
            cases.push_back(CorpusCase{path, relPath, p});
        }
    }
    return cases;
}

QString sanitizeTestName(const QString& relPath) {
    QString out;
    out.reserve(relPath.size());
    for (const QChar c : relPath) {
        out += (c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return out;
}

// --- params.txt (DefURLBase, RequestHead) -- spec 11.5.1 step 1 ---------------------------------
struct MapParams {
    QByteArray urlBase;
    QByteArray requestHead;
};

MapParams loadMapParams(const QString& scriptPath) {
    const QString paramsPath =
        QDir(QFileInfo(scriptPath).absolutePath()).filePath(QStringLiteral("params.txt"));
    MapParams params;
    auto ini = cg::core::IniFile::fromFile(paramsPath);
    if (!ini.has_value()) {
        return params;
    }
    params.urlBase = ini->readString(u"PARAMS", u"DefURLBase").toUtf8();
    params.requestHead = ini->readString(u"PARAMS", u"RequestHead").toUtf8();
    return params;
}

// A host that counts calls to the three sources rule 4 (isDeterministic()) says a deterministic
// script must never reach: random(), unixTime(), and (via the bound FakeHttpDownloader) doHttpRequest.
class CountingScriptHost : public FakeScriptHost {
public:
    [[nodiscard]] qint32 random(qint32 upperExclusive) override {
        ++randomCalls;
        return FakeScriptHost::random(upperExclusive);
    }
    [[nodiscard]] qint64 unixTime() const override {
        ++unixTimeCalls;
        return FakeScriptHost::unixTime();
    }

    mutable int unixTimeCalls = 0;
    int randomCalls = 0;
};

ScriptVars buildVars(const TilePoint& point, const MapParams& params,
                     const std::shared_ptr<FakeHttpDownloader>& downloader,
                     const std::shared_ptr<FakeScriptGlobal>& global,
                     const std::shared_ptr<FakeScriptLogger>& logger,
                     const std::shared_ptr<FakeScriptTileCache>& tileCache) {
    ScriptVars vars;
    vars.getX = point.x;
    vars.getY = point.y;
    vars.getZ = point.z;
    vars.getLlon = point.llon;
    vars.getTLat = point.tlat;
    vars.getBLat = point.blat;
    vars.getRLon = point.rlon;
    vars.getLMetr = point.lmetr;
    vars.getRMetr = point.rmetr;
    vars.getTMetr = point.tmetr;
    vars.getBMetr = point.bmetr;
    vars.getUrlBase = params.urlBase;
    vars.requestHead = params.requestHead;
    vars.version = QByteArray(); // ticket: Version is '' for every corpus case here
    vars.lang = QByteArrayLiteral("en");
    // Global/Logger/TileCache are legacy-always-non-nil per spec 8.1 (one instance per ZMP) -
    // bound live so a script that calls them unconditionally does not surface a spurious nil-
    // interface error (docs/handoffs/T-0015B.md "What T-0015E must know"). Converter/
    // DefProjConverter/ProjFactory are left nil (0/2/0 corpus hits, T-0015A's usage table) - a
    // script that reaches one unguarded needs a corpus_run_expected_errors.csv row (fake-host
    // limitation), exactly as that same handoff note anticipates.
    vars.downloader = downloader;
    vars.global = global;
    vars.logger = logger;
    vars.tileCache = tileCache;
    return vars;
}

// Rule 3: a non-empty ResultURL has no byte < 0x20 and starts with [a-z]+://.
bool isWellFormedUrl(const QByteArray& url) {
    for (const char ch : url) {
        if (static_cast<unsigned char>(ch) < 0x20) {
            return false;
        }
    }
    int i = 0;
    const int n = url.size();
    while (i < n && url.at(i) >= 'a' && url.at(i) <= 'z') {
        ++i;
    }
    return i > 0 && i + 2 < n && url.at(i) == ':' && url.at(i + 1) == '/' && url.at(i + 2) == '/';
}

} // namespace

class CorpusRun : public ::testing::TestWithParam<CorpusCase> {};

TEST_P(CorpusRun, ExecutesStructurally) {
    CG_SKIP_IF_NO_CORPUS();
    const CorpusCase& c = GetParam();
    const TilePoint& point = kPoints[static_cast<std::size_t>(c.pointIndex)];

    if (const auto defect = portDefectsByRelPath().constFind(c.relPath);
        defect != portDefectsByRelPath().constEnd() &&
        (defect->onlyPoints.isEmpty() || defect->onlyPoints.contains(QString::fromLatin1(point.name)))) {
        GTEST_SKIP() << defect->id.toStdString() << ": " << defect->note.toStdString();
    }

    const QByteArray source = readRawBytes(c.absPath);
    const MapParams params = loadMapParams(c.absPath);

    auto host = std::make_unique<CountingScriptHost>();
    auto downloader = std::make_shared<FakeHttpDownloader>(); // empty queue: status 0, empty strings
    auto global = std::make_shared<FakeScriptGlobal>();
    auto logger = std::make_shared<FakeScriptLogger>();
    auto tileCache = std::make_shared<FakeScriptTileCache>();
    ScriptVars vars = buildVars(point, params, downloader, global, logger, tileCache);

    auto created = UrlScript::create(source, c.relPath, *host);
    ASSERT_TRUE(created.has_value()) << c.relPath.toStdString()
                                     << ": compile failed unexpectedly (corpus_parse_test.cpp "
                                     << "already asserts every non-excluded script compiles) - "
                                     << created.error().what().toStdString();
    UrlScript& script = **created;

    auto result = script.buildRequest(vars); // default (never-cancelled) stop_token, default ExecBudget

    if (!result.has_value()) {
        // Note this is NOT the same axis as ErrorCode: ErrorCode::Unsupported only says "this
        // slice's interpreter cannot execute the compiled construct" - it does not by itself say
        // whether category (a) (a port defect - legacy runs this fine) or (b)/(c) (legacy fails
        // here too, or this is purely a fake-host/harness limitation) applies. That classification
        // is made by hand, once, and recorded in either portDefectsByRelPath() (category (a),
        // skipped above) or the CSV (categories (b)/(c), which must also pin the expected error
        // text - see ExpectedRow above); this branch only catches whatever neither table has
        // classified yet, plus any CSV row whose pinned text no longer matches (a regression at
        // the same (script, point) that the row's stale text would otherwise silently absorb).
        const QString actualText = result.error().what();
        const RowLookupResult lookup = matchExpectedRow(c.relPath, point.name, actualText);
        const char* codeName = "IoError";
        switch (result.error().code()) {
        case cg::core::ErrorCode::Unsupported:
            codeName = "Unsupported";
            break;
        case cg::core::ErrorCode::Cancelled:
            codeName = "Cancelled";
            break;
        default:
            break;
        }
        switch (lookup.match) {
        case RowMatch::NoRow:
            ADD_FAILURE() << c.relPath.toStdString() << " @ " << point.name << ": " << codeName << " - "
                          << actualText.toStdString()
                          << " - not justified by any corpus_run_expected_errors.csv row and not "
                          << "listed in portDefectsByRelPath()";
            break;
        case RowMatch::Mismatched:
            ADD_FAILURE() << c.relPath.toStdString() << " @ " << point.name << ": " << codeName << " - "
                          << actualText.toStdString()
                          << " - a corpus_run_expected_errors.csv row exists for this (path,point) but "
                          << "its pinned 'expected' text (\"" << lookup.expected.toStdString()
                          << "\") is not a substring of the actual error - the row is stale or a "
                          << "regression occurred; fix the row or the interpreter";
            break;
        case RowMatch::Matched:
            break; // accepted
        }
        return;
    }

    if (!result->tileExists) {
        return; // empty ResultURL: a normal outcome (rule 2), nothing further to check
    }

    if (!isWellFormedUrl(result->url)) {
        const QString actualUrl = QString::fromUtf8(result->url);
        const RowLookupResult lookup = matchExpectedRow(c.relPath, point.name, actualUrl);
        switch (lookup.match) {
        case RowMatch::NoRow:
            ADD_FAILURE() << c.relPath.toStdString() << " @ " << point.name << ": malformed ResultURL "
                          << result->url.toStdString()
                          << " - not justified by any corpus_run_expected_errors.csv row";
            break;
        case RowMatch::Mismatched:
            ADD_FAILURE() << c.relPath.toStdString() << " @ " << point.name << ": malformed ResultURL "
                          << result->url.toStdString()
                          << " - a corpus_run_expected_errors.csv row exists for this (path,point) but "
                          << "its pinned 'expected' text (\"" << lookup.expected.toStdString()
                          << "\") is not a substring of the actual URL - the row is stale or a "
                          << "regression occurred; fix the row or the interpreter";
            break;
        case RowMatch::Matched:
            break; // accepted
        }
        return;
    }

    // Rule 4: isDeterministic() scripts must give byte-identical output on a second, completely
    // independent run (fresh host/downloader/interfaces), touching none of Random/GetUnixTime/
    // Downloader.
    auto compiled = CompiledScript::compile(source, c.relPath);
    ASSERT_TRUE(compiled.has_value());
    if ((*compiled)->isDeterministic()) {
        auto host2 = std::make_unique<CountingScriptHost>();
        auto downloader2 = std::make_shared<FakeHttpDownloader>();
        auto global2 = std::make_shared<FakeScriptGlobal>();
        auto logger2 = std::make_shared<FakeScriptLogger>();
        auto tileCache2 = std::make_shared<FakeScriptTileCache>();
        ScriptVars vars2 = buildVars(point, params, downloader2, global2, logger2, tileCache2);
        auto created2 = UrlScript::create(source, c.relPath, *host2);
        ASSERT_TRUE(created2.has_value());
        auto result2 = (*created2)->buildRequest(vars2);
        ASSERT_TRUE(result2.has_value()) << c.relPath.toStdString() << " @ " << point.name
                                         << ": second run of a deterministic script failed";
        EXPECT_EQ(result->url, result2->url) << c.relPath.toStdString() << " @ " << point.name;
        EXPECT_EQ(result->requestHead, result2->requestHead)
            << c.relPath.toStdString() << " @ " << point.name;
        EXPECT_EQ(result->postData, result2->postData) << c.relPath.toStdString() << " @ " << point.name;
        EXPECT_EQ(host->randomCalls, 0)
            << c.relPath.toStdString() << " @ " << point.name << ": deterministic script called Random";
        EXPECT_EQ(host->unixTimeCalls, 0)
            << c.relPath.toStdString() << " @ " << point.name << ": deterministic script called GetUnixTime";
        EXPECT_EQ(downloader->calls().size(), 0)
            << c.relPath.toStdString() << " @ " << point.name << ": deterministic script called Downloader";
        EXPECT_EQ(host2->randomCalls, 0);
        EXPECT_EQ(host2->unixTimeCalls, 0);
        EXPECT_EQ(downloader2->calls().size(), 0);
    }
}

INSTANTIATE_TEST_SUITE_P(Zmp, CorpusRun, ::testing::ValuesIn(allCorpusCases()),
                         [](const ::testing::TestParamInfo<CorpusCase>& paramInfo) {
                             return sanitizeTestName(paramInfo.param.relPath).toStdString() + "_" +
                                    kPoints[static_cast<std::size_t>(paramInfo.param.pointIndex)].name;
                         });

// T-0321: the public tree has no catalogue, so the generator yields no case; that is not a failure.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CorpusRun);

TEST(Corpus, AtLeast350ScriptsFound) {
    CG_SKIP_IF_NO_CORPUS();
    // The blocking-gate canary corpus_parse_test.cpp already owns this assertion; repeated here
    // (cheaply - just a directory listing) so this file's own corpus size is self-checking even if
    // run in isolation (e.g. `--gtest_filter=Zmp/*`).
    EXPECT_GE(allScriptPaths().size(), 350) << "the ZMP script corpus fixture changed size";
}

// Review finding 3 / fix after the landing gate (T-0015E): a corpus_run_expected_errors.csv row
// that never actually fires its pinned text - because its (path,point) case now passes
// structurally, the error text changed, or the script/point no longer exists in the corpus - must
// fail a test, so the CSV cannot rot. This used to be a global ::testing::Environment whose
// TearDown() ran after every Zmp/CorpusRun case in the (single) test process and checked a
// process-wide "consumed" bitmap - correct only when every case shares one process. `ctest` (via
// gtest_discover_tests(), tests/CMakeLists.txt) instead runs the binary once PER CASE with
// --gtest_filter=<case>, so under that invocation style every per-case process saw every *other*
// row as "never consumed" and failed spuriously, even though each case was individually [ OK ].
// This ordinary TEST() is self-contained instead: it does not depend on any other test having run,
// or on how many other cases share its process. For each CSV row it independently drives the exact
// script at the exact tile point through the same UrlScript::create()/buildRequest() path the
// parameterized CorpusRun test uses, and asserts the run actually produces the row's pinned
// expected error/URL substring. A row whose script now passes cleanly, whose actual text differs,
// whose tile point name is unknown, or whose script file no longer exists in the corpus fails this
// test - the stale-row guarantee, now independent of test-process layout.
TEST(CorpusRunCsv, EveryRowFiresWithItsExpectedError) {
    CG_SKIP_IF_NO_CORPUS();
    const auto& rows = expectedErrorRows();
    ASSERT_FALSE(rows.isEmpty()) << "corpus_run_expected_errors.csv failed to load or is empty";

    for (const ExpectedRow& row : rows) {
        const TilePoint* point = nullptr;
        for (const TilePoint& candidate : kPoints) {
            if (row.point == QString::fromLatin1(candidate.name)) {
                point = &candidate;
                break;
            }
        }
        if (point == nullptr) {
            ADD_FAILURE() << "stale corpus_run_expected_errors.csv row: " << row.path.toStdString() << " @ "
                          << row.point.toStdString() << " - unknown tile point name";
            continue;
        }

        const QString absPath = QDir(zmpRoot()).filePath(row.path);
        if (!QFileInfo::exists(absPath)) {
            ADD_FAILURE() << "stale corpus_run_expected_errors.csv row: " << row.path.toStdString() << " @ "
                          << row.point.toStdString() << " - script no longer exists in the corpus";
            continue;
        }

        const QByteArray source = readRawBytes(absPath);
        const MapParams params = loadMapParams(absPath);

        auto host = std::make_unique<CountingScriptHost>();
        auto downloader = std::make_shared<FakeHttpDownloader>();
        auto global = std::make_shared<FakeScriptGlobal>();
        auto logger = std::make_shared<FakeScriptLogger>();
        auto tileCache = std::make_shared<FakeScriptTileCache>();
        ScriptVars vars = buildVars(*point, params, downloader, global, logger, tileCache);

        auto created = UrlScript::create(source, row.path, *host);
        if (!created.has_value()) {
            ADD_FAILURE() << "stale corpus_run_expected_errors.csv row: " << row.path.toStdString() << " @ "
                          << row.point.toStdString()
                          << " - script no longer compiles: " << created.error().what().toStdString();
            continue;
        }
        auto result = (*created)->buildRequest(vars);

        QString actualText;
        if (!result.has_value()) {
            actualText = result.error().what();
        } else if (result->tileExists && !isWellFormedUrl(result->url)) {
            actualText = QString::fromUtf8(result->url);
        } else {
            ADD_FAILURE() << "stale corpus_run_expected_errors.csv row: " << row.path.toStdString() << " @ "
                          << row.point.toStdString()
                          << " - the run now passes structurally (no error, well-formed/empty ResultURL); "
                          << "delete the row";
            continue;
        }

        EXPECT_TRUE(actualText.contains(row.expected))
            << row.path.toStdString() << " @ " << row.point.toStdString() << ": pinned expected text (\""
            << row.expected.toStdString() << "\") is not a substring of the actual text (\""
            << actualText.toStdString() << "\") - the row is stale or a regression occurred; fix the row "
            << "or the interpreter";
    }
}

// --- the two UrlScript cases moved here from T-0015C (need interface method dispatch, T-0015B) --

TEST(UrlScript, NestedHttpRequestUsesTheFakeHost) {
    // A script that calls Downloader.DoHttpRequest() from inside its own execution - i.e. nested
    // inside the buildRequest() call that is itself running the script - must reach the exact
    // FakeHttpDownloader bound into ScriptVars, and its 'out' response must flow back into the
    // script's own locals, which then feed ResultURL.
    FakeScriptHost host;
    auto downloader = std::make_shared<FakeHttpDownloader>();
    downloader->enqueueReply(FakeHttpDownloader::Reply{200, QByteArrayLiteral("h"), QByteArrayLiteral("ok")});

    auto created = UrlScript::create(
        QByteArrayLiteral("var Status: Integer; RH, RD: string;\n"
                          "begin\n"
                          "  Status := Downloader.DoHttpRequest('http://inner/', '', '', RH, RD);\n"
                          "  ResultURL := 'http://outer/?status=' + IntToStr(Status) + '&data=' + RD;\n"
                          "end."),
        QStringLiteral("test"), host);
    ASSERT_TRUE(created.has_value());

    ScriptVars vars;
    vars.downloader = downloader;
    auto result = (*created)->buildRequest(vars);
    ASSERT_TRUE(result.has_value()) << result.error().what().toStdString();
    EXPECT_EQ(result->url, QByteArrayLiteral("http://outer/?status=200&data=ok"));
    ASSERT_EQ(downloader->calls().size(), 1);
    EXPECT_EQ(downloader->calls().front().url, QByteArrayLiteral("http://inner/"));
}

TEST(UrlScript, GlobalStoreSharedBetweenTwoUrlScriptsOfTheSameMap) {
    // spec 5.6/8.1: IScriptGlobal is process-wide, shared across every download slot of a map (and
    // across maps) - two independent UrlScript instances (as maptype would build for two download
    // slots) that share one IScriptGlobal must see each other's writes.
    FakeScriptHost hostA;
    FakeScriptHost hostB;
    auto global = std::make_shared<FakeScriptGlobal>();

    auto slotA = UrlScript::create(
        QByteArrayLiteral("begin Global.SetVar(1, 'shared-value'); ResultURL := 'http://a/'; end."),
        QStringLiteral("test"), hostA);
    ASSERT_TRUE(slotA.has_value());
    auto slotB = UrlScript::create(QByteArrayLiteral("begin ResultURL := Global.GetVar(1); end."),
                                   QStringLiteral("test"), hostB);
    ASSERT_TRUE(slotB.has_value());

    ScriptVars varsA;
    varsA.global = global;
    auto resultA = (*slotA)->buildRequest(varsA);
    ASSERT_TRUE(resultA.has_value()) << resultA.error().what().toStdString();
    EXPECT_EQ(resultA->url, QByteArrayLiteral("http://a/"));

    ScriptVars varsB;
    varsB.global = global;
    auto resultB = (*slotB)->buildRequest(varsB);
    ASSERT_TRUE(resultB.has_value()) << resultB.error().what().toStdString();
    EXPECT_EQ(resultB->url, QByteArrayLiteral("shared-value"));
}
