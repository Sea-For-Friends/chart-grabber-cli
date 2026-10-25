// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/ini_file.h"

#include "cg/core/ini_value_codec.h"

#include <QFile>
#include <QSaveFile>

#include <cstdint>
#include <iterator>
#include <utility>

namespace cg::core {
namespace {

[[nodiscard]] bool sameIdent(QStringView left, QStringView right) {
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

} // namespace

struct IniFile::Line {
    enum class Kind : std::uint8_t { Blank, Comment, SectionHeader, KeyValue, Orphan };

    QString raw;    // the physical line, without its terminator
    QByteArray eol; // this line's own terminator; empty for an unterminated last line
    Kind kind = Kind::Blank;
    QString section;          // owning section name ("" before the first header)
    qsizetype sectionId = -1; // index of the owning header line, -1 before the first header
    QString key;
    QString value;
};

IniFile::IniFile() = default;
IniFile::~IniFile() = default;
IniFile::IniFile(const IniFile& other) = default;
IniFile& IniFile::operator=(const IniFile& other) = default;
IniFile::IniFile(IniFile&& other) noexcept = default;
IniFile& IniFile::operator=(IniFile&& other) noexcept = default;

Result<IniFile> IniFile::fromBytes(const QByteArray& raw) {
    IniFile file;
    const DecodedText decoded = decodeConfigText(raw);
    file.encoding_ = decoded.encoding;

    const QString& text = decoded.text;
    qsizetype pos = 0;
    bool eolSeen = false;
    while (pos < text.size()) {
        const qsizetype start = pos;
        while (pos < text.size() && text.at(pos) != u'\n' && text.at(pos) != u'\r') {
            ++pos;
        }
        QString rawLine = text.sliced(start, pos - start);
        QByteArray eol;
        if (pos < text.size()) {
            if (text.at(pos) == u'\r' && pos + 1 < text.size() && text.at(pos + 1) == u'\n') {
                eol = "\r\n";
                pos += 2;
            } else {
                eol = text.at(pos) == u'\r' ? "\r" : "\n";
                pos += 1;
            }
            if (!eolSeen) {
                file.eol_ = eol;
                eolSeen = true;
            }
        }
        Line line;
        line.raw = std::move(rawLine);
        line.eol = std::move(eol);
        file.lines_.push_back(std::move(line));
    }
    file.rebuildIndex();
    return file;
}

Result<IniFile> IniFile::fromFile(const QString& path) {
    QFile input(path);
    if (!input.exists()) {
        IniFile file;
        file.sourcePath_ = path;
        return file; // a missing settings file is an empty one, not an error
    }
    if (!input.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::IoError, QStringLiteral("cannot open %1: %2").arg(path, input.errorString()));
    }
    Result<IniFile> parsed = fromBytes(input.readAll());
    if (parsed) {
        parsed->sourcePath_ = path;
    }
    return parsed;
}

void IniFile::setSourcePath(QString path) {
    sourcePath_ = std::move(path);
}

const QString& IniFile::sourcePath() const noexcept {
    return sourcePath_;
}

void IniFile::rebuildIndex() {
    qsizetype currentSection = -1;
    for (qsizetype i = 0; i < std::ssize(lines_); ++i) {
        Line& line = lines_[static_cast<std::size_t>(i)];
        const QString trimmed = line.raw.trimmed();
        line.key.clear();
        line.value.clear();
        if (trimmed.isEmpty()) {
            line.kind = Line::Kind::Blank;
        } else if (trimmed.startsWith(u';')) {
            line.kind = Line::Kind::Comment; // Q2: ';' only, '#' starts a key
        } else if (trimmed.size() >= 2 && trimmed.front() == u'[' && trimmed.back() == u']') {
            line.kind = Line::Kind::SectionHeader; // Q3
            currentSection = i;
            line.section = trimmed.sliced(1, trimmed.size() - 2).trimmed();
            line.sectionId = i;
            continue;
        } else {
            const qsizetype eq = trimmed.indexOf(u'='); // Q4: first '=' only
            if (eq >= 0 && currentSection >= 0) {
                line.kind = Line::Kind::KeyValue;
                line.key = trimmed.first(eq).trimmed();        // Q5
                line.value = trimmed.sliced(eq + 1).trimmed(); // Q5, Q6 (quotes kept)
            } else {
                line.kind = Line::Kind::Orphan; // Q7 and Q8
            }
        }
        line.sectionId = currentSection;
        line.section =
            currentSection >= 0 ? lines_[static_cast<std::size_t>(currentSection)].section : QString();
    }
}

qsizetype IniFile::firstSectionHeader(QStringView section) const {
    for (qsizetype i = 0; i < std::ssize(lines_); ++i) {
        const Line& line = lines_[static_cast<std::size_t>(i)];
        if (line.kind == Line::Kind::SectionHeader && sameIdent(line.section, section)) {
            return i; // Q10: the first header wins
        }
    }
    return -1;
}

qsizetype IniFile::findKeyLine(QStringView section, QStringView key) const {
    const qsizetype header = firstSectionHeader(section);
    if (header < 0) {
        return -1;
    }
    for (qsizetype i = header + 1; i < std::ssize(lines_); ++i) {
        const Line& line = lines_[static_cast<std::size_t>(i)];
        if (line.kind == Line::Kind::SectionHeader) {
            break;
        }
        if (line.kind == Line::Kind::KeyValue && sameIdent(line.key, key)) {
            return i; // Q9: the first occurrence wins
        }
    }
    return -1;
}

bool IniFile::hasSection(QStringView section) const {
    return firstSectionHeader(section) >= 0;
}

QStringList IniFile::sectionNames() const {
    QStringList names;
    for (const Line& line : lines_) {
        if (line.kind == Line::Kind::SectionHeader) {
            names.append(line.section);
        }
    }
    return names;
}

QStringList IniFile::keyNames(QStringView section) const {
    QStringList names;
    const qsizetype header = firstSectionHeader(section);
    if (header < 0) {
        return names;
    }
    for (qsizetype i = header + 1; i < std::ssize(lines_); ++i) {
        const Line& line = lines_[static_cast<std::size_t>(i)];
        if (line.kind == Line::Kind::SectionHeader) {
            break;
        }
        if (line.kind == Line::Kind::KeyValue) {
            names.append(line.key);
        }
    }
    return names;
}

bool IniFile::hasKey(QStringView section, QStringView key) const {
    return findKeyLine(section, key) >= 0;
}

QString IniFile::readString(QStringView section, QStringView key, const QString& def) const {
    const qsizetype index = findKeyLine(section, key);
    return index < 0 ? def : lines_[static_cast<std::size_t>(index)].value; // Q12
}

qint32 IniFile::readInt(QStringView section, QStringView key, qint32 def) const {
    return valuecodec::parseInt(readString(section, key, QString()), def);
}

bool IniFile::readBool(QStringView section, QStringView key, bool def) const {
    // Q14: ReadBool is ReadInteger(..., Ord(Default)) <> 0; "True"/"yes" do not parse.
    return readInt(section, key, def ? 1 : 0) != 0;
}

double IniFile::readFloat(QStringView section, QStringView key, double def) const {
    return valuecodec::parseFloat(readString(section, key, QString()), def);
}

QDate IniFile::readDate(QStringView section, QStringView key, QDate def) const {
    const QString text = readString(section, key, QString());
    return text.isEmpty() ? def : valuecodec::parseDate(text, def);
}

QTime IniFile::readTime(QStringView section, QStringView key, QTime def) const {
    const QString text = readString(section, key, QString());
    return text.isEmpty() ? def : valuecodec::parseTime(text, def);
}

QDateTime IniFile::readDateTime(QStringView section, QStringView key, const QDateTime& def) const {
    return valuecodec::parseDateTime(readString(section, key, QString()), def);
}

QByteArray IniFile::readBinary(QStringView section, QStringView key) const {
    const QString text = readString(section, key, QString());
    if (text.isEmpty() || (text.size() % 2) != 0 || !valuecodec::isHexDigits(text)) {
        return {}; // Q21
    }
    return QByteArray::fromHex(text.toLatin1());
}

void IniFile::setValue(QStringView section, QStringView key, const QString& value) {
    const qsizetype existing = findKeyLine(section, key);
    if (existing >= 0) {
        Line& line = lines_[static_cast<std::size_t>(existing)];
        const QString rendered = line.key + QLatin1Char('=') + value; // Q11: keep the stored key casing
        if (line.raw != rendered) {
            line.raw = rendered;
            line.value = value;
            dirty_ = true;
        }
        return;
    }

    qsizetype header = firstSectionHeader(section);
    if (header < 0) {
        // Q23: a new section goes to the end of the file, after one blank separator line.
        if (!lines_.empty()) {
            if (lines_.back().eol.isEmpty()) {
                lines_.back().eol = eol_;
            }
            if (lines_.back().kind != Line::Kind::Blank) {
                Line blank;
                blank.eol = eol_;
                lines_.push_back(std::move(blank));
            }
        }
        Line head;
        head.raw = QLatin1Char('[') + section.toString() + QLatin1Char(']');
        head.eol = eol_;
        lines_.push_back(std::move(head));
        header = std::ssize(lines_) - 1;
    }

    // Q23: append after the section's last non-blank line.
    qsizetype insertAt = header + 1;
    for (qsizetype i = header + 1; i < std::ssize(lines_); ++i) {
        if (lines_[static_cast<std::size_t>(i)].kind == Line::Kind::SectionHeader) {
            break;
        }
        if (lines_[static_cast<std::size_t>(i)].kind != Line::Kind::Blank) {
            insertAt = i + 1;
        }
    }
    if (insertAt > 0 && lines_[static_cast<std::size_t>(insertAt - 1)].eol.isEmpty()) {
        lines_[static_cast<std::size_t>(insertAt - 1)].eol = eol_;
    }
    Line line;
    line.raw = key.toString() + QLatin1Char('=') + value;
    line.eol = eol_;
    lines_.insert(lines_.begin() + insertAt, std::move(line));
    dirty_ = true;
    rebuildIndex();
}

void IniFile::writeString(QStringView section, QStringView key, const QString& value) {
    setValue(section, key, value);
}

void IniFile::writeInt(QStringView section, QStringView key, qint32 value) {
    setValue(section, key, QString::number(value));
}

void IniFile::writeBool(QStringView section, QStringView key, bool value) {
    setValue(section, key, value ? QStringLiteral("1") : QStringLiteral("0")); // Q15
}

void IniFile::writeFloat(QStringView section, QStringView key, double value) {
    setValue(section, key, valuecodec::formatFloat(value)); // Q17
}

void IniFile::writeDate(QStringView section, QStringView key, QDate value) {
    setValue(section, key, valuecodec::formatDate(value)); // Q18
}

void IniFile::writeTime(QStringView section, QStringView key, QTime value) {
    setValue(section, key, valuecodec::formatTime(value)); // Q18
}

void IniFile::writeDateTime(QStringView section, QStringView key, const QDateTime& value) {
    setValue(section, key, valuecodec::formatDateTime(value)); // Q18
}

void IniFile::writeBinary(QStringView section, QStringView key, const QByteArray& value) {
    setValue(section, key, QString::fromLatin1(value.toHex().toUpper())); // Q21
}

void IniFile::eraseSection(QStringView section) {
    const qsizetype header = firstSectionHeader(section);
    if (header < 0) {
        return;
    }
    qsizetype end = header + 1;
    while (end < std::ssize(lines_) &&
           lines_[static_cast<std::size_t>(end)].kind != Line::Kind::SectionHeader) {
        ++end;
    }
    lines_.erase(lines_.begin() + header, lines_.begin() + end);
    dirty_ = true;
    rebuildIndex();
}

void IniFile::deleteKey(QStringView section, QStringView key) {
    const qsizetype header = firstSectionHeader(section);
    if (header < 0) {
        return;
    }
    bool removed = false;
    for (qsizetype i = std::ssize(lines_) - 1; i > header; --i) {
        const Line& line = lines_[static_cast<std::size_t>(i)];
        if (line.sectionId != header) {
            continue;
        }
        if (line.kind == Line::Kind::KeyValue && sameIdent(line.key, key)) {
            lines_.erase(lines_.begin() + i); // D-2: every duplicate goes
            removed = true;
        }
    }
    if (removed) {
        dirty_ = true;
        rebuildIndex();
    }
}

bool IniFile::isDirty() const noexcept {
    return dirty_;
}

QString IniFile::renderedText() const {
    QString text;
    for (const Line& line : lines_) {
        text += line.raw;
        text += QString::fromLatin1(line.eol);
    }
    return text;
}

QByteArray IniFile::toBytes() const {
    return encodeConfigText(renderedText(), encoding_);
}

Result<void> IniFile::save() {
    if (sourcePath_.isEmpty()) {
        return fail(ErrorCode::IoError, QStringLiteral("IniFile::save() without a source path"));
    }
    return saveAs(sourcePath_);
}

Result<void> IniFile::saveAs(const QString& path) {
    // Spec §4 "nothing is ever swallowed": a value that cannot round-trip through the file's own
    // codec (only possible for Cp1251) must fail loudly, before anything touches disk, rather
    // than writing a '?'-corrupted file (finding 2, T-0002A rework).
    const Result<QByteArray> encoded = encodeConfigTextChecked(renderedText(), encoding_);
    if (!encoded.has_value()) {
        return fail(encoded.error().code(),
                    QStringLiteral("cannot encode %1: %2").arg(path, encoded.error().what()));
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return fail(ErrorCode::IoError,
                    QStringLiteral("cannot write %1: %2").arg(path, output.errorString()));
    }
    const QByteArray& bytes = encoded.value();
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        return fail(ErrorCode::IoError,
                    QStringLiteral("cannot write %1: %2").arg(path, output.errorString()));
    }
    dirty_ = false;
    return {};
}

} // namespace cg::core
