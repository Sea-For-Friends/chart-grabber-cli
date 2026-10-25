// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_PascalScriptUrlTemplate.pas (the class; the two ...Reg_UrlTemplate procedures are
// replaced by script::IScriptHost::templateToUrl, which the script wiring forwards here)
//
// The URL mini-language of a ZMP's DefURLBase: {x} {y} {-y} {z} {q} {bbox} {s} {timestamp} {ver}
// {lang} {sas_path}, a comma list {a,b,c} (server names) and integer arithmetic {z-1} (spec
// maptype.md 2.8). The template is parsed once into items and re-parsed only when the config
// notifies a change or a different template text arrives. Not thread-safe: owned by one request
// builder and used under its lock, like legacy.

#include "cg/core/language_manager.h"
#include "cg/core/notifier.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/math_expression_evaluator.h"
#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/storage/map_version.h"

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cg::maptype {

// What a render needs from ITileRequest (i_TileRequest.pas: Tile, Zoom, VersionInfo).
struct UrlTemplateRequest {
    QPoint tile;
    quint8 zoom = 0;
    const storage::MapVersionInfo* versionInfo = nullptr; // may be null
};

class UrlTemplate {
public:
    // Delphi's Random(N): [0, N). Legacy uses the process-wide RNG, which the downloader threads seed
    // with Randomize (system time); the default is QRandomGenerator::global(). Tests inject their own.
    using RandomFn = std::function<qint32(qint32 upperExclusive)>;

    UrlTemplate(std::shared_ptr<core::ILanguageManager> languageManager, geo::ProjectionSetPtr projectionSet,
                std::shared_ptr<TileDownloadRequestBuilderConfig> requestBuilderConfig, RandomFn random = {});
    ~UrlTemplate();
    UrlTemplate(const UrlTemplate&) = delete;
    UrlTemplate& operator=(const UrlTemplate&) = delete;

    // FEAT-NET-004
    [[nodiscard]] QString render(const UrlTemplateRequest& request);
    // PSTemplateToUrl: the script's TemplateToUrl, rendered for the stored request.
    [[nodiscard]] QString templateToUrl(const QString& tmpl);
    // Lifetime: the request keeps the raw versionInfo pointer after render() returns (legacy holds a counted
    // ITileRequest), so the version object must outlive the next templateToUrl().
    void setRequest(const UrlTemplateRequest& request) { request_ = request; }

private:
    enum class ItemValueType : std::uint8_t {
        Text,
        S,
        X,
        Y,
        Yb,
        Z,
        Q,
        BBox,
        TimeStamp,
        Ver,
        Lang,
        SasPath,
        MathExpression
    };
    struct ItemRec {
        QString expr;
        QString value; // the literal of a Text item
        ItemValueType valueType = ItemValueType::Text;
    };

    void parse(const QString& tmpl);
    void parseServerNames(const QString& names);
    [[nodiscard]] ItemValueType tagNameToType(const QString& name);
    [[nodiscard]] QString doRender();
    [[nodiscard]] QString getLangValue();
    [[nodiscard]] QString getVersionValue(const UrlTemplateRequest& request) const;
    [[nodiscard]] QString getServerNameValue();
    [[nodiscard]] QString getBBox(QPoint tile, quint8 zoom) const;

    std::shared_ptr<core::ILanguageManager> langManager_;
    geo::ProjectionSetPtr projectionSet_;
    std::shared_ptr<TileDownloadRequestBuilderConfig> requestBuilderConfig_;
    RandomFn random_;
    MathExpressionEvaluator mathExpressionEvaluator_;

    std::vector<ItemRec> items_;
    QString urlTmpl_;
    QString serverName_;
    QStringList serverNames_;
    QString lang_;
    std::optional<UrlTemplateRequest> request_;

    std::atomic<bool> configChanged_{false};
    std::atomic<bool> langChanged_{false};
    core::Subscription configSubscription_; // declared last: drops first, before the flags die
    core::Subscription langSubscription_;
};

} // namespace cg::maptype
