// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_XmlStreamingWriter.pas; docs/specs/vector.md §2.13 (no legacy DUnit; the expected bytes are
// worked out by hand from the unit, line numbers in the comments). No FEAT id: enabler of the exporters.
#include "cg/vector/xml_streaming_writer.h"

#include <QBuffer>
#include <QIODevice>

#include <gtest/gtest.h>

#include <stdexcept>

using cg::vector::XmlStreamingWriter;

namespace {

// Runs `body` on a writer over an in-memory device, ends the document as the legacy callers do,
// and returns the bytes written.
template <typename Body> QByteArray render(Body body) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    XmlStreamingWriter writer(buf);
    body(writer);
    writer.endDocument();
    return buf.data();
}

constexpr const char* kProlog = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

} // namespace

// :105-108 and :162-164: the constructor assigns no StandAlone (CStandAloneDefault 'yes', :36, is never
// used), so the default prolog has no standalone attribute. Version 1.0, encoding utf-8 and LF are the
// defaults too.
TEST(XmlStreamingWriter, StartDocumentDefaults) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) { w.startDocument(); }), QByteArray(kProlog));
}

// :162-163: an explicit standalone value is written.
TEST(XmlStreamingWriter, StartDocumentWithStandAlone) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.setStandAlone(QStringLiteral("yes"));
                  w.startDocument();
              }),
              QByteArray("<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n"));
}

// The version, standalone and line-break properties feed the prolog.
TEST(XmlStreamingWriter, PropertiesChangeTheProlog) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.setVersion(QStringLiteral("1.1"));
                  w.setStandAlone(QStringLiteral("no"));
                  w.setLineBreak(QStringLiteral("\r\n"));
                  w.startDocument();
              }),
              QByteArray("<?xml version=\"1.1\" encoding=\"utf-8\" standalone=\"no\"?>\r\n"));
}

// :148-150 and :194-197: a childless element is written in the empty-tag form.
TEST(XmlStreamingWriter, EmptyElementUsesTheEmptyTagForm) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("a"));
                  w.endElement();
              }),
              QByteArray("<a/>\n"));
}

// :177-185, :187-204 and :226-231 on a nested document: indent two spaces per level.
TEST(XmlStreamingWriter, NestedElementsAreIndented) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("kml"));
                  w.startElement(QStringLiteral("Document"));
                  w.writeElementString(QStringLiteral("name"), QStringLiteral("Roads"));
                  w.endElement();
                  w.endElement();
              }),
              QByteArray("<kml>\n  <Document>\n    <name>Roads</name>\n  </Document>\n</kml>\n"));
}

// An empty child inside a non-empty parent: the parent's '>' comes with its line break.
TEST(XmlStreamingWriter, EmptyChildInsideParent) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("Document"));
                  w.startElement(QStringLiteral("Placemark"));
                  w.endElement();
                  w.endElement();
              }),
              QByteArray("<Document>\n  <Placemark/>\n</Document>\n"));
}

// :220-224: WriteString closes the start tag without a line break and escapes with numeric references
// (the quotes are not special in text).
TEST(XmlStreamingWriter, WriteStringEscapesNumeric) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("a"));
                  w.writeString(QStringLiteral("A < B & B > A \"q\" 'x'"));
                  w.endElement();
              }),
              QByteArray("<a>A &#60; B &#38; B &#62; A \"q\" 'x'</a>\n"));
}

// U+0001 is dropped; the non-ASCII letters are written as their UTF-8 bytes (no BOM).
TEST(XmlStreamingWriter, WriteStringDropsIllegalAndWritesUtf8) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("a"));
                  w.writeString(QString::fromUtf16(u"Город\x01!"));
                  w.endElement();
              }),
              QByteArray("<a>\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4!</a>\n"));
}

// Non-BMP code point: one UTF-16 surrogate pair in the string, four UTF-8 bytes on the device (U+1F600).
TEST(XmlStreamingWriter, WriteStringNonBmpIsFourUtf8Bytes) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("a"));
                  w.writeString(QString::fromUtf16(u"\U0001F600"));
                  w.endElement();
              }),
              QByteArray("<a>\xF0\x9F\x98\x80</a>\n"));
}

// :206-212 (WriteAttribute): numeric references for the attribute table, quotes included.
TEST(XmlStreamingWriter, WriteAttributeEscapesNumeric) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("Placemark"));
                  w.writeAttribute(QStringLiteral("id"), QStringLiteral("x\"Y&'"));
                  w.endElement();
              }),
              QByteArray("<Placemark id=\"x&#34;Y&#38;&#39;\"/>\n"));
}

// Non-ASCII attribute value: written raw, as UTF-8.
TEST(XmlStreamingWriter, WriteAttributeNonAsciiIsRaw) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("a"));
                  w.writeAttribute(QStringLiteral("name"), QString::fromUtf16(u"Город"));
                  w.endElement();
              }),
              QByteArray("<a name=\"\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4\"/>\n"));
}

// :206-212 in call order: attributes are written as they are added, not sorted.
TEST(XmlStreamingWriter, TwoAttributesKeepCallOrder) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("Placemark"));
                  w.writeAttribute(QStringLiteral("b"), QStringLiteral("2"));
                  w.writeAttribute(QStringLiteral("a"), QStringLiteral("1"));
                  w.endElement();
              }),
              QByteArray("<Placemark b=\"2\" a=\"1\"/>\n"));
}

// :208-210: an attribute after content (the start tag is closed) raises.
TEST(XmlStreamingWriter, WriteAttributeAfterContentThrows) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    XmlStreamingWriter writer(buf);
    writer.startElement(QStringLiteral("a"));
    writer.writeString(QStringLiteral("t"));
    EXPECT_THROW(writer.writeAttribute(QStringLiteral("x"), QStringLiteral("y")), std::logic_error);
}

// :214-218 (WriteRaw): no escaping, the start tag is closed without a line break.
TEST(XmlStreamingWriter, WriteRawIsNotEscaped) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("a"));
                  w.writeRaw(QStringLiteral("<b/>&"));
                  w.endElement();
              }),
              QByteArray("<a><b/>&</a>\n"));
}

// :226-231: an empty value still gives an open and a close tag.
TEST(XmlStreamingWriter, WriteElementStringWithEmptyValue) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("Document"));
                  w.writeElementString(QStringLiteral("name"), QString());
                  w.endElement();
              }),
              QByteArray("<Document>\n  <name></name>\n</Document>\n"));
}

// :187-204 with AWriteIndent = False: the closing tag of a non-empty element gets no indent.
TEST(XmlStreamingWriter, EndElementWithoutIndentWritesColumnZeroClose) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.startElement(QStringLiteral("a"));
                  w.startElement(QStringLiteral("b"));
                  w.writeRaw(QStringLiteral("x"));
                  w.endElement(false);
                  w.endElement();
              }),
              QByteArray("<a>\n  <b>x</b>\n</a>\n"));
}

// :189-190: EndElement with no open element writes nothing.
TEST(XmlStreamingWriter, EndElementOnEmptyStackWritesNothing) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) { w.endElement(); }), QByteArray());
}

// :135-144 with an empty FIndentChars: no indentation at all.
TEST(XmlStreamingWriter, EmptyIndentCharsDisablesIndentation) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.setIndentChars(QString());
                  w.startElement(QStringLiteral("a"));
                  w.startElement(QStringLiteral("b"));
                  w.endElement();
                  w.endElement();
              }),
              QByteArray("<a>\n<b/>\n</a>\n"));
}

// Custom indent and line break are used by the start-tag and end-tag writers alike.
TEST(XmlStreamingWriter, CustomIndentAndLineBreak) {
    EXPECT_EQ(render([](XmlStreamingWriter& w) {
                  w.setIndentChars(QStringLiteral("\t"));
                  w.setLineBreak(QStringLiteral("\r\n"));
                  w.startElement(QStringLiteral("a"));
                  w.startElement(QStringLiteral("b"));
                  w.endElement();
                  w.endElement();
              }),
              QByteArray("<a>\r\n\t<b/>\r\n</a>\r\n"));
}

// :168-175 (EndDocument): every open element is closed, innermost first.
TEST(XmlStreamingWriter, DestructorEndsOpenDocument) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    {
        XmlStreamingWriter writer(buf);
        writer.startDocument();
        writer.startElement(QStringLiteral("a"));
        writer.startElement(QStringLiteral("b"));
    }
    EXPECT_EQ(buf.data(), QByteArray(kProlog) + QByteArray("<a>\n  <b/>\n</a>\n"));
}

// :122-124 and :174: after EndDocument the destructor writes nothing more. The element opened after
// it keeps its start tag open (no '>' is written), which only happens when the destructor skips.
TEST(XmlStreamingWriter, DestructorDoesNothingAfterExplicitEndDocument) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    {
        XmlStreamingWriter writer(buf);
        writer.startElement(QStringLiteral("a"));
        writer.endDocument();
        writer.startElement(QStringLiteral("b"));
    }
    EXPECT_EQ(buf.data(), QByteArray("<a/>\n<b"));
}
