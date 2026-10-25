// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_EnumDoublePointClipInternal.pas (the four half-plane primitives) and
// u_EnumDoublePointWithClip.pas (the clipByRect composition). docs/specs/geo.md §3.4.1.
#include "cg/geo/point_stream.h"

#include <utility>

namespace cg::geo {
namespace {

using PointCodeFn = int (*)(DoublePoint p, double line);
using IntersectFn = DoublePoint (*)(DoublePoint prev, DoublePoint curr, double line);

// TEnumDoublePointClipByLineAbstract.Next (u_EnumDoublePointClipInternal.pas:107-166): classify
// each point with a code (3 separator, 0 outside, 2 inside, 1 on the line) and switch on
// prevCode*16 + curCode. Delphi's `case` with no matching branch is a silent no-op (not even the
// prev-point update) -- that is the 0x33 (separator-after-separator) case here. The four border
// variants (left/right/top/bottom) differ only in pointCode/intersect, passed in as plain
// function pointers instead of a virtual hierarchy (no second user, and an abstract base here
// would need the project's "I" interface prefix, which is reserved for ported legacy `i_*`
// interfaces).
class ClipByLineStream final : public IPointStream {
public:
    ClipByLineStream(double line, PointCodeFn pointCode, IntersectFn intersect, PointStreamPtr source)
        : line_(line), pointCode_(pointCode), intersect_(intersect), source_(std::move(source)) {}

    bool next(DoublePoint& out) override {
        while (!finished_) {
            if (preparedPointExists_) {
                out = preparedPoint_;
                preparedPointExists_ = false;
                return true;
            }
            DoublePoint curr;
            if (!source_->next(curr)) {
                finished_ = true;
                break;
            }
            const int currCode = pointCode_(curr, line_);
            const int lineCode = prevCode_ * 16 + currCode;
            switch (lineCode) {
            case 0x01:
            case 0x11:
            case 0x12:
            case 0x21:
            case 0x22:
            case 0x03:
            case 0x13:
            case 0x23:
            case 0x31:
            case 0x32:
                out = curr;
                prevPoint_ = curr;
                prevCode_ = currCode;
                return true;
            case 0x02:
                out = intersect_(prevPoint_, curr, line_);
                preparedPoint_ = curr;
                preparedPointExists_ = true;
                prevPoint_ = curr;
                prevCode_ = currCode;
                return true;
            case 0x20:
                out = intersect_(prevPoint_, curr, line_);
                prevPoint_ = curr;
                prevCode_ = currCode;
                return true;
            case 0x00:
            case 0x10:
            case 0x30:
                prevPoint_ = curr;
                prevCode_ = currCode;
                break;
            default:
                // 0x33: no matching legacy branch, nothing happens at all (not even prevPoint_).
                break;
            }
        }
        out = emptyDoublePoint();
        return false;
    }

private:
    double line_;
    PointCodeFn pointCode_;
    IntersectFn intersect_;
    PointStreamPtr source_;
    bool finished_ = false;
    DoublePoint prevPoint_{};
    int prevCode_ = 3; // FPrevPointCode starts as the separator code.
    DoublePoint preparedPoint_{};
    bool preparedPointExists_ = false;
};

[[nodiscard]] DoublePoint verticalIntersect(DoublePoint prev, DoublePoint curr, double x) {
    const double y = (curr.y() - prev.y()) / (curr.x() - prev.x()) * (x - prev.x()) + prev.y();
    return {x, y};
}

[[nodiscard]] DoublePoint horizontalIntersect(DoublePoint prev, DoublePoint curr, double y) {
    const double x = (curr.x() - prev.x()) / (curr.y() - prev.y()) * (y - prev.y()) + prev.x();
    return {x, y};
}

[[nodiscard]] int leftPointCode(DoublePoint p, double x) {
    if (isSeparator(p)) {
        return 3;
    }
    if (p.x() < x) {
        return 0;
    }
    if (p.x() > x) {
        return 2;
    }
    return 1;
}

[[nodiscard]] int rightPointCode(DoublePoint p, double x) {
    if (isSeparator(p)) {
        return 3;
    }
    if (p.x() > x) {
        return 0;
    }
    if (p.x() < x) {
        return 2;
    }
    return 1;
}

[[nodiscard]] int topPointCode(DoublePoint p, double y) {
    if (isSeparator(p)) {
        return 3;
    }
    if (p.y() < y) {
        return 0;
    }
    if (p.y() > y) {
        return 2;
    }
    return 1;
}

[[nodiscard]] int bottomPointCode(DoublePoint p, double y) {
    if (isSeparator(p)) {
        return 3;
    }
    if (p.y() > y) {
        return 0;
    }
    if (p.y() < y) {
        return 2;
    }
    return 1;
}

} // namespace

PointStreamPtr clipByLeftBorder(double x, PointStreamPtr source) {
    return std::make_unique<ClipByLineStream>(x, leftPointCode, verticalIntersect, std::move(source));
}

PointStreamPtr clipByRightBorder(double x, PointStreamPtr source) {
    return std::make_unique<ClipByLineStream>(x, rightPointCode, verticalIntersect, std::move(source));
}

PointStreamPtr clipByTopBorder(double y, PointStreamPtr source) {
    return std::make_unique<ClipByLineStream>(y, topPointCode, horizontalIntersect, std::move(source));
}

PointStreamPtr clipByBottomBorder(double y, PointStreamPtr source) {
    return std::make_unique<ClipByLineStream>(y, bottomPointCode, horizontalIntersect, std::move(source));
}

// TEnumDoublePointClipByRect.Create (u_EnumDoublePointWithClip.pas:79-106): order-sensitive, the
// two branches differ.
PointStreamPtr clipByRect(bool closed, const DoubleRect& rect, PointStreamPtr source) {
    PointStreamPtr s = std::move(source);
    if (closed) {
        s = closePoly(std::move(s));
        s = clipByBottomBorder(rect.bottom, std::move(s));
        s = closePoly(std::move(s));
        s = clipByRightBorder(rect.right, std::move(s));
        s = closePoly(std::move(s));
        s = clipByTopBorder(rect.top, std::move(s));
        s = closePoly(std::move(s));
        s = clipByLeftBorder(rect.left, std::move(s));
        s = closePoly(std::move(s));
    } else {
        s = clipByBottomBorder(rect.bottom, std::move(s));
        s = clipByTopBorder(rect.top, std::move(s));
        s = clipByLeftBorder(rect.left, std::move(s));
        s = clipByRightBorder(rect.right, std::move(s));
    }
    return s;
}

} // namespace cg::geo
