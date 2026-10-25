// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/region/zoom_array_func.h"

#include "cg/core/delimited_text.h"
#include "cg/core/delphi_text.h"

#include <QStringList>

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace cg::region {

namespace {

// Legacy cUndefRangeValue (:42). Zoom values are bytes, so the sentinel never equals a stored value.
constexpr int kUndefRangeValue = std::numeric_limits<int>::max();

// Legacy DiffValueIsOne (:150-153).
bool diffValueIsOne(int a, int b) {
    return std::abs(std::abs(a) - std::abs(b)) == 1;
}

// Legacy FlashRange (:155-179): appends one range to the text, parts separated by commas.
void flashRange(QString& range, int start, int end) {
    const QString sep = range.isEmpty() ? QString() : QStringLiteral(",");
    if (start != end) {
        if (diffValueIsOne(start, end)) {
            range += sep + QString::number(start) + QStringLiteral(",") + QString::number(end);
        } else {
            range += sep + QString::number(start) + QStringLiteral("-") + QString::number(end);
        }
    } else {
        range += sep + QString::number(start);
    }
}

} // namespace

bool isSortedZoomArray(const std::vector<quint8>& array) {
    return std::is_sorted(array.begin(), array.end());
}

// The legacy hand-written quicksort (:57-99) gives the same array: equal bytes are indistinguishable.
void sortZoomArray(std::vector<quint8>& array) {
    std::sort(array.begin(), array.end());
}

// FEAT-REG-004
bool zoomArrayFromStr(const QString& str, std::vector<quint8>& out) {
    out.clear();

    // TStringList.CommaText (:114): a comma and any blank separate items; a double quote groups.
    const QStringList items = core::parseDelimitedText(str, u',');
    for (const QString& item : items) {
        const qsizetype dash = item.indexOf(u'-');
        if (dash >= 0) {
            int zoomMin = 0;
            int zoomMax = 0;
            if (core::delphiTryStrToInt(item.left(dash), zoomMin) &&
                core::delphiTryStrToInt(item.mid(dash + 1), zoomMax) && zoomMin <= zoomMax) {
                // qint64 counter: the legacy Integer loop cannot overflow on MaxInt either.
                for (qint64 zoom = zoomMin; zoom <= zoomMax; ++zoom) {
                    out.push_back(static_cast<quint8>(zoom)); // range checks off (:123): 300 -> 44
                }
            }
        } else {
            int zoom = 0;
            if (core::delphiTryStrToInt(item, zoom)) {
                out.push_back(static_cast<quint8>(zoom)); // range checks off (:132)
            }
        }
    }

    const bool result = !out.empty();
    if (result && !isSortedZoomArray(out)) {
        sortZoomArray(out);
    }
    return result;
}

// FEAT-REG-004
QString zoomArrayToStr(const std::vector<quint8>& zoomArr) {
    // An empty input falls through to "" (legacy Assert is off in the shipped build).
    if (zoomArr.size() == 1) {
        return QString::number(static_cast<int>(zoomArr[0]) + 1); // Integer arithmetic (:192): 255 -> 256
    }

    // Byte assignment of z + 1 (:198) wraps 255 to 0, as in legacy.
    std::vector<quint8> arr;
    arr.reserve(zoomArr.size());
    for (const quint8 zoom : zoomArr) {
        arr.push_back(static_cast<quint8>(zoom + 1));
    }
    if (!isSortedZoomArray(arr)) {
        sortZoomArray(arr);
    }

    QString result;
    int rangeStart = kUndefRangeValue;
    int rangeEnd = kUndefRangeValue;
    for (const quint8 value : arr) {
        if (rangeStart == kUndefRangeValue) {
            rangeStart = value;
            rangeEnd = rangeStart;
            continue;
        }
        if (diffValueIsOne(value, rangeEnd)) {
            rangeEnd = value;
            continue;
        }
        flashRange(result, rangeStart, rangeEnd);
        rangeStart = value;
        rangeEnd = rangeStart;
    }
    if (rangeStart != kUndefRangeValue) {
        flashRange(result, rangeStart, rangeEnd);
    }
    return result;
}

bool isZoomInZoomArray(quint8 zoom, const std::vector<quint8>& zoomArr) {
    return std::find(zoomArr.begin(), zoomArr.end(), zoom) != zoomArr.end();
}

} // namespace cg::region
