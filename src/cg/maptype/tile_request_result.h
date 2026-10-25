// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileRequestResult.pas, u_TileRequestResult.pas
//
// What one tile request ended with. Results are immutable. Legacy tells the kinds apart with
// Supports() on seven interfaces; here the same questions are dynamic_cast to the interface classes
// below (the pattern of ITileRequestWithSizeCheck in tile_request.h). Every interface is a pure
// mixin that does NOT derive from ITileRequestResult, as in legacy (ITileRequestResultWithDownloadRequest
// and ...WithDownloadResult do not descend from ITileRequestResult there either); only
// ITileRequestResultStorageError derives from ITileRequestResultError, as in legacy.
//
// Legacy hands a result the IDownloadResult, whose .Request is the ITileDownloadRequest. The ported
// net::DownloadResult carries no request, so the constructors that took only the result take the
// net::DownloadRequest next to it. net::DownloadRequest::source holds the ITileRequest
// (tile_request.h); a request without one is "not a tile request" and the constructors that legacy
// reaches through `Request as ITileDownloadRequest` throw std::logic_error("Interface not supported")
// (the text of Delphi's EIntfCastError, from RTL knowledge: the RTL is not in the tree), as the cast does
// there.

#include "cg/maptype/tile_request.h"
#include "cg/net/download_request.h"
#include "cg/net/download_result.h"

#include <QString>

#include <memory>

namespace cg::maptype {

class ITileRequestResult {
public:
    virtual ~ITileRequestResult() = default;
    [[nodiscard]] virtual ITileRequestPtr request() const = 0;
};
using ITileRequestResultPtr = std::shared_ptr<const ITileRequestResult>;

class ITileRequestResultCanceled {
public:
    virtual ~ITileRequestResultCanceled() = default;
};

class ITileRequestResultOk {
public:
    virtual ~ITileRequestResultOk() = default;
};

class ITileRequestResultError {
public:
    virtual ~ITileRequestResultError() = default;
    [[nodiscard]] virtual QString errorText() const = 0;
};

// Declared in legacy, implemented by no class there (grep of Src/); kept so the interface exists.
class ITileRequestResultStorageError : public ITileRequestResultError {};

class ITileRequestResultWithDownloadRequest {
public:
    virtual ~ITileRequestResultWithDownloadRequest() = default;
    [[nodiscard]] virtual const net::DownloadRequest& downloadRequest() const = 0;
};

class ITileRequestResultWithDownloadResult {
public:
    virtual ~ITileRequestResultWithDownloadResult() = default;
    [[nodiscard]] virtual const net::DownloadResult& downloadResult() const = 0;
};

// True for the result kinds whose legacy class implements IDownloadResultError (i_DownloadResult.pas:68-110,
// u_DownloadResult.pas:75-216): the downloader asks the same question (u_TileDownloaderSimple.pas:245).
[[nodiscard]] bool isDownloadErrorKind(net::ResultKind kind) noexcept;

// u_TileRequestResult.pas:34-43
class TileRequestResult : public ITileRequestResult {
public:
    explicit TileRequestResult(ITileRequestPtr request);
    [[nodiscard]] ITileRequestPtr request() const override;

private:
    ITileRequestPtr request_;
};

// :45-46. No download request exists yet.
class TileRequestResultCanceledBeforBuildDownloadRequest : public TileRequestResult,
                                                           public ITileRequestResultCanceled {
public:
    using TileRequestResult::TileRequestResult;
};

// :48-57
class TileRequestResultCanceledAfterBuildDownloadRequest
    : public TileRequestResultCanceledBeforBuildDownloadRequest,
      public ITileRequestResultWithDownloadRequest {
public:
    explicit TileRequestResultCanceledAfterBuildDownloadRequest(net::DownloadRequest downloadRequest);
    [[nodiscard]] const net::DownloadRequest& downloadRequest() const override;

private:
    net::DownloadRequest downloadRequest_;
};

// :59-68. The request is the one of `downloadResult`'s legacy .Request, i.e. `downloadRequest`.
class TileRequestResultCanceledAfterDownloadRequest final
    : public TileRequestResultCanceledAfterBuildDownloadRequest,
      public ITileRequestResultWithDownloadResult {
public:
    TileRequestResultCanceledAfterDownloadRequest(net::DownloadRequest downloadRequest,
                                                  net::DownloadResult downloadResult);
    [[nodiscard]] const net::DownloadResult& downloadResult() const override;

private:
    net::DownloadResult downloadResult_;
};

// :70-82. Takes any result kind (the saver also wraps DataNotExists in it, :273-285; NotNecessary is not
// wrapped: it ends as ErrorAfterDownloadRequest "Unknown error", u_TileDownloaderSimple.pas:249-251).
class TileRequestResultOk final : public TileRequestResult,
                                  public ITileRequestResultOk,
                                  public ITileRequestResultWithDownloadRequest,
                                  public ITileRequestResultWithDownloadResult {
public:
    TileRequestResultOk(net::DownloadRequest downloadRequest, net::DownloadResult downloadResult);
    [[nodiscard]] const net::DownloadRequest& downloadRequest() const override;
    [[nodiscard]] const net::DownloadResult& downloadResult() const override;

private:
    net::DownloadRequest downloadRequest_;
    net::DownloadResult downloadResult_;
};

// :84-94
class TileRequestResultErrorBeforBuildDownloadRequest : public TileRequestResult,
                                                        public ITileRequestResultError {
public:
    TileRequestResultErrorBeforBuildDownloadRequest(ITileRequestPtr request, QString errorText);
    [[nodiscard]] QString errorText() const override;

private:
    QString errorText_;
};

// :96-106
class TileRequestResultErrorAfterBuildDownloadRequest
    : public TileRequestResultErrorBeforBuildDownloadRequest,
      public ITileRequestResultWithDownloadRequest {
public:
    TileRequestResultErrorAfterBuildDownloadRequest(net::DownloadRequest downloadRequest, QString errorText);
    [[nodiscard]] const net::DownloadRequest& downloadRequest() const override;

private:
    net::DownloadRequest downloadRequest_;
};

// :108-117. Text is "Download error: " + the download result's own text. Legacy takes an
// IDownloadResultError; a result of another kind throws std::invalid_argument.
class TileRequestResultDownloadError final : public TileRequestResultErrorAfterBuildDownloadRequest,
                                             public ITileRequestResultWithDownloadResult {
public:
    TileRequestResultDownloadError(net::DownloadRequest downloadRequest,
                                   net::DownloadResult downloadResultError);
    [[nodiscard]] const net::DownloadResult& downloadResult() const override;

private:
    net::DownloadResult downloadResultError_;
};

// :119-129
class TileRequestResultErrorAfterDownloadRequest final
    : public TileRequestResultErrorAfterBuildDownloadRequest,
      public ITileRequestResultWithDownloadResult {
public:
    TileRequestResultErrorAfterDownloadRequest(net::DownloadRequest downloadRequest,
                                               net::DownloadResult downloadResult, QString errorText);
    [[nodiscard]] const net::DownloadResult& downloadResult() const override;

private:
    net::DownloadResult downloadResult_;
};

} // namespace cg::maptype
