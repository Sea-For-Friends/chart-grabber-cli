// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_XmlStreamingWriter.pas
//
// Hand-ported writer, not QXmlStreamWriter (spec vector.md §2.13, Q-8): the indentation, the
// empty-tag form and the escape tables define the bytes of the KML/GPX/TCX exports.
// Bytes are always UTF-8 without BOM: the three legacy exporters pass TUTF8Encoding.Create(False)
// (u_ExportMarks2KML.pas:144, u_ExportMarks2GPX.pas:382, u_ExportMarks2TCX.pas:170). The legacy
// 4 MB TStreamWriter buffer is not ported: each write goes to the device at once, and the device
// stays owned by the caller (it is not flushed or closed here).

#include <QIODevice>
#include <QString>

#include <vector>

namespace cg::vector {

class XmlStreamingWriter final { // TXmlStreamingWriter
public:
    // The writer does not own the device; it must outlive the writer.
    explicit XmlStreamingWriter(QIODevice& device);
    // Ends the document (closes the open elements) unless endDocument() has been called.
    ~XmlStreamingWriter();

    XmlStreamingWriter(const XmlStreamingWriter&) = delete;
    XmlStreamingWriter& operator=(const XmlStreamingWriter&) = delete;

    void startDocument();
    void endDocument();

    void startElement(const QString& name);
    void endElement(bool writeIndent = true);

    // Throws std::logic_error when no start tag is open (legacy EXmlStreamingWriter).
    void writeAttribute(const QString& name, const QString& value);
    void writeString(const QString& text);
    void writeElementString(const QString& name, const QString& value);
    void writeRaw(const QString& text);

    // Properties (legacy defaults: "1.0", "utf-8", empty standalone, two spaces, LF). The constructor never
    // assigns FStandAlone (u_XmlStreamingWriter.pas:105-108), so CStandAloneDefault = 'yes' (:36) is unused.
    // `encoding` is only the text of the prolog; the bytes are UTF-8 whatever it says (as the legacy callers
    // use it).
    void setVersion(const QString& version);
    void setEncoding(const QString& encoding);
    void setStandAlone(const QString& standAlone);
    void setIndentChars(const QString& indentChars);
    void setLineBreak(const QString& lineBreak);

    [[nodiscard]] QString version() const;
    [[nodiscard]] QString encoding() const;
    [[nodiscard]] QString standAlone() const;
    [[nodiscard]] QString indentChars() const;
    [[nodiscard]] QString lineBreak() const;

private:
    void put(const QString& text);
    void indent();
    void closeStartTag(bool withNewLine);

    QIODevice& device_;

    QString version_ = QStringLiteral("1.0");
    QString encoding_ = QStringLiteral("utf-8");
    QString standAlone_; // empty: no standalone attribute in the prolog (:162-164)
    QString indentChars_ = QStringLiteral("  ");
    QString lineBreak_ = QStringLiteral("\n");

    std::vector<QString> openTags_; // TStringStack
    int indentLevel_ = 0;
    bool isEmptyTag_ = false; // a start tag is open and its '>' is not written yet
    bool isDocumentEnded_ = false;
};

} // namespace cg::vector
