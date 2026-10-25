// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/zmp_info.h"

#include "cg/core/delimited_text.h"
#include "cg/core/delphi_text.h"
#include "cg/vector/config_provider_helpers.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QPainter>

#include <chrono>

namespace cg::maptype {
namespace {

using core::IConfigProvider;
using core::IConfigProviderPtr;
using core::IStringByLanguagePtr;
using core::StringByLanguageWithStaticList;

// InternalMakeStringByLanguage (:251-293, spec 2.5.2): the plain value is the default of every
// language's "<name>_<code>" key; index 0 (en) is read like the others.
IStringByLanguagePtr makeStringByLanguage(const core::ILanguageListStatic& langList,
                                          const IConfigProvider& config, const QString& paramName,
                                          const QString& defValue) {
    const QString def = config.readString(paramName, defValue);
    QStringList values;
    for (int i = 0; i < langList.count(); ++i) {
        values.append(config.readString(paramName + u'_' + langList.code(i), def));
    }
    return std::make_shared<const StringByLanguageWithStaticList>(values);
}

// ReadAnsiString on an INI section (e.g. :941-942): the decoded text, held as bytes (maptype.md 2.6.2).
QByteArray readAnsi(const IConfigProvider& config, QStringView id, const QByteArray& def) {
    const QVariant v = config.value(id);
    return v.isValid() ? v.toString().toUtf8() : def;
}

// CreateDefaultIcon (:324-351): 32x32 light gray, the first two characters of pnum centred (DV-4: Qt font).
QImage createDefaultIcon(int pnum) {
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(0xFFBFBFBF);
    const QString text = QString::number(pnum).left(2);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    const QFontMetrics metrics(painter.font());
    const QSize size = metrics.size(0, text);
    painter.drawText(QPoint((32 - size.width()) / 2, (32 - size.height()) / 2 + metrics.ascent()), text);
    return image;
}

// GetBitmap (:444-477) + UpdateBmpTransparency (:413-442): a `.bmp` loses its magenta key colour.
QImage getBitmap(const core::ContentTypeRegistry& contentTypes, const IConfigProvider& config,
                 const IConfigProvider& params, const QString& defName, const QString& ident) {
    const QString imageName = params.readString(ident, defName);
    const QByteArray data = config.binary(imageName);
    if (data.isEmpty()) {
        return {};
    }
    const core::ContentTypeInfo* info = contentTypes.infoByFileName(imageName);
    if (info == nullptr || info->kind != core::ContentKind::Bitmap) { // GetBitmapLoaderByFileName = nil
        return {};
    }
    QImage image;
    if (!image.loadFromData(data, QFileInfo(imageName).suffix().toUpper().toLatin1().constData())) {
        return {};
    }
    image = image.convertToFormat(QImage::Format_ARGB32); // IBitmap32Static is always 32-bit ARGB
    if (QFileInfo(imageName).suffix().toLower() == u"bmp") {
        for (int y = 0; y < image.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                if (line[x] == 0xFFFF00FFU) {
                    line[x] = 0;
                }
            }
        }
    }
    return image;
}

// LoadIcon, local to LoadIcons (:479-522): '.bmp' then '.png'; any failure goes on to the next extension.
QImage loadIcon(const core::ContentTypeRegistry& contentTypes, const IConfigProvider& config,
                const IConfigProvider& params, const QString& defName, const QString& ident) {
    for (const QString& ext : {QStringLiteral(".bmp"), QStringLiteral(".png")}) {
        const QImage image = getBitmap(contentTypes, config, params, defName + ext, ident);
        if (!image.isNull()) {
            return image;
        }
    }
    return {};
}

// GetBinaryListByConfig (:636-663): every value of the folder, in its order; nil -> empty.
std::vector<QByteArray> binaryList(const IConfigProviderPtr& provider) {
    std::vector<QByteArray> result;
    if (provider) {
        for (const QString& name : provider->valueNames()) {
            result.push_back(provider->binary(name));
        }
    }
    return result;
}

} // namespace

ZmpError::ZmpError(Kind kind, const QString& message)
    : std::runtime_error(message.toUtf8().toStdString()), kind_(kind), message_(message) {}

// ---- TZmpInfoGUI (u_ZmpInfo.pas:297-583) ----

ZmpInfoGui::ZmpInfoGui(const core::ILanguageManager& languageManager,
                       const core::ContentTypeRegistry& contentTypes, const IConfigProviderPtr& config,
                       const IConfigProviderPtr& configIniParams, int pnum) {
    const core::ILanguageListStaticPtr langList = languageManager.languageList();
    loadUIParams(*langList, *configIniParams, pnum); // ctor :297-320, LoadConfig :398-411
    loadIcons(contentTypes, *config, *configIniParams, pnum);
    loadInfo(*langList, *config);
}

void ZmpInfoGui::loadUIParams(const core::ILanguageListStatic& langList, const IConfigProvider& params,
                              int pnum) {
    name_ = makeStringByLanguage(langList, params, QStringLiteral("name"),
                                 QStringLiteral("map#") + QString::number(pnum));
    parentSubMenu_ = makeStringByLanguage(langList, params, QStringLiteral("ParentSubMenu"), QString());
    qint32 hotKey = params.readInt(u"DefHotKey", 0);
    hotKey = params.readInt(u"HotKey", hotKey);
    hotKey_ = static_cast<quint16>(hotKey); // Integer -> TShortCut (Word)
    separator_ = params.readBool(u"separator", false);
    enabled_ = params.readBool(u"Enabled", true);
    sortIndex_ = params.readInt(u"pnum", -1);
}

void ZmpInfoGui::loadIcons(const core::ContentTypeRegistry& contentTypes, const IConfigProvider& config,
                           const IConfigProvider& params, int pnum) {
    bmp24_ = loadIcon(contentTypes, config, params, QStringLiteral("24"), QStringLiteral("BigIconName"));
    if (bmp24_.isNull()) {
        bmp24_ = createDefaultIcon(pnum);
    }
    bmp18_ = loadIcon(contentTypes, config, params, QStringLiteral("18"), QStringLiteral("SmallIconName"));
    if (bmp18_.isNull()) {
        bmp18_ = bmp24_;
    }
}

void ZmpInfoGui::loadInfo(const core::ILanguageListStatic& langList, const IConfigProvider& config) {
    QString def;
    if (!config.readString(u"index.html", QString()).isEmpty()) {
        def = QStringLiteral("/index.html");
    } else if (!config.readString(u"info.txt", QString()).isEmpty()) {
        def = QStringLiteral("/info.txt");
    }
    QStringList values;
    for (int i = 0; i < langList.count(); ++i) {
        QString value = def;
        QString fileName = QStringLiteral("index_") + langList.code(i) + QStringLiteral(".html");
        if (!config.readString(fileName, QString()).isEmpty()) {
            value = u'/' + fileName;
        } else {
            fileName = QStringLiteral("info_") + langList.code(i) + QStringLiteral(".txt");
            if (!config.readString(fileName, QString()).isEmpty()) {
                value = u'/' + fileName;
            }
        }
        values.append(value);
    }
    infoUrl_ = std::make_shared<const StringByLanguageWithStaticList>(values);
}

// ---- TZmpInfo (u_ZmpInfo.pas:585-1071) ----

ZmpInfo::ZmpInfo(const ZmpConfig& zmpConfig, const core::ILanguageManager& languageManager,
                 const geo::ProjectionSetFactory& projectionSetFactory,
                 const core::ContentTypeRegistry& contentTypes,
                 const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
                 const vector::MarkPictureListPtr& markPictureList, QString fileName,
                 IConfigProviderPtr config, int pnum)
    : fileName_(std::move(fileName)), config_(std::move(config)) {
    configIni_ = config_->subItem(u"params.txt");
    if (!configIni_) {
        throw ZmpError(ZmpError::Kind::IniNotFound, QStringLiteral("Not found \"params.txt\" in zmp"));
    }
    configIniParams_ = configIni_->subItem(u"PARAMS");
    if (!configIniParams_) {
        throw ZmpError(ZmpError::Kind::ParamsNotFound, QStringLiteral("Not found PARAMS section in zmp"));
    }
    loadConfig(zmpConfig, projectionSetFactory, appearanceOfMarkFactory, markPictureList);
    gui_ = std::make_shared<const ZmpInfoGui>(languageManager, contentTypes, config_, configIniParams_, pnum);
    license_ = makeStringByLanguage(*languageManager.languageList(), *configIniParams_,
                                    QStringLiteral("License"), QString());
    layerZOrder_ = configIniParams_->readInt(u"LayerZOrder", 0);

    // OQ-3 (D-74): u_MapType.pas:376-381 looks the content type up by MainStorageContentType, then by
    // the extension, and keeps nil when both fail (the map crashes on first use). Rejected here instead.
    const auto& storage = *storageConfig_;
    const bool known =
        (!storage.mainContentType.isEmpty() && contentTypes.infoByType(storage.mainContentType)) ||
        contentTypes.infoByExt(storage.tileFileExt);
    if (!known) {
        throw ZmpError(ZmpError::Kind::UnknownContentType,
                       QStringLiteral("Unknown tile content type \"%1\" / extension \"%2\" in map %3")
                           .arg(QString::fromUtf8(storage.mainContentType),
                                QString::fromUtf8(storage.tileFileExt), fileName_));
    }
}

void ZmpInfo::loadConfig(const ZmpConfig& zmpConfig, const geo::ProjectionSetFactory& projectionSetFactory,
                         const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
                         const vector::MarkPictureListPtr& markPictureList) {
    const IConfigProvider& params = *configIniParams_;
    guid_ = loadGuid(params);
    isLayer_ = params.readBool(u"asLayer", false);
    loadVersion(params);
    loadProjectionInfo(*configIni_, projectionSetFactory);
    loadTileRequestBuilderConfig(params);
    loadTileDownloaderConfig(zmpConfig, params);
    loadCropConfig(params);
    loadStorageConfig(zmpConfig, params);
    loadAbilities(params);
    loadCoverageInfo(params);
    loadVectorAppearanceConfig(appearanceOfMarkFactory, markPictureList, params); // :797
    loadSamples(*config_);
    contentTypeSubst_ = core::ContentTypeSubst::fromConfig(configIniParams_);
}

// LoadVectorAppearanceConfig :1001-1062. Sub-items Vector/Point, Vector/Line, Vector/Poly of PARAMS; the
// force flags default to True (vector.md 2.9). A sub-item that is absent leaves its params null.
void ZmpInfo::loadVectorAppearanceConfig(const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
                                         const vector::MarkPictureListPtr& markPictureList,
                                         const IConfigProvider& config) {
    const IConfigProviderPtr vectorConfig = config.subItem(u"Vector");
    if (!vectorConfig) {
        return;
    }
    if (const IConfigProviderPtr sub = vectorConfig->subItem(u"Point")) { // :1020-1038
        auto appearance = vector::readAppearancePoint(sub, markPictureList, appearanceOfMarkFactory, nullptr);
        const bool isForceTextColor = sub->readBool(u"IsForceTextColor", true);
        const bool isForceTextBgColor = sub->readBool(u"IsForceShadowColor", true);
        const bool isForceFontSize = sub->readBool(u"IsForceFontSize", true);
        const bool isForceMarkerSize = sub->readBool(u"IsForceIconSize", true);
        const bool isForcePicName = sub->readBool(u"IsForceIconName", true);
        pointParams_ = std::make_shared<const vector::ImportPointParams>(
            std::move(appearance), isForceTextColor, isForceTextBgColor, isForceFontSize, isForceMarkerSize,
            isForcePicName);
    }
    if (const IConfigProviderPtr sub = vectorConfig->subItem(u"Line")) { // :1040-1046
        auto appearance = vector::readAppearanceLine(sub, appearanceOfMarkFactory, nullptr);
        const bool isForceLineColor = sub->readBool(u"IsForceLineColor", true);
        const bool isForceLineWidth = sub->readBool(u"IsForceLineWidth", true);
        lineParams_ = std::make_shared<const vector::ImportLineParams>(std::move(appearance),
                                                                       isForceLineColor, isForceLineWidth);
    }
    if (const IConfigProviderPtr sub = vectorConfig->subItem(u"Poly")) { // :1048-1057
        auto appearance = vector::readAppearancePolygon(sub, appearanceOfMarkFactory, nullptr);
        const bool isForceLineColor = sub->readBool(u"IsForceLineColor", true);
        const bool isForceLineWidth = sub->readBool(u"IsForceLineWidth", true);
        const bool isForceFillColor = sub->readBool(u"IsForceFillColor", true);
        polyParams_ = std::make_shared<const vector::ImportPolyParams>(
            std::move(appearance), isForceLineColor, isForceLineWidth, isForceFillColor);
    }
}

// LoadGUID :833-839. ReadGUID (u_ConfigProviderHelpers.pas:430-448) gives the default for empty and
// malformed text alike, so every failure reads "Empty GUID" (LD-8; N-1: braces are required).
QUuid ZmpInfo::loadGuid(const IConfigProvider& params) const {
    const QUuid guid = params.readUuid(u"GUID", QUuid());
    if (guid.isNull()) {
        throw ZmpError(ZmpError::Kind::Guid, QStringLiteral("Empty GUID"));
    }
    return guid;
}

void ZmpInfo::loadVersion(const IConfigProvider& params) { // :1064-1069
    version_ = params.readString(u"Version", QString());
}

void ZmpInfo::loadProjectionInfo(const IConfigProvider& configIni, const geo::ProjectionSetFactory& factory) {
    if (const IConfigProviderPtr view = configIni.subItem(u"ViewInfo")) { // LoadProjectionInfo :841-856
        viewProjectionSet_ = factory.byConfig(view.get());
    }
    projectionSet_ = factory.byConfig(configIniParams_.get());
    if (!viewProjectionSet_) {
        viewProjectionSet_ = projectionSet_;
    }
}

void ZmpInfo::loadTileRequestBuilderConfig(const IConfigProvider& params) { // :974-999
    auto cfg = std::make_shared<TileDownloadRequestBuilderConfigStatic>();
    cfg->urlBase = readAnsi(params, u"DefURLBase", QByteArray());
    cfg->urlBase = readAnsi(params, u"URLBase", cfg->urlBase);
    cfg->serverNames = readAnsi(params, u"ServerNames", QByteArray());
    cfg->requestHeader = readAnsi(params, u"RequestHead", QByteArray());
    cfg->isUseDownloader = params.readBool(u"IsUseDownloaderInScript", false);
    cfg->defaultProjConverterArgs = readAnsi(params, u"Proj4Args", QByteArray());
    requestBuilderConfig_ = std::move(cfg);
}

void ZmpInfo::loadTileDownloaderConfig(const ZmpConfig& zmpConfig,
                                       const IConfigProvider& params) { // :923-972
    auto cfg = std::make_shared<net::TileDownloaderSettings>();
    cfg->enabled = params.readBool(u"UseDwn", true);
    cfg->allowUseCookie = params.readBool(u"AllowUseCookie", false);
    cfg->ignoreMimeType = params.readBool(u"IgnoreContentType", false);
    cfg->detectMimeType = params.readBool(u"DetectContentType", false);
    cfg->defaultMimeType = readAnsi(params, u"DefaultContentType", "image/jpg");
    cfg->expectedMimeTypes = readAnsi(params, u"ContentType", "image/jpg");
    cfg->waitIntervalMs = static_cast<quint32>(params.readInt(u"Sleep", 0)); // Integer -> Cardinal wraps
    cfg->restartDownloadOnMemCacheTTL = params.readBool(u"RestartDownloadOnMemCacheTTL", false);
    cfg->maxConnectToServerCount = static_cast<quint32>(
        params.readInt(u"MaxConnectToServerCount", static_cast<qint32>(zmpConfig.maxConnectToServerCount())));

    // LD-7: StrictDelimiter list, then StrToInt of items 0 and 1; the RTL exceptions reach the set's
    // handler as plain messages without the map name.
    const QStringList items = core::parseDelimitedText(
        params.readString(u"IteratorSubRectSize", QStringLiteral("1,1")), u',', u'"', true);
    int xy[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        if (i >= items.size()) {
            throw std::runtime_error(QStringLiteral("List index out of bounds (%1)").arg(i).toStdString());
        }
        if (!core::delphiTryStrToInt(items[i], xy[i])) {
            throw std::runtime_error(
                QStringLiteral("'%1' is not a valid integer value").arg(items[i]).toStdString());
        }
    }
    cfg->iteratorSubRectSize = QPoint(xy[0], xy[1]);
    downloaderConfig_ = std::move(cfg);
}

void ZmpInfo::loadCropConfig(const IConfigProvider& params) { // :810-831
    const geo::RectI rect{params.readInt(u"TileRLeft", 0), params.readInt(u"TileRTop", 0),
                          params.readInt(u"TileRRight", 0), params.readInt(u"TileRBottom", 0)};
    const geo::PointI cutCount(params.readInt(u"CutCountX", 0), params.readInt(u"CutCountY", 0));
    const geo::PointI cutSize(params.readInt(u"CutSizeX", 0), params.readInt(u"CutSizeY", 0));
    const geo::PointI cutTile(params.readInt(u"CutTileX", 0), params.readInt(u"CutTileY", 0));
    cropConfig_ = std::make_shared<const TilePostDownloadCropConfigStatic>(
        rect, cutCount, cutSize, cutTile, params.readString(u"CutToSkip", QString()));
}

void ZmpInfo::loadStorageConfig(const ZmpConfig& zmpConfig, const IConfigProvider& params) { // :864-921
    auto cfg = std::make_shared<storage::SimpleTileStorageConfigStatic>();
    cfg->nameInCache = params.readString(u"NameInCache", QString());
    cfg->cacheType = static_cast<storage::CacheTypeId>(params.readInt(u"CacheType", 0));
    cfg->useMemCache = params.readBool(u"UseMemCache", zmpConfig.useMemCache());
    cfg->memCacheCapacity = params.readInt(u"MemCacheCapacity", zmpConfig.memCacheCapacity());
    cfg->memCacheTtl = std::chrono::milliseconds(
        static_cast<quint32>(params.readInt(u"MemCacheTTL", static_cast<qint32>(zmpConfig.memCacheTtl()))));
    cfg->memCacheClearStrategy = static_cast<storage::ClearStrategy>(
        params.readInt(u"MemCacheClearStrategy", zmpConfig.memCacheClearStrategy()));
    cfg->mainContentType = readAnsi(params, u"MainStorageContentType", QByteArray());
    cfg->tileFileExt = core::delphiLowerCase(QString::fromUtf8(readAnsi(params, u"Ext", ".jpg"))).toUtf8();

    const bool isReadOnly = params.readBool(u"IsReadOnly", false);
    cfg->abilities.allowRead = params.readBool(u"AllowRead", true);
    cfg->abilities.allowScan = params.readBool(u"AllowScan", true);
    cfg->abilities.allowAdd = params.readBool(u"AllowAdd", !isReadOnly);
    cfg->abilities.allowDelete = params.readBool(u"AllowDelete", !isReadOnly);
    cfg->abilities.allowReplace = params.readBool(u"AllowReplace", !isReadOnly);
    storageConfig_ = std::move(cfg);
}

void ZmpInfo::loadAbilities(const IConfigProvider& params) { // :765-778
    auto cfg = std::make_shared<MapAbilitiesConfigStatic>();
    cfg->isShowOnSmMap = params.readBool(u"CanShowOnSmMap", true);
    cfg->useDownload = params.readBool(u"UseDwn", true);
    abilities_ = std::move(cfg);
}

void ZmpInfo::loadCoverageInfo(const IConfigProvider& params) { // :802-808
    coverage_ = std::make_shared<const MapCoverageInfo>(params.readString(u"MapCenter", QString()));
}

void ZmpInfo::loadSamples(const IConfigProvider& config) { // :858-862
    emptyTileSamples_ = binaryList(config.subItem(u"EmptyTiles"));
    banTileSamples_ = binaryList(config.subItem(u"BanTiles"));
}

} // namespace cg::maptype
