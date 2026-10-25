// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
//
// docs/specs/geo.md names an injected `core::HashFunction` parameter on DatumFactory,
// ProjectionTypeFactory, ProjectionSetFactory and LocalCoordConverterFactory (e.g. §2.3.3
// "explicit DatumFactory(core::HashFunction hash = {})", and the ad-hoc-type construction formula
// "hash(1, d, 0), d, epsg = 0" for a fresh MercatorSphereProjectionType in §2.4). No such type
// exists in cg::core, and per the T-0016 review/ruling it never will (core.md §1.2 deleted
// IHashFunction deliberately, and there is exactly one implementation to inject): this is geo's
// own helper, not a stand-in for a type cg::core will later provide.
//
// This header supplies the minimal callable geo's own factories need, built on
// core::hashOf/core::hashUpdate. It reproduces the legacy CalcHashByX/UpdateHashByX chaining
// (u_HashFunctionByImpl.pas) that every "Hash seed: ..." note in the spec describes: the first
// argument starts a fresh hash (CalcHashByX), every later argument folds into it (UpdateHashByX).
// Per docs/specs/geo.md §2.3.3, the concrete values never need to match the Windows build --
// only "equal inputs hash equal, unequal inputs (almost certainly) hash unequal" is required.

#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"

#include <QtGlobal>

#include <memory>

namespace cg::geo {

class HashSeed {
public:
    template <class First, class... Rest>
    [[nodiscard]] core::HashValue operator()(const First& first, const Rest&... rest) const noexcept {
        core::HashValue h = start(first);
        (fold(h, rest), ...);
        return h;
    }

private:
    // Scalars: start() is CalcHashByX (core::hashOf), fold() is UpdateHashByX (core::hashUpdate).
    // The two primitives differ (hashUpdate seeds CityHash with the running value; hashOf does
    // not), so they cannot be collapsed into one without changing the hash chain.
    [[nodiscard]] core::HashValue start(int v) const noexcept { return core::hashOf(static_cast<qint32>(v)); }
    [[nodiscard]] core::HashValue start(double v) const noexcept { return core::hashOf(v); }
    [[nodiscard]] core::HashValue start(core::HashValue v) const noexcept { return v; }
    template <class T> [[nodiscard]] core::HashValue start(const std::shared_ptr<const T>& p) const noexcept {
        return p ? p->hash() : core::kNoHash;
    }
    static void fold(core::HashValue& h, int v) noexcept { core::hashUpdate(h, static_cast<qint32>(v)); }
    static void fold(core::HashValue& h, double v) noexcept { core::hashUpdate(h, v); }
    static void fold(core::HashValue& h, core::HashValue v) noexcept { core::hashUpdate(h, v); }
    template <class T> static void fold(core::HashValue& h, const std::shared_ptr<const T>& p) noexcept {
        core::hashUpdate(h, p ? p->hash() : core::kNoHash);
    }

    // Composite types (PointI, DoublePoint, RectI) decompose to their scalar members: start()
    // seeds from the first member via operator() itself instead of repeating the scalar/fold
    // split per type, so only fold() -- needed when the composite is not the first argument --
    // states the member list.
    [[nodiscard]] core::HashValue start(PointI p) const noexcept { return (*this)(p.x(), p.y()); }
    [[nodiscard]] core::HashValue start(DoublePoint p) const noexcept { return (*this)(p.x(), p.y()); }
    [[nodiscard]] core::HashValue start(const RectI& r) const noexcept {
        return (*this)(r.left, r.top, r.right, r.bottom);
    }
    static void fold(core::HashValue& h, PointI p) noexcept {
        fold(h, p.x());
        fold(h, p.y());
    }
    static void fold(core::HashValue& h, DoublePoint p) noexcept {
        fold(h, p.x());
        fold(h, p.y());
    }
    static void fold(core::HashValue& h, const RectI& r) noexcept {
        fold(h, r.left);
        fold(h, r.top);
        fold(h, r.right);
        fold(h, r.bottom);
    }
};

} // namespace cg::geo
