// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_request_builder_config.h"

#include "cg/core/delphi_text.h"

#include <QString>

namespace cg::maptype {

namespace {

// StringReplaceA(TrimA(s), ' ', '%20', [rfReplaceAll])
QByteArray strToUrl(const QByteArray& s) {
    const QString trimmed = core::delphiTrim(QString::fromUtf8(s));
    return trimmed.toUtf8().replace(' ', "%20");
}

// StringReplaceA(s, '\r\n', #13#10, [rfIgnoreCase, rfReplaceAll]): the 4 characters \ r \ n.
QByteArray strToHeader(const QByteArray& s) {
    QByteArray out;
    out.reserve(s.size());
    qsizetype i = 0;
    while (i < s.size()) {
        if (i + 3 < s.size() && s[i] == '\\' && (s[i + 1] == 'r' || s[i + 1] == 'R') && s[i + 2] == '\\' &&
            (s[i + 3] == 'n' || s[i + 3] == 'N')) {
            out.append("\r\n");
            i += 4;
        } else {
            out.append(s[i]);
            ++i;
        }
    }
    return out;
}

QByteArray headerToStr(const QByteArray& header) {
    return QByteArray(header).replace("\r\n", "\\r\\n");
}

} // namespace

TileDownloadRequestBuilderConfig::TileDownloadRequestBuilderConfig(
    const TileDownloadRequestBuilderConfigStaticPtr& defaults)
    : defaults_(std::make_shared<const TileDownloadRequestBuilderConfigStatic>(
          TileDownloadRequestBuilderConfigStatic{
              strToUrl(defaults->urlBase), defaults->serverNames, strToHeader(defaults->requestHeader),
              defaults->isUseDownloader, defaults->defaultProjConverterArgs})),
      urlBase_(defaults_->urlBase), serverNames_(defaults_->serverNames),
      requestHeader_(defaults_->requestHeader) {}

TileDownloadRequestBuilderConfig::~TileDownloadRequestBuilderConfig() = default;

QByteArray TileDownloadRequestBuilderConfig::urlBase() const {
    const auto guard = readLock();
    return urlBase_;
}

void TileDownloadRequestBuilderConfig::setUrlBase(const QByteArray& v) {
    const auto guard = writeLock();
    setUrlBaseLocked(v);
}

QByteArray TileDownloadRequestBuilderConfig::serverNames() const {
    const auto guard = readLock();
    return serverNames_;
}

void TileDownloadRequestBuilderConfig::setServerNames(const QByteArray& v) {
    const auto guard = writeLock();
    setServerNamesLocked(v);
}

QByteArray TileDownloadRequestBuilderConfig::requestHeader() const {
    const auto guard = readLock();
    return requestHeader_;
}

void TileDownloadRequestBuilderConfig::setRequestHeader(const QByteArray& v) {
    const auto guard = writeLock();
    setRequestHeaderLocked(v);
}

bool TileDownloadRequestBuilderConfig::isUseDownloader() const {
    return defaults_->isUseDownloader;
}

TileDownloadRequestBuilderConfigStatic TileDownloadRequestBuilderConfig::snapshot() const {
    const auto guard = readLock();
    return {urlBase_, serverNames_, requestHeader_, defaults_->isUseDownloader,
            defaults_->defaultProjConverterArgs};
}

QByteArray TileDownloadRequestBuilderConfig::defaultProjConverterArgs() const {
    return defaults_->defaultProjConverterArgs;
}

void TileDownloadRequestBuilderConfig::setUrlBaseLocked(const QByteArray& v) {
    const QByteArray url = strToUrl(v);
    if (urlBase_ != url) {
        urlBase_ = url;
        setChanged();
    }
}

void TileDownloadRequestBuilderConfig::setServerNamesLocked(const QByteArray& v) {
    if (serverNames_ != v) {
        serverNames_ = v;
        setChanged();
    }
}

void TileDownloadRequestBuilderConfig::setRequestHeaderLocked(const QByteArray& v) {
    const QByteArray header = strToHeader(v);
    if (requestHeader_ != header) {
        requestHeader_ = header;
        setChanged();
    }
}

void TileDownloadRequestBuilderConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    setUrlBaseLocked(src->readString(u"URLBase", QString::fromUtf8(urlBase_)).toUtf8());
    setServerNamesLocked(src->readString(u"ServerNames", QString::fromUtf8(serverNames_)).toUtf8());
    setRequestHeaderLocked(src->readString(u"RequestHead", QString::fromUtf8(requestHeader_)).toUtf8());
}

// Differs-rule; no ServerNames (L-140).
void TileDownloadRequestBuilderConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) {
        return;
    }
    if (urlBase_ != defaults_->urlBase) {
        dst->writeString(u"URLBase", QString::fromUtf8(urlBase_));
    } else {
        dst->deleteValue(u"URLBase");
    }
    if (requestHeader_ != defaults_->requestHeader) {
        dst->writeString(u"RequestHead", QString::fromUtf8(headerToStr(requestHeader_)));
    } else {
        dst->deleteValue(u"RequestHead");
    }
}

} // namespace cg::maptype
