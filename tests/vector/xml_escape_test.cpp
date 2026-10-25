// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_XmlEscape.pas; docs/specs/vector.md §2.13, §7.1, §8.1 LV-8. The seven DUnit methods of
// Test/u_XmlEscape_Test.pas are rows of the tables below (each row names its DUnit method) and the
// TEST blocks that follow. No FEAT id: enabler of the exporters. Expected strings are the literal DUnit
// values or worked out from the legacy tables. The legacy code was not run; the NUL rows are read from
// u_XmlEscape.pas:126-211 and are not run.
#include "cg/vector/xml_escape.h"

#include <gtest/gtest.h>

using cg::vector::xmlEscapeAttr;
using cg::vector::xmlEscapeText;

namespace {

struct Row {
    const char16_t* in;
    const char16_t* out;
};

// One shape, data-driven. TestEmptyString: the empty rows. TestNoSpecialChars: "Hello World! 123".
// TestTextNamed: "A < B & B > A". TestAttrNamed: the quote row is also checked by the text variant.
// TestIllegalCharsStripped: the "Bad..." row (named and numeric).
const Row kTextNamed[] = {
    {u"", u""},
    {u"Hello World! 123", u"Hello World! 123"},
    {u"A < B & B > A", u"A &lt; B &amp; B &gt; A"},
    {u"Node \"Name\" & 'Value'", u"Node \"Name\" &amp; 'Value'"},
    {u"Bad\x01\x02"
     u"Chars\x09"
     u"Tab",
     u"BadChars&#x09;Tab"},
    {u"\U0001F600", u"\U0001F600"}, // non-BMP: a surrogate pair, not touched
    {u"a\nb\rc", u"a&#x0a;b&#x0d;c"},
    {u"x\x0b"
     u"y\x1f"
     u"z\x0e",
     u"xyz"},
    {u"\x7f Г", u"\x7f Г"},
    {u"Гор & од < г", u"Гор &amp; од &lt; г"},
};

const Row kTextNumeric[] = {
    {u"A < B & B > A", u"A &#60; B &#38; B &#62; A"},
    {u"Node \"Name\" & 'Value'", u"Node \"Name\" &#38; 'Value'"},
    {u"Bad\x01\x02"
     u"Chars\x09"
     u"Tab",
     u"BadChars&#09;Tab"},
    {u"a\nb\rc", u"a&#10;b&#13;c"},
    {u"x\x0b"
     u"y\x1f"
     u"z\x0e",
     u"xyz"},
    {u"Г > г", u"Г &#62; г"},
    {u"", u""},
};

const Row kAttrNamed[] = {
    {u"", u""},
    {u"Node \"Name\" & 'Value'", u"Node &quot;Name&quot; &amp; &apos;Value&apos;"},
    {u"a\nb", u"a&#x0a;b"},
    {u"Гор & \"д\"", u"Гор &amp; &quot;д&quot;"},
    {u"Bad\x01"
     u"x",
     u"Badx"},
};

const Row kAttrNumeric[] = {
    {u"Node \"Name\" & 'Value'", u"Node &#34;Name&#34; &#38; &#39;Value&#39;"},
    {u"'", u"&#39;"},
    {u"<Г>", u"&#60;Г&#62;"},
    {u"", u""},
};

} // namespace

// Legacy TestNoSpecialChars: the input is returned unchanged.
TEST(XmlEscape, NoSpecialCharsUnchanged) {
    const QString cStr = QString::fromUtf16(u"Hello World! 123");
    EXPECT_EQ(xmlEscapeText(cStr), cStr);
    EXPECT_EQ(xmlEscapeAttr(cStr), cStr);
    EXPECT_EQ(xmlEscapeAttr(cStr, false), cStr);
}

// Legacy TestTextNamed (and the table rows of the named text variant).
TEST(XmlEscape, TextNamedTable) {
    for (const Row& row : kTextNamed) {
        EXPECT_EQ(xmlEscapeText(QString::fromUtf16(row.in), false), QString::fromUtf16(row.out));
    }
}

// Legacy TestTextNumeric (and the table rows of the numeric text variant).
TEST(XmlEscape, TextNumericTable) {
    for (const Row& row : kTextNumeric) {
        EXPECT_EQ(xmlEscapeText(QString::fromUtf16(row.in), true), QString::fromUtf16(row.out));
    }
}

// Legacy TestAttrNamed: the attribute variant escapes quotes, the text variant does not.
TEST(XmlEscape, AttrNamedTable) {
    for (const Row& row : kAttrNamed) {
        EXPECT_EQ(xmlEscapeAttr(QString::fromUtf16(row.in), false), QString::fromUtf16(row.out));
    }
    EXPECT_EQ(xmlEscapeText(QStringLiteral("Node \"Name\" & 'Value'"), false),
              QStringLiteral("Node \"Name\" &amp; 'Value'"));
}

// Legacy TestAttrNumeric.
TEST(XmlEscape, AttrNumericTable) {
    for (const Row& row : kAttrNumeric) {
        EXPECT_EQ(xmlEscapeAttr(QString::fromUtf16(row.in), true), QString::fromUtf16(row.out));
    }
}

// Legacy LV-8, read from :126-211: U+0000 ends the scan. The text is cut at the NUL when an escape
// was written before it (the output then stops there).
TEST(XmlEscape, NulEndsTheTextAfterAnEscape) {
    const QString input = QString::fromUtf16(u"a<\0b", 4);
    EXPECT_EQ(xmlEscapeText(input, false), QStringLiteral("a&lt;"));
    EXPECT_EQ(xmlEscapeText(input, true), QStringLiteral("a&#60;"));
}

// Legacy LV-8, read from :171-173: when nothing is escaped before the NUL the input is returned as
// it is, the part after the NUL included.
TEST(XmlEscape, NulWithoutEscapeReturnsInputUnchanged) {
    const QString input = QString::fromUtf16(u"a\0<b", 4);
    EXPECT_EQ(xmlEscapeText(input, false), input);
    EXPECT_EQ(xmlEscapeAttr(input, true), input);
}

// An illegal character counts as a modification (:166), so the NUL after it cuts the text.
TEST(XmlEscape, IllegalBeforeNulCutsTheText) {
    const QString input = QString::fromUtf16(u"\x01\0<", 3);
    EXPECT_EQ(xmlEscapeText(input, false), QString());
}
