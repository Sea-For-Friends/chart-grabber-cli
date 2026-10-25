// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PascalScriptTypes.pas:51-59,139-150 (the `AddTypeS` records),
// i_PascalScriptTileCache.pas:27-35 (TTileInfo),
// Src/Network/u_TileDownloadRequestBuilderPascalScriptVars.pas:106-155,157-190,192-267,269-292
// (the 26 bound variables and their registration order),
// i_PascalScriptTileCache.pas / i_PascalScriptLogger.pas / i_PascalScriptGlobal.pas /
// i_CoordConverterSimple.pas / i_ProjConverter.pas / i_ProjConverterFactory.pas /
// i_SimpleHttpDownloader.pas (the 7 script-visible interfaces).
//
// T-0015A: grows the Slice A record-only stub to the full spec 2.7 surface - the 7 pure-abstract
// interfaces, IScriptHost (the host facade the registered free functions in interpreter.cpp call
// through) and ScriptVars (the 26 bound variables). Declarations only: no implementation lives
// here (the production host is a later ticket; the test double is tests/script/script_host_fake.h).
// Method calls on an interface variable stay ErrorCode::Unsupported until T-0015B. This is the
// only header cg::maptype includes (spec 2.7 preamble) - every interface is pure abstract, no
// data members, and mentions no `net`/`storage`/`geo` type.

#include <QByteArray>
#include <QDateTime>
#include <QFlags>
#include <QString>

#include <cstdint>
#include <memory>

namespace cg::script {

class Value; // value.h - forward-declared only; IScriptGlobal never needs the complete type here.

// --- the 6 registered record types (u_PascalScriptTypes.pas:51-59) ----------------------------
struct Point {
    qint32 x = 0;
    qint32 y = 0;
};
struct DoublePoint {
    double x = 0.0;
    double y = 0.0;
};
struct Rect {
    qint32 left = 0;
    qint32 top = 0;
    qint32 right = 0;
    qint32 bottom = 0;
};
struct DoubleRect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

enum class ReplaceFlag : std::uint8_t { ReplaceAll = 0, IgnoreCase = 1 };
using ReplaceFlags = QFlags<ReplaceFlag>;

// Registered by the tile-cache unit, not by u_PascalScriptTypes - kept here because it is the
// seventh, ubiquitous record shape the SAS API surface exposes to a script.
struct TileInfo {
    bool isExists = false;
    bool isExistsTne = false;
    QDateTime loadDate;
    qint64 size = 0;
    QByteArray version;
    QByteArray contentType;
    QByteArray data;
};

// --- the 7 script-visible interfaces (spec 2.7) -------------------------------------------------

class ICoordConverter {
public:
    virtual ~ICoordConverter() = default;
    [[nodiscard]] virtual DoublePoint pos2LonLat(Point xy, quint8 zoom) const = 0;
    [[nodiscard]] virtual Point lonLat2Pos(DoublePoint ll, quint8 zoom) const = 0;
    [[nodiscard]] virtual DoublePoint lonLat2Metr(DoublePoint ll) const = 0;
    [[nodiscard]] virtual DoublePoint metr2LonLat(DoublePoint mm) const = 0;
    [[nodiscard]] virtual qint32 tilesAtZoom(quint8 zoom) const = 0;
    [[nodiscard]] virtual qint32 pixelsAtZoom(quint8 zoom) const = 0;
    [[nodiscard]] virtual Point tilePos2PixelPos(Point xy, quint8 zoom) const = 0;
    [[nodiscard]] virtual Rect tilePos2PixelRect(Point xy, quint8 zoom) const = 0;
};

class IProjConverter {
public:
    virtual ~IProjConverter() = default;
    [[nodiscard]] virtual DoublePoint lonLat2XY(DoublePoint projLonLat) const = 0;
    [[nodiscard]] virtual DoublePoint xy2LonLat(DoublePoint projXY) const = 0;
};

class IProjConverterFactory {
public:
    virtual ~IProjConverterFactory() = default;
    // Both return nullptr when the code/init-string is unknown; the script sees nil and
    // Assigned() is False, exactly as in legacy.
    [[nodiscard]] virtual std::shared_ptr<const IProjConverter> byEpsg(int epsg) const = 0;
    [[nodiscard]] virtual std::shared_ptr<const IProjConverter>
    byInitString(const QByteArray& projArgs) const = 0;
};

class IHttpDownloader {
public:
    virtual ~IHttpDownloader() = default;
    // FEAT-NET-016. Blocking. Called on the download worker thread that is running the script,
    // with the per-map script lock held - see spec 8. 5 script-visible arguments, not 3 returning
    // a record (T-0013 review D-2, ruled 2026-09-18): corpus-verified across GGC/*,
    // Google/GoogleSat.zmp, Navionics/* and Yandex/YaTraf.zmp, which all pass exactly 5. The two
    // `var` output parameters are observable independently of the result - a script reads
    // `responseHeader` after the call even when the Cardinal result is non-zero - so they cannot
    // be folded into a return-by-value record; legacy is truth over an earlier record-shape draft.
    [[nodiscard]] virtual quint32 doHttpRequest(const QByteArray& url, const QByteArray& requestHeader,
                                                const QByteArray& postData, QByteArray& responseHeader,
                                                QByteArray& responseData) = 0;
};

class IScriptGlobal {
public:
    virtual ~IScriptGlobal() = default;
    // FEAT-NET-018. Per ZMP (shared by its download slots). Variant -> Value (value.h).
    virtual void setVar(int varId, const Value& v) = 0;
    [[nodiscard]] virtual Value getVar(int varId) const = 0; // Empty when absent
    [[nodiscard]] virtual bool exists(int varId) const = 0;
    virtual void setVarTs(int varId, const Value& v) = 0;
    [[nodiscard]] virtual Value getVarTs(int varId) const = 0;
    [[nodiscard]] virtual bool existsTs(int varId) const = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual void lockRead() = 0;
    virtual void unlockRead() = 0;
};

class IScriptLogger {
public:
    virtual ~IScriptLogger() = default;
    virtual void write(const QByteArray& text) = 0; // FEAT-NET-020
};

class IScriptTileCache {
public:
    virtual ~IScriptTileCache() = default;
    // FEAT-NET-017
    [[nodiscard]] virtual TileInfo read(int x, int y, int zoom, const QByteArray& version,
                                        bool withData) const = 0;
    virtual bool write(int x, int y, int zoom, const QByteArray& version, const QByteArray& contentType,
                       const QByteArray& data, bool overwrite) = 0;
    virtual bool writeTne(int x, int y, int zoom, const QByteArray& version) = 0;
    virtual bool remove(int x, int y, int zoom, const QByteArray& version) = 0; // 'Delete'
};

// --- the host facade: everything the registered free functions need -----------------------------
class IScriptHost {
public:
    virtual ~IScriptHost() = default;

    // FEAT-NET-004 via TemplateToUrl: the {x}/{y}/{z}/{-y}/{q}/{bbox}/{s}/{timestamp}/{ver}/
    // {lang}/{sas_path} mini-language, owned by maptype (spec 5.5). Out of this ticket's scope;
    // the fake host substitutes only {x}/{y}/{z}.
    [[nodiscard]] virtual QByteArray templateToUrl(const QByteArray& tmpl) const = 0;

    // SaveToLocalFile (F36). Returns the byte count written, or 0 on any failure -- never a
    // negative code (legacy u_PascalScriptUtils.pas:197-199; spec 2.7).
    virtual int saveToLocalFile(const QString& fullPath, const QByteArray& data) = 0;
    [[nodiscard]] virtual bool fileExists(const QString& fullPath) const = 0;

    // WriteLn - the IDE console / stderr sink. Distinct from IScriptLogger (FEAT-NET-020), which
    // is the per-ZMP log file the script reaches through the `Logger` bound variable.
    virtual void writeLn(const QByteArray& text) = 0;

    // Unix seconds. Injected so the corpus tests can freeze time.
    [[nodiscard]] virtual qint64 unixTime() const = 0;

    // Random(X) -> [0, X). Injected so the corpus tests can seed it deterministically.
    [[nodiscard]] virtual qint32 random(qint32 upperExclusive) = 0;
};

// --- the 26 bound variables (u_TileDownloadRequestBuilderPascalScriptVars.pas:106-155) ----------
// Field order here has no significance of its own (unlike the record types above) - the
// interpreter binds each field to a fixed slot index 0..25 in registration order (not by name),
// which is what carries the spec 5.1 registration order.
struct ScriptVars {
    // in
    qint32 getX = 0, getY = 0;
    qint32 getZ = 1; // 1-based: internal zoom + 1
    double getLlon = 0, getTLat = 0, getBLat = 0, getRLon = 0;
    double getLMetr = 0, getRMetr = 0, getTMetr = 0, getBMetr = 0;
    QByteArray getUrlBase;
    QByteArray responseHead;
    QByteArray version;
    QByteArray lang;

    // in/out
    QByteArray requestHead;  // pre-filled from params.txt, script may rewrite
    QByteArray scriptBuffer; // persists across calls for this map (FEAT-NET-019)

    // out
    QByteArray resultUrl; // empty => tile does not exist (NOT an error)
    QByteArray postData;  // non-empty => request becomes POST

    // interfaces (any may be null; the script sees nil and Assigned() is False)
    std::shared_ptr<const ICoordConverter> converter;
    std::shared_ptr<const IProjConverter> defProjConverter;
    std::shared_ptr<const IProjConverterFactory> projFactory;
    std::shared_ptr<IHttpDownloader> downloader; // null unless IsUseDownloaderInScript
    std::shared_ptr<IScriptGlobal> global;
    std::shared_ptr<IScriptLogger> logger;
    std::shared_ptr<IScriptTileCache> tileCache;
};

} // namespace cg::script
