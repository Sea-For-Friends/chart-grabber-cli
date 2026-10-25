// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: nothing (test double). See script_host_fake.h.
#include "script_host_fake.h"

#include <cmath>

namespace cg::script::test {

namespace {
constexpr double kEarthRadius = 6378137.0; // FakeCoordConverter: spherical Mercator, R = 6378137
constexpr double kPi = 3.14159265358979323846;
} // namespace

DoublePoint FakeCoordConverter::lonLat2Metr(DoublePoint ll) const {
    return DoublePoint{kEarthRadius * ll.x * kPi / 180.0,
                       kEarthRadius * std::log(std::tan(kPi / 4.0 + ll.y * kPi / 360.0))};
}

DoublePoint FakeCoordConverter::metr2LonLat(DoublePoint mm) const {
    return DoublePoint{mm.x / kEarthRadius * 180.0 / kPi,
                       (2.0 * std::atan(std::exp(mm.y / kEarthRadius)) - kPi / 2.0) * 180.0 / kPi};
}

DoublePoint FakeCoordConverter::pos2LonLat(Point xy, quint8 zoom) const {
    const double n = pixelsAtZoom(zoom);
    return metr2LonLat(DoublePoint{(xy.x / n - 0.5) * 2.0 * kPi * kEarthRadius,
                                   (0.5 - xy.y / n) * 2.0 * kPi * kEarthRadius});
}

Point FakeCoordConverter::lonLat2Pos(DoublePoint ll, quint8 zoom) const {
    const DoublePoint mm = lonLat2Metr(ll);
    const double n = pixelsAtZoom(zoom);
    const double px = (mm.x / (2.0 * kPi * kEarthRadius) + 0.5) * n;
    const double py = (0.5 - mm.y / (2.0 * kPi * kEarthRadius)) * n;
    return Point{static_cast<qint32>(std::lround(px)), static_cast<qint32>(std::lround(py))};
}

Point FakeCoordConverter::tilePos2PixelPos(Point xy, quint8 /*zoom*/) const {
    return Point{xy.x * 256, xy.y * 256};
}

Rect FakeCoordConverter::tilePos2PixelRect(Point xy, quint8 /*zoom*/) const {
    return Rect{xy.x * 256, xy.y * 256, xy.x * 256 + 256, xy.y * 256 + 256};
}

quint32 FakeHttpDownloader::doHttpRequest(const QByteArray& url, const QByteArray& requestHeader,
                                          const QByteArray& postData, QByteArray& responseHeader,
                                          QByteArray& responseData) {
    calls_.push_back(Call{url, requestHeader, postData});
    const Reply reply = queue_.isEmpty() ? Reply{} : queue_.takeFirst();
    responseHeader = reply.responseHeader;
    responseData = reply.responseData;
    return reply.status; // status 0 is passed through unchanged - never normalised away (spec 5.6)
}

TileInfo FakeScriptTileCache::read(int x, int y, int zoom, const QByteArray& version, bool withData) const {
    TileInfo info;
    const auto it = tiles_.find(Key{x, y, zoom, version});
    if (it == tiles_.end()) {
        return info; // all-empty, IsExists/IsExistsTne both false
    }
    const Entry& e = it->second;
    if (e.tne) {
        info.isExistsTne = true;
        return info;
    }
    info.isExists = true;
    info.loadDate = e.loadDate;
    info.contentType = e.contentType;
    info.size = e.data.size();
    if (withData) {
        info.data = e.data;
    }
    return info;
}

bool FakeScriptTileCache::write(int x, int y, int zoom, const QByteArray& version,
                                const QByteArray& contentType, const QByteArray& data, bool overwrite) {
    const Key key{x, y, zoom, version};
    if (!overwrite && tiles_.contains(key)) {
        return false;
    }
    tiles_[key] = Entry{false, contentType, data, QDateTime::currentDateTimeUtc()};
    return true;
}

bool FakeScriptTileCache::writeTne(int x, int y, int zoom, const QByteArray& version) {
    tiles_[Key{x, y, zoom, version}] =
        Entry{true, QByteArray(), QByteArray(), QDateTime::currentDateTimeUtc()};
    return true;
}

bool FakeScriptTileCache::remove(int x, int y, int zoom, const QByteArray& version) {
    return tiles_.erase(Key{x, y, zoom, version}) > 0;
}

qint32 FakeScriptHost::random(qint32 upperExclusive) {
    // spec 5.3 F1, Delphi System.Random's exact LCG: RandSeed := RandSeed*134775813 + 1 (wraps mod
    // 2^32, Cardinal arithmetic), result = the high 32 bits of UInt64(Cardinal(RandSeed)) * X.
    // Interpreter::callBuiltin already turns X <= 0 into 0 without reaching here (spec F1).
    randSeed_ = randSeed_ * 134775813u + 1u;
    const quint64 product =
        static_cast<quint64>(randSeed_) * static_cast<quint64>(static_cast<quint32>(upperExclusive));
    return static_cast<qint32>(product >> 32);
}

QByteArray FakeScriptHost::templateToUrl(const QByteArray& tmpl) const {
    // Minimal per T-0015A: only {x}/{y}/{z}, case-insensitive. Everything else - including an
    // unrecognised tag or an unmatched brace - is emitted literally, matching the general engine's
    // fallback rule (spec 5.5) even though this fake implements none of its other tags.
    QByteArray out;
    out.reserve(tmpl.size());
    int i = 0;
    const int n = static_cast<int>(tmpl.size());
    while (i < n) {
        if (tmpl.at(i) == '{') {
            const int close = static_cast<int>(tmpl.indexOf('}', i + 1));
            if (close < 0) {
                out += tmpl.mid(i);
                break;
            }
            const QByteArray tag = tmpl.mid(i + 1, close - i - 1).toLower();
            if (tag == "x") {
                out += QByteArray::number(tileX_);
            } else if (tag == "y") {
                out += QByteArray::number(tileY_);
            } else if (tag == "z") {
                out += QByteArray::number(tileZ_);
            } else {
                out += tmpl.mid(i, close - i + 1); // literal, braces included
            }
            i = close + 1;
        } else {
            out += tmpl.at(i);
            ++i;
        }
    }
    return out;
}

int FakeScriptHost::saveToLocalFile(const QString& fullPath, const QByteArray& data) {
    saveCalls_.push_back(SaveCall{fullPath, data});
    return static_cast<int>(data.size()); // F36: byte count written; the fake never fails
}

bool FakeScriptHost::fileExists(const QString& fullPath) const {
    fileExistsCalls_.push_back(fullPath);
    return existingFiles_.contains(fullPath);
}

void FakeScriptHost::writeLn(const QByteArray& text) {
    written_.push_back(text);
}

} // namespace cg::script::test
