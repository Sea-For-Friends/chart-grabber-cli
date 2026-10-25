// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (test double). See docs/specs/script.md §11.1.
//
// T-0015A: the shared test double for `IScriptHost` (script_host.h), used by interpreter_test.cpp
// to reach the 7 host-backed builtins (Random/RandomRange/FileExists/GetUnixTime/
// SaveToLocalFile/WriteLn/TemplateToUrl). Deliberately minimal, per the ticket's scope: a seeded
// LCG for `random()` (the exact Delphi generator, spec 5.3 F1), a frozen `unixTime()`,
// `writeLn`/`fileExists`/`saveToLocalFile` that record every call, and a `templateToUrl` that
// substitutes only `{x}`/`{y}`/`{z}` (the full `{x}/{y}/{z}/{-y}/{q}/{bbox}/{s}/...` engine, spec
// 5.5, belongs to `maptype`, FEAT-NET-004).
//
// T-0015B: the 7 script-visible interface fakes (ICoordConverter, IProjConverter,
// IProjConverterFactory, IHttpDownloader, IScriptGlobal, IScriptLogger, IScriptTileCache) below -
// deliberately minimal, only what interpreter_test.cpp's method-call dispatch tests need (ticket
// Scope). None of these ports a legacy *implementation* (Scope (out): "the real implementations of
// the interfaces (maptype, storage, net)" arrive with later tickets) - their formulas/storage are
// this test double's own, internally-consistent choices, not a legacy port, except where a
// docstring says otherwise (the spec-mandated "empty write is dropped" and "nil result on an
// unknown code" rules).

#include "cg/script/script_host.h"
#include "cg/script/value.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include <map>
#include <tuple>

namespace cg::script::test {

class FakeScriptHost : public IScriptHost {
public:
    struct SaveCall {
        QString path;
        QByteArray data;
    };

    // `seed` is System.Random's RandSeed at construction (spec 5.3 F1) - the golden-URL harness
    // and this ticket's tests pass a fixed value so a run is reproducible.
    explicit FakeScriptHost(quint32 seed = 1) : randSeed_(seed) {}

    [[nodiscard]] QByteArray templateToUrl(const QByteArray& tmpl) const override;
    int saveToLocalFile(const QString& fullPath, const QByteArray& data) override;
    [[nodiscard]] bool fileExists(const QString& fullPath) const override;
    void writeLn(const QByteArray& text) override;
    [[nodiscard]] qint64 unixTime() const override { return unixTime_; }
    [[nodiscard]] qint32 random(qint32 upperExclusive) override;

    // --- test configuration ---
    void setUnixTime(qint64 t) { unixTime_ = t; }
    void setTile(qint32 x, qint32 y, qint32 z) {
        tileX_ = x;
        tileY_ = y;
        tileZ_ = z;
    }
    void addExistingFile(const QString& path) { existingFiles_.insert(path); }

    // --- test inspection: every call recorded in order ---
    [[nodiscard]] const QList<QByteArray>& writtenLines() const { return written_; }
    [[nodiscard]] const QList<SaveCall>& saveCalls() const { return saveCalls_; }
    [[nodiscard]] const QList<QString>& fileExistsCalls() const { return fileExistsCalls_; }

private:
    quint32 randSeed_;
    qint64 unixTime_ = 0;
    qint32 tileX_ = 0, tileY_ = 0, tileZ_ = 0;
    QSet<QString> existingFiles_;
    QList<QByteArray> written_;
    QList<SaveCall> saveCalls_;
    mutable QList<QString> fileExistsCalls_;
};

// --- ICoordConverter (spec 2.7/5.6, ticket: "a spherical Mercator with R = 6378137") ---------------
// A standard slippy-map tiling (256px tiles, spherical Web Mercator) - self-consistent, not a
// legacy port (Scope (out)). zoom 0, pixel (128,128) is the tiling's centre, i.e. lon/lat (0,0) -
// the clean round-trip point interpreter_test.cpp uses.
class FakeCoordConverter : public ICoordConverter {
public:
    [[nodiscard]] DoublePoint pos2LonLat(Point xy, quint8 zoom) const override;
    [[nodiscard]] Point lonLat2Pos(DoublePoint ll, quint8 zoom) const override;
    [[nodiscard]] DoublePoint lonLat2Metr(DoublePoint ll) const override;
    [[nodiscard]] DoublePoint metr2LonLat(DoublePoint mm) const override;
    [[nodiscard]] qint32 tilesAtZoom(quint8 zoom) const override { return qint32{1} << zoom; }
    [[nodiscard]] qint32 pixelsAtZoom(quint8 zoom) const override { return 256 * tilesAtZoom(zoom); }
    [[nodiscard]] Point tilePos2PixelPos(Point xy, quint8 zoom) const override;
    [[nodiscard]] Rect tilePos2PixelRect(Point xy, quint8 zoom) const override;
};

// --- IProjConverter / IProjConverterFactory ---------------------------------------------------
// An arbitrary affine transform (XY = LonLat * 1000), not a real projection - only self-consistency
// (xy2LonLat(lonLat2XY(p)) == p) matters for the dispatch/marshalling tests this fake serves.
class FakeProjConverter : public IProjConverter {
public:
    [[nodiscard]] DoublePoint lonLat2XY(DoublePoint projLonLat) const override {
        return DoublePoint{projLonLat.x * 1000.0, projLonLat.y * 1000.0};
    }
    [[nodiscard]] DoublePoint xy2LonLat(DoublePoint projXY) const override {
        return DoublePoint{projXY.x / 1000.0, projXY.y / 1000.0};
    }
};

// spec 5.6/script_host.h: both methods return nullptr for an unknown code/init-string - the script
// sees nil, Assigned() is False. `kKnownEpsg`/`kKnownInitString` are this fake's only "known" cases.
class FakeProjConverterFactory : public IProjConverterFactory {
public:
    static constexpr int kKnownEpsg = 3857;
    static constexpr const char* kKnownInitString = "known";

    [[nodiscard]] std::shared_ptr<const IProjConverter> byEpsg(int epsg) const override {
        return epsg == kKnownEpsg ? std::make_shared<FakeProjConverter>() : nullptr;
    }
    [[nodiscard]] std::shared_ptr<const IProjConverter>
    byInitString(const QByteArray& projArgs) const override {
        return projArgs == kKnownInitString ? std::make_shared<FakeProjConverter>() : nullptr;
    }
};

// --- IHttpDownloader (ticket: "a queue of canned replies") -----------------------------------------
class FakeHttpDownloader : public IHttpDownloader {
public:
    struct Reply {
        quint32 status = 0;
        QByteArray responseHeader;
        QByteArray responseData;
    };
    struct Call {
        QByteArray url;
        QByteArray requestHeader;
        QByteArray postData;
    };

    // Replies are consumed in FIFO order, one per doHttpRequest() call; an empty queue yields the
    // default Reply{} (status 0, empty strings) rather than an out-of-bounds access.
    void enqueueReply(Reply reply) { queue_.push_back(std::move(reply)); }
    [[nodiscard]] const QList<Call>& calls() const { return calls_; }

    quint32 doHttpRequest(const QByteArray& url, const QByteArray& requestHeader, const QByteArray& postData,
                          QByteArray& responseHeader, QByteArray& responseData) override;

private:
    QList<Reply> queue_;
    QList<Call> calls_;
};

// --- IScriptGlobal -----------------------------------------------------------------------------
// spec 5.6: SetVar/GetVar/Exists do no locking of their own (the script brackets them with its own
// Lock/Unlock); the …TS variants self-lock. This fake is only ever driven single-threaded by
// interpreter_test.cpp, so `lockDepth_`/`lockReadDepth_` just record nesting for inspection - real
// mutual exclusion is a later ticket's production `IScriptGlobal` (maptype), out of this ticket's
// scope.
class FakeScriptGlobal : public IScriptGlobal {
public:
    void setVar(int varId, const Value& v) override { items_.insert(varId, v); }
    [[nodiscard]] Value getVar(int varId) const override {
        return items_.value(varId, Value());
    } // Empty absent
    [[nodiscard]] bool exists(int varId) const override { return items_.contains(varId); }
    void setVarTs(int varId, const Value& v) override { setVar(varId, v); }
    [[nodiscard]] Value getVarTs(int varId) const override { return getVar(varId); }
    [[nodiscard]] bool existsTs(int varId) const override { return exists(varId); }
    void lock() override { ++lockDepth_; }
    void unlock() override { --lockDepth_; }
    void lockRead() override { ++lockReadDepth_; }
    void unlockRead() override { --lockReadDepth_; }

    [[nodiscard]] int lockDepth() const { return lockDepth_; }
    [[nodiscard]] int lockReadDepth() const { return lockReadDepth_; }

private:
    QMap<int, Value> items_;
    int lockDepth_ = 0;
    int lockReadDepth_ = 0;
};

// --- IScriptLogger -------------------------------------------------------------------------------
class FakeScriptLogger : public IScriptLogger {
public:
    // spec 5.6: "an empty string is silently dropped before the lock" - legacy
    // (u_PascalScriptLogger.pas:121-127) does this inside the implementation itself, not at the
    // interpreter's dispatch site (interpreter.cpp always forwards unconditionally), so it belongs
    // here.
    void write(const QByteArray& text) override {
        if (!text.isEmpty()) {
            writes_.push_back(text);
        }
    }
    [[nodiscard]] const QList<QByteArray>& writes() const { return writes_; }

private:
    QList<QByteArray> writes_;
};

// --- IScriptTileCache ----------------------------------------------------------------------------
// An in-memory store keyed by (x, y, zoom, version) - enough for a Write-then-Read round-trip of
// the TTileInfo layout (spec 5.2); does not reproduce the storage-layer nuances of
// u_PascalScriptTileCache.pas (partial TNE re-query on a data miss, "" as "default version", ...)
// since the real ITileStorage binding is Scope (out).
class FakeScriptTileCache : public IScriptTileCache {
public:
    [[nodiscard]] TileInfo read(int x, int y, int zoom, const QByteArray& version,
                                bool withData) const override;
    bool write(int x, int y, int zoom, const QByteArray& version, const QByteArray& contentType,
               const QByteArray& data, bool overwrite) override;
    bool writeTne(int x, int y, int zoom, const QByteArray& version) override;
    bool remove(int x, int y, int zoom, const QByteArray& version) override;

private:
    struct Key {
        int x = 0, y = 0, zoom = 0;
        QByteArray version;
        bool operator<(const Key& o) const {
            return std::tie(x, y, zoom, version) < std::tie(o.x, o.y, o.zoom, o.version);
        }
    };
    struct Entry {
        bool tne = false;
        QByteArray contentType;
        QByteArray data;
        QDateTime loadDate;
    };
    std::map<Key, Entry> tiles_;
};

} // namespace cg::script::test
