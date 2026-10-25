// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/xml_streaming_writer.h"

#include "cg/vector/xml_escape.h"

#include <QIODevice>

#include <stdexcept>

namespace cg::vector {

// u_XmlStreamingWriter.pas:98-128 (Create / Destroy).
XmlStreamingWriter::XmlStreamingWriter(QIODevice& device) : device_(device) {}

XmlStreamingWriter::~XmlStreamingWriter() {
    if (!isDocumentEnded_) {
        endDocument();
    }
}

// :130-133 (InternalWrite). The writer does not raise on a failed write (legacy raises EWriteError on
// flush): the exporter checks the device instead (proposed D row).
void XmlStreamingWriter::put(const QString& text) {
    static_cast<void>(device_.write(text.toUtf8()));
}

// :135-144 (InternalWriteIndent): FIndentChars once per level, nothing when it is empty.
void XmlStreamingWriter::indent() {
    if (!indentChars_.isEmpty()) {
        for (int i = 1; i <= indentLevel_; ++i) {
            put(indentChars_);
        }
    }
}

// :146-155 (InternalCloseTag): writes the '>' of an open start tag.
void XmlStreamingWriter::closeStartTag(bool withNewLine) {
    if (isEmptyTag_) {
        isEmptyTag_ = false;
        put(QStringLiteral(">"));
        if (withNewLine) {
            put(lineBreak_);
        }
    }
}

// :157-166 (StartDocument). The standalone attribute is left out when the property is empty.
void XmlStreamingWriter::startDocument() {
    QString prolog = QStringLiteral("<?xml version=\"") + version_ + QStringLiteral("\" encoding=\"") +
                     encoding_ + QStringLiteral("\"");
    if (!standAlone_.isEmpty()) {
        prolog += QStringLiteral(" standalone=\"") + standAlone_ + QStringLiteral("\"");
    }
    put(prolog + QStringLiteral("?>") + lineBreak_);
}

// :168-175 (EndDocument): closes every open element, then marks the document as ended.
void XmlStreamingWriter::endDocument() {
    while (!openTags_.empty()) {
        endElement();
    }
    isDocumentEnded_ = true;
}

// :177-185 (StartElement).
void XmlStreamingWriter::startElement(const QString& name) {
    closeStartTag(true);
    indent();
    put(QStringLiteral("<") + name);
    openTags_.push_back(name);
    isEmptyTag_ = true;
    ++indentLevel_;
}

// :187-204 (EndElement). Without an open element it does nothing.
void XmlStreamingWriter::endElement(bool writeIndent) {
    if (openTags_.empty()) {
        return;
    }

    --indentLevel_;
    if (isEmptyTag_) {
        isEmptyTag_ = false;
        put(QStringLiteral("/>") + lineBreak_);
        openTags_.pop_back();
    } else {
        if (writeIndent) {
            indent();
        }
        put(QStringLiteral("</") + openTags_.back() + QStringLiteral(">") + lineBreak_);
        openTags_.pop_back();
    }
}

// :206-212 (WriteAttribute): only while the start tag is still open.
void XmlStreamingWriter::writeAttribute(const QString& name, const QString& value) {
    if (!isEmptyTag_) {
        throw std::logic_error("Cannot write attribute outside of an element start.");
    }
    put(QStringLiteral(" ") + name + QStringLiteral("=\"") + xmlEscapeAttr(value) + QStringLiteral("\""));
}

// :214-218 (WriteRaw): no escaping.
void XmlStreamingWriter::writeRaw(const QString& text) {
    closeStartTag(false);
    put(text);
}

// :220-224 (WriteString).
void XmlStreamingWriter::writeString(const QString& text) {
    closeStartTag(false);
    put(xmlEscapeText(text));
}

// :226-231 (WriteElementString): an empty value gives <name></name>, not the empty-tag form.
void XmlStreamingWriter::writeElementString(const QString& name, const QString& value) {
    startElement(name);
    writeString(value);
    endElement(false);
}

void XmlStreamingWriter::setVersion(const QString& version) {
    version_ = version;
}

void XmlStreamingWriter::setEncoding(const QString& encoding) {
    encoding_ = encoding;
}

void XmlStreamingWriter::setStandAlone(const QString& standAlone) {
    standAlone_ = standAlone;
}

void XmlStreamingWriter::setIndentChars(const QString& indentChars) {
    indentChars_ = indentChars;
}

void XmlStreamingWriter::setLineBreak(const QString& lineBreak) {
    lineBreak_ = lineBreak;
}

QString XmlStreamingWriter::version() const {
    return version_;
}

QString XmlStreamingWriter::encoding() const {
    return encoding_;
}

QString XmlStreamingWriter::standAlone() const {
    return standAlone_;
}

QString XmlStreamingWriter::indentChars() const {
    return indentChars_;
}

QString XmlStreamingWriter::lineBreak() const {
    return lineBreak_;
}

} // namespace cg::vector
