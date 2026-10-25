// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileDownloaderConfig.pas, u_TileDownloaderConfig.pas:102-179,331-410,
// u_TileDownloaderConfigStatic.pas. docs/specs/net.md §2.16.

#include "cg/core/config_element_complex.h"
#include "cg/core/snapshot.h"
#include "cg/net/inet_config.h"

#include <QByteArray>
#include <QPoint>
#include <QtGlobal>

#include <memory>

namespace cg::net {

struct TileDownloaderSettings { // ports ITileDownloaderConfigStatic
    std::shared_ptr<const InetSettings> inet;
    bool enabled = true; // ZMP UseDwn
    bool allowUseCookie = false;
    quint32 waitIntervalMs = 0;          // ZMP/INI key "Sleep"
    quint32 maxConnectToServerCount = 4; // clamped 1..64
    bool ignoreMimeType = false;
    bool detectMimeType = false;
    QByteArray expectedMimeTypes = "image/jpg";
    QByteArray defaultMimeType = "image/jpg";
    QPoint iteratorSubRectSize{1, 1};
    bool restartDownloadOnMemCacheTTL = false;
};

// Two-layer defaults, exactly as legacy: the ZMP `params.txt` values (`zmpDefaults`, ported from
// legacy's `ADefault: ITileDownloaderConfigStatic` constructor argument) are the baseline, and the
// per-map local INI section overrides a subset (u_TileDownloaderConfig.pas:145-161). The ZMP
// reader itself lives in `maptype`; `net` owns the runtime config object it feeds. `zmpDefaults.inet`
// is ignored (legacy's own constructor ignores `ADefault.InetConfigStatic` too, CreateStatic always
// takes it live from the InetConfig argument instead).
class TileDownloaderConfig final : public cg::core::ConfigElementComplex {
public:
    TileDownloaderConfig(std::shared_ptr<InetConfig> inetConfig, TileDownloaderSettings zmpDefaults);
    ~TileDownloaderConfig() override;

    [[nodiscard]] std::shared_ptr<const TileDownloaderSettings> settings() const;

    [[nodiscard]] bool enabled() const;
    void setEnabled(bool v); // coerced down: cannot rise above the ZMP baseline
    [[nodiscard]] bool allowUseCookie() const;
    void setAllowUseCookie(bool v);
    [[nodiscard]] quint32 waitIntervalMs() const;
    void setWaitIntervalMs(quint32 v);
    [[nodiscard]] quint32 maxConnectToServerCount() const;
    void setMaxConnectToServerCount(quint32 v); // clamped: >64 -> 64, ==0 -> 1
    [[nodiscard]] bool ignoreMimeType() const;
    void setIgnoreMimeType(bool v);
    [[nodiscard]] bool detectMimeType() const;
    void setDetectMimeType(bool v);
    [[nodiscard]] QByteArray expectedMimeTypes() const;
    void setExpectedMimeTypes(QByteArray v);
    [[nodiscard]] QByteArray defaultMimeType() const;
    void setDefaultMimeType(QByteArray v);
    [[nodiscard]] QPoint iteratorSubRectSize() const;
    void setIteratorSubRectSize(QPoint v);
    [[nodiscard]] bool restartDownloadOnMemCacheTTL() const;
    void setRestartDownloadOnMemCacheTTL(bool v);

protected:
    void doReadConfig(const cg::core::IConfigProviderPtr& src) override;
    void doWriteConfig(
        const cg::core::IConfigWriterPtr& dst) const override; // Sleep + MaxConnect only, differential
    void doBeforeChangeNotify() override;

private:
    void setMaxConnectToServerCountLocked(quint32 v); // the "...Locked()" split (core.md 2.16):
                                                      // doReadConfig calls this while already
                                                      // inside the write lock.

    std::shared_ptr<InetConfig> inetConfig_; // listened to, not persisted here (see .cpp)
    TileDownloaderSettings zmpDefault_;

    bool enabled_;
    bool allowUseCookie_;
    quint32 waitIntervalMs_;
    quint32 maxConnectToServerCount_;
    bool ignoreMimeType_;
    bool detectMimeType_;
    QByteArray expectedMimeTypes_;
    QByteArray defaultMimeType_;
    QPoint iteratorSubRectSize_;
    bool restartDownloadOnMemCacheTTL_;

    cg::core::Snapshot<TileDownloaderSettings> settings_;
};

} // namespace cg::net
