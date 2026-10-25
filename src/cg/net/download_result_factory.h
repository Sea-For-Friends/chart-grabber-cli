// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_DownloadResultFactory.pas, u_DownloadResultFactory.pas. docs/specs/net.md §2.5. The
// legacy factory is an interface with 17 methods and exactly one implementation whose only state
// is nothing: per idioms.md §13 purpose (a) it becomes free functions in cg::net::result, not an
// interface. The value they add is the message table below, a real contract -- docs/parity texts,
// .mo translation keys and the tile-error log depend on the exact English strings. [D-9]: the
// second, textually-inconsistent copy of this table (i_DownloadResultTextProvider.pas) is dropped;
// there is one table, and messages are translated at construction (tr()), not in a getter.

#include "cg/net/download_result.h"

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace cg::net::result {

[[nodiscard]] DownloadResult ok(quint32 status, QByteArray rawHeader, QByteArray contentType,
                                QByteArray data);
[[nodiscard]] DownloadResult canceled();
[[nodiscard]] DownloadResult badProxyAuth();
[[nodiscard]] DownloadResult unexpectedProxyAuth();
[[nodiscard]] DownloadResult noConnectToServer(int errorCode);
[[nodiscard]] DownloadResult loadErrorByStatusCode(quint32 status);
[[nodiscard]] DownloadResult loadErrorByUnknownStatusCode(quint32 status);
[[nodiscard]] DownloadResult loadErrorByErrorCode(int errorCode);
[[nodiscard]] DownloadResult loadErrorByUnknownReason(QString text);
[[nodiscard]] DownloadResult badContentLength(qint64 actual, qint64 expected, quint32 status,
                                              QByteArray rawHeader);
[[nodiscard]] DownloadResult badContentType(QByteArray contentType, quint32 status, QByteArray rawHeader);
[[nodiscard]] DownloadResult badContentEncoding(quint32 status, QByteArray rawHeader, QByteArray encoding,
                                                QByteArray data, QString text);
[[nodiscard]] DownloadResult banned(quint32 status, QByteArray rawHeader);
[[nodiscard]] DownloadResult dataNotExists(QString reason, quint32 status, QByteArray rawHeader);
[[nodiscard]] DownloadResult dataNotExistsByStatusCode(quint32 status, QByteArray rawHeader);
[[nodiscard]] DownloadResult dataNotExistsZeroSize(quint32 status, QByteArray rawHeader);
[[nodiscard]] DownloadResult notNecessary(QString reason, quint32 status, QByteArray rawHeader);

} // namespace cg::net::result
