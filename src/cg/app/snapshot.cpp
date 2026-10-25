// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). Inherits: the fetch-once pattern of the stock Google/GoogleSat.zmp script, with
// the result fed into the map version (Src/MapVersion/u_MapVersionRequestConfig.pas).
// docs/specs/weather-sources.md §2, §3.2, §3.3, §3.4, §6.
#include "cg/app/snapshot.h"

#include "cg/core/platform/qt_compat.h"
#include "cg/maptype/map_type.h"
#include "cg/maptype/zmp_info.h"

#include <QCoreApplication>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(Snapshot)
};

constexpr int kMaxVersionLength = 200;

core::Unexpected parseFail(QString text) {
    return core::fail(core::ErrorCode::ParseError, std::move(text));
}

// §2: map.zmp()->dataProvider()->subItem(params.txt)->subItem(PARAMS); a null provider on the way = key
// absent.
QString readKey(const maptype::IMapType& map, QStringView key) {
    const auto zmp = map.zmp();
    const core::IConfigProviderPtr data = zmp ? zmp->dataProvider() : nullptr;
    const core::IConfigProviderPtr paramsFile = data ? data->subItem(u"params.txt") : nullptr;
    const core::IConfigProviderPtr params = paramsFile ? paramsFile->subItem(u"PARAMS") : nullptr;
    return params ? params->readString(key, {}).trimmed() : QString();
}

bool isHttpUrl(const QString& text) {
    if (!text.startsWith(u"http://", Qt::CaseInsensitive) &&
        !text.startsWith(u"https://", Qt::CaseInsensitive)) {
        return false;
    }
    return QUrl(text, QUrl::StrictMode).isValid();
}

// §3.3 version rule.
core::Result<void> checkVersion(const QString& version) {
    bool bad = version.isEmpty() || version.size() > kMaxVersionLength;
    for (const QChar c : version) {
        bad = bad || c.unicode() < 0x20;
    }
    if (bad) {
        return parseFail(Tr::tr("the version \"%1\" cannot be used").arg(version));
    }
    return {};
}

// "workspace:name" -> "name" (everything up to and including the first ':').
QString withoutWorkspace(const QString& name) {
    const qsizetype colon = name.indexOf(QLatin1Char(':'));
    return colon < 0 ? name : name.mid(colon + 1);
}

core::Result<Snapshot> nearestToNow(const QString& item, const QString& values, const QDateTime& now);
core::Result<Snapshot> withFrameList(const SnapshotDefinition& def, const QString& values,
                                     core::Result<Snapshot> chosen);

// §3.2 json-last.
core::Result<Snapshot> parseJsonLast(const SnapshotDefinition& def, const QByteArray& body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        return parseFail(Tr::tr("not a JSON object"));
    }
    const QStringList steps = def.item.split(QLatin1Char('.'));
    QJsonValue node = doc.object();
    for (qsizetype i = 0; i < steps.size(); ++i) {
        const bool last = (i + 1 == steps.size());
        const QJsonValue next = node.toObject().value(steps[i]);
        if (last ? !next.isArray() : !next.isObject()) {
            return parseFail(Tr::tr("no array at %1").arg(def.item));
        }
        node = next;
    }
    const QJsonArray array = node.toArray();
    if (array.isEmpty()) {
        return parseFail(Tr::tr("the array at %1 is empty").arg(def.item));
    }
    if (def.previous && array.size() < 2) { // T-0356
        return parseFail(Tr::tr("the array at %1 has no entry before the last").arg(def.item));
    }
    const QJsonValue lastEntry =
        array.at(array.size() - (def.previous ? 2 : 1)); // T-0356: previous = one back
    if (!lastEntry.isObject()) {
        return parseFail(Tr::tr("the last entry of %1 has no usable %2").arg(def.item, def.timeField));
    }
    const QJsonObject entry = lastEntry.toObject();
    const QString versionField = def.versionField.isEmpty() ? def.timeField : def.versionField;

    Snapshot snap;
    snap.newest = !def.previous; // T-0356: the last entry is the newest frame
    const QJsonValue timeValue = entry.value(def.timeField);
    if (timeValue.isString()) {
        auto parsed = snapshotFromText(QStringLiteral("x"), timeValue.toString());
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        snap.time = parsed->time;
        snap.dateOnly = parsed->dateOnly;
    } else {
        const double seconds = timeValue.toDouble(-1);
        // Unix seconds: integral, > 0, below year 10000.
        if (!timeValue.isDouble() || seconds != std::floor(seconds) || seconds <= 0 ||
            seconds > 253402300799.0) {
            return parseFail(Tr::tr("the last entry of %1 has no usable %2").arg(def.item, def.timeField));
        }
        snap.time = core::platform::utcFromSecsSinceEpoch(static_cast<qint64>(seconds));
    }

    const QJsonValue versionValue = entry.value(versionField);
    if (versionValue.isString()) {
        snap.version = versionValue.toString();
    } else {
        const double number = versionValue.toDouble(0.5);
        if (!versionValue.isDouble() || number != std::floor(number) || std::fabs(number) > 9.0e15) {
            return parseFail(Tr::tr("the last entry of %1 has no usable %2").arg(def.item, versionField));
        }
        snap.version = QString::number(static_cast<qint64>(number));
    }
    if (auto ok = checkVersion(snap.version); !ok) {
        return std::unexpected(ok.error());
    }
    return snap;
}

struct WmsLayer {
    qsizetype depth = 0;
    QString name;
    bool hasName = false;
    bool hasTimeDefault = false;
    QString timeDefault;
    QString timeValues; // T-0337: the text of that Dimension (list and/or start/end/step)
};

// §3.2 wms-default.
core::Result<Snapshot> parseWmsDefault(const SnapshotDefinition& def, const QByteArray& body) {
    QXmlStreamReader xml(body);
    const auto notCapabilities = [&xml]() {
        QString text = Tr::tr("not a WMS 1.3.0 capabilities document");
        if (xml.hasError()) {
            text += QStringLiteral(" (%1)").arg(xml.errorString());
        }
        return parseFail(std::move(text));
    };
    while (!xml.atEnd() && !xml.isStartElement()) {
        xml.readNext();
    }
    if (!xml.isStartElement() || xml.name() != u"WMS_Capabilities") {
        return notCapabilities();
    }

    std::vector<WmsLayer> layers; // document order
    std::vector<qsizetype> open;  // indexes into layers of the Layer elements still open
    qsizetype depth = 1;          // the root
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            const auto name = xml.name();
            if (name == u"Layer") {
                layers.emplace_back();
                layers.back().depth = depth;
                open.push_back(static_cast<qsizetype>(layers.size()) - 1);
                continue;
            }
            const bool directChild =
                !open.empty() && layers[static_cast<size_t>(open.back())].depth == depth - 1;
            if (!directChild) {
                continue;
            }
            WmsLayer& layer = layers[static_cast<size_t>(open.back())];
            if (name == u"Name") {
                if (!layer.hasName) {
                    layer.name = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                    layer.hasName = true;
                } else {
                    xml.readElementText(QXmlStreamReader::SkipChildElements);
                }
                --depth; // readElementText consumed the end tag
            } else if (name == u"Dimension" && !layer.hasTimeDefault &&
                       xml.attributes().value(QStringLiteral("name")).compare(u"time", Qt::CaseInsensitive) ==
                           0 &&
                       xml.attributes().hasAttribute(QStringLiteral("default"))) {
                layer.timeDefault = xml.attributes().value(QStringLiteral("default")).toString().trimmed();
                layer.hasTimeDefault = true;
                layer.timeValues = xml.readElementText(QXmlStreamReader::SkipChildElements);
                --depth; // readElementText consumed the end tag
            }
        } else if (xml.isEndElement()) {
            if (!open.empty() && layers[static_cast<size_t>(open.back())].depth == depth) {
                open.pop_back();
            }
            --depth;
        }
    }

    for (const WmsLayer& layer : layers) {
        if (!layer.hasName) {
            continue;
        }
        const bool match = layer.name == def.item || withoutWorkspace(layer.name) == def.item ||
                           layer.name == withoutWorkspace(def.item);
        if (!match) {
            continue;
        }
        if (!layer.hasTimeDefault || layer.timeDefault.isEmpty()) {
            if (xml.hasError()) {
                return notCapabilities();
            }
            return parseFail(Tr::tr("layer %1 has no default time").arg(def.item));
        }
        if (def.pick == SnapshotPick::NearestNow) {
            return withFrameList(def, layer.timeValues, nearestToNow(def.item, layer.timeValues, def.now));
        }
        return withFrameList(def, layer.timeValues, snapshotFromText(layer.timeDefault, layer.timeDefault));
    }
    if (xml.hasError()) {
        return notCapabilities();
    }
    return parseFail(Tr::tr("no layer %1").arg(def.item));
}

struct WmtsLayer {
    qsizetype depth = 0;
    QString name;
    bool hasName = false;
    bool hasTime = false; // a direct Dimension with Identifier time was seen
    QString timeDefault;
    QString timeRange;  // the first Value with a start/end/step form; empty = none
    QString timeValues; // T-0337: every Value, joined with ','
};

// The Dimension element just started: its direct children Identifier, Default and Value; deeper elements are
// skipped. Consumes the end tag.
void readWmtsDimension(QXmlStreamReader& xml, WmtsLayer& layer) {
    QString id;
    QString def;
    QString range;
    QStringList values;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement()) {
            break;
        }
        if (!xml.isStartElement()) {
            continue;
        }
        const auto name = xml.name();
        if (name == u"Identifier" && id.isEmpty()) {
            id = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        } else if (name == u"Default" && def.isEmpty()) {
            def = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        } else if (name == u"Value") {
            const QString value = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            values.append(value);
            if (range.isEmpty() && value.count(QLatin1Char('/')) == 2) {
                range = value;
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    // A second time Dimension cannot occur (WMTS Dimension identifiers are unique per layer): first wins,
    // untested.
    if (!layer.hasTime && id.compare(u"time", Qt::CaseInsensitive) == 0) {
        layer.hasTime = true;
        layer.timeDefault = def;
        layer.timeRange = range;
        layer.timeValues = values.join(QLatin1Char(','));
    }
}

QString timeIsoZ(const QDateTime& time) {
    return time.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
}

// "PT10800S", "PT3H", "P1D" ...: seconds, 0 = not a duration.
qint64 durationSeconds(const QString& text) {
    static const QRegularExpression re(
        QStringLiteral(R"(^P(?:(\d{1,6})D)?(?:T(?:(\d{1,6})H)?(?:(\d{1,8})M)?(?:(\d{1,10})S)?)?$)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return 0;
    }
    return m.captured(1).toLongLong() * 86400 + m.captured(2).toLongLong() * 3600 +
           m.captured(3).toLongLong() * 60 + m.captured(4).toLongLong();
}

QString stepText(qint64 seconds) {
    return seconds % 3600 == 0 ? QStringLiteral("%1 h").arg(seconds / 3600)
                               : QStringLiteral("%1 s").arg(seconds);
}

// The frame the user asked for (--time) inside the layer's "start/end/step" Value; the default time when
// none.
core::Result<Snapshot> chooseWmtsTime(const SnapshotDefinition& def, const WmtsLayer& layer) {
    if (!def.time) {
        if (def.pick == SnapshotPick::NearestNow) {
            return withFrameList(def, layer.timeValues, nearestToNow(def.item, layer.timeValues, def.now));
        }
        return withFrameList(def, layer.timeValues, snapshotFromText(layer.timeDefault, layer.timeDefault));
    }
    const QStringList fields = layer.timeRange.split(QLatin1Char('/'));
    const qint64 step = fields.size() == 3 ? durationSeconds(fields[2].trimmed()) : 0;
    const QDateTime start =
        fields.size() == 3 ? QDateTime::fromString(fields[0].trimmed(), Qt::ISODateWithMs) : QDateTime();
    const QDateTime end =
        fields.size() == 3 ? QDateTime::fromString(fields[1].trimmed(), Qt::ISODateWithMs) : QDateTime();
    if (step <= 0 || !start.isValid() || !end.isValid()) {
        // A list of instants (or an unusable interval) is no range to choose from: a refusal like the others
        return core::fail(core::ErrorCode::Unsupported,
                          Tr::tr("--time: \"%1\" has no time range to choose from").arg(def.sourceName));
    }
    QDateTime wanted;
    QString valueText;
    if (def.time->relative) {
        const QDateTime now = def.now.isValid() ? def.now : QDateTime::currentDateTimeUtc();
        valueText = QStringLiteral("+%1h").arg(def.time->hours);
        // Rounded down to the step, counted from the start of the range (floor, also before the start).
        const qint64 ahead = start.secsTo(now) + def.time->hours * 3600;
        const qint64 whole = ahead >= 0 ? ahead / step : -((-ahead + step - 1) / step);
        wanted = start.addSecs(whole * step);
    } else {
        wanted = def.time->instant;
        valueText = timeIsoZ(wanted);
    }
    const qint64 offset = start.secsTo(wanted);
    if (wanted < start || wanted > end || offset % step != 0) {
        return core::fail(
            core::ErrorCode::Unsupported,
            Tr::tr("--time %1 is not a time of \"%2\" (range %3..%4, step %5)")
                .arg(valueText, def.sourceName, timeIsoZ(start), timeIsoZ(end), stepText(step)));
    }
    const QString text = timeIsoZ(wanted);
    return snapshotFromText(text, text);
}

// T-0337: the frame of a time dimension nearest to `now` (invalid = the clock). `values` is the text of a
// WMS Dimension or the Values of a WMTS one: parts separated by ',', each an instant or "start/end/step"
// (ISO 8601 duration). Distance in whole seconds, a tie goes to the earlier frame. The frame must not lie
// after now + one step (the step of a range; for a listed instant the gap to the previous one, 0 for the
// first). A range frame's version is yyyy-MM-ddTHH:mm:ssZ; a listed instant's is its text as the server gave
// it.
struct NearFrame {
    QDateTime time;
    QString version;
    QString timeText; // what snapshotFromText takes as the time
    qint64 step = 0;
};

core::Result<Snapshot> nearestToNow(const QString& item, const QString& values, const QDateTime& nowIn) {
    const QDateTime now = (nowIn.isValid() ? nowIn : QDateTime::currentDateTimeUtc()).toUTC();
    std::vector<NearFrame> frames;
    std::vector<NearFrame> listed;
    for (const QString& rawPart : values.split(QLatin1Char(','))) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) {
            continue;
        }
        const QStringList fields = part.split(QLatin1Char('/'));
        if (fields.size() == 1) {
            if (const auto instant = snapshotFromText(part, part)) {
                listed.push_back({instant->time, instant->version, part, 0});
            }
            continue;
        }
        if (fields.size() != 3) {
            continue;
        }
        const auto start = snapshotFromText(QStringLiteral("x"), fields[0].trimmed());
        const auto end = snapshotFromText(QStringLiteral("x"), fields[1].trimmed());
        const qint64 step = durationSeconds(fields[2].trimmed());
        if (!start || !end || step <= 0 || end->time < start->time) {
            continue;
        }
        const qint64 last = start->time.secsTo(end->time) / step;
        const qint64 offset = start->time.secsTo(now);
        qint64 k = 0;
        if (offset > 0) {
            k = offset / step;
            if ((offset - k * step) * 2 > step) {
                ++k;
            }
        }
        const QDateTime time = start->time.addSecs(std::min(k, last) * step);
        frames.push_back({time, timeIsoZ(time), timeIsoZ(time), step});
    }
    std::ranges::sort(listed, [](const NearFrame& a, const NearFrame& b) { return a.time < b.time; });
    for (size_t i = 0; i < listed.size(); ++i) {
        // T-0352: the first one takes the gap to the next, as a range does (0 for a single instant)
        listed[i].step = i > 0               ? listed[i - 1].time.secsTo(listed[i].time)
                         : listed.size() > 1 ? listed[0].time.secsTo(listed[1].time)
                                             : 0;
        frames.push_back(listed[i]);
    }
    if (frames.empty()) {
        return parseFail(Tr::tr("layer %1 has no time values to choose from").arg(item));
    }
    const NearFrame* best = nullptr;
    qint64 bestDistance = 0;
    for (const NearFrame& frame : frames) {
        const qint64 distance = std::abs(now.secsTo(frame.time));
        if (best == nullptr || distance < bestDistance ||
            (distance == bestDistance && frame.time < best->time)) {
            best = &frame;
            bestDistance = distance;
        }
    }
    if (now.secsTo(best->time) > best->step) {
        return parseFail(Tr::tr("layer %1 has no time up to now + one step (the nearest is %2)")
                             .arg(item, timeIsoZ(best->time)));
    }
    return snapshotFromText(best->version, best->timeText);
}

// T-0356: the frames of a time dimension (`values` as nearestToNow reads them) against the chosen one:
// newest = no listed frame is later; previous = the latest listed frame strictly earlier. A range frame's
// version is yyyy-MM-ddTHH:mm:ssZ, a listed instant's is its text as the server gave it.
core::Result<Snapshot> withFrameList(const SnapshotDefinition& def, const QString& values,
                                     core::Result<Snapshot> chosen) {
    if (!chosen) {
        return chosen;
    }
    const QDateTime at = chosen->time;
    bool any = false;
    bool later = false;
    std::optional<NearFrame> before;
    const auto note = [&](const QDateTime& time, const QString& version, const QString& timeText) {
        any = true;
        if (time > at) {
            later = true;
        } else if (time < at && (!before || before->time < time)) {
            before = NearFrame{time, version, timeText, 0};
        }
    };
    for (const QString& rawPart : values.split(QLatin1Char(','))) {
        const QString part = rawPart.trimmed();
        const QStringList fields = part.split(QLatin1Char('/'));
        if (part.isEmpty()) {
            continue;
        }
        if (fields.size() == 1) {
            if (const auto instant = snapshotFromText(part, part)) {
                note(instant->time, instant->version, part);
            }
            continue;
        }
        if (fields.size() != 3) {
            continue;
        }
        const auto start = snapshotFromText(QStringLiteral("x"), fields[0].trimmed());
        const auto end = snapshotFromText(QStringLiteral("x"), fields[1].trimmed());
        const qint64 step = durationSeconds(fields[2].trimmed());
        if (!start || !end || step <= 0 || end->time < start->time) {
            continue;
        }
        const qint64 last = start->time.secsTo(end->time) / step;
        const QDateTime lastTime = start->time.addSecs(last * step);
        note(lastTime, timeIsoZ(lastTime), timeIsoZ(lastTime));
        const qint64 offset = start->time.secsTo(at);
        if (offset > 0) { // the latest grid point strictly before `at`, inside the range
            const QDateTime time = start->time.addSecs(std::min((offset - 1) / step, last) * step);
            note(time, timeIsoZ(time), timeIsoZ(time));
        }
    }
    if (!def.previous) {
        chosen->newest = any && !later;
        return chosen;
    }
    if (!before) {
        return parseFail(Tr::tr("layer %1 has no time before %2").arg(def.item, timeIsoZ(at)));
    }
    return snapshotFromText(before->version, before->timeText);
}

// §3.2 wmts-default.
core::Result<Snapshot> parseWmtsDefault(const SnapshotDefinition& def, const QByteArray& body) {
    QXmlStreamReader xml(body);
    const auto notCapabilities = [&xml]() {
        QString text = Tr::tr("not a WMTS capabilities document");
        if (xml.hasError()) {
            text += QStringLiteral(" (%1)").arg(xml.errorString());
        }
        return parseFail(std::move(text));
    };
    while (!xml.atEnd() && !xml.isStartElement()) {
        xml.readNext();
    }
    if (!xml.isStartElement() || xml.name() != u"Capabilities") {
        return notCapabilities();
    }

    std::vector<WmtsLayer> layers; // document order
    std::vector<qsizetype> open;   // indexes into layers of the Layer elements still open
    qsizetype depth = 1;           // the root
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            const auto name = xml.name();
            if (name == u"Layer") {
                layers.emplace_back();
                layers.back().depth = depth;
                open.push_back(static_cast<qsizetype>(layers.size()) - 1);
                continue;
            }
            const bool directChild =
                !open.empty() && layers[static_cast<size_t>(open.back())].depth == depth - 1;
            if (!directChild) {
                continue;
            }
            WmtsLayer& layer = layers[static_cast<size_t>(open.back())];
            if (name == u"Identifier") {
                const QString text = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                if (!layer.hasName) {
                    layer.name = text;
                    layer.hasName = true;
                }
                --depth; // readElementText consumed the end tag
            } else if (name == u"Dimension") {
                readWmtsDimension(xml, layer);
                --depth; // consumed the end tag
            }
        } else if (xml.isEndElement()) {
            if (!open.empty() && layers[static_cast<size_t>(open.back())].depth == depth) {
                open.pop_back();
            }
            --depth;
        }
    }

    for (const WmtsLayer& layer : layers) {
        if (!layer.hasName || layer.name != def.item) {
            continue;
        }
        if (!layer.hasTime || layer.timeDefault.isEmpty()) {
            if (xml.hasError()) {
                return notCapabilities();
            }
            return parseFail(Tr::tr("layer %1 has no default time").arg(def.item));
        }
        return chooseWmtsTime(def, layer);
    }
    if (xml.hasError()) {
        return notCapabilities();
    }
    return parseFail(Tr::tr("no layer %1").arg(def.item));
}

// §3.2 wmts-domain.
core::Result<Snapshot> parseWmtsDomain(const QByteArray& body) {
    QXmlStreamReader xml(body);
    while (!xml.atEnd() && !xml.isStartElement()) {
        xml.readNext();
    }
    if (!xml.isStartElement() || xml.name() != u"Domains") {
        return parseFail(Tr::tr("not a DescribeDomains document"));
    }
    QString domain;
    bool found = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"Domain") {
            domain = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            found = true;
            break;
        }
    }
    if (!found && xml.hasError()) {
        return parseFail(Tr::tr("not a DescribeDomains document"));
    }
    QString lastPart;
    for (const QString& part : domain.split(QLatin1Char(','))) {
        if (!part.trimmed().isEmpty()) {
            lastPart = part.trimmed();
        }
    }
    const QStringList fields = lastPart.split(QLatin1Char('/'));
    QString value;
    if (lastPart.isEmpty() || (fields.size() != 1 && fields.size() != 3)) {
        return parseFail(Tr::tr("no time domain"));
    }
    value = fields.size() == 3 ? fields[1].trimmed() : fields[0].trimmed();
    return snapshotFromText(value, value);
}

} // namespace

core::Result<std::optional<SnapshotDefinition>> snapshotDefinition(const maptype::IMapType& map) {
    const QString url = readKey(map, u"SnapshotIndex");
    if (url.isEmpty()) {
        return std::optional<SnapshotDefinition>{};
    }
    if (!isHttpUrl(url)) {
        return parseFail(Tr::tr("SnapshotIndex=%1 is not an http(s) URL").arg(url));
    }
    SnapshotDefinition def;
    def.indexUrl = url;

    const QString typeText = readKey(map, u"SnapshotIndexType");
    if (typeText.isEmpty()) {
        return parseFail(Tr::tr("SnapshotIndexType is missing"));
    }
    if (typeText.compare(u"json-last", Qt::CaseInsensitive) == 0) {
        def.type = SnapshotIndexType::JsonLast;
    } else if (typeText.compare(u"wms-default", Qt::CaseInsensitive) == 0) {
        def.type = SnapshotIndexType::WmsDefault;
    } else if (typeText.compare(u"wmts-default", Qt::CaseInsensitive) == 0) {
        def.type = SnapshotIndexType::WmtsDefault;
    } else if (typeText.compare(u"wmts-domain", Qt::CaseInsensitive) == 0) {
        def.type = SnapshotIndexType::WmtsDomain;
    } else if (typeText.compare(u"none-refetch", Qt::CaseInsensitive) == 0) {
        def.type = SnapshotIndexType::NoneRefetch;
    } else {
        return parseFail(Tr::tr("SnapshotIndexType=%1 is not json-last, wms-default, wmts-default, "
                                "wmts-domain or none-refetch")
                             .arg(typeText));
    }

    def.item = readKey(map, u"SnapshotItem");
    if (def.type == SnapshotIndexType::WmtsDomain || def.type == SnapshotIndexType::NoneRefetch) {
        if (!def.item.isEmpty()) {
            return parseFail(Tr::tr("SnapshotItem must be empty for %1")
                                 .arg(def.type == SnapshotIndexType::WmtsDomain
                                          ? QStringLiteral("wmts-domain")
                                          : QStringLiteral("none-refetch")));
        }
    } else if (def.item.isEmpty()) {
        QString typeName = QStringLiteral("json-last");
        if (def.type == SnapshotIndexType::WmsDefault) {
            typeName = QStringLiteral("wms-default");
        } else if (def.type == SnapshotIndexType::WmtsDefault) {
            typeName = QStringLiteral("wmts-default");
        }
        return parseFail(Tr::tr("SnapshotItem is required for %1").arg(typeName));
    }

    const QString timeField = readKey(map, u"SnapshotTimeField");
    if (!timeField.isEmpty()) {
        def.timeField = timeField;
    }
    def.versionField = readKey(map, u"SnapshotVersionField");

    // T-0337: SnapshotPick, wms-default and wmts-default only.
    const QString pickText = readKey(map, u"SnapshotPick");
    if (!pickText.isEmpty() && pickText.compare(u"default", Qt::CaseInsensitive) != 0) {
        if (pickText.compare(u"nearest-now", Qt::CaseInsensitive) != 0) {
            return parseFail(Tr::tr("SnapshotPick=%1 is not default or nearest-now").arg(pickText));
        }
        if (def.type != SnapshotIndexType::WmsDefault && def.type != SnapshotIndexType::WmtsDefault) {
            return parseFail(Tr::tr("SnapshotPick=nearest-now needs wms-default or wmts-default"));
        }
        def.pick = SnapshotPick::NearestNow;
    }
    return std::optional<SnapshotDefinition>{std::move(def)};
}

core::Result<std::optional<quint8>> maxZoom(const maptype::IMapType& map) {
    const QString text = readKey(map, u"MaxZoom");
    if (text.isEmpty()) {
        return std::optional<quint8>{};
    }
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok || value < 0 || value > 23) {
        return parseFail(Tr::tr("MaxZoom=%1 is not a zoom from 0 to 23").arg(text));
    }
    return std::optional<quint8>{static_cast<quint8>(value)};
}

core::Result<Snapshot> parseSnapshotIndex(const SnapshotDefinition& definition, const QByteArray& body) {
    switch (definition.type) {
    case SnapshotIndexType::JsonLast:
        return parseJsonLast(definition, body);
    case SnapshotIndexType::WmsDefault:
        return parseWmsDefault(definition, body);
    case SnapshotIndexType::WmtsDomain:
        return parseWmtsDomain(body);
    case SnapshotIndexType::WmtsDefault:
        return parseWmtsDefault(definition, body);
    case SnapshotIndexType::NoneRefetch: {
        // T-0337: no body; the instant of the resolve (whole seconds) is the version and the time.
        const QDateTime now = definition.now.isValid() ? definition.now : QDateTime::currentDateTimeUtc();
        const QString text = timeIsoZ(now);
        return snapshotFromText(text, text);
    }
    }
    return parseFail(Tr::tr("SnapshotIndexType is missing"));
}

std::expected<SnapshotTime, QString> parseSnapshotTime(const QString& text) {
    static const QRegularExpression relativeRe(QStringLiteral(R"(^\+(\d{1,5})h$)"));
    static const QRegularExpression absoluteRe(QStringLiteral(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$)"));
    SnapshotTime time;
    if (const QRegularExpressionMatch m = relativeRe.match(text); m.hasMatch()) {
        time.relative = true;
        time.hours = m.captured(1).toLongLong();
        return time;
    }
    if (absoluteRe.match(text).hasMatch()) {
        time.instant = QDateTime::fromString(text, Qt::ISODate);
        if (time.instant.isValid()) {
            return time;
        }
    }
    return std::unexpected(Tr::tr("\"%1\" is not a time such as 2026-10-18T12:00:00Z or +24h").arg(text));
}

core::Result<Snapshot> snapshotFromText(const QString& version, const QString& timeText) {
    static const QRegularExpression dateOnlyRe(QStringLiteral(R"(^\d{4}-\d{2}-\d{2}$)"));
    const QString text = timeText.trimmed();
    Snapshot snap;
    if (dateOnlyRe.match(text).hasMatch()) {
        const QDate date = QDate::fromString(text, Qt::ISODate);
        if (!date.isValid()) {
            return parseFail(Tr::tr("%1 is not a time").arg(text));
        }
        snap.dateOnly = true;
        snap.time = core::platform::utcDateTime(date, QTime(0, 0));
    } else {
        QDateTime time = QDateTime::fromString(text, Qt::ISODateWithMs);
        // Qt 6.4 returns an invalid value for a zoneless time inside a DST gap (6.5+ shifts the fields): an
        // invalid first parse is retried with a Z too; a text that is invalid for any other reason stays
        // invalid.
        if (!time.isValid() || time.timeSpec() == Qt::LocalTime) {
            // No zone in the text: UTC. Parse again with a Z so that no local-time step (a DST gap
            // would shift the fields) is involved.
            time = QDateTime::fromString(text + QLatin1Char('Z'), Qt::ISODateWithMs);
            if (!time.isValid()) {
                return parseFail(Tr::tr("%1 is not a time").arg(text));
            }
            time = time.toUTC();
        } else {
            time = time.toUTC();
        }
        snap.time = time;
    }
    if (auto ok = checkVersion(version.trimmed()); !ok) {
        return std::unexpected(ok.error());
    }
    snap.version = version.trimmed();
    return snap;
}

QString snapshotTimeText(const Snapshot& s) {
    if (!s.time.isValid()) {
        return Tr::tr("unknown");
    }
    const QDateTime utc = s.time.toUTC();
    return s.dateOnly ? utc.toString(QStringLiteral("yyyy-MM-dd"))
                      : utc.toString(QStringLiteral("yyyy-MM-dd HH:mm")) + QStringLiteral(" UTC");
}

QString withYear(QString text, int year) {
    return text.replace(QStringLiteral("{year}"), QString::number(year));
}

QString snapshotTimeIso(const Snapshot& s) {
    if (!s.time.isValid()) {
        return {};
    }
    const QDateTime utc = s.time.toUTC();
    return utc.toString(s.dateOnly ? QStringLiteral("yyyy-MM-dd")
                                   : QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
}

} // namespace cg::app
