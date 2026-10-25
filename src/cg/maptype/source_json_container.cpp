// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/source_json_container.h"

#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace cg::maptype {

namespace {

using core::ErrorCode;
using namespace Qt::StringLiterals;

constexpr auto kParams = u"PARAMS";

// The decoded `emptyTiles` samples as the "EmptyTiles" folder of a ZMP: one value per sample,
// in array order (ZmpInfo reads them with valueNames() + binary()).
class SampleFolder final : public core::IConfigProvider {
public:
    explicit SampleFolder(std::vector<QByteArray> samples) : samples_(std::move(samples)) {}

    core::IConfigProviderPtr subItem(QStringView) const override { return nullptr; }
    QVariant value(QStringView) const override { return {}; }
    QByteArray binary(QStringView id) const override {
        bool ok = false;
        const qsizetype index = id.toString().toLongLong(&ok);
        return ok && index >= 0 && index < std::ssize(samples_) ? samples_[static_cast<std::size_t>(index)]
                                                                : QByteArray();
    }
    QStringList subItemNames() const override { return {}; }
    QStringList valueNames() const override {
        QStringList names;
        for (qsizetype i = 0; i < std::ssize(samples_); ++i) {
            names.append(QString::number(i));
        }
        return names;
    }

private:
    std::vector<QByteArray> samples_;
};

// Spec section 2: "Comments are not allowed". QJsonDocument skips /* */ and // silently, so a '/'
// outside a string is refused before parsing.
bool hasComment(const QByteArray& json) {
    bool inString = false;
    for (qsizetype i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (inString) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '/') {
            return true;
        }
    }
    return false;
}

bool isIntegral(const QJsonValue& v) {
    if (!v.isDouble()) {
        return false;
    }
    const double d = v.toDouble();
    return std::floor(d) == d && d >= std::numeric_limits<qint32>::min() &&
           d <= std::numeric_limits<qint32>::max();
}

// Walks the object once; the first failure is kept in error_ and every later step is a no-op.
class Builder {
public:
    Builder(const QJsonObject& root, const QString& folder) : root_(root), folder_(folder) {}

    [[nodiscard]] bool run() {
        checkKnownKeys();
        format();
        guid();
        localised(u"name", u"name", true);
        localised(u"group", u"ParentSubMenu", false);
        integer(u"sortIndex", u"pnum");
        boolean(u"layer", u"asLayer");
        integer(u"layerZOrder", u"LayerZOrder");
        script();
        text(u"url", u"DefURLBase", !hasScript_);
        servers();
        headers();
        text(u"version", u"Version", false);
        epsg();
        text(u"contentType", u"ContentType", true);
        text(u"ext", u"Ext", true);
        boolean(u"detectContentType", u"DetectContentType");
        boolean(u"ignoreContentType", u"IgnoreContentType");
        contentTypeSubst();
        text(u"defaultContentType", u"DefaultContentType", false);
        nameInCache();
        localised(u"license", u"License", false);
        integer(u"maxConnections", u"MaxConnectToServerCount");
        integer(u"sleepMs", u"Sleep");
        crop();
        emptyTiles();
        boolean(u"allowHttp", u"IsUseDownloaderInScript");
        text(u"termsOfUseWarning", u"TermsOfUseWarning", false);
        boolean(u"navigationWarning", u"NavigationWarning");
        maxZoom();
        cacheType();
        snapshot();
        return error_.isEmpty();
    }

    [[nodiscard]] const QString& error() const { return error_; }
    [[nodiscard]] core::IniFile takeIni() { return std::move(ini_); }
    [[nodiscard]] std::vector<QByteArray> takeSamples() { return std::move(samples_); }
    [[nodiscard]] const QByteArray& scriptText() const { return scriptText_; }
    [[nodiscard]] bool hasScript() const { return hasScript_; }

private:
    void fail(const QString& key, const QString& what) {
        if (error_.isEmpty()) {
            error_ = QStringLiteral("\"%1\": %2").arg(key, what);
        }
    }

    [[nodiscard]] bool ok() const { return error_.isEmpty(); }

    void put(QStringView paramsKey, const QString& value) { ini_.writeString(kParams, paramsKey, value); }

    void checkKnownKeys() {
        static const QStringList known{u"format"_s,
                                       u"guid"_s,
                                       u"name"_s,
                                       u"group"_s,
                                       u"sortIndex"_s,
                                       u"layer"_s,
                                       u"layerZOrder"_s,
                                       u"url"_s,
                                       u"servers"_s,
                                       u"headers"_s,
                                       u"version"_s,
                                       u"epsg"_s,
                                       u"contentType"_s,
                                       u"ext"_s,
                                       u"detectContentType"_s,
                                       u"ignoreContentType"_s,
                                       u"contentTypeSubst"_s,
                                       u"defaultContentType"_s,
                                       u"nameInCache"_s,
                                       u"license"_s,
                                       u"maxConnections"_s,
                                       u"sleepMs"_s,
                                       u"crop"_s,
                                       u"emptyTiles"_s,
                                       u"allowHttp"_s,
                                       u"script"_s,
                                       u"termsOfUseWarning"_s,
                                       u"navigationWarning"_s,
                                       u"maxZoom"_s,
                                       u"cacheType"_s,
                                       u"snapshot"_s};
        for (const QString& key : root_.keys()) {
            if (!known.contains(key)) {
                fail(key, QStringLiteral("unknown key"));
                return;
            }
        }
    }

    // Returns the value, or Undefined after reporting a missing required key.
    [[nodiscard]] QJsonValue get(QStringView key, bool required) {
        const QJsonValue v = root_.value(key);
        if (v.isUndefined() && required) {
            fail(key.toString(), QStringLiteral("missing required key"));
        }
        return v;
    }

    void format() {
        const QJsonValue v = get(u"format", true);
        if (ok() && (!isIntegral(v) || v.toInt() != 1)) {
            fail(QStringLiteral("format"), QStringLiteral("must be the integer 1"));
        }
    }

    void guid() {
        const QJsonValue v = get(u"guid", true);
        if (!ok()) {
            return;
        }
        static const QRegularExpression re(QStringLiteral(
            "^\\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\}$"));
        if (!v.isString() || !re.match(v.toString()).hasMatch()) {
            fail(QStringLiteral("guid"), QStringLiteral("must be a GUID in braces {8-4-4-4-12}"));
            return;
        }
        put(u"GUID", v.toString());
    }

    void text(QStringView key, QStringView paramsKey, bool required) {
        const QJsonValue v = get(key, required);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (!v.isString()) {
            fail(key.toString(), QStringLiteral("must be a string"));
            return;
        }
        if (required && v.toString().isEmpty()) {
            fail(key.toString(), QStringLiteral("must not be empty"));
            return;
        }
        put(paramsKey, v.toString());
    }

    void integer(QStringView key, QStringView paramsKey) {
        const QJsonValue v = get(key, false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (!isIntegral(v)) {
            fail(key.toString(), QStringLiteral("must be an integer"));
            return;
        }
        put(paramsKey, QString::number(v.toInt()));
    }

    void boolean(QStringView key, QStringView paramsKey) {
        const QJsonValue v = get(key, false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (!v.isBool()) {
            fail(key.toString(), QStringLiteral("must be true or false"));
            return;
        }
        put(paramsKey, v.toBool() ? QStringLiteral("1") : QStringLiteral("0"));
    }

    // Localised: a string, or an object with the default under "" (required) and "<lang>" entries
    // that become "<paramsKey>_<lang>".
    void localised(QStringView key, QStringView paramsKey, bool required) {
        const QJsonValue v = get(key, required);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (v.isString()) {
            put(paramsKey, v.toString());
            return;
        }
        if (!v.isObject()) {
            fail(key.toString(), QStringLiteral("must be a string or an object of strings"));
            return;
        }
        const QJsonObject o = v.toObject();
        if (!o.value(u"").isString()) {
            fail(key.toString(),
                 QStringLiteral("the object form needs a string default under the empty key"));
            return;
        }
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!it.value().isString()) {
                fail(key.toString(), QStringLiteral("must be a string or an object of strings"));
                return;
            }
            put(it.key().isEmpty() ? paramsKey.toString() : paramsKey.toString() + u'_' + it.key(),
                it.value().toString());
        }
    }

    void servers() {
        const QJsonValue v = get(u"servers", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        QStringList names;
        if (v.isArray()) {
            for (const auto& item : v.toArray()) {
                if (!item.isString()) {
                    names.clear();
                    break;
                }
                names.append(item.toString());
            }
        }
        if (!v.isArray() || names.size() != v.toArray().size()) {
            fail(QStringLiteral("servers"), QStringLiteral("must be an array of strings"));
            return;
        }
        put(u"ServerNames", names.join(u','));
    }

    // RequestHead holds the four characters backslash-r-backslash-n between headers, as in a
    // params.txt (an ini value has no line break; the request builder expands them).
    void headers() {
        const QJsonValue v = get(u"headers", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (!v.isObject()) {
            fail(QStringLiteral("headers"), QStringLiteral("must be an object of strings"));
            return;
        }
        const QJsonObject o = v.toObject();
        QStringList lines;
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!it.value().isString()) {
                fail(QStringLiteral("headers"), QStringLiteral("must be an object of strings"));
                return;
            }
            lines.append(it.key() + QStringLiteral(": ") + it.value().toString());
        }
        put(u"RequestHead", lines.join(QStringLiteral("\\r\\n")));
    }

    // MimeTypeSubst: items `"<k>=<v>"` joined with ';', k and v each in quotes (inner quote doubled),
    // then the item in quotes again: the two TStringList levels of ParseSubstList / ParseSubstListItem
    // (u_ContentTypeSubstByList.pas:75-118).
    void contentTypeSubst() {
        const QJsonValue v = get(u"contentTypeSubst", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        const auto quoted = [](const QString& text) {
            QString inner = text;
            return u'"' + inner.replace(u'"', u"\"\""_s) + u'"';
        };
        const QJsonObject o = v.toObject();
        bool valid = v.isObject() && !o.isEmpty();
        QStringList items;
        for (auto it = o.begin(); valid && it != o.end(); ++it) {
            valid = !it.key().isEmpty() && it.value().isString();
            if (valid) {
                items.append(quoted(quoted(it.key()) + u'=' + quoted(it.value().toString())));
            }
        }
        if (!valid) {
            fail(QStringLiteral("contentTypeSubst"),
                 QStringLiteral("must be a non-empty object of strings with no empty key"));
            return;
        }
        put(u"MimeTypeSubst", items.join(u';'));
    }

    void epsg() {
        const QJsonValue v = get(u"epsg", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        const int code = isIntegral(v) ? v.toInt() : 0;
        if (code != 3857 && code != 3395 && code != 4326 && code != 3785) {
            fail(QStringLiteral("epsg"), QStringLiteral("must be 3857, 3395, 4326 or 3785"));
            return;
        }
        put(u"EPSG", QString::number(code));
    }

    void nameInCache() {
        const QJsonValue v = get(u"nameInCache", true);
        if (!ok()) {
            return;
        }
        static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.-]+$"));
        if (!v.isString() || !re.match(v.toString()).hasMatch()) {
            fail(QStringLiteral("nameInCache"),
                 QStringLiteral("must be ASCII letters, digits, '_', '-' or '.'"));
            return;
        }
        put(u"NameInCache", v.toString());
    }

    void crop() {
        const QJsonValue v = get(u"crop", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        static constexpr std::pair<QStringView, QStringView> kSides[] = {{u"left", u"TileRLeft"},
                                                                         {u"top", u"TileRTop"},
                                                                         {u"right", u"TileRRight"},
                                                                         {u"bottom", u"TileRBottom"}};
        const QJsonObject o = v.toObject();
        bool valid = v.isObject() && o.size() == 4;
        for (const auto& [side, paramsKey] : kSides) {
            valid = valid && isIntegral(o.value(side));
        }
        if (!valid) {
            fail(QStringLiteral("crop"),
                 QStringLiteral("must be an object with integer left, top, right, bottom"));
            return;
        }
        for (const auto& [side, paramsKey] : kSides) {
            put(paramsKey, QString::number(o.value(side).toInt()));
        }
    }

    void emptyTiles() {
        const QJsonValue v = get(u"emptyTiles", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (!v.isArray()) {
            fail(QStringLiteral("emptyTiles"), QStringLiteral("must be an array of base64 strings"));
            return;
        }
        for (const auto& item : v.toArray()) {
            if (!item.isString()) {
                fail(QStringLiteral("emptyTiles"), QStringLiteral("must be an array of base64 strings"));
                return;
            }
            const auto decoded = QByteArray::fromBase64Encoding(item.toString().toLatin1(),
                                                                QByteArray::Base64Encoding |
                                                                    QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) {
                fail(QStringLiteral("emptyTiles"), QStringLiteral("must be an array of base64 strings"));
                return;
            }
            samples_.push_back(*decoded);
        }
    }

    void maxZoom() {
        const QJsonValue v = get(u"maxZoom", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (!isIntegral(v) || v.toInt() < 0 || v.toInt() > 23) {
            fail(QStringLiteral("maxZoom"), QStringLiteral("must be an integer 0 ... 23"));
            return;
        }
        put(u"MaxZoom", QString::number(v.toInt()));
    }

    void cacheType() {
        const QJsonValue v = get(u"cacheType", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        if (!isIntegral(v) || v.toInt() != 71) {
            fail(QStringLiteral("cacheType"), QStringLiteral("only 71 (SQLite) is accepted in format 1"));
            return;
        }
        put(u"CacheType", QStringLiteral("71"));
    }

    void snapshot() {
        const QJsonValue v = get(u"snapshot", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        static constexpr std::pair<QStringView, QStringView> kFields[] = {
            {u"index", u"SnapshotIndex"},
            {u"type", u"SnapshotIndexType"},
            {u"item", u"SnapshotItem"},
            {u"timeField", u"SnapshotTimeField"},
            {u"versionField", u"SnapshotVersionField"},
            {u"pick", u"SnapshotPick"}}; // T-0337
        if (!v.isObject()) {
            fail(QStringLiteral("snapshot"), QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const bool known =
                std::ranges::any_of(kFields, [&](const auto& f) { return f.first == it.key(); });
            if (!known) {
                fail(QStringLiteral("snapshot.") + it.key(), QStringLiteral("unknown key"));
                return;
            }
        }
        for (const auto& [name, paramsKey] : kFields) {
            const QJsonValue f = o.value(name);
            const bool required = name == u"index" || name == u"type";
            if (f.isUndefined() && required) {
                fail(QStringLiteral("snapshot.") + name.toString(), QStringLiteral("missing required key"));
                return;
            }
            if (f.isUndefined()) {
                continue;
            }
            if (!f.isString()) {
                fail(QStringLiteral("snapshot.") + name.toString(), QStringLiteral("must be a string"));
                return;
            }
            put(paramsKey, f.toString());
        }
    }

    // `script`: exactly one of {"file"} or {"code"}.
    void script() {
        const QJsonValue v = get(u"script", false);
        if (!ok() || v.isUndefined()) {
            return;
        }
        const QJsonObject o = v.toObject();
        const bool hasFile = o.contains(u"file");
        const bool hasCode = o.contains(u"code");
        if (!v.isObject() || o.size() != 1 || hasFile == hasCode) {
            fail(QStringLiteral("script"),
                 QStringLiteral("must be an object with exactly one of \"file\" or \"code\""));
            return;
        }
        if (hasCode) {
            if (!o.value(u"code").isString()) {
                fail(QStringLiteral("script.code"), QStringLiteral("must be a string"));
                return;
            }
            scriptText_ = o.value(u"code").toString().toUtf8();
            hasScript_ = true;
            return;
        }
        const QJsonValue f = o.value(u"file");
        const QString name = f.toString();
        const QStringList parts = name.split(QRegularExpression(QStringLiteral("[/\\\\]")));
        if (!f.isString() || name.isEmpty() || QDir::isAbsolutePath(name) ||
            parts.contains(QStringLiteral(".."))) {
            fail(QStringLiteral("script.file"),
                 QStringLiteral("must be a relative path without a \"..\" component"));
            return;
        }
        QFile file(QDir(folder_).filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("script.file"), QStringLiteral("cannot read \"%1\"").arg(name));
            return;
        }
        scriptText_ = file.readAll();
        hasScript_ = true;
    }

    const QJsonObject& root_;
    QString folder_;
    QString error_;
    core::IniFile ini_;
    std::vector<QByteArray> samples_;
    QByteArray scriptText_;
    bool hasScript_ = false;
};

} // namespace

bool SourceJsonContainer::isSourceJsonName(const QString& name) {
    return name.endsWith(QLatin1String(".source.json"), Qt::CaseInsensitive);
}

core::Result<std::shared_ptr<const SourceJsonContainer>> SourceJsonContainer::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return core::fail(ErrorCode::IoError, file.errorString());
    }
    return parse(file.readAll(), QFileInfo(filePath).absolutePath());
}

core::Result<std::shared_ptr<const SourceJsonContainer>> SourceJsonContainer::parse(const QByteArray& bytes,
                                                                                    const QString& folder) {
    QByteArray json = bytes;
    if (json.startsWith("\xEF\xBB\xBF")) { // a leading BOM is accepted and ignored
        json.remove(0, 3);
    }
    if (hasComment(json)) {
        return core::fail(ErrorCode::ParseError, QStringLiteral("comments are not allowed"));
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return core::fail(ErrorCode::ParseError, parseError.errorString());
    }
    if (!doc.isObject()) {
        return core::fail(ErrorCode::ParseError, QStringLiteral("the top level must be an object"));
    }
    const QJsonObject root = doc.object();
    Builder builder(root, folder);
    if (!builder.run()) {
        return core::fail(ErrorCode::ParseError, builder.error());
    }
    auto params = core::ConfigProviderIni::wrap(std::make_shared<core::IniFile>(builder.takeIni()));
    std::vector<QByteArray> samples = builder.takeSamples();
    core::IConfigProviderPtr emptyTiles;
    if (!samples.empty()) {
        emptyTiles = std::make_shared<SampleFolder>(std::move(samples));
    }
    return std::make_shared<const SourceJsonContainer>(std::move(params), std::move(emptyTiles),
                                                       builder.scriptText(), builder.hasScript());
}

SourceJsonContainer::SourceJsonContainer(core::IConfigProviderPtr params, core::IConfigProviderPtr emptyTiles,
                                         QByteArray script, bool hasScript)
    : params_(std::move(params)), emptyTiles_(std::move(emptyTiles)), script_(std::move(script)),
      hasScript_(hasScript) {}

core::IConfigProviderPtr SourceJsonContainer::subItem(QStringView id) const {
    if (id.compare(u"params.txt", Qt::CaseInsensitive) == 0) {
        return params_;
    }
    if (id.compare(u"EmptyTiles", Qt::CaseInsensitive) == 0) {
        return emptyTiles_;
    }
    return nullptr;
}

QVariant SourceJsonContainer::value(QStringView) const {
    return {};
}

QByteArray SourceJsonContainer::binary(QStringView id) const {
    return hasScript_ && id.compare(u"GetUrlScript.js", Qt::CaseInsensitive) == 0 ? script_ : QByteArray();
}

QStringList SourceJsonContainer::subItemNames() const {
    QStringList names{QStringLiteral("params.txt")};
    if (emptyTiles_) {
        names.append(QStringLiteral("EmptyTiles"));
    }
    return names;
}

QStringList SourceJsonContainer::valueNames() const {
    return hasScript_ ? QStringList{QStringLiteral("GetUrlScript.js")} : QStringList{};
}

} // namespace cg::maptype
