// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: the fetch-once pattern of the stock Google/GoogleSat.zmp script
// (Downloader.DoHttpRequest + Global vars), with the result fed into the map version
// (Src/MapVersion/u_MapVersionRequestConfig.pas) instead of the URL. docs/specs/weather-sources.md §3.
// Ports: nothing.
#include "cg/core/result.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>

namespace cg::maptype {
class IMapType;
}

namespace cg::app {

// T-0337: NoneRefetch = a layer without a time dimension; nothing is fetched, the instant of the resolve is
// the snapshot, so every download takes fresh tiles.
enum class SnapshotIndexType : std::uint8_t { JsonLast, WmsDefault, WmtsDomain, WmtsDefault, NoneRefetch };

// T-0337: `SnapshotPick` of a wms-default / wmts-default source: the layer's default time, or the frame of
// its time dimension nearest to now.
enum class SnapshotPick : std::uint8_t { Default, NearestNow };

// T-0356: a newest frame younger than this at the start of the run may still be being produced by the
// provider; when none of its tiles is served, the run goes one frame back.
inline constexpr qint64 kYoungFrameSeconds = qint64{60} * 60;

// T-0283 (weather-sources.md 3.2 wmts-default, app-cli.md 1.2 --time): the frame the user asked for.
struct SnapshotTime {
    bool relative = false; // "+<N>h": now + hours, floored to the layer step
    qint64 hours = 0;
    QDateTime instant; // UTC; the absolute form
};

// "2026-10-18T12:00:00Z" (the Z is required) or "+<N>h" (whole hours, N from 0 to 99999). The error is the
// message without the option.
[[nodiscard]] std::expected<SnapshotTime, QString> parseSnapshotTime(const QString& text);

struct SnapshotDefinition { // the keys of §2
    QString indexUrl;
    SnapshotIndexType type = SnapshotIndexType::JsonLast;
    QString
        item; // json-last: dotted path; wms-default: layer name; wmts-domain: empty; wmts-default: layer id
    QString timeField = QStringLiteral("time");
    QString versionField;                      // empty = timeField
    SnapshotPick pick = SnapshotPick::Default; // T-0337: wms-default and wmts-default only
    // wmts-default only (--time): nullopt = the layer's default time. `now` (invalid = the clock) is the
    // origin of "+<N>h"; `sourceName` is the name in the refusal text.
    std::optional<SnapshotTime> time;
    QDateTime now; // also the instant of nearest-now and none-refetch (T-0337)
    // T-0356: the frame BEFORE the one the index gives (json-last: the second last entry; wms/wmts-default:
    // the largest listed frame earlier than the chosen one). wmts-domain, none-refetch and --time have none.
    bool previous = false;
    QString sourceName;
};

struct Snapshot {
    QString version;       // goes into {ver} and the cache key, verbatim
    QDateTime time;        // UTC; invalid = unknown (a resume of a session without a store entry, §4.4)
    bool dateOnly = false; // the index gave a date (GIBS daily layers)
    // T-0356: the frame is the newest one the index lists (json-last: the last entry; wms/wmts-default: no
    // listed frame is later). False for wmts-domain, none-refetch, --time, a previous frame and a stored
    // snapshot.
    bool newest = false;
};

// §2. nullopt = a static source. Error (ErrorCode::ParseError) = S-14 with the detail of §3.4.
[[nodiscard]] core::Result<std::optional<SnapshotDefinition>>
snapshotDefinition(const maptype::IMapType& map);

// §6. nullopt = no limit. Error (ParseError) = S-14 "MaxZoom=<v> is not a zoom from 0 to 23".
[[nodiscard]] core::Result<std::optional<quint8>> maxZoom(const maptype::IMapType& map);

// §3.2, pure: the body of the index answer -> the snapshot. Error (ParseError) with the texts of §3.4. The
// one other code: Unsupported = definition.time is not a time of the layer (the whole refusal text, T-0283).
[[nodiscard]] core::Result<Snapshot> parseSnapshotIndex(const SnapshotDefinition& definition,
                                                        const QByteArray& body);

// §3.3, pure. "2026-10-15" -> date only; ISO date-time with Z, an offset, or none (= UTC); fractions allowed.
[[nodiscard]] core::Result<Snapshot> snapshotFromText(const QString& version, const QString& timeText);

[[nodiscard]] QString
snapshotTimeText(const Snapshot& s);                      // "2026-10-15 20:10 UTC" | "2026-10-15" | "unknown"
[[nodiscard]] QString snapshotTimeIso(const Snapshot& s); // "2026-10-15T20:10:00Z" | "2026-10-15" | ""

// T-0249 (Q-W3): every "{year}" of a credit text replaced by the year.
[[nodiscard]] QString withYear(QString text, int year);

} // namespace cg::app
