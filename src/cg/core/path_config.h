// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/i_PathConfig.pas, Src/u_PathConfig.pas
//
// A path that is also a persisted setting, optionally relative to another PathConfig (the app
// install dir, say). Regression-anchored by the legacy DUnit suite `Test/u_PathConfig_Test.pas`.
// Legacy stores Windows paths; on read every backslash becomes a forward slash before any further
// processing, and a value that still looks like a Windows path (`C:\...`, `\\server\...`) is kept
// verbatim and flagged non-relative -- it will simply not exist on Linux, which is the honest
// outcome.

#include "cg/core/config_element.h"

#include <memory>

namespace cg::core {

class PathConfig;
using PathConfigPtr = std::shared_ptr<PathConfig>;

class PathConfig final : public ConfigElement {
public:
    PathConfig(QString storeIdent, QString defaultPath, PathConfigPtr base);
    ~PathConfig() override;

    // storeIdent: the ini key this path is read from / written to; empty = not persisted.
    // defaultPath: e.g. "./Maps". base: the PathConfig this one is relative to, or nullptr.
    [[nodiscard]] static PathConfigPtr create(QString storeIdent, QString defaultPath, PathConfigPtr base);

    [[nodiscard]] QString defaultPath() const noexcept; // never locked: immutable
    [[nodiscard]] QString path() const;                 // read-locked
    void setPath(const QString& value);                 // write-locked
    [[nodiscard]] bool isRelative() const;              // read-locked
    // read-locked. Absolute: == path(), delimiter kept (rule 1). Relative: no trailing separator.
    [[nodiscard]] QString fullPath() const;

    [[nodiscard]] PathConfigPtr basePathConfig() const;
    void setBasePathConfig(PathConfigPtr value);

protected:
    void doReadConfig(const IConfigProviderPtr& src) override;
    void doWriteConfig(const IConfigWriterPtr& dst) const override;

private:
    void setPathLocked(const QString& value); // the "...Locked()" split
    [[nodiscard]] QString computeFullPathLocked() const;
    void updateFullPathLocked();
    void subscribeToBaseLocked(); // only while relative

    QString storeIdent_;
    QString defaultPath_;
    PathConfigPtr base_;
    QString path_;
    QString fullPath_;
    bool relative_ = true;
    bool windowsPathWarned_ = false;                // Warn once, not on every setPath()
    const PathConfig* subscribedBaseRaw_ = nullptr; // Identity of the currently subscribed
                                                    // base, to skip a needless
                                                    // teardown/rebuild; never dereferenced.
    Subscription baseChanged_;
};

} // namespace cg::core
