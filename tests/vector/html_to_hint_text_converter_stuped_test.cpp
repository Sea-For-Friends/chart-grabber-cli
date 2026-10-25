// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037 (mark hint text). Ports u_HtmlToHintTextConverterStuped.pas;
// docs/specs/vector.md §2.2.2, §7.2, §8.1 LV-12. Expected values are worked out by hand from the
// legacy statements (no legacy DUnit exists).
#include "cg/vector/html_to_hint_text_converter_stuped.h"

#include <gtest/gtest.h>

using cg::vector::HtmlToHintTextConverterStuped;
using cg::vector::stupedHtmlToTextConverter;

namespace {

QString q(const char16_t* s) {
    return QString::fromUtf16(s);
}

QString convert(const QString& name, const QString& description) {
    return HtmlToHintTextConverterStuped().convert(name, description);
}

struct Row {
    const char16_t* in;
    const char16_t* out;
};

TEST(StupedHtmlToTextConverter, EveryEntityOfTheLegacyListInItsOrder) {
    const Row rows[] = {
        {u"&#36;", u"$"},     {u"&#37;", u"%"},   {u"&#39;", u"'"},
        {u"&amp;", u"&"},     {u"&#38;", u"&"},   {u"&quot;", u"\""},
        {u"&#34;", u"\""},    {u"&lt;", u"<"},    {u"&#60;", u"<"},
        {u"&gt;", u">"},      {u"&#62;", u">"},   {u"&nbsp;", u" "},
        {u"&#160;", u" "},    {u"&raquo;", u"»"}, {u"&#187;", u"»"},
        {u"&laquo;", u"«"},   {u"&copy;", u"©"},  {u"&#169;", u"©"},
        {u"&reg;", u"®"},     {u"&#174;", u"®"},  {u"a&amp;b&amp;c", u"a&b&c"}, // replace-all
        {u"&AMP;", u"&AMP;"},                                                   // case-sensitive
        {u"&amp;lt;", u"<"}, // &amp; is replaced before &lt;
    };
    for (const Row& r : rows) {
        EXPECT_EQ(stupedHtmlToTextConverter(q(r.in)), q(r.out)) << QString(q(r.in)).toStdString();
    }
}

// LV-12 (L-124): "&#171" has no ';' in legacy, so the ';' of "&#171;" stays.
TEST(StupedHtmlToTextConverter, LaquoNumericEntityLeavesTheSemicolon) {
    EXPECT_EQ(stupedHtmlToTextConverter(q(u"&#171;")), q(u"«;"));
    EXPECT_EQ(stupedHtmlToTextConverter(q(u"&#171")), q(u"«"));
}

TEST(HtmlToHintTextConverterStuped, NameAndDescriptionAreJoinedByCrLfAndTrimmed) {
    EXPECT_EQ(convert(q(u"Name"), q(u"Desc")), q(u"Name\r\nDesc"));
    EXPECT_EQ(convert(q(u"Name"), {}), q(u"Name"));
    EXPECT_EQ(convert({}, q(u"Desc")), q(u"Desc"));
    EXPECT_EQ(convert({}, {}), QString());
    EXPECT_EQ(convert(q(u"\t A"), q(u"B \n\x01")), q(u"A\r\nB")); // Trim: every char <= ' '
}

TEST(HtmlToHintTextConverterStuped, NameFoundInDescriptionIsNotShown) {
    EXPECT_EQ(convert(q(u"Name"), q(u"The Name here")), q(u"The Name here"));
    EXPECT_EQ(convert(q(u"name"), q(u"The Name here")), q(u"name\r\nThe Name here")); // Pos is case-sensitive
}

TEST(HtmlToHintTextConverterStuped, MarkupIsConvertedOnlyWhenThereIsALessThanSign) {
    EXPECT_EQ(convert(q(u"<b>T</b>"), q(u"x &amp; y")), q(u"T\r\nx &amp; y"));
    EXPECT_EQ(convert({}, q(u"x &amp; <i>y</i>")), q(u"x & y"));
}

TEST(HtmlToHintTextConverterStuped, Html2TxtRulesInTheirOrder) {
    const Row rows[] = {
        // sas.cut: text after the marker is dropped (case-insensitive), "-->" is appended
        {u"abc<p>x<!-- SAS.CUT -->tail", u"abcx-->"},
        // <body ...>: only the body content, case-insensitive
        {u"<html><head><title>t</title></head><BODY bgcolor=1>Hi<b>there</b></BODY></html>", u"Hithere"},
        // body without '>' after it keeps the whole text; without </body> to the end
        {u"<body", u"<body"},
        // runs of blanks collapse to one
        {u"a   <i>b</i>    c", u"a b c"},
        // br, br /, div (case-insensitive)
        {u"a<BR>b<br />c</DIV>d", u"a\r\nb\r\nc\r\n\r\nd"},
        // lower-case <style> block removed; upper-case is not recognised, only its tags go
        {u"<style>p{}</style>x", u"x"},
        {u"<STYLE>p{}</STYLE>x", u"p{}x"},
        // UpperCase folds ASCII only: U+017F is not 'S', so "<STYLE" is found at the second block
        {u"<\u017ftyle>a</\u017ftyle><style>b</style>x", u"ax"},
        // an unclosed '<' stops the tag loop
        {u"a<b", u"a<b"},
        // entities after the tags; the accented ones become a literal '?'
        {u"<p>caf&eacute; &Uuml; &uuml;</p>", u"caf? &Uuml; ?"},
        {u"<b>&amp;lt;&nbsp;&copy;</b>", u"< ©"},
    };
    for (const Row& r : rows) {
        EXPECT_EQ(convert({}, q(r.in)), q(r.out)) << QString(q(r.in)).toStdString();
    }
}

TEST(HtmlToHintTextConverterStuped, LongLinesWrapAtTheFirstBlankPastColumn40) {
    const QString g = QStringLiteral("0123456789");
    const QString in = g + " " + g + " " + g + " " + g + " " + g + " " + g;
    // blanks sit at 11, 22, 33, 44; the one at 44 is the first with j > 40
    EXPECT_EQ(convert({}, in), g + " " + g + " " + g + " " + g + "\r\n" + g + " " + g);
    // not when 5 or fewer characters remain
    const QString tail = QString(45, u'x') + QStringLiteral(" abcde");
    EXPECT_EQ(convert({}, tail), tail);
}

TEST(HtmlToHintTextConverterStuped, PastColumn500TheTextIsCutWithEllipsis) {
    const QString in = QString(510, u'x') + QStringLiteral(" ") + QString(10, u'y');
    EXPECT_EQ(convert({}, in), QString(510, u'x') + QStringLiteral("..."));
}

} // namespace
