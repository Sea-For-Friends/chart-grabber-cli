// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/url_template.h"

#include "cg/core/delphi_format.h"
#include "cg/core/delphi_text.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/projection_type.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <utility>

namespace cg::maptype {

namespace {

QString getQuadkey(qint32 x, qint32 y, quint8 zoom) {
    // https://docs.microsoft.com/en-us/bingmaps/articles/bing-maps-tile-system
    if (zoom == 0) {
        return {};
    }
    QString result(zoom, QChar(u'0'));
    for (int i = zoom; i >= 1; --i) {
        // 1 shl (I - 1): the shift count is taken modulo 32 like the x86 instruction
        const quint32 mask = 1U << (static_cast<unsigned>(i - 1) & 31U);
        int key = 0;
        if ((static_cast<quint32>(x) & mask) != 0) {
            key += 1;
        }
        if ((static_cast<quint32>(y) & mask) != 0) {
            key += 2;
        }
        result[zoom - i] = QChar(static_cast<char16_t>(u'0' + key));
    }
    return result;
}

QString getSasPathValue(qint32 x, qint32 y, quint8 zoom) {
    return QStringLiteral("z") + QString::number(zoom + 1) + u'/' + QString::number(x / 1024) +
           QStringLiteral("/x") + QString::number(x) + u'/' + QString::number(y / 1024) +
           QStringLiteral("/y") + QString::number(y);
}

} // namespace

UrlTemplate::UrlTemplate(std::shared_ptr<core::ILanguageManager> languageManager,
                         geo::ProjectionSetPtr projectionSet,
                         std::shared_ptr<TileDownloadRequestBuilderConfig> requestBuilderConfig,
                         RandomFn random)
    : langManager_(std::move(languageManager)), projectionSet_(std::move(projectionSet)),
      requestBuilderConfig_(std::move(requestBuilderConfig)), random_(std::move(random)) {
    // Legacy Random is unseeded (first pick always index 0) until Randomize runs; the port is always seeded
    // (L-136).
    if (!random_) {
        random_ = [](qint32 n) { return static_cast<qint32>(QRandomGenerator::global()->bounded(n)); };
    }
    configSubscription_ = requestBuilderConfig_->changed().subscribe([this] { configChanged_ = true; });
    configChanged_ = true; // OnConfigChange once in the constructor
    langSubscription_ = langManager_->changed().subscribe([this] { langChanged_ = true; });
    langChanged_ = true;
}

UrlTemplate::~UrlTemplate() = default;

QString UrlTemplate::templateToUrl(const QString& tmpl) {
    if (configChanged_.exchange(false)) {
        parseServerNames(QString::fromUtf8(requestBuilderConfig_->serverNames()));
    }
    parse(tmpl);
    return doRender();
}

QString UrlTemplate::render(const UrlTemplateRequest& request) {
    request_ = request;
    if (configChanged_.exchange(false)) {
        parseServerNames(QString::fromUtf8(requestBuilderConfig_->serverNames()));
        parse(QString::fromUtf8(requestBuilderConfig_->urlBase()));
    }
    return doRender();
}

QString UrlTemplate::getVersionValue(const UrlTemplateRequest& request) const {
    return request.versionInfo != nullptr ? request.versionInfo->storeString() : QString(); // UrlString
}

QString UrlTemplate::getLangValue() {
    if (langChanged_.exchange(false)) {
        lang_ = langManager_->currentLanguageCode();
    }
    return lang_;
}

QString UrlTemplate::getServerNameValue() {
    const qint32 n = static_cast<qint32>(serverNames_.size());
    return n > 0 ? serverNames_[random_(n)] : QString();
}

QString UrlTemplate::getBBox(QPoint tile, quint8 zoom) const {
    // zoom >= 24 throws here; legacy indexes Zooms[AZoom] unchecked (undefined). Request zooms are validated
    // upstream.
    const geo::ProjectionPtr& projection = projectionSet_->zoom(zoom);
    const geo::LonLatRect lonLatRect = projection->tilePos2LonLatRect({tile.x(), tile.y()});
    geo::DoubleRect rect;
    if (projection->projectionType()->projectionEpsg() == geo::kProjEpsgGeographic) {
        rect = {lonLatRect.left, lonLatRect.top, lonLatRect.right, lonLatRect.bottom};
    } else {
        const geo::DoublePoint topLeft = projection->projectionType()->lonLat2Metr(lonLatRect.topLeft());
        const geo::DoublePoint bottomRight =
            projection->projectionType()->lonLat2Metr(lonLatRect.bottomRight());
        rect = {topLeft.x(), topLeft.y(), bottomRight.x(), bottomRight.y()};
    }
    const auto roundEx = [](double v) { return QString::fromLatin1(core::roundEx(v, 8)); };
    return roundEx(rect.left) + u',' + roundEx(rect.bottom) + u',' + roundEx(rect.right) + u',' +
           roundEx(rect.top);
}

// Legacy picks the host here and nowhere else (L-136); an empty list leaves the old host (L-138).
void UrlTemplate::parseServerNames(const QString& names) {
    serverNames_.clear();
    if (names.isEmpty()) {
        return;
    }
    const qsizetype n = names.size();
    qsizetype s = 0;
    while (s < n) {
        const qsizetype p = s;
        while (s < n && names[s] > u' ' && names[s] != u',') {
            ++s;
        }
        serverNames_.push_back(
            names.mid(p, s - p)); // an empty name when the text starts with a separator (L-139)
        while (s < n && (names[s] <= u' ' || names[s] == u',')) {
            ++s;
        }
    }
    serverName_ = getServerNameValue();
}

UrlTemplate::ItemValueType UrlTemplate::tagNameToType(const QString& name) {
    static constexpr std::pair<const char16_t*, ItemValueType> kKnown[] = {
        {u"", ItemValueType::Text},
        {u"s", ItemValueType::S},
        {u"x", ItemValueType::X},
        {u"y", ItemValueType::Y},
        {u"-y", ItemValueType::Yb},
        {u"z", ItemValueType::Z},
        {u"q", ItemValueType::Q},
        {u"bbox", ItemValueType::BBox},
        {u"timestamp", ItemValueType::TimeStamp},
        {u"ver", ItemValueType::Ver},
        {u"lang", ItemValueType::Lang},
        {u"sas_path", ItemValueType::SasPath},
    };
    for (const auto& [text, type] : kKnown) {
        if (name == QStringView(text)) {
            return type;
        }
    }
    for (const QChar c : name) {
        if (c == u',') {
            parseServerNames(name); // a comma tag re-parses the server list now (L-141)
            return ItemValueType::S;
        }
        if (c == u'+' || c == u'-' || c == u'*' || c == u'/') {
            return ItemValueType::MathExpression; // an unknown tag with an operator renders "" (L-142)
        }
    }
    return ItemValueType::Text; // legacy: Assert(False, 'Unrecognized tag'), a no-op in release; emitted
                                // literally
}

void UrlTemplate::parse(const QString& tmpl) {
    if (urlTmpl_ == tmpl) {
        return;
    }
    items_.clear();
    const auto addItem = [this](const QString& expr, const QString& value, ItemValueType type) {
        if (value.isEmpty() && type == ItemValueType::Text) {
            return;
        }
        items_.push_back({expr, value, type});
    };

    qsizetype i = 0;
    while (true) {
        const qsizetype k = i;
        const qsizetype tagBegin = tmpl.indexOf(u'{', i);
        const qsizetype tagEnd = tagBegin >= 0 ? tmpl.indexOf(u'}', tagBegin + 1) : -1;
        if (tagEnd < 0) {
            addItem({}, tmpl.mid(k), ItemValueType::Text);
            break;
        }
        const QString tag = core::delphiLowerCase(tmpl.mid(tagBegin + 1, tagEnd - tagBegin - 1));
        const ItemValueType type = tagNameToType(tag);
        if (type == ItemValueType::Text) {
            addItem({}, tmpl.mid(k, tagEnd - k + 1),
                    ItemValueType::Text); // text before the tag and the unrecognised tag
        } else {
            addItem({}, tmpl.mid(k, tagBegin - k), ItemValueType::Text);
            addItem(type == ItemValueType::MathExpression ? tag : QString(), {}, type);
        }
        i = tagEnd + 1;
    }
    urlTmpl_ = tmpl;
}

QString UrlTemplate::doRender() {
    if (!request_) {
        return {}; // legacy: Assert(False) and ''
    }
    const UrlTemplateRequest& request = *request_;
    const QPoint tile = request.tile;
    const quint8 zoom = request.zoom;
    QString result;
    for (const ItemRec& item : items_) {
        switch (item.valueType) {
        case ItemValueType::Text:
            result += item.value;
            break;
        case ItemValueType::S:
            result += serverName_;
            break;
        case ItemValueType::X:
            result += QString::number(tile.x());
            break;
        case ItemValueType::Y:
            result += QString::number(tile.y());
            break;
        case ItemValueType::Yb: {
            const quint32 size = 1U << (zoom & 31U); // 1 shl VZoom (count modulo 32)
            result += QString::number(static_cast<qint32>(size - 1U - static_cast<quint32>(tile.y())));
            break;
        }
        case ItemValueType::Z:
            result += QString::number(zoom);
            break;
        case ItemValueType::Q:
            result += getQuadkey(tile.x(), tile.y(), zoom);
            break;
        case ItemValueType::BBox:
            result += getBBox(tile, zoom);
            break;
        case ItemValueType::TimeStamp:
            result += QString::number(QDateTime::currentSecsSinceEpoch()); // true UTC, D-7
            break;
        case ItemValueType::Ver:
            result += getVersionValue(request);
            break;
        case ItemValueType::Lang:
            result += getLangValue();
            break;
        case ItemValueType::SasPath:
            result += getSasPathValue(tile.x(), tile.y(), zoom);
            break;
        case ItemValueType::MathExpression:
            result += mathExpressionEvaluator_.evaluate(item.expr, tile.x(), tile.y(), zoom);
            break;
        }
    }
    return result;
}

} // namespace cg::maptype
