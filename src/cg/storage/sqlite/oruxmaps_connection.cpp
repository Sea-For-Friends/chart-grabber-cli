// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/oruxmaps_connection.h"

#include "cg/core/delphi_text.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace cg::storage::sqlite {

namespace {

// Ports SetBit/IsBitSet (:112-119) with the same masked-shift-count convention landed for
// invertY()/invertZ() in the sibling flavours (mbtiles_connection.cpp, rmaps_connection.cpp): the
// compiled SHL/test-bit instruction masks its count to 5 bits, so a bit index is folded into
// [0,31] here explicitly instead of relying on `1 << n`'s C++ undefined behaviour for n >= 32.
// Every bit basePointsStrToArray() ever SETS is < 24 (zoomCount()), so any index folded into
// [24,31] is simply never set -- isBitSet() returns false for it exactly like legacy's IsBitSet
// would for the same folded shift count.
[[nodiscard]] quint32 bitIndex(quint32 z) {
    return z & 31u;
}
void setBit(quint32& bits, quint32 index) {
    bits |= (1u << index);
}
[[nodiscard]] bool isBitSet(quint32 bits, quint32 index) {
    return (bits & (1u << index)) != 0;
}

// Ports the (Windows-only, case-insensitive-by-OS-convention) '*.otrk2.xml' match
// TDirectory.GetFiles performs (:202-203) -- reproduced explicitly instead of relying on the host
// file system (coding standard: "never assume ... case-insensitive FS"), ASCII-only (Tech Lead
// ruling: ported SameText/LowerCase fold ASCII letters only).
[[nodiscard]] bool asciiEqualFold(QChar a, QChar b) {
    const auto fold = [](QChar c) -> char16_t {
        const char16_t u = c.unicode();
        return (u >= u'A' && u <= u'Z') ? static_cast<char16_t>(u + (u'a' - u'A')) : u;
    };
    return fold(a) == fold(b);
}
[[nodiscard]] bool endsWithOtrk2XmlCi(const QString& name) {
    static const QString kSuffix = QStringLiteral(".otrk2.xml");
    if (name.size() < kSuffix.size()) {
        return false;
    }
    const QStringView tail = QStringView(name).right(kSuffix.size());
    for (qsizetype i = 0; i < kSuffix.size(); ++i) {
        if (!asciiEqualFold(tail[i], kSuffix[i])) {
            return false;
        }
    }
    return true;
}

// --- TOruxMapsXmlFile.Parse + SaveToBase64String (Includes/OruxMapsXmlFile.pas:62-178, 180-267) ---

struct OruxLayer {
    QString datum;
    QString projection;
    qint32 zoom = 0;
    double minLat = 0.0;
    double maxLat = 0.0;
    double minLon = 0.0;
    double maxLon = 0.0;
};

// The slice of IXMLNode legacy touches: element name, attributes, direct text, element children.
struct XmlNode {
    QString name;
    QHash<QString, QString> attributes;
    QString text;
    std::vector<XmlNode> children;

    // IXMLNodeList.FindNode: first child with this name.
    [[nodiscard]] const XmlNode* find(QStringView childName) const {
        for (const XmlNode& child : children) {
            if (child.name == childName) {
                return &child;
            }
        }
        return nullptr;
    }
    // IXMLNode.GetAttribute: legacy gets a Null variant for an absent attribute and the string
    // conversion raises (L-81), failing the whole parse; `failed` records that, '' is returned.
    [[nodiscard]] QString attribute(const QString& key, bool& failed) const {
        const auto it = attributes.constFind(key);
        if (it == attributes.constEnd()) {
            failed = true;
            return {};
        }
        return *it;
    }
};

// A document that does not load makes LoadXMLDocument raise in legacy; the connection reports it as
// the same "Error reading XML file!" a rejected document gets (no separate message exists).
[[nodiscard]] std::optional<XmlNode> loadXmlRoot(const QString& fileName) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QXmlStreamReader xml(&file);
    std::vector<XmlNode> stack;
    std::optional<XmlNode> root;
    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement: {
            XmlNode node;
            node.name = xml.qualifiedName().toString();
            for (const QXmlStreamAttribute& a : xml.attributes()) {
                node.attributes.insert(a.qualifiedName().toString(), a.value().toString());
            }
            stack.push_back(std::move(node));
            break;
        }
        case QXmlStreamReader::Characters:
            if (!stack.empty()) {
                stack.back().text += xml.text();
            }
            break;
        case QXmlStreamReader::EndElement: {
            XmlNode done = std::move(stack.back());
            stack.pop_back();
            if (stack.empty()) {
                root = std::move(done);
            } else {
                stack.back().children.push_back(std::move(done));
            }
            break;
        }
        default:
            break;
        }
    }
    if (xml.hasError()) {
        return std::nullopt;
    }
    return root;
}

[[nodiscard]] bool sameTextAscii(const QString& a, QLatin1StringView b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (!asciiEqualFold(a[i], QChar(b[i]))) {
            return false;
        }
    }
    return true;
}

// Ports ParseLayer (:62-122): one <OruxTracker> child of the top <MapCalibration>.
// `failed` is set where legacy raises on an absent attribute; a plain nullopt means skip the layer.
[[nodiscard]] std::optional<OruxLayer> parseLayer(const XmlNode& node, bool& failed) {
    if (node.name != QLatin1StringView("OruxTracker")) {
        return std::nullopt;
    }
    const XmlNode* calibration = node.find(u"MapCalibration");
    if (calibration == nullptr || !sameTextAscii(calibration->attribute(QStringLiteral("layers"), failed),
                                                 QLatin1StringView("false"))) {
        return std::nullopt;
    }
    int level = 0;
    const bool levelOk =
        core::delphiTryStrToInt(calibration->attribute(QStringLiteral("layerLevel"), failed), level);
    if (!levelOk || level < 0 || level > 23) {
        return std::nullopt;
    }

    OruxLayer layer;
    if (const XmlNode* chunks = calibration->find(u"MapChunks"); chunks != nullptr) {
        layer.datum = chunks->attribute(QStringLiteral("datum"), failed);
        layer.projection = chunks->attribute(QStringLiteral("projection"), failed);
    }
    const XmlNode* bounds = calibration->find(u"MapBounds");
    if (bounds == nullptr) {
        return std::nullopt;
    }
    // TryStrToFloat with DecimalSeparator '.' (the initialization section, :299-300).
    const auto number = [bounds, &failed](const char* key, double& out) {
        return core::delphiTryStrToFloat(bounds->attribute(QString::fromLatin1(key), failed), out);
    };
    // Legacy reads all four attributes before converting any of them.
    const bool okMinLat = number("minLat", layer.minLat);
    const bool okMaxLat = number("maxLat", layer.maxLat);
    const bool okMinLon = number("minLon", layer.minLon);
    const bool okMaxLon = number("maxLon", layer.maxLon);
    if (failed || !okMinLat || !okMaxLat || !okMinLon || !okMaxLon) {
        return std::nullopt;
    }
    layer.zoom = level;
    return layer;
}

struct OruxCalibration {
    QString mapName;
    std::vector<OruxLayer> layers;
};

// Ports TOruxMapsXmlFile.Parse (:124-178).
[[nodiscard]] std::optional<OruxCalibration> parseOruxXml(const QString& fileName) {
    const auto root = loadXmlRoot(fileName);
    if (!root || root->name != QLatin1StringView("OruxTracker")) {
        return std::nullopt;
    }
    const XmlNode* calibration = root->find(u"MapCalibration");
    bool failed = false;
    if (calibration == nullptr ||
        !sameTextAscii(calibration->attribute(QStringLiteral("layers"), failed), QLatin1StringView("true")) ||
        !sameTextAscii(calibration->attribute(QStringLiteral("layerLevel"), failed),
                       QLatin1StringView("0")) ||
        failed) {
        return std::nullopt;
    }
    OruxCalibration info;
    if (const XmlNode* name = calibration->find(u"MapName"); name != nullptr) {
        info.mapName = name->text;
    }
    for (const XmlNode& child : calibration->children) {
        auto layer = parseLayer(child, failed);
        if (failed) {
            return std::nullopt;
        }
        if (layer) {
            info.layers.push_back(std::move(*layer));
        }
    }
    if (info.layers.empty()) {
        return std::nullopt;
    }
    return info;
}

void appendInt32(QByteArray& out, qint32 value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void appendDouble(QByteArray& out, double value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void appendString(QByteArray& out, const QString& str) {
    const QByteArray utf8 = str.toUtf8();
    appendInt32(out, static_cast<qint32>(utf8.size()));
    out.append(utf8);
}

// Ports SaveToStream + SaveToBase64String (:180-267). TNetEncoding.Base64's default constructor
// wraps at 76 characters with the platform line break (CRLF on Windows), no break after the last
// line. That wrapping follows the assumed TNetEncoding.Base64 default and is unverified; the value
// never leaves the process and both decoders ignore line breaks.
[[nodiscard]] QString saveToBase64String(const OruxCalibration& info) {
    QByteArray bytes;
    appendString(bytes, info.mapName);
    appendInt32(bytes, static_cast<qint32>(info.layers.size()));
    for (const OruxLayer& layer : info.layers) {
        appendString(bytes, layer.datum);
        appendString(bytes, layer.projection);
        appendInt32(bytes, layer.zoom);
        appendDouble(bytes, layer.minLat);
        appendDouble(bytes, layer.maxLat);
        appendDouble(bytes, layer.minLon);
        appendDouble(bytes, layer.maxLon);
    }
    const QByteArray flat = bytes.toBase64();
    QByteArray wrapped;
    for (qsizetype pos = 0; pos < flat.size(); pos += 76) {
        if (pos > 0) {
            wrapped.append("\r\n");
        }
        wrapped.append(flat.mid(pos, 76));
    }
    return QString::fromLatin1(wrapped);
}

// --- the base64 blob BasePointsStrToArray decodes (Includes/OruxMapsXmlFile.pas:180-285) --------
//
// TMemoryStream.WriteBuffer/ReadBuffer write/read the host's native representation with no framing
// beyond each length prefix (int32, host-endian): a length-prefixed UTF-8 string (MapName), an
// int32 layer count, then per layer two more length-prefixed strings (Datum, Projection -- neither
// read by BasePointsStrToArray, but still on the wire and must be skipped to reach the next
// layer), an int32 Zoom, and four IEEE-754 doubles (MinLat, MaxLat, MinLon, MaxLon). Both this
// build and the Windows box legacy runs on are little-endian, so a raw byte copy matches. Bounds-
// checked (unlike TStream.ReadBuffer, which raises EReadError on a short stream -- reproduced here
// as a clean ParseError instead of reading past the decoded buffer: the same "no undefined
// behaviour on values from a foreign file" instruction this ticket documents for the zoom/base-
// point arithmetic below applies to a truncated/corrupt `base_points` blob too).
class CalibrationReader {
public:
    explicit CalibrationReader(QByteArray bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] core::Result<qint32> readInt32() {
        if (pos_ + 4 > bytes_.size()) {
            return core::fail(core::ErrorCode::ParseError, kTruncatedMessage());
        }
        qint32 value = 0;
        std::memcpy(&value, bytes_.constData() + pos_, sizeof(value));
        pos_ += 4;
        return value;
    }

    [[nodiscard]] core::Result<double> readDouble() {
        if (pos_ + 8 > bytes_.size()) {
            return core::fail(core::ErrorCode::ParseError, kTruncatedMessage());
        }
        double value = 0.0;
        std::memcpy(&value, bytes_.constData() + pos_, sizeof(value));
        pos_ += 8;
        return value;
    }

    [[nodiscard]] core::Result<void> skipString() {
        auto len = readInt32();
        if (!len) {
            return core::Unexpected(len.error());
        }
        if (*len <= 0) { // legacy _ReadString: nothing is read and '' is returned for VLen <= 0 (:217-232)
            return {};
        }
        if (pos_ + *len > bytes_.size()) {
            return core::fail(core::ErrorCode::ParseError, kTruncatedMessage());
        }
        pos_ += *len;
        return {};
    }

private:
    static QString kTruncatedMessage() { return QStringLiteral("OruxMaps: Invalid base_points value!"); }

    QByteArray bytes_;
    qsizetype pos_ = 0;
};

struct BasePointsDecoded {
    quint32 bits = 0;
    std::array<geo::PointI, 24> points{};
};

// Ports BasePointsStrToArray (:121-157).
[[nodiscard]] core::Result<BasePointsDecoded>
basePointsStrToArray(const QString& str, const geo::ProjectionSetPtr& projectionSet) {
    CalibrationReader reader(QByteArray::fromBase64(str.toUtf8()));

    if (auto r = reader.skipString(); !r) { // MapName: unused by BasePointsStrToArray itself
        return core::Unexpected(r.error());
    }
    auto layerCount = reader.readInt32();
    if (!layerCount) {
        return core::Unexpected(layerCount.error());
    }
    if (*layerCount < 0) {
        return core::fail(core::ErrorCode::ParseError,
                          QStringLiteral("OruxMaps: Invalid base_points value!"));
    }

    BasePointsDecoded result;
    for (qint32 i = 0; i < *layerCount; ++i) {
        if (auto r = reader.skipString(); !r) { // Datum
            return core::Unexpected(r.error());
        }
        if (auto r = reader.skipString(); !r) { // Projection
            return core::Unexpected(r.error());
        }
        auto zoom = reader.readInt32();
        if (!zoom) {
            return core::Unexpected(zoom.error());
        }
        auto minLat = reader.readDouble(); // unused by BasePointsStrToArray, must still be consumed
        if (!minLat) {
            return core::Unexpected(minLat.error());
        }
        auto maxLat = reader.readDouble();
        if (!maxLat) {
            return core::Unexpected(maxLat.error());
        }
        auto minLon = reader.readDouble();
        if (!minLon) {
            return core::Unexpected(minLon.error());
        }
        auto maxLon = reader.readDouble(); // unused by BasePointsStrToArray, must still be consumed
        if (!maxLon) {
            return core::Unexpected(maxLon.error());
        }

        // Ports CheckZoom's implicit Integer->Byte narrowing (i_ProjectionSet.pas:45 declares
        // `CheckZoom(const AZoom: Byte)`; BasePointsStrToArray passes its Integer VZoom straight
        // in) -- reproduced explicitly rather than left to an implicit conversion.
        const quint8 zoomByte = static_cast<quint8>(static_cast<quint32>(*zoom));
        if (!projectionSet->checkZoom(zoomByte)) {
            continue; // legacy: Assert(False); Continue -- an invalid/foreign zoom is skipped
        }

        const geo::LonLat lonLat{*minLon, *maxLat};
        const geo::DoublePoint tilePos = projectionSet->zoom(zoomByte)->lonLat2TilePosFloat(lonLat);
        // Ports `Result[VZoom] := PointFromDoublePoint(..., prToTopLeft)` (:145-149) -- indexed by
        // zoomByte, the SAME truncated value CheckZoom just validated, not the untruncated VZoom
        // legacy indexes with: for a VZoom whose low byte is a valid zoom but whose full value
        // is not (e.g. 261 -> Byte 5), legacy writes `Result[261]`, out of `TBasePointsArray`'s
        // declared `[0..23]` bounds -- a memory-unsafe write on a corrupt/foreign `base_points`
        // blob. Reproduced safely here (noted in the handoff's "Legacy defects noticed").
        result.points[zoomByte] = geo::roundPoint(tilePos, geo::PointRounding::ToTopLeft);
        setBit(result.bits, zoomByte);
    }
    return result;
}

// --- the four read statement hooks (:227-370) -----------------------------------------------
// OruxMaps never inverts Y or Z the way MBTiles/RMaps do -- every statement below chains to
// `inherited Create(False, False)` in legacy (:246, :268, :291, ctor at :356) -- the per-zoom
// offset instead comes entirely from `bits_`/`basePoints_`, threaded in by finishOpen().

// Ports TTileDataConnectionStatementOruxMaps (:244-264).
class OruxmapsTileDataStatement final : public TileDataStatement {
public:
    OruxmapsTileDataStatement(quint32 bits, const std::array<geo::PointI, 24>& basePoints)
        : TileDataStatement(/*invertedY=*/false, /*invertedZ=*/false), bits_(bits), basePoints_(basePoints) {}

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int x = xy.x();
        int y = xy.y();
        int z = zoom;
        const quint32 idx = bitIndex(zoom);
        if (isBitSet(bits_, idx)) {
            x -= basePoints_[idx].x();
            y -= basePoints_[idx].y();
        } else {
            z = -1;
        }
        if (auto r = stmt().bindInt(1, x); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, y); !r) {
            return r;
        }
        return stmt().bindInt(3, z);
    }
    QByteArray result() override { return blobColumn(0); }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT image FROM tiles WHERE x = ? AND y = ? AND z = ?");
    }

private:
    quint32 bits_;
    std::array<geo::PointI, 24> basePoints_;
};

// Ports TTileInfoConnectionStatementOruxMaps (:266-286).
class OruxmapsTileInfoStatement final : public TileInfoStatement {
public:
    OruxmapsTileInfoStatement(quint32 bits, const std::array<geo::PointI, 24>& basePoints)
        : TileInfoStatement(/*invertedY=*/false, /*invertedZ=*/false), bits_(bits), basePoints_(basePoints) {}

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int x = xy.x();
        int y = xy.y();
        int z = zoom;
        const quint32 idx = bitIndex(zoom);
        if (isBitSet(bits_, idx)) {
            x -= basePoints_[idx].x();
            y -= basePoints_[idx].y();
        } else {
            z = -1;
        }
        if (auto r = stmt().bindInt(1, x); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, y); !r) {
            return r;
        }
        return stmt().bindInt(3, z);
    }
    quint32 result() override { return static_cast<quint32>(stmt().columnInt(0)); }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT length(image) FROM tiles WHERE x = ? AND y = ? AND z = ?");
    }

private:
    quint32 bits_;
    std::array<geo::PointI, 24> basePoints_;
};

// Ports TRectInfoConnectionStatementOruxMaps (:288-330).
class OruxmapsRectInfoStatement final : public RectInfoStatement {
public:
    OruxmapsRectInfoStatement(quint32 bits, const std::array<geo::PointI, 24>& basePoints)
        : RectInfoStatement(/*invertedY=*/false, /*invertedZ=*/false), bits_(bits), basePoints_(basePoints) {}

    core::Result<void> bindParams(geo::RectI rect, quint8 zoom) override {
        zoomForResult_ = zoom; // ports `FZoom := Z` (:295), before Z is possibly reset to -1 below
        int left = rect.left;
        int right = rect.right;
        int top = rect.top;
        int bottom = rect.bottom;
        int z = zoom;
        const quint32 idx = bitIndex(zoom);
        if (isBitSet(bits_, idx)) {
            left -= basePoints_[idx].x();
            right -= basePoints_[idx].x();
            top -= basePoints_[idx].y();
            bottom -= basePoints_[idx].y();
        } else {
            z = -1;
        }
        if (auto r = stmt().bindInt(1, left); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, right); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(3, top); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(4, bottom); !r) {
            return r;
        }
        return stmt().bindInt(5, z);
    }
    void result(int& x, int& y, quint32& size) override {
        x = stmt().columnInt(0);
        y = stmt().columnInt(1);
        size = static_cast<quint32>(stmt().columnInt(2));
        const quint32 idx = bitIndex(zoomForResult_);
        if (isBitSet(bits_, idx)) {
            x += basePoints_[idx].x();
            y += basePoints_[idx].y();
        }
        // else: Assert(False) in legacy (:327) -- compiles out in a release build, a no-op here
        // too (ConnectionStatement::blobColumn's own doc comment documents this convention).
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT x, y, length(image) FROM tiles WHERE x >= ? AND x < ? AND "
                                 "y >= ? AND y < ? AND z = ?");
    }

private:
    quint32 bits_;
    std::array<geo::PointI, 24> basePoints_;
    quint8 zoomForResult_ = 0;
};

// Ports TEnumTilesConnectionStatementOruxMaps (:332-370).
class OruxmapsEnumTilesStatement final : public EnumTilesStatement {
public:
    OruxmapsEnumTilesStatement(quint32 bits, const std::array<geo::PointI, 24>& basePoints)
        : EnumTilesStatement(/*invertedY=*/false, /*invertedZ=*/false), bits_(bits), basePoints_(basePoints) {
    }

    void result(int& x, int& y, quint8& z, QByteArray& blob) override {
        x = stmt().columnInt(0);
        y = stmt().columnInt(1);
        const int zi = stmt().columnInt(2); // full-width column value, possibly foreign/corrupt
        blob = blobColumn(3);
        const quint32 idx = bitIndex(static_cast<quint32>(zi));
        if (isBitSet(bits_, idx)) {
            x += basePoints_[idx].x();
            y += basePoints_[idx].y();
        }
        // else: Assert(False) in legacy (:369) -- a no-op in a release build, same as RectInfo's.
        z = static_cast<quint8>(zi); // ports `ATileInfo.FZoom := Z`'s own truncation, run last,
                                     // matching legacy's assignment order (rmaps_connection.cpp's
                                     // EnumTiles::result() documents the same convention).
    }

protected:
    QByteArray sqlText() const override { return QByteArrayLiteral("SELECT x, y, z, image FROM tiles"); }

private:
    quint32 bits_;
    std::array<geo::PointI, 24> basePoints_;
};

} // namespace

// --- OruxmapsConnection ------------------------------------------------------------------------

OruxmapsConnection::OruxmapsConnection(ConstructKey, QString fileName, core::ContentTypeInfo mainContentType,
                                       const core::ContentTypeRegistry& contentTypeManager,
                                       geo::ProjectionSetPtr projectionSet)
    : SqliteFileConnection(/*isReadOnly=*/true, std::move(fileName), std::move(mainContentType),
                           contentTypeManager, std::move(projectionSet)) {
    // Placeholder bits/basePoints (empty -- every zoom "uncalibrated"): their SQL text does not
    // depend on either, so initialize()'s eager prepare of these two succeeds/fails identically
    // regardless of this placeholder; finishOpen() rebuilds both with the real, decoded values
    // (see the header's class doc comment).
    tileDataStmt_ = std::make_unique<OruxmapsTileDataStatement>(0, std::array<geo::PointI, 24>{});
    tileInfoStmt_ = std::make_unique<OruxmapsTileInfoStatement>(0, std::array<geo::PointI, 24>{});
}

core::Result<std::unique_ptr<OruxmapsConnection>>
OruxmapsConnection::open(QString fileName, core::ContentTypeInfo mainContentType,
                         const core::ContentTypeRegistry& contentTypeManager,
                         geo::ProjectionSetPtr projectionSet, SqliteFileInfoPtr existingInfo) {
    // ConstructKey{}: only open() can name `ConstructKey` (private), so this is the only call site
    // in the codebase that can construct an OruxmapsConnection.
    auto connection =
        std::make_unique<OruxmapsConnection>(ConstructKey{}, std::move(fileName), std::move(mainContentType),
                                             contentTypeManager, std::move(projectionSet));
    if (auto r = connection->initialize(std::move(existingInfo)); !r) {
        return core::Unexpected(r.error());
    }
    if (auto r = connection->finishOpen(); !r) {
        return core::Unexpected(r.error());
    }
    return connection;
}

core::Result<void> OruxmapsConnection::createTables() {
    return core::fail(core::ErrorCode::Unsupported,
                      QStringLiteral("OruxMaps: Write access is not supported!"));
}

core::Result<void> OruxmapsConnection::updateMetadata(geo::PointI /*xy*/, quint8 /*zoom*/) {
    return core::fail(core::ErrorCode::Unsupported,
                      QStringLiteral("OruxMaps: Write access is not supported!"));
}

core::Result<void> OruxmapsConnection::fetchMetadata() {
    // Ports FetchMetadata (:202-218): parse the first '*.otrk2.xml' beside the .db file and store
    // its calibration as the `base_points` metadata value; same error order as legacy.
    const QString xmlFile = oruxmapsSiblingXmlFile(fileInfo()->fileName());
    if (xmlFile.isEmpty()) {
        return core::fail(core::ErrorCode::NotFound, QStringLiteral("OruxMaps: .otrk2.xml file not found!"));
    }
    const auto info = parseOruxXml(xmlFile);
    if (!info) {
        return core::fail(core::ErrorCode::ParseError, QStringLiteral("OruxMaps: Error reading XML file!"));
    }
    fileInfo()->addOrSetMetadataValue(QStringLiteral("base_points"), saveToBase64String(*info));
    return {};
}

core::Result<void> OruxmapsConnection::finishOpen() {
    // Ports the constructor's body that runs after `inherited Create` (:169-193): `base_points` is
    // in fileInfo()'s metadata cache -- stored by fetchMetadata() on a first open, or carried over
    // from an earlier connection's info -- and a missing key raises as legacy does (:175-177).
    auto basePointsStr = fileInfo()->metadataValue(QStringLiteral("base_points"));
    if (!basePointsStr) {
        return core::fail(core::ErrorCode::NotFound,
                          QStringLiteral("OruxMaps: BasePoints metadata not found!"));
    }

    auto decoded = basePointsStrToArray(*basePointsStr, projectionSet());
    if (!decoded) {
        return core::Unexpected(decoded.error());
    }
    bits_ = decoded->bits;
    basePoints_ = decoded->points;

    // Read access (:181-184): rebuild tileData/tileInfo now that bits_/basePoints_ are finally
    // known, plus rectInfo/enumTiles for the first time. Gated on isEnabled(), the same convention
    // mbtiles_connection.cpp/rmaps_connection.cpp use for their own finishOpen().
    if (isEnabled()) {
        tileDataStmt_ = std::make_unique<OruxmapsTileDataStatement>(bits_, basePoints_);
        tileInfoStmt_ = std::make_unique<OruxmapsTileInfoStatement>(bits_, basePoints_);
        if (auto r = tileDataStmt_->ensurePrepared(db()); !r) {
            return core::Unexpected(r.error());
        }
        if (auto r = tileInfoStmt_->ensurePrepared(db()); !r) {
            return core::Unexpected(r.error());
        }
    }
    rectInfoStmt_ = std::make_unique<OruxmapsRectInfoStatement>(bits_, basePoints_);
    enumTilesStmt_ = std::make_unique<OruxmapsEnumTilesStatement>(bits_, basePoints_);

    // Write access (:186-190): OruxMaps never has any -- insertStmt_/updateStmt_/deleteStmt_ stay
    // null, matching legacy's own FInsertTileStmt/FUpdateTileStmt/FDeleteTileStmt := nil.
    return {};
}

QString oruxmapsSiblingXmlFile(const QString& dbFilePath) {
    // Ports TryGetOruxMapsXmlFileName's TDirectory.GetFiles(dir, '*.otrk2.xml')[0]: the first match
    // in name order (D-43).
    const QString dirPath = QFileInfo(dbFilePath).absolutePath();
    const QStringList entries = QDir(dirPath).entryList(QDir::Files);
    const auto found = std::find_if(entries.cbegin(), entries.cend(), endsWithOtrk2XmlCi);
    return found == entries.cend() ? QString() : QDir(dirPath).filePath(*found);
}

} // namespace cg::storage::sqlite
