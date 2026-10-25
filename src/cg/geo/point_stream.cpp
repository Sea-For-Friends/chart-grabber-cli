// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/point_stream.h"

#include "cg/geo/datum.h"
#include "cg/geo/local_coord_converter.h"
#include "cg/geo/projection.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace cg::geo {

bool IPointStream::next(DoublePoint& out, PointMetaItem& meta) {
    meta = PointMetaItem{};
    return next(out);
}

namespace {

// --- sources -------------------------------------------------------------------------------

// TEnumDoublePointsByArray.
class PointsByArrayStream final : public IPointStream {
public:
    explicit PointsByArrayStream(std::span<const DoublePoint> points) : points_(points) {}

    bool next(DoublePoint& out) override {
        if (index_ < points_.size()) {
            out = points_[index_];
            ++index_;
            return true;
        }
        out = emptyDoublePoint();
        return false;
    }

private:
    std::span<const DoublePoint> points_;
    std::size_t index_ = 0;
};

// TEnumDoublePointBySingleLineBase / DoNext.
class SingleLineStream final : public IPointStream {
public:
    SingleLineStream(std::span<const DoublePoint> points, const PointsMeta* meta, bool closed)
        : points_(points), meta_(meta), closed_(closed) {
        Q_ASSERT(!points_.empty()); // legacy: Assert(ACount > 0, 'No points')
    }

    bool next(DoublePoint& out) override { return doNext(out, nullptr); }
    bool next(DoublePoint& out, PointMetaItem& meta) override { return doNext(out, &meta); }

private:
    [[nodiscard]] PointMetaItem itemAt(std::size_t i) const {
        // Ports SetMetaItem (u_DoublePointsMetaFunc.pas:261-280).
        PointMetaItem m;
        if (meta_ != nullptr) {
            if (meta_->hasElevation()) {
                m.hasElevation = true;
                m.elevation = meta_->elevation[i];
            }
            if (meta_->hasTimestamp()) {
                m.hasTimestamp = true;
                m.timestamp = meta_->timestamp[i];
            }
        }
        return m;
    }

    bool doNext(DoublePoint& out, PointMetaItem* metaOut) {
        if (index_ < points_.size()) {
            out = points_[index_];
            if (metaOut != nullptr) {
                *metaOut = itemAt(index_);
            }
            ++index_;
            return true;
        }
        if (index_ == points_.size()) {
            bool result;
            if (closed_ && points_.size() > 1) {
                out = points_[0];
                if (metaOut != nullptr) {
                    *metaOut = itemAt(0);
                }
                result = true;
            } else {
                out = emptyDoublePoint();
                if (metaOut != nullptr) {
                    *metaOut = PointMetaItem{};
                }
                result = false;
            }
            ++index_;
            return result;
        }
        out = emptyDoublePoint();
        if (metaOut != nullptr) {
            *metaOut = PointMetaItem{};
        }
        return false;
    }

    std::span<const DoublePoint> points_;
    const PointsMeta* meta_;
    bool closed_;
    std::size_t index_ = 0;
};

// TEnumDoublePointByLineSetBase / TEnumLonLatPointByPath.
class LineSetStream final : public IPointStream {
public:
    explicit LineSetStream(const IGeometryLonLatMultiLine& lineSet)
        : lineSet_(lineSet), count_(static_cast<long>(lineSet.count())) {}

    bool next(DoublePoint& out) override {
        while (!finished_) {
            if (current_) {
                if (preparedPointExists_) {
                    out = preparedPoint_;
                    preparedPointExists_ = false;
                    needEmptyPoint_ = true;
                    return true;
                }
                if (current_->next(out)) {
                    needEmptyPoint_ = true;
                    return true;
                }
                current_.reset();
            } else {
                ++index_;
                if (index_ < count_) {
                    current_ = makeCurrent(static_cast<std::size_t>(index_));
                    if (current_ && needEmptyPoint_) {
                        DoublePoint p;
                        if (current_->next(p)) {
                            preparedPoint_ = p;
                            preparedPointExists_ = true;
                            needEmptyPoint_ = false;
                            out = emptyDoublePoint();
                            return true;
                        }
                    }
                } else {
                    finished_ = true;
                }
            }
        }
        out = emptyDoublePoint();
        return false;
    }

private:
    PointStreamPtr makeCurrent(std::size_t i) {
        const auto& item = lineSet_.item(i);
        const std::span<const LonLat> src = item->points();
        buffer_.clear();
        buffer_.reserve(src.size());
        for (const LonLat& p : src) {
            buffer_.push_back(p.toPoint());
        }
        return singleLineStream(buffer_, &item->meta(), false);
    }

    const IGeometryLonLatMultiLine& lineSet_;
    long count_;
    long index_ = -1;
    PointStreamPtr current_;
    std::vector<DoublePoint> buffer_;
    bool needEmptyPoint_ = false;
    bool finished_ = false;
    bool preparedPointExists_ = false;
    DoublePoint preparedPoint_{};
};

// --- decorators ------------------------------------------------------------------------------

// TEnumDoublePointClosePoly.
class ClosePolyStream final : public IPointStream {
public:
    explicit ClosePolyStream(PointStreamPtr source) : source_(std::move(source)) {}

    bool next(DoublePoint& out) override {
        if (finished_) {
            out = emptyDoublePoint();
            return false;
        }
        if (needAddBreak_) {
            needAddBreak_ = false;
            out = emptyDoublePoint();
            return true;
        }
        DoublePoint p;
        if (source_->next(p)) {
            if (isSeparator(p)) {
                if (pointsInPolyCount_ > 1 && !lastPointEqualToFirst_) {
                    out = firstPoint_;
                    needAddBreak_ = true;
                } else {
                    out = p;
                }
                pointsInPolyCount_ = 0;
                return true;
            }
            if (pointsInPolyCount_ == 0) {
                firstPoint_ = p;
                pointsInPolyCount_ = 1;
                lastPointEqualToFirst_ = true;
            } else {
                // DoublePointsEqual (u_GeoFunc.pas:177): neither operand can be NaN here.
                lastPointEqualToFirst_ = (p == firstPoint_);
                ++pointsInPolyCount_;
            }
            out = p;
            return true;
        }
        finished_ = true;
        if (pointsInPolyCount_ > 1 && !lastPointEqualToFirst_) {
            out = firstPoint_;
            return true;
        }
        out = emptyDoublePoint();
        return false;
    }

private:
    PointStreamPtr source_;
    DoublePoint firstPoint_{};
    bool lastPointEqualToFirst_ = false;
    int pointsInPolyCount_ = 0;
    bool finished_ = false;
    bool needAddBreak_ = false;
};

// TEnumDoublePointFilterEqual.
class FilterEqualStream final : public IPointStream {
public:
    explicit FilterEqualStream(PointStreamPtr source) : source_(std::move(source)) {}

    bool next(DoublePoint& out) override {
        while (!finished_) {
            DoublePoint p;
            if (!source_->next(p)) {
                finished_ = true;
                break;
            }
            if (isSeparator(p)) {
                if (!prevEmpty_) {
                    prevEmpty_ = true;
                    prevPoint_ = p;
                    out = p;
                    return true;
                }
            } else if (prevEmpty_ || std::abs(p.x() - prevPoint_.x()) > 1 ||
                       std::abs(p.y() - prevPoint_.y()) > 1) {
                // Chebyshev decimation against the last *emitted* point; prevEmpty_ always
                // passes (the first point after a break, or the very first point of the stream).
                prevEmpty_ = false;
                prevPoint_ = p;
                out = p;
                return true;
            }
        }
        out = emptyDoublePoint();
        return false;
    }

private:
    PointStreamPtr source_;
    bool prevEmpty_ = true;
    DoublePoint prevPoint_{};
    bool finished_ = false;
};

// TEnumDoublePointFilterFirstSegment.
class FilterFirstSegmentStream final : public IPointStream {
public:
    explicit FilterFirstSegmentStream(PointStreamPtr source) : source_(std::move(source)) {}

    bool next(DoublePoint& out) override {
        while (!finished_) {
            DoublePoint p;
            if (!source_->next(p)) {
                finished_ = true;
                break;
            }
            if (started_) {
                if (isSeparator(p)) {
                    finished_ = true;
                    break;
                }
                out = p;
                return true;
            }
            if (!isSeparator(p)) {
                started_ = true;
                out = p;
                return true;
            }
        }
        out = emptyDoublePoint();
        return false;
    }

private:
    PointStreamPtr source_;
    bool started_ = false;
    bool finished_ = false;
};

// TEnumDoublePointLonLatToMapPixel.
class LonLatToMapPixelStream final : public IPointStream {
public:
    LonLatToMapPixelStream(const IProjection& projection, PointStreamPtr source)
        : projection_(projection), projectionType_(projection.projectionType()), source_(std::move(source)) {
        Q_ASSERT(projectionType_);
    }

    bool next(DoublePoint& out) override {
        if (finished_) {
            out = emptyDoublePoint();
            return false;
        }
        DoublePoint p;
        if (!source_->next(p)) {
            finished_ = true;
            out = emptyDoublePoint();
            return false;
        }
        if (isSeparator(p)) {
            out = p;
            return true;
        }
        LonLat ll = LonLat::fromPoint(p);
        projectionType_->validateLonLatPos(ll);
        out = projection_.lonLat2PixelPosFloat(ll);
        return true;
    }

private:
    const IProjection& projection_;
    ProjectionTypePtr projectionType_;
    PointStreamPtr source_;
    bool finished_ = false;
};

// TEnumDoublePointMapPixelToLocalPixel.
class MapPixelToLocalPixelStream final : public IPointStream {
public:
    MapPixelToLocalPixelStream(const ILocalCoordConverter& converter, PointStreamPtr source)
        : converter_(converter), source_(std::move(source)) {}

    bool next(DoublePoint& out) override {
        if (finished_) {
            out = emptyDoublePoint();
            return false;
        }
        DoublePoint p;
        if (!source_->next(p)) {
            finished_ = true;
            out = emptyDoublePoint();
            return false;
        }
        out = isSeparator(p) ? p : converter_.mapPixelFloat2LocalPixelFloat(p);
        return true;
    }

private:
    const ILocalCoordConverter& converter_;
    PointStreamPtr source_;
    bool finished_ = false;
};

// TEnumDoublePointMapPixelToLocalPixelSimple.
class MapPixelToLocalPixelSimpleStream final : public IPointStream {
public:
    MapPixelToLocalPixelSimpleStream(DoublePoint topLeft, PointStreamPtr source)
        : topLeft_(topLeft), source_(std::move(source)) {}

    bool next(DoublePoint& out) override {
        if (finished_) {
            out = emptyDoublePoint();
            return false;
        }
        DoublePoint p;
        if (!source_->next(p)) {
            finished_ = true;
            out = emptyDoublePoint();
            return false;
        }
        out = isSeparator(p) ? p : DoublePoint(p.x() - topLeft_.x(), p.y() - topLeft_.y());
        return true;
    }

private:
    DoublePoint topLeft_;
    PointStreamPtr source_;
    bool finished_ = false;
};

// TEnumDoublePointLine2Poly. Field-for-field port of u_EnumDoublePointLine2Poly.pas:81-256 --
// see docs/handoffs/T-0017B.md for the forward/return-trip trace that pins the 5/11-point test
// fixtures (§11.1 item 6).
class Line2PolyStream final : public IPointStream {
public:
    Line2PolyStream(PointStreamPtr source, double radius, const IProjection& projection,
                    std::vector<DoublePoint>& scratch)
        : source_(std::move(source)), radius_(radius), projection_(projection),
          datum_(*projection.projectionType()->datum()), scratch_(scratch) {}

    bool next(DoublePoint& out) override {
        if (returnScratch_) {
            out = scratch_[static_cast<std::size_t>(scratchIndex_)];
            --scratchIndex_;
            if (scratchIndex_ < 0) {
                returnScratch_ = false;
            }
            return true;
        }
        if (finished_) {
            out = emptyDoublePoint();
            return false;
        }

        bool result = false;
        while (!finished_) {
            DoublePoint currLonLat;
            finished_ = !source_->next(currLonLat);
            if (!finished_) {
                if (isSeparator(currLonLat)) {
                    finished_ = true;
                } else if (lineStarted_) {
                    result = stepStarted(currLonLat, out);
                    if (result) {
                        break;
                    }
                } else {
                    result = stepInitial(currLonLat, out);
                    if (result) {
                        break;
                    }
                }
            }
            if (finished_ && lineStarted_) {
                finishLine(out);
                result = true;
            }
        }
        if (!result) {
            out = emptyDoublePoint();
        }
        return result;
    }

private:
    [[nodiscard]] LonLat cornerAt(DoublePoint origin, double radius, double angle) const {
        DoublePoint p{origin.x() + radius * std::cos(angle), origin.y() + radius * std::sin(angle)};
        projection_.validatePixelPosFloat(p, false);
        return projection_.pixelPosFloat2LonLat(p);
    }

    // The steady-state step (u_EnumDoublePointLine2Poly.pas:130-158): a new vertex arrives while
    // a line is already in progress. Emits the forward corner at the *previous* vertex live,
    // stashes the mirrored reverse corner into scratch_.
    bool stepStarted(DoublePoint currLonLat, DoublePoint& out) {
        const DoublePoint currPoint = projection_.lonLat2PixelPosFloat(LonLat::fromPoint(currLonLat));
        const DoublePoint vector{currPoint.x() - prevPoint_.x(), currPoint.y() - prevPoint_.y()};
        const double dist = datum_.calcDist(LonLat::fromPoint(prevLonLat_), LonLat::fromPoint(currLonLat));
        if (dist <= 0.000001) {
            return false; // legacy: no Break, the point is silently skipped as a duplicate.
        }
        double lonLatMul = radius_ / dist;
        lonLatMul *= std::sqrt(vector.x() * vector.x() + vector.y() * vector.y());

        double currVectorAngle = std::atan2(vector.y(), vector.x());
        if (currVectorAngle < 0) {
            currVectorAngle += 2 * std::numbers::pi;
        }
        double angle = (currVectorAngle + prevVectorAngle_) / 2;
        if (std::abs(prevVectorAngle_ - currVectorAngle) > std::numbers::pi) {
            angle -= std::numbers::pi;
        }
        double a3 = std::abs((std::numbers::pi / 2 + angle) - currVectorAngle);
        if (a3 > std::numbers::pi) {
            a3 -= std::numbers::pi;
        }
        const double sinA3 = std::sin(a3);
        const double vRadius = (sinA3 < 1.0 / 7) ? (lonLatMul * 7) : (lonLatMul / sinA3);

        out = cornerAt(prevPoint_, vRadius, std::numbers::pi / 2 + angle).toPoint();
        scratch_.push_back(
            cornerAt(prevPoint_, vRadius, std::numbers::pi / 2 + angle + std::numbers::pi).toPoint());

        prevLonLat_ = currLonLat;
        prevPoint_ = currPoint;
        prevVectorAngle_ = currVectorAngle;
        prevRadius_ = lonLatMul;
        return true;
    }

    // The line-start step (u_EnumDoublePointLine2Poly.pas:161-208): the very first vertex,
    // peeking ahead at the second to seed the initial 45-degree bisector.
    bool stepInitial(DoublePoint firstLonLat, DoublePoint& out) {
        scratch_.clear();
        prevLonLat_ = firstLonLat;
        prevPoint_ = projection_.lonLat2PixelPosFloat(LonLat::fromPoint(firstLonLat));

        DoublePoint secondLonLat;
        finished_ = !source_->next(secondLonLat);
        if (!finished_ && isSeparator(secondLonLat)) {
            finished_ = true;
        }
        if (finished_) {
            // Only one point in the whole line: emit it verbatim, no polygon.
            out = prevLonLat_;
            return true;
        }

        const DoublePoint secondPoint = projection_.lonLat2PixelPosFloat(LonLat::fromPoint(secondLonLat));
        const DoublePoint vector{secondPoint.x() - prevPoint_.x(), secondPoint.y() - prevPoint_.y()};
        const double dist = datum_.calcDist(LonLat::fromPoint(prevLonLat_), LonLat::fromPoint(secondLonLat));
        if (dist <= 0.000001) {
            return false; // legacy: falls through, re-enters stepInitial on the next point.
        }
        double lonLatMul = radius_ / dist;
        lonLatMul *= std::sqrt(vector.x() * vector.x() + vector.y() * vector.y());

        double currVectorAngle = std::atan2(vector.y(), vector.x());
        if (currVectorAngle < 0) {
            currVectorAngle += 2 * std::numbers::pi;
        }
        const double angle = currVectorAngle;
        const double vRadius = lonLatMul / std::sin(std::numbers::pi / 4);

        const LonLat forwardCorner =
            cornerAt(prevPoint_, vRadius, std::numbers::pi / 2 + std::numbers::pi / 4 + angle);
        out = forwardCorner.toPoint();
        scratch_.push_back(forwardCorner.toPoint());
        scratch_.push_back(cornerAt(prevPoint_, vRadius,
                                    std::numbers::pi / 2 - std::numbers::pi / 4 + angle + std::numbers::pi)
                               .toPoint());

        lineStarted_ = true;
        prevLonLat_ = secondLonLat;
        prevPoint_ = secondPoint;
        prevVectorAngle_ = currVectorAngle;
        prevRadius_ = lonLatMul;
        return true;
    }

    // EOF/separator with a line in progress (u_EnumDoublePointLine2Poly.pas:227-246): emit the
    // final forward corner live, stash the final reverse corner, and start the return trip.
    void finishLine(DoublePoint& out) {
        const double angle = prevVectorAngle_;
        const double vRadius = prevRadius_ / std::sin(std::numbers::pi / 4);

        out = cornerAt(prevPoint_, vRadius, std::numbers::pi / 4 + angle).toPoint();
        scratch_.push_back(cornerAt(prevPoint_, vRadius,
                                    std::numbers::pi / 2 + std::numbers::pi / 4 + angle + std::numbers::pi)
                               .toPoint());
        scratchIndex_ = static_cast<long>(scratch_.size()) - 1;
        returnScratch_ = true;
    }

    PointStreamPtr source_;
    double radius_;
    const IProjection& projection_;
    const IDatum& datum_;
    std::vector<DoublePoint>& scratch_;

    bool lineStarted_ = false;
    bool finished_ = false;
    bool returnScratch_ = false;
    long scratchIndex_ = 0;
    DoublePoint prevLonLat_{};
    DoublePoint prevPoint_{};
    double prevVectorAngle_ = 0.0;
    double prevRadius_ = 0.0;
};

} // namespace

// --- free functions ----------------------------------------------------------------------------

PointStreamPtr pointsStream(std::span<const DoublePoint> points) {
    return std::make_unique<PointsByArrayStream>(points);
}

PointStreamPtr singleLineStream(std::span<const DoublePoint> points, const PointsMeta* meta, bool closed) {
    return std::make_unique<SingleLineStream>(points, meta, closed);
}

PointStreamPtr lineSetStream(const IGeometryLonLatMultiLine& lineSet) {
    return std::make_unique<LineSetStream>(lineSet);
}

PointStreamPtr closePoly(PointStreamPtr source) {
    return std::make_unique<ClosePolyStream>(std::move(source));
}

PointStreamPtr filterEqual(PointStreamPtr source) {
    return std::make_unique<FilterEqualStream>(std::move(source));
}

PointStreamPtr filterFirstSegment(PointStreamPtr source) {
    return std::make_unique<FilterFirstSegmentStream>(std::move(source));
}

PointStreamPtr lonLatToMapPixel(const IProjection& projection, PointStreamPtr lonLatSource) {
    return std::make_unique<LonLatToMapPixelStream>(projection, std::move(lonLatSource));
}

PointStreamPtr mapPixelToLocalPixel(const ILocalCoordConverter& converter, PointStreamPtr source) {
    return std::make_unique<MapPixelToLocalPixelStream>(converter, std::move(source));
}

PointStreamPtr mapPixelToLocalPixelSimple(DoublePoint topLeft, PointStreamPtr source) {
    return std::make_unique<MapPixelToLocalPixelSimpleStream>(topLeft, std::move(source));
}

PointStreamPtr line2Poly(PointStreamPtr lonLatSource, double radiusMetres, const IProjection& projection,
                         std::vector<DoublePoint>& scratch) {
    return std::make_unique<Line2PolyStream>(std::move(lonLatSource), radiusMetres, projection, scratch);
}

} // namespace cg::geo
