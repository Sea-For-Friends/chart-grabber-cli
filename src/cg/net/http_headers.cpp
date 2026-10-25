// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/Basic/u_NetworkStrFunc.pas:69-403; Src/PascalScript/u_PascalScriptUtils.pas:88-119.
// FEAT-NET-011 (this file backs the raw ResponseHead/RequestHead contract that content-type
// detection and the ZMP script header functions both sit on top of). T-0028: the line-scan engine
// (IdemPChar/GetHeaderValueUp/ReplaceHeaderValueUp/DeleteHeaderValueUp and the script-facing
// wrappers) moved to cg::core::header_text; this file keeps thin forwards under the names net's
// own code calls unqualified (curl_downloader.cpp, proxy_resolver.cpp), plus the net-only helpers
// (addHeaderValue, parseStatusLine, urlEncode/urlDecode).
#include "cg/net/http_headers.h"

#include "cg/core/header_text.h"

namespace cg::net {

namespace {

[[nodiscard]] bool isControl(char c) noexcept {
    return static_cast<unsigned char>(c) < static_cast<unsigned char>(' ');
}

} // namespace

QByteArray getHeaderValueUp(QByteArrayView headers, QByteArrayView upName) {
    return core::getHeaderValueUp(headers, upName);
}

bool replaceHeaderValueUp(QByteArray& headers, QByteArrayView upName, QByteArrayView value) {
    return core::replaceHeaderValueUp(headers, upName, value);
}

void deleteHeaderValueUp(QByteArray& headers, QByteArrayView upName) {
    core::deleteHeaderValueUp(headers, upName);
}

void addHeaderValue(QByteArray& headers, QByteArrayView name, QByteArrayView value) {
    const QByteArray insertText = name.toByteArray() + ": " + value.toByteArray();
    if (headers.isEmpty()) {
        headers = insertText + "\r\n";
        return;
    }
    const qsizetype n = headers.size();
    qsizetype runStart = n; // == n means no trailing control-char run at all
    qsizetype i = n - 1;
    while (i >= 0 && isControl(headers[i])) {
        runStart = i;
        --i;
    }
    if (runStart == n) {
        // No trailing terminator: add our own separator and our own trailing CRLF.
        headers += "\r\n" + insertText + "\r\n";
    } else if (runStart > 0) {
        // Splice before the trailing control-char run so a trailing blank line stays last.
        headers.insert(runStart, "\r\n" + insertText);
    } else {
        // The whole blob is control characters: prepend with no CRLF added.
        headers = insertText + headers;
    }
}

quint32 parseStatusLine(QByteArrayView headers) {
    if (!core::idemPChar(headers, 0, QByteArrayView("HTTP/"))) {
        return 0;
    }
    const qsizetype n = headers.size();
    qsizetype firstSpace = -1;
    qsizetype p = 0;
    for (; p < n; ++p) {
        if (headers[p] == '\0') {
            break;
        }
        if (headers[p] == ' ') {
            if (firstSpace < 0) {
                firstSpace = p + 1;
            } else {
                break;
            }
        }
    }
    if (firstSpace < 0 || firstSpace > p) {
        return 0;
    }
    const QByteArrayView codeStr = headers.sliced(firstSpace, p - firstSpace);
    if (codeStr.isEmpty()) {
        return 0;
    }
    bool ok = false;
    const quint32 code = codeStr.toByteArray().toUInt(&ok);
    return ok ? code : 0;
}

QByteArray urlEncode(QByteArrayView s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    QByteArray out;
    out.reserve(s.size() * 3);
    for (const char c : s) {
        const auto uc = static_cast<unsigned char>(c);
        if ((uc >= '0' && uc <= '9') || (uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z') || uc == '_' ||
            uc == '*' || uc == '-' || uc == '.') {
            out.append(c);
        } else if (uc == ' ') {
            out.append('+');
        } else {
            out.append('%');
            out.append(kHex[uc >> 4]);
            out.append(kHex[uc & 0x0F]);
        }
    }
    return out;
}

QByteArray urlDecode(QByteArrayView s) {
    return QByteArray::fromPercentEncoding(s.toByteArray());
}

} // namespace cg::net
