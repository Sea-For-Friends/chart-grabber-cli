// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Delphi System.IniFiles.TMemIniFile as used by u_ConfigDataProviderByIniFile.pas and
//        u_ConfigDataWriteProviderByIniFile.pas
//
// The INI model behind every legacy settings file and every .zmp params.txt. It is line-oriented
// on purpose (ADR-0010): reading and rewriting a file the user has not changed must reproduce the
// original bytes, comments, blank lines and key order included, which TMemIniFile does not do.
// Reads reproduce the Delphi parsing quirks exactly (spec core.md 2.12.2, Q1-Q24).

#include "cg/core/result.h"
#include "cg/core/text_codec.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTime>
#include <QtGlobal>

#include <vector>

namespace cg::core {

// FEAT-UI-200
class IniFile {
public:
    IniFile();
    ~IniFile();
    IniFile(const IniFile& other);
    IniFile& operator=(const IniFile& other);
    IniFile(IniFile&& other) noexcept;
    IniFile& operator=(IniFile&& other) noexcept;

    // --- loading ------------------------------------------------------------
    [[nodiscard]] static Result<IniFile> fromBytes(const QByteArray& raw);
    [[nodiscard]] static Result<IniFile> fromFile(const QString& path); // missing file => empty
    void setSourcePath(QString path);
    [[nodiscard]] const QString& sourcePath() const noexcept;

    // --- reading (all identifiers compared case-insensitively, ASCII) --------
    [[nodiscard]] bool hasSection(QStringView section) const;
    [[nodiscard]] QStringList sectionNames() const;                // file order, duplicates kept
    [[nodiscard]] QStringList keyNames(QStringView section) const; // file order, first section wins

    [[nodiscard]] bool hasKey(QStringView section, QStringView key) const;
    [[nodiscard]] QString readString(QStringView section, QStringView key,
                                     const QString& def = QString()) const;
    [[nodiscard]] qint32 readInt(QStringView section, QStringView key, qint32 def) const;
    [[nodiscard]] bool readBool(QStringView section, QStringView key, bool def) const;
    [[nodiscard]] double readFloat(QStringView section, QStringView key, double def) const;
    [[nodiscard]] QDate readDate(QStringView section, QStringView key, QDate def) const;
    [[nodiscard]] QTime readTime(QStringView section, QStringView key, QTime def) const;
    [[nodiscard]] QDateTime readDateTime(QStringView section, QStringView key, const QDateTime& def) const;
    [[nodiscard]] QByteArray readBinary(QStringView section, QStringView key) const;

    // --- writing ------------------------------------------------------------
    void writeString(QStringView section, QStringView key, const QString& value);
    void writeInt(QStringView section, QStringView key, qint32 value);
    void writeBool(QStringView section, QStringView key, bool value);
    void writeFloat(QStringView section, QStringView key, double value);
    void writeDate(QStringView section, QStringView key, QDate value);
    void writeTime(QStringView section, QStringView key, QTime value);
    void writeDateTime(QStringView section, QStringView key, const QDateTime& value);
    void writeBinary(QStringView section, QStringView key, const QByteArray& value);

    void eraseSection(QStringView section);
    void deleteKey(QStringView section, QStringView key);

    [[nodiscard]] bool isDirty() const noexcept;

    // --- serialising --------------------------------------------------------
    // Byte-identical to the loaded bytes when nothing was written (ADR-0010).
    [[nodiscard]] QByteArray toBytes() const;
    [[nodiscard]] Result<void> save(); // to sourcePath(), atomic rename
    [[nodiscard]] Result<void> saveAs(const QString& path);

private:
    // Line-oriented model: every physical line is retained verbatim in `lines_`, with its own
    // terminator, so mixed CRLF/LF files round-trip. Section/key lookup is a scan over it.
    struct Line;

    void rebuildIndex();
    [[nodiscard]] qsizetype firstSectionHeader(QStringView section) const;
    [[nodiscard]] qsizetype findKeyLine(QStringView section, QStringView key) const;
    void setValue(QStringView section, QStringView key, const QString& value);
    [[nodiscard]] QString renderedText() const;

    std::vector<Line> lines_;
    QString sourcePath_;
    bool dirty_ = false;
    TextEncoding encoding_ = TextEncoding::Utf8Bom;
    QByteArray eol_ = "\r\n";
};

} // namespace cg::core
