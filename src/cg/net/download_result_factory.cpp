// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/net/download_result_factory.h"

#include <QCoreApplication>

#include <utility>

namespace cg::net::result {

namespace {

// Free functions have no QObject context for tr(), so translation goes through
// QCoreApplication::translate with a fixed context string -- the deferred-gettext-getter C++
// lacks (see [D-9] in download_result_factory.h). With no translators loaded (as in tests) this
// returns the English source text unchanged.
constexpr const char* kTrContext = "cg::net::result";

QString dataNotExistsMessage(quint32 status) {
    switch (status) {
    case 204:
        return QCoreApplication::translate(kTrContext, "HTTP %1 No Content").arg(status);
    case 400:
        return QCoreApplication::translate(kTrContext, "HTTP %1 Bad Request").arg(status);
    case 404:
        return QCoreApplication::translate(kTrContext, "HTTP %1 Not Found").arg(status);
    default:
        return QCoreApplication::translate(kTrContext, "HTTP %1 Unknown Error").arg(status);
    }
}

} // namespace

DownloadResult ok(quint32 status, QByteArray rawHeader, QByteArray contentType, QByteArray data) {
    DownloadResult r;
    r.kind = ResultKind::Ok;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.contentType = std::move(contentType);
    r.data = std::move(data);
    return r;
}

DownloadResult canceled() {
    DownloadResult r;
    r.kind = ResultKind::Canceled;
    return r;
}

DownloadResult badProxyAuth() {
    DownloadResult r;
    r.kind = ResultKind::ProxyError;
    r.text = QCoreApplication::translate(kTrContext, "Proxy authorization error");
    return r;
}

DownloadResult unexpectedProxyAuth() {
    DownloadResult r;
    r.kind = ResultKind::ProxyError;
    r.text = QCoreApplication::translate(kTrContext, "Unexpected proxy authorization");
    return r;
}

DownloadResult noConnectToServer(int errorCode) {
    DownloadResult r;
    r.kind = ResultKind::NoConnectToServer;
    r.text = QCoreApplication::translate(kTrContext, "Connect to server error. Error code %1").arg(errorCode);
    return r;
}

DownloadResult loadErrorByStatusCode(quint32 status) {
    DownloadResult r;
    r.kind = ResultKind::LoadError;
    r.statusCode = status;
    r.text = QCoreApplication::translate(kTrContext, "Download error. Status code %1").arg(status);
    return r;
}

DownloadResult loadErrorByUnknownStatusCode(quint32 status) {
    DownloadResult r;
    r.kind = ResultKind::LoadError;
    r.statusCode = status;
    r.text = QCoreApplication::translate(kTrContext, "Unknown status code %1").arg(status);
    return r;
}

DownloadResult loadErrorByErrorCode(int errorCode) {
    DownloadResult r;
    r.kind = ResultKind::LoadError;
    r.text = QCoreApplication::translate(kTrContext, "Download error. Error code %1").arg(errorCode);
    return r;
}

DownloadResult loadErrorByUnknownReason(QString text) {
    DownloadResult r;
    // ADR-0004: BuildLoadErrorByUnknownReason constructs TDownloadResultUnknownError, not
    // TDownloadResultLoadError, despite the builder's name -- every curl-level failure ends up
    // UnknownError (isServerExists == false). Caller-supplied text (the curl error string), not
    // translated here.
    r.kind = ResultKind::UnknownError;
    r.text = std::move(text);
    return r;
}

DownloadResult badContentLength(qint64 actual, qint64 expected, quint32 status, QByteArray rawHeader) {
    DownloadResult r;
    r.kind = ResultKind::BadContentLength;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.text =
        QCoreApplication::translate(kTrContext, "Unexpected content length: %1 bytes (expecting: %2 bytes)")
            .arg(actual)
            .arg(expected);
    return r;
}

DownloadResult badContentType(QByteArray contentType, quint32 status, QByteArray rawHeader) {
    DownloadResult r;
    r.kind = ResultKind::BadContentType;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.text = QCoreApplication::translate(kTrContext, "Unexpected content type %1")
                 .arg(QString::fromUtf8(contentType));
    r.contentType = std::move(contentType);
    return r;
}

DownloadResult badContentEncoding(quint32 status, QByteArray rawHeader, QByteArray encoding, QByteArray data,
                                  QString text) {
    DownloadResult r;
    r.kind = ResultKind::BadContentEncoding;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.contentEncoding = std::move(encoding);
    r.data = std::move(data);
    r.text = std::move(text); // caller-supplied: "<exception class>: <message>", not translated
    return r;
}

DownloadResult banned(quint32 status, QByteArray rawHeader) {
    DownloadResult r;
    r.kind = ResultKind::Banned;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.text = QCoreApplication::translate(kTrContext, "Most likely you've been banned by the server!");
    return r;
}

DownloadResult dataNotExists(QString reason, quint32 status, QByteArray rawHeader) {
    DownloadResult r;
    r.kind = ResultKind::DataNotExists;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.text = std::move(reason); // caller-supplied, e.g. "Tile is recognized as empty"
    return r;
}

DownloadResult dataNotExistsByStatusCode(quint32 status, QByteArray rawHeader) {
    DownloadResult r;
    r.kind = ResultKind::DataNotExists;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.text = dataNotExistsMessage(status);
    return r;
}

DownloadResult dataNotExistsZeroSize(quint32 status, QByteArray rawHeader) {
    DownloadResult r;
    r.kind = ResultKind::DataNotExists;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.text = QCoreApplication::translate(kTrContext, "Size of answer is zero");
    return r;
}

DownloadResult notNecessary(QString reason, quint32 status, QByteArray rawHeader) {
    DownloadResult r;
    r.kind = ResultKind::NotNecessary;
    r.statusCode = status;
    r.rawResponseHeader = std::move(rawHeader);
    r.text = std::move(reason); // caller-supplied, e.g. "Tile with same size exists"
    return r;
}

} // namespace cg::net::result
