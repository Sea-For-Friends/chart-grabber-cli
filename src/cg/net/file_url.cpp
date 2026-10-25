// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/net/file_url.h"

#include "cg/core/platform/file_ops.h"

namespace cg::net {

std::optional<QByteArray> filePathFromUrl(const QByteArray& url) {
    const qsizetype len = url.size();
    if (len < 4) {
        return std::nullopt; // too short for any recognized form
    }

    const char first = url.at(0);
    const char second = url.at(1);
    const char third = url.at(2);

    if (second == 't' || second == 'T') {
        return std::nullopt; // fast-reject http/https/ftp
    }
    if (first == '\\' && second == '\\') {
        return std::nullopt; // UNC, e.g. \\server\share\... -- no Windows path forms here [D-11]
    }
    if (second == ':' && third == '\\') {
        return std::nullopt; // drive letter, e.g. C:\dir\... [D-11]
    }
    if (first != 'f' && first != 'F') {
        return std::nullopt;
    }
    if (len <= 10) {
        return std::nullopt;
    }
    if (url.left(8).compare(QByteArrayLiteral("file:///"), Qt::CaseInsensitive) != 0) {
        return std::nullopt;
    }
    // Strip the 7-byte "file://" scheme+authority, keeping the 3rd '/' as the path's leading
    // separator (RFC 8089: file:// + empty host + "/" path). Legacy strips all 8 bytes of
    // "file:///" and reinstates a root by replacing '/' with '\' for its Windows path forms
    // (C:\..., \\server\...); there is no such reinstatement on Linux, so keeping the 8th byte
    // here is what makes file:///tmp/example/a.png become /tmp/example/a.png rather than home/u/a.png.
    return core::platform::nativeFilePathFromUrlPath(url.mid(7)); // T-0335: "/C:/x" -> "C:/x" on Windows only
}

} // namespace cg::net
