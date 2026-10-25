// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/path_config.h"

#include "cg/core/log.h"
#include "cg/core/platform/native_path.h"

#include <QDir>
#include <QRegularExpression>

#include <utility>

namespace cg::core {

PathConfig::PathConfig(QString storeIdent, QString defaultPath, PathConfigPtr base)
    : storeIdent_(std::move(storeIdent)), defaultPath_(std::move(defaultPath)), base_(std::move(base)) {
    setPathLocked(defaultPath_);
    // No setChanged() here: nothing can be subscribed yet, and dirty_ must stay false so the
    // object's first real mutation is not mistaken for a spurious change (finishWrite() would
    // otherwise fire a notification for a call that itself changed nothing).
    fullPath_ = computeFullPathLocked();
}

PathConfig::~PathConfig() = default;

PathConfigPtr PathConfig::create(QString storeIdent, QString defaultPath, PathConfigPtr base) {
    return std::make_shared<PathConfig>(std::move(storeIdent), std::move(defaultPath), std::move(base));
}

QString PathConfig::defaultPath() const noexcept {
    return defaultPath_;
}

QString PathConfig::path() const {
    const ReadGuard guard = readLock();
    return path_;
}

void PathConfig::setPath(const QString& value) {
    const WriteGuard guard = writeLock();
    setPathLocked(value);
    updateFullPathLocked();
}

bool PathConfig::isRelative() const {
    const ReadGuard guard = readLock();
    return relative_;
}

QString PathConfig::fullPath() const {
    const ReadGuard guard = readLock();
    return fullPath_;
}

PathConfigPtr PathConfig::basePathConfig() const {
    const ReadGuard guard = readLock();
    return base_;
}

void PathConfig::setBasePathConfig(PathConfigPtr value) {
    const WriteGuard guard = writeLock();
    base_ = std::move(value);
    subscribeToBaseLocked();
    updateFullPathLocked();
}

void PathConfig::doReadConfig(const IConfigProviderPtr& src) {
    // Ports u_PathConfig's DoReadConfig -- the observable effect of setPath(src->readString(
    // storeIdent_, path())), but calling the private ...Locked() helpers directly: we are already
    // inside the write lock ConfigElement::readConfig() took (core.md 2.16 "...Locked() rule").
    const QString value = (src && !storeIdent_.isEmpty()) ? src->readString(storeIdent_, path_) : path_;
    setPathLocked(value);
    updateFullPathLocked();
}

void PathConfig::doWriteConfig(const IConfigWriterPtr& dst) const {
    if (storeIdent_.isEmpty() || !dst) {
        return;
    }
    dst->writeString(storeIdent_, path_); // already read-locked by ConfigElement::writeConfig()
}

void PathConfig::setPathLocked(const QString& value) {
    QString normalized = value;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    static const QRegularExpression driveLetter(QStringLiteral("^[A-Za-z]:/"));
    const bool windowsAbsolute =
        normalized.startsWith(QStringLiteral("//")) || driveLetter.match(normalized).hasMatch();
    if (windowsAbsolute) {
        path_ = normalized;
        relative_ = false;
        baseChanged_.reset();
        subscribedBaseRaw_ = nullptr;
        // T-0335: on Windows such a path is the normal native form: no warning
        if (!platform::kWindowsHost &&
            !windowsPathWarned_) { // finding 5a: once per instance, not once per setPath() call
            qCWarning(logConfig) << "PathConfig: Windows path kept verbatim, will not exist on Linux:"
                                 << normalized;
            windowsPathWarned_ = true;
        }
        return;
    }

    path_ = normalized;
    relative_ = !normalized.startsWith(QLatin1Char('/'));
    subscribeToBaseLocked();
}

QString PathConfig::computeFullPathLocked() const {
    if (!relative_) {
        // core.md 2.18 rule 1: an absolute path is kept verbatim, delimiter included (u_PathConfig.pas:262).
        return path_;
    }
    const QString rel = path_.isEmpty() ? QStringLiteral(".") : path_;
    // base_->fullPath() takes the BASE's own read lock, a different mutex from ours: safe.
    QString computed = base_ ? QDir::cleanPath(QDir(base_->fullPath()).absoluteFilePath(rel))
                             : QDir::cleanPath(QDir::current().absoluteFilePath(rel));
    while (computed.size() > 1 && computed.endsWith(QLatin1Char('/'))) {
        computed.chop(1); // ExcludeTrailingPathDelimiter semantics (rule 3), relative branch only
    }
    return computed;
}

void PathConfig::updateFullPathLocked() {
    const QString computed = computeFullPathLocked();
    if (computed != fullPath_) {
        fullPath_ = computed;
        setChanged(); // _UpdateFullPath calls SetChanged only on a real difference (rule 4)
    }
}

void PathConfig::subscribeToBaseLocked() {
    // The subscription to the base's changed() exists only while this path is relative (rule 5).
    const bool wantSubscribed = relative_ && base_ != nullptr;
    if (wantSubscribed && baseChanged_.isActive() && subscribedBaseRaw_ == base_.get()) {
        return; // finding 5b: already subscribed to this exact base; don't tear down and rebuild
    }
    if (!wantSubscribed) {
        baseChanged_.reset();
        subscribedBaseRaw_ = nullptr;
        return;
    }
    baseChanged_ = base_->changed().subscribe([this] {
        // Notifier::notify() runs outside the base's own lock, so taking ours here is safe.
        const WriteGuard guard = writeLock();
        updateFullPathLocked();
    });
    subscribedBaseRaw_ = base_.get();
}

} // namespace cg::core
