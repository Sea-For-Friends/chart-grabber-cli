// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileDownloadRequestBuilderConfig.pas, u_TileDownloadRequestBuilderConfig.pas
//
// The three strings a ZMP gives the request builder: URL template, server-name list and request
// header (spec maptype.md 2.7.3). The text is an AnsiString in legacy; here the QByteArray holds
// UTF-8 bytes (the INI side is read/written as QString). The URL passes through StrToUrl (trim,
// space -> %20) and the header through StrToHeader (the two characters \r\n -> CR LF) on every
// entry, the ZMP value included; the INI form of the header escapes CR LF back. `ServerNames` is
// read from Maps.ini but never written (docs/parity/legacy-defects.md, L-140).

#include "cg/core/config_element.h"

#include <QByteArray>

#include <memory>

namespace cg::maptype {

struct TileDownloadRequestBuilderConfigStatic { // i_/u_TileDownloadRequestBuilderConfig.pas
    QByteArray urlBase;
    QByteArray serverNames;
    QByteArray requestHeader;
    bool isUseDownloader = false;
    QByteArray defaultProjConverterArgs;
};
using TileDownloadRequestBuilderConfigStaticPtr =
    std::shared_ptr<const TileDownloadRequestBuilderConfigStatic>;

// FEAT-NET-004
class TileDownloadRequestBuilderConfig final : public core::ConfigElement {
public:
    explicit TileDownloadRequestBuilderConfig(const TileDownloadRequestBuilderConfigStaticPtr& defaults);
    ~TileDownloadRequestBuilderConfig() override;

    [[nodiscard]] QByteArray urlBase() const;
    void setUrlBase(const QByteArray& v);
    [[nodiscard]] QByteArray serverNames() const;
    void setServerNames(const QByteArray& v);
    [[nodiscard]] QByteArray requestHeader() const;
    void setRequestHeader(const QByteArray& v);
    [[nodiscard]] bool isUseDownloader() const;
    [[nodiscard]] QByteArray defaultProjConverterArgs() const;
    // The current values read under ONE read lock (legacy Config.LockRead; UrlBase; RequestHeader;
    // IsUseDownloader; UnlockRead, u_TileDownloadRequestBuilderPascalScript.pas:333-340).
    [[nodiscard]] TileDownloadRequestBuilderConfigStatic snapshot() const;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    // doReadConfig runs inside the write lock (core.md 2.16).
    void setUrlBaseLocked(const QByteArray& v);
    void setServerNamesLocked(const QByteArray& v);
    void setRequestHeaderLocked(const QByteArray& v);

    TileDownloadRequestBuilderConfigStaticPtr defaults_; // with StrToUrl / StrToHeader applied
    QByteArray urlBase_;
    QByteArray serverNames_;
    QByteArray requestHeader_;
};

} // namespace cg::maptype
