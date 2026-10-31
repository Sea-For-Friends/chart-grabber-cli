// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_ProjConverterByProj4.pas, u_ProjConverterFactory.pas, Proj4.Defines.pas
// (Proj4ArgsByEpsg). The only translation unit in the project allowed to #include <proj.h>
// (docs/specs/geo.md §7.1, ADR-0006 decision 3); tools/validate.sh's PROJ-LEAK grep enforces it.
//
// docs/specs/geo.md §7.3 numbers the PROJ-9 rules this file has to get right; each is called out
// by number below.

#include "cg/geo/proj_converter.h"

#include "cg/geo/log_geo.h"
#include "cg/geo/proj/proj4_defines.h"
#include "cg/geo/proj/proj_data_dir.h"

#include <QFileInfo>
#include <QString>
#include <QtGlobal>

#include <mutex>
#include <unordered_set>
#include <utility>

#if CG_HAVE_PROJ
#include <proj.h>
#endif

namespace cg::geo {

QByteArray proj4ArgsByEpsg(int epsg) {
    switch (epsg) {
    case 53004:
        return QByteArray(kEsri53004);
    case 3785: // deprecated alias, same string
    case 3857:
        return QByteArray(kEpsg3857);
    case 3395:
        return QByteArray(kEpsg3395);
    case 4269:
        return QByteArray(kNad83);
    case 4326:
        return QByteArray(kWgs84);
    default:
        break;
    }
    if (epsg >= 2463 && epsg <= 2491) { // Pulkovo 1995
        int lon0 = 21 + (epsg - 2463) * 6;
        if (lon0 > 180) {
            lon0 -= 360;
        }
        return QString(QLatin1String(kSk42GkFmt)).arg(lon0).arg(500000).arg(0).toLatin1();
    }
    if (epsg >= 2492 && epsg <= 2522) { // Pulkovo 1942
        int lon0 = 9 + (epsg - 2492) * 6;
        if (lon0 > 180) {
            lon0 -= 360;
        }
        return QString(QLatin1String(kSk42GkFmt)).arg(lon0).arg(500000).arg(0).toLatin1();
    }
    if (epsg >= 32601 && epsg <= 32660) {
        return QString(QLatin1String(kUtmNorthFmt)).arg(epsg - 32600).toLatin1();
    }
    if (epsg >= 32701 && epsg <= 32760) {
        return QString(QLatin1String(kUtmSouthFmt)).arg(epsg - 32700).toLatin1();
    }
    return QByteArray(); // legacy's "+init=epsg:<n>" fallback is dead under PROJ 9 (decision 5);
                         // the caller falls through to "EPSG:<n>" via proj.db instead.
}

#if CG_HAVE_PROJ

namespace {

// Rule 6: log once per CRS, never silently substitute an identity transform. Only the EPSG:<n>
// proj.db fallback (byEpsg for a code proj4ArgsByEpsg doesn't know) can pick a grid/ballpark
// pipeline instead of our explicit +towgs84 -- every other string in proj4_defines.h is either a
// plain ellipsoid/sphere or carries its own +towgs84, so this is the only path that needs it.
// (proj_coordoperation_get_accuracy() rejects the PJ* proj_create_crs_to_crs() returns -- "Object
// is not a CoordinateOperation" on every pipeline tested, UTM included -- so it cannot drive this
// decision; deviation recorded as D-15 in docs/parity/deviations.md.)
void logProjDbFallbackOnce(const QByteArray& key) {
    static std::mutex logMutex;
    static std::unordered_set<QByteArray> logged;
    const std::scoped_lock lock(logMutex);
    if (logged.insert(key).second) {
        qCWarning(logGeo) << "PROJ: custom CRS" << key
                          << "resolved via proj.db, not the legacy +towgs84 Helmert; results may "
                             "differ from the Windows build in the last metres";
    }
}

// Rule 7: one PJ_CONTEXT per thread, mandatory -- a PJ* must never cross threads. Rule 5: no grid
// downloads. Each distinct init string (`key_`) gets its own PJ* per context that asks for it;
// both are torn down together when the thread exits.
//
// Keyed on the interned QByteArray init string, not on `ProjConverterImpl*`: ProjConverterImpl
// objects are only weak_ptr-cached by ProjConverterFactory (proj_converter.h's `cache_`), so once
// every ProjConverterPtr a caller holds goes out of scope the object is freed, and its address can
// later be reused by a *different* key's converter (intern() only replaces the old weak_ptr, and
// frees its control block, on the next intern() of that same key). Keying this map on the address
// let a thread's stale entry for the old key silently answer a lookup for the new converter at the
// same address, transforming through the wrong CRS. Keying on the init string instead makes a hit
// correct by construction: the same string always builds the same PJ*, so sharing it across
// ProjConverterImpl instances that carry that string is exactly what we want, and a freed-then-
// reused address can never alias a different CRS's cache entry.
// T-0376: a relocated Linux CLI keeps proj.db next to the exe (Windows: libproj already looks in
// its own directory). An explicit PROJ_DATA / PROJ_LIB always wins; otherwise PROJ's own lookup.
void applyBundledProjData(PJ_CONTEXT* ctx) {
    // /proc/self/exe exists on Linux only; elsewhere (Windows) exeDir stays empty and nothing changes.
    const QString exeDir = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalPath();
    if (exeDir.isEmpty()) {
        return;
    }
    const QString dir = bundledProjDataDir(exeDir, qEnvironmentVariableIsSet("PROJ_DATA") ||
                                                       qEnvironmentVariableIsSet("PROJ_LIB"));
    if (!dir.isEmpty()) {
        const QByteArray path = dir.toUtf8();
        const char* paths[] = {path.constData()};
        proj_context_set_search_paths(ctx, 1, paths);
    }
}

struct ThreadProjState {
    PJ_CONTEXT* ctx = nullptr;
    std::unordered_map<QByteArray, PJ*> pjByKey;

    ThreadProjState() {
        ctx = proj_context_create();
        if (ctx != nullptr) {
            applyBundledProjData(ctx);
            proj_context_set_enable_network(ctx, 0);
        }
    }
    ThreadProjState(const ThreadProjState&) = delete;
    ThreadProjState& operator=(const ThreadProjState&) = delete;
    ~ThreadProjState() {
        for (const auto& entry : pjByKey) {
            if (entry.second != nullptr) { // a cached construction failure stores nullptr
                proj_destroy(entry.second);
            }
        }
        if (ctx != nullptr) {
            proj_context_destroy(ctx);
        }
    }
};

ThreadProjState& threadState() {
    thread_local ThreadProjState state;
    return state;
}

// Rule 1: build with proj_create_crs_to_crs, not pj_init_plus_ctx/pj_transform (removed in PROJ
// 8+). `isEpsgFallback` is true only for the "EPSG:<n>" proj.db path (byEpsg's unknown-code
// fallback): that string is a full CRS spec on its own, so unlike a proj4 init string it takes no
// "+type=crs" suffix, and rule 3 requires proj_normalize_for_visualization() to force lon,lat /
// easting,northing axis order (an authority CRS object otherwise honours its own, e.g. lat,lon
// for a bare geographic EPSG code).
PJ* buildPj(PJ_CONTEXT* ctx, const QByteArray& key, bool isEpsgFallback) {
    if (ctx == nullptr) {
        return nullptr;
    }
    PJ* pj = nullptr;
    if (isEpsgFallback) {
        PJ* raw = proj_create_crs_to_crs(ctx, kWgs84, key.constData(), nullptr);
        if (raw != nullptr) {
            pj = proj_normalize_for_visualization(ctx, raw); // rule 3
            proj_destroy(raw);
        }
        if (pj != nullptr) {
            logProjDbFallbackOnce(key);
        }
    } else {
        const QByteArray target = key + " +type=crs";
        pj = proj_create_crs_to_crs(ctx, kWgs84, target.constData(), nullptr);
    }
    if (pj == nullptr) {
        const int err = proj_context_errno(ctx);
        qCWarning(logGeo) << "PROJ: cannot build CRS" << key << ":" << proj_context_errno_string(ctx, err);
    }
    return pj;
}

class ProjConverterImpl final : public IProjConverter {
public:
    ProjConverterImpl(QByteArray key, bool isEpsgFallback)
        : key_(std::move(key)), isEpsgFallback_(isEpsgFallback) {}

    [[nodiscard]] std::optional<DoublePoint> lonLat2XY(LonLat lonLat) const override {
        // Rule 2: degrees, not radians -- a crs_to_crs object takes the CRS's own units, which
        // for our WGS84 source (+proj=longlat) is degrees. `pj_transform`'s DEG_TO_RAD multiply
        // has no place here; keeping it would be wrong by a factor of 57.3 (proj_converter_test.cpp
        // Utm37NRoundTripsToTheMetre pins this).
        const auto out = transform(PJ_FWD, proj_coord(lonLat.lon, lonLat.lat, 0, 0));
        if (!out) {
            return std::nullopt;
        }
        return DoublePoint(out->xy.x, out->xy.y);
    }

    [[nodiscard]] std::optional<LonLat> xy2LonLat(DoublePoint xy) const override {
        const auto out = transform(PJ_INV, proj_coord(xy.x(), xy.y(), 0, 0));
        if (!out) {
            return std::nullopt;
        }
        return LonLat{out->xy.x, out->xy.y};
    }

private:
    [[nodiscard]] std::optional<PJ_COORD> transform(PJ_DIRECTION dir, PJ_COORD in) const {
        ThreadProjState& state = threadState();
        PJ* pj = pjFor(state);
        if (pj == nullptr) {
            return std::nullopt;
        }
        proj_errno_reset(pj);
        const PJ_COORD out = proj_trans(pj, dir, in);
        const int err = proj_errno(pj);
        if (err != 0) {
            qCWarning(logGeo) << "PROJ: transform failed for" << key_ << ":"
                              << proj_context_errno_string(state.ctx, err);
            return std::nullopt;
        }
        return out;
    }

    [[nodiscard]] PJ* pjFor(ThreadProjState& state) const {
        auto it = state.pjByKey.find(key_);
        if (it != state.pjByKey.end()) {
            return it->second;
        }
        PJ* pj = buildPj(state.ctx, key_, isEpsgFallback_);
        state.pjByKey.emplace(key_, pj); // cache failure too: a broken init string stays broken,
                                         // no point retrying every call.
        return pj;
    }

    QByteArray key_;
    bool isEpsgFallback_;
};

} // namespace

#endif // CG_HAVE_PROJ

ProjConverterFactory::ProjConverterFactory() = default;

bool ProjConverterFactory::isAvailable() const noexcept {
#if CG_HAVE_PROJ
    // Rule 8: proj.db must be found at runtime (PROJ_DATA); a fresh, disposable context is used
    // (rather than the shared thread-local one) so this reflects the environment as it is right
    // now, not whatever a prior call happened to resolve.
    PJ_CONTEXT* ctx = proj_context_create();
    if (ctx == nullptr) {
        return false;
    }
    applyBundledProjData(ctx);
    const bool ok = proj_context_get_database_path(ctx) != nullptr;
    proj_context_destroy(ctx);
    return ok;
#else
    return false;
#endif
}

ProjConverterPtr ProjConverterFactory::intern(const QByteArray& key, bool isEpsgFallback) const {
#if CG_HAVE_PROJ
    if (key.isEmpty()) {
        return nullptr;
    }
    const std::scoped_lock lock(mutex_);
    if (auto it = cache_.find(key); it != cache_.end()) {
        if (ProjConverterPtr existing = it->second.lock()) {
            return existing;
        }
    }
    // Validate eagerly on the calling thread (mirrors legacy raising on AfterConstruction), then
    // discard: each thread that actually uses this converter builds and caches its own PJ*
    // lazily (rule 7 -- a PJ* must never cross threads).
    PJ* probe = buildPj(threadState().ctx, key, isEpsgFallback);
    if (probe == nullptr) {
        return nullptr;
    }
    proj_destroy(probe);
    auto converter = std::make_shared<ProjConverterImpl>(key, isEpsgFallback);
    cache_[key] = converter;
    return converter;
#else
    (void)key;
    (void)isEpsgFallback;
    return nullptr;
#endif
}

ProjConverterPtr ProjConverterFactory::byEpsg(int epsg) const {
    QByteArray args = proj4ArgsByEpsg(epsg);
    if (!args.isEmpty()) {
        return intern(args, /*isEpsgFallback=*/false);
    }
    return intern("EPSG:" + QByteArray::number(epsg), /*isEpsgFallback=*/true);
}

ProjConverterPtr ProjConverterFactory::byInitString(const QByteArray& proj4Args) const {
    static const QByteArray kEpsgPrefix = "EPSG:";
    // Case-insensitive, matching legacy's SameTextA(Copy(AArgs, 1, 5), 'EPSG:').
    if (proj4Args.left(kEpsgPrefix.size()).compare(kEpsgPrefix, Qt::CaseInsensitive) == 0) {
        bool ok = false;
        const int epsg = proj4Args.mid(kEpsgPrefix.size()).toInt(&ok);
        if (ok) {
            return byEpsg(epsg); // u_ProjConverterFactory.pas:115-131
        }
    }
    return intern(proj4Args, /*isEpsgFallback=*/false);
}

} // namespace cg::geo
