// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-003 (regex surface, T-0014C2a). docs/specs/script.md:586-609, :1073-1078
// (F13/F14), :1225-1324 (§6, the R1-R16 TRegExpr->QRegularExpression rewrite table and the
// substitution-template rewrite).
#include "cg/script/regex_compat.h"

#include <gtest/gtest.h>

#include <vector>

using namespace cg::script;

namespace {

// --- R1-R16 pattern rewrite table (spec §6.2, script.md:1254-1271) -------------------------------
//
// Each row exercises the exact TRegExpr behaviour named in the "Decided behaviour" column, derived
// directly from Includes/RegExpr.pas (line ranges in each comment) rather than assumed. Every case
// asserts against `tRegExprMatch`/`compileTRegExpr` - what a real F13/F14 call observes.

struct RTableCase {
    const char* rule;
    QByteArray pattern;
    QByteArray subject;
    int matchId;
    bool expectOk;    // compileTRegExpr(pattern).ok
    bool expectMatch; // only checked when expectOk
    const char* expected;
};

class Regex_RTable : public ::testing::TestWithParam<RTableCase> {};

TEST_P(Regex_RTable, MatchesTRegExprSemantics) {
    const auto& c = GetParam();
    const CompiledRegex cre = compileTRegExpr(c.pattern);
    ASSERT_EQ(cre.ok, c.expectOk) << c.rule << ": " << cre.error.toStdString();
    if (!c.expectOk) {
        EXPECT_FALSE(cre.error.isEmpty()) << c.rule;
        return;
    }
    const QByteArray got = tRegExprMatch(c.subject, c.pattern, c.matchId);
    if (c.expectMatch) {
        EXPECT_EQ(got, QByteArray(c.expected)) << c.rule;
    } else {
        EXPECT_TRUE(got.isEmpty()) << c.rule << ": got '" << got.toStdString() << "'";
    }
}

INSTANTIATE_TEST_SUITE_P(
    Rules, Regex_RTable,
    ::testing::Values(
        // R1: ModifierS defaults ON (RegExpr.pas:151-156) - '.' crosses a newline.
        RTableCase{"R1", "a.b", "a\nb", 0, true, true, "a\nb"},
        // R2: '$' is TRegExpr's "true end of string" (RegExpr.pas:2854-2855, the EOL opcode) - it
        // must NOT allow a trailing '\n' the way PCRE2's bare '$' does.
        RTableCase{"R2", "c$", "c\n", 0, true, false, ""},
        // R3: an unknown escape's backslash is dropped, the letter is literal (RegExpr.pas:2286
        // UnQuoteChar default case) - the corpus-critical `1.\maps\.nlp` shape
        // (Here/here.com.terrain.zmp:4).
        RTableCase{"R3", "1.\\maps\\.nlp", "1Xmaps.nlp", 0, true, true, "1Xmaps.nlp"},
        // R4: `[\D]` inside a class is the literal char 'D' (RegExpr.pas:2397 class-parse branch
        // only special-cases lowercase d/w/s).
        RTableCase{"R4", "[\\D]+", "DDD5", 0, true, true, "DDD"},
        // R5: a '-' bordering a `\d`/`\w` shorthand is a literal dash in TRegExpr, not a range
        // (PCRE2 raises error 150 for `[a-\d]` otherwise).
        RTableCase{"R5", "[a-\\d]+", "a-5b", 0, true, true, "a-5"},
        // F3/R5: the range-end check (RegExpr.pas:2345-2349) treats '-' as literal next to any of
        // \d \D \s \S \w \W, not just \d/\w - \s (via EmitRangeStr) never sets CanBeRange either.
        RTableCase{"R5b", "[\\s-z]+", "-A", 0, true, true, "-"},
        RTableCase{"R5c", "[a-\\s]+", "a-b", 0, true, true, "a-"},
        RTableCase{"R5d", "[a-\\D]+", "a-D", 0, true, true, "a-D"},
        // R6: TRegExpr has no POSIX class syntax - the inner '[' of `[[:alpha:]]` is a literal
        // char, closing the class at the first real ']'; what follows is a literal ']' quantified
        // by '+'.
        RTableCase{"R6", "[[:alpha:]]+", "a]]]", 0, true, true, "a]]]"},
        // R7: ModifierR (always on) expands Cyrillic class ranges via a CP1251 table this port
        // does not vendor - compileTRegExpr must fail loudly, not silently mis-match.
        RTableCase{"R7", QByteArray("[") + char(0xE0) + "-" + char(0xFF) + "]", "", 0, false, false, ""},
        // R8: `(?:...)` was a TRegExpr compile error (RegExpr.pas:2426-2440); PCRE2 supports it - a
        // strict improvement (D-9), left working.
        RTableCase{"R8", "(?:abc)+", "abcabc", 0, true, true, "abcabc"},
        // R9: possessive quantifiers were a TRegExpr compile error (reeNestedSQP); left working.
        RTableCase{"R9", "a*+b", "aaab", 0, true, true, "aaab"},
        // R10: TRegExpr's backreference escape is always a single digit - `\10` is `\1` (backref)
        // followed by the literal char '0'.
        RTableCase{"R10", "(a)\\10", "aa0", 0, true, true, "aa0"},
        // R11: NSUBEXP = 15 capped TRegExpr at 14 real groups ("Too Many ()"); PCRE2 goes to
        // 65535 - left working (D-9).
        RTableCase{"R11", "(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)", "aaaaaaaaaaaaaaa", 0, true, true,
                   "aaaaaaaaaaaaaaa"},
        // R12: `{,3}` has no leading digit, so TRegExpr's brace-quantifier grammar never
        // recognises it as one - it stays literal text (PCRE2 10.43+ reads it as `{0,3}`).
        RTableCase{"R12", "a{,3}b", "a{,3}b", 0, true, true, "a{,3}b"},
        // R13: SpaceChars (RegExpr.pas:159-160) is ' '\t\n\r\f - no \v (0x0B) - unlike PCRE2's \s.
        RTableCase{"R13", "a\\sb", "a\x0Bb", 0, true, false, ""},
        // R14: WordChars is a fixed ASCII table (0-9A-Za-z_); UseUnicodePropertiesOption stays off
        // so a high byte never joins a `\w+` run.
        RTableCase{"R14", "\\w+", QByteArray("ab") + char(0xE0) + "cd", 0, true, true, "ab"},
        // R15: TRegExpr copies the subject into a NUL-terminated C buffer (RegExpr.pas:3603-3629) -
        // matching truncates at the first '\0'.
        RTableCase{"R15", "abc.*", QByteArray("abc") + '\0' + "def", 0, true, true, "abc"},
        // R16: an invalid pattern must fail loudly (`ok == false`), never silently no-match.
        RTableCase{"R16", "a(", "a(", 0, false, false, ""}));

// --- substitution templates (spec §6.3, script.md:1273-1296) -------------------------------------
//
// Derived directly from TRegExpr.Substitute (RegExpr.pas:3700-3813): ParseVarName's greedy
// multi-digit parse, the `${n}` disambiguation, and the "any other \\X drops the backslash" rule
// applied uniformly (Substitute() never special-cases \\n/\\t etc. - only $ substitution is
// special; everything else after a backslash in the TEMPLATE is emitted verbatim).

struct TemplateCase {
    const char* name;
    QByteArray pattern;
    QByteArray subject;
    QByteArray tmpl;
    QByteArray expected; // QByteArray, not const char*: the NUL-tail row's expected result embeds a
                         // literal 0x00 byte, which a C-string literal cannot represent.
};

class Regex_Substitution : public ::testing::TestWithParam<TemplateCase> {};

TEST_P(Regex_Substitution, MatchesTRegExprSubstitute) {
    const auto& c = GetParam();
    EXPECT_EQ(tRegExprReplace(c.subject, c.pattern, c.tmpl), c.expected) << c.name;
}

INSTANTIATE_TEST_SUITE_P(
    Templates, Regex_Substitution,
    ::testing::Values(TemplateCase{"WholeMatch_Dollar0", "(a)(b)", "Xab Y", "[$0]", "X[ab] Y"},
                      TemplateCase{"WholeMatch_DollarAmp", "(a)(b)", "Xab Y", "[$&]", "X[ab] Y"},
                      TemplateCase{"WholeMatch_BracedZero", "(a)(b)", "Xab Y", "[${0}]", "X[ab] Y"},
                      TemplateCase{"WholeMatch_BracedAmp", "(a)(b)", "Xab Y", "[${&}]", "X[ab] Y"},
                      TemplateCase{"MultiDigitGreedy_Dollar12", "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)",
                                   "abcdefghijkl", "$12", "l"},
                      TemplateCase{"BracedThenLiteralDigit", "(a)bc", "Xabc Y", "${1}2", "Xa2 Y"},
                      TemplateCase{"EscapedDollarAndBackslash", "a", "Xa Y", "\\$\\\\", "X$\\ Y"},
                      TemplateCase{"BackslashDropsForAnyChar", "a", "Xa Y", "\\n", "Xn Y"},
                      TemplateCase{"BareDollarNotAVarRefIsLiteral", "a", "Xa Y", "$x", "X$x Y"},
                      TemplateCase{"NonParticipatingGroupIsEmpty", "(a)|(b)", "a", "[$2]", "[]"},
                      TemplateCase{"GroupFifteenOrAboveIsAlwaysEmpty", "(a)", "a", "$15", ""},
                      TemplateCase{"EmptyTemplateClearsTheMatch", "abc", "Xabc Y", "", "X Y"},
                      // F2/R15 (RegExpr.pas:3845): TRegExpr.Replace copies the post-match tail from
                      // the ORIGINAL input, so bytes after an embedded NUL survive a match - the
                      // port must not truncate the tail at the NUL-truncation boundary.
                      TemplateCase{"NulTailSurvivesReplace", "a", QByteArray("ab") + '\0' + "cd", "X",
                                   QByteArray("Xb") + '\0' + "cd"}));

// --- Latin-1 byte<->QString shim (spec §6.4, script.md:1301-1307) --------------------------------
// A byte >= 0x80 must round-trip exactly through match and replace - `fromUtf8` would corrupt it
// (0xE9 alone is not a valid UTF-8 sequence and would become U+FFFD).

TEST(Regex_Latin1, HighByteRoundTripsThroughMatch) {
    const QByteArray subject = QByteArray("ab") + char(0xE9) + "cd";
    const QByteArray pattern(1, char(0xE9));
    EXPECT_EQ(tRegExprMatch(subject, pattern, 0), pattern);
}

TEST(Regex_Latin1, HighByteRoundTripsThroughReplace) {
    const QByteArray subject = QByteArray("ab") + char(0xE9) + "cd";
    const QByteArray pattern(1, char(0xE9));
    const QByteArray replacement(1, char(0xC0));
    const QByteArray expected = QByteArray("ab") + char(0xC0) + "cd";
    EXPECT_EQ(tRegExprReplace(subject, pattern, replacement), expected);
}

// --- 32-entry LRU (spec §2.9, script.md:606-609) --------------------------------------------------
// No accessor exposes the cache's internal state (by design - it is a hidden implementation
// detail of `compileTRegExpr`), so this proves the *observable* contract: pushing more than 32
// distinct patterns through evicts the oldest, and a since-evicted pattern still compiles and
// matches correctly when asked for again (a stale/aliased cache entry would be the failure mode).

TEST(Regex_Lru, EvictionDoesNotBreakReCompilation) {
    constexpr int kPatternCount = 40; // > the 32-entry capacity
    std::vector<QByteArray> patterns;
    patterns.reserve(kPatternCount);
    for (int i = 0; i < kPatternCount; ++i) {
        patterns.push_back(QByteArrayLiteral("pat") + QByteArray::number(i));
    }
    for (const QByteArray& p : patterns) {
        EXPECT_EQ(tRegExprMatch(p, p, 0), p) << p.toStdString();
    }
    // pattern 0 was evicted long ago (only the last 32 of 40 insertions survive) - ask for it
    // again and confirm it still compiles and matches, not a corrupted/aliased cache slot.
    EXPECT_EQ(tRegExprMatch(patterns[0], patterns[0], 0), patterns[0]);
    EXPECT_EQ(tRegExprMatch(patterns[39], patterns[39], 0), patterns[39]);
}

// --- real corpus patterns (spec §6.2/§6.5, script.md:1258, :1310-1319) ---------------------------
//
// The .zmp map corpus lives in this repo at tests/fixtures/zmp/ (92 scripts, 98 RegExpr* call
// sites: 87 Replace, 11 Get; verified: grep -rhoE "RegExpr(GetMatchSubStr|ReplaceMatchSubStr)" on
// tests/fixtures/zmp piped through wc -l gives 98). This table covers all 15 distinct literal
// patterns found there (the YaTraf script's Get and Replace calls as two separate rows); see
// docs/handoffs/T-0014C2A.md for the full note. `replacement.isNull()` rows go through
// `tRegExprMatch` (Get); the rest go through `tRegExprReplace` (Replace, F14).

struct CorpusCase {
    const char* script;
    QByteArray pattern;
    QByteArray replacement; // null -> tRegExprMatch via matchId; else -> tRegExprReplace
    QByteArray subject;
    int matchId;
    const char* expected;
};

class Regex_CorpusCritical : public ::testing::TestWithParam<CorpusCase> {};

TEST_P(Regex_CorpusCritical, MatchesAsTRegExprWould) {
    const auto& c = GetParam();
    if (c.replacement.isNull()) {
        EXPECT_EQ(tRegExprMatch(c.subject, c.pattern, c.matchId), QByteArray(c.expected)) << c.script;
    } else {
        EXPECT_EQ(tRegExprReplace(c.subject, c.pattern, c.replacement), QByteArray(c.expected)) << c.script;
    }
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, Regex_CorpusCritical,
    ::testing::Values(
        // Here/here.com.terrain.zmp:4, _broken_maps/Nokia/here.com.hybrid.zmp:4 - PCRE2 refuses to
        // compile `1.\maps\.nlp` outright without R3; TRegExpr reads `\m` as literal 'm'.
        CorpusCase{"here.com.terrain.zmp", "1.\\maps\\.nlp", QByteArray(), "server1.maps.nlp/x", 0,
                   "1.maps.nlp"},
        // The 4 Navionics maps' `configuration_token` extraction - without R1
        // (DotMatchesEverythingOption) a newline inside the JSON body truncates the greedy `.*`.
        CorpusCase{"Navionics configuration_token", "\"configuration_token\":\"(.*)\"", QByteArray(),
                   "{\"configuration_token\":\"AB\nCD\"}", 1, "AB\nCD"},
        // The 4 Navionics maps' `access_token` extraction (note the trailing comma in the literal
        // pattern - part of the JSON syntax the pattern anchors on).
        CorpusCase{"Navionics access_token", "\"access_token\":\"(.*)\",", QByteArray(),
                   "{\"access_token\":\"XY\nZ\",\"other\":1}", 1, "XY\nZ"},
        // 42 calls - the itoworld.com tile-server relay rewrite.
        CorpusCase{"t.beta.itoworld.com relay (42 calls)", "t\\.beta\\.itoworld\\.com",
                   "t3.beta.itoworld.com", "http://t.beta.itoworld.com/2/k/{z}/{x}/{y}.png", 0,
                   "http://t3.beta.itoworld.com/2/k/{z}/{x}/{y}.png"},
        // 28 calls - the melda.ru relay rewrite.
        CorpusCase{"91.237.82.95:8088 -> melda.ru (28 calls)", "http://91.237.82.95:8088",
                   "https://maps.melda.ru", "http://91.237.82.95:8088/pub/genshtab/z", 0,
                   "https://maps.melda.ru/pub/genshtab/z"},
        // 4 calls - the Nokia/Ovi maptile.lbs relay rewrite.
        CorpusCase{"maptile.lbs relay (4 calls)", "maptile\\.lbs", "2.maptile.lbs",
                   "http://maptile.lbs.ovi.com/maptiler/v2", 0, "http://2.maptile.lbs.ovi.com/maptiler/v2"},
        // 3 calls - the Google mt.google relay rewrite.
        CorpusCase{"mt.google relay (3 calls)", "mt\\.google", "mt2.google", "http://mt.google.com/vt/lyrs=s",
                   0, "http://mt2.google.com/vt/lyrs=s"},
        // u_RegularExpressions_Test.TestRegExprReplaceMatchSubStrSimple, cExample2 (T-0040): multi-line
        // CRLF subject; only the numeric `<testKey=1578>` matches, `<testKey=asdfad>` is untouched.
        CorpusCase{"DUnit cExample2 testKey (CRLF)", "<testKey=(\\d+)>", "<testKey=8751>",
                   "<test1=100>\r\n<test2=asdf>\r\n<testKey=asdfad>\r\n<testKey=1578>\r\n", 0,
                   "<test1=100>\r\n<test2=asdf>\r\n<testKey=asdfad>\r\n<testKey=8751>\r\n"},
        // 2 calls - literal `{s}` subdomain-placeholder substitution (literal in both engines).
        CorpusCase{"{s} subdomain substitution (2 calls)", "{s}", "2", "http://{s}.tile.example/{z}", 0,
                   "http://2.tile.example/{z}"},
        // 1 call - the Nokia/HERE tile.maps relay rewrite (R3, `1.\maps\.nlp` sibling pattern).
        CorpusCase{"tile.maps relay (1 call)", "tile.maps", "tile1.maps", "http://tile.maps.example/x", 0,
                   "http://tile1.maps.example/x"},
        // 1 call - the zeskmi.ru relay rewrite.
        CorpusCase{"91.237.82.95:8088/pub -> map.zeskmi.ru (1 call)", "http://91.237.82.95:8088/pub",
                   "http://map.zeskmi.ru", "http://91.237.82.95:8088/pub/x", 0, "http://map.zeskmi.ru/x"},
        // 1 call - the camapka.ru/lotsia relay rewrite.
        CorpusCase{"gmap.camapka.ru/lotsia relay (1 call)", "gmap.camapka.ru/lotsia/",
                   "91.237.82.95:8088/pub/water/camapka.ru/", "http://gmap.camapka.ru/lotsia/1/2", 0,
                   "http://91.237.82.95:8088/pub/water/camapka.ru/1/2"},
        // 1 call - the caucasia.ru/ggc relay rewrite.
        CorpusCase{"91.237.82.95:8088/pub/ggc relay (1 call)", "91.237.82.95:8088/pub/ggc",
                   "t.caucasia.ru:80", "http://91.237.82.95:8088/pub/ggc/1", 0, "http://t.caucasia.ru:80/1"},
        // 1 call - the `//4` subdomain-number relay rewrite.
        CorpusCase{"//4 subdomain relay (1 call)", "//4", "//2", "http://4.example.com/a", 0,
                   "http://2.example.com/a"},
        // 1 call - JSON `version` extraction (Get).
        CorpusCase{"version extraction (1 call)", "\"version\":\"(\\d+)\"", QByteArray(),
                   "var x={\"version\":\"1234\",\"b\":\"5\"}", 1, "1234"},
        // Yandex/YaTraf.zmp - the `ytrf_lts` counter rewrite (Replace).
        CorpusCase{"YaTraf.zmp ytrf_lts (Replace)", "<ytrf_lts=(\\d+)>", "<ytrf_lts=1700000000>",
                   "<ytrf_ts=0>_<ytrf_lts=0>", 0, "<ytrf_ts=0>_<ytrf_lts=1700000000>"},
        // Yandex/YaTraf.zmp - the `ytrf_ts` counter extraction (Get).
        CorpusCase{"YaTraf.zmp ytrf_ts (Get)", "<ytrf_ts=(\\d+)>", QByteArray(), "<ytrf_ts=17>_<ytrf_lts=0>",
                   1, "17"}));

} // namespace
