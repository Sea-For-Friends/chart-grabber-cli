// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/active_layers_config.h"

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"

#include <QString>
#include <QStringView>

namespace cg::maptype {

namespace {

// Ports SameText(LeftStr(VKeyName, length(CKeyNameLayer)), CKeyNameLayer) (u_ActiveLayersConfig.pas:83,
// :107). A key shorter than the prefix never matches, as in legacy.
[[nodiscard]] bool hasLayerPrefix(QStringView key) {
    return key.left(5).compare(u"Layer", Qt::CaseInsensitive) == 0;
}

} // namespace

core::IGuidSetStaticPtr ActiveLayersConfig::layerGuids() const {
    const ReadGuard guard = readLock();
    return layerGuids_;
}

void ActiveLayersConfig::setLayerGuids(const core::IGuidSetStaticPtr& value) {
    const WriteGuard guard = writeLock();
    layerGuids_ = value;
    setChanged();
}

// Ports InvertLayerSelectionByGUID (:132-145): notifies always.
void ActiveLayersConfig::invertLayerSelectionByGuid(const QUuid& mapGuid) {
    const WriteGuard guard = writeLock();
    if (!layerGuids_ || !layerGuids_->isExists(mapGuid)) {
        layerGuids_ = core::GuidSetStatic::createByAdd(layerGuids_, mapGuid);
    } else {
        layerGuids_ = core::GuidSetStatic::createByRemove(layerGuids_, mapGuid);
    }
    setChanged();
}

void ActiveLayersConfig::selectLayerByGuid(const QUuid& mapGuid) {
    const WriteGuard guard = writeLock();
    selectLayerByGuidLocked(mapGuid);
}

// Ports UnSelectLayerByGUID (:171-182): notifies only when the set really changes.
void ActiveLayersConfig::unSelectLayerByGuid(const QUuid& mapGuid) {
    const WriteGuard guard = writeLock();
    if (layerGuids_ && layerGuids_->isExists(mapGuid)) {
        layerGuids_ = core::GuidSetStatic::createByRemove(layerGuids_, mapGuid);
        setChanged();
    }
}

// Ports SelectLayerByGUID (:147-158): notifies only when the set really changes.
void ActiveLayersConfig::selectLayerByGuidLocked(const QUuid& mapGuid) {
    if (!layerGuids_ || !layerGuids_->isExists(mapGuid)) {
        layerGuids_ = core::GuidSetStatic::createByAdd(layerGuids_, mapGuid);
        setChanged();
    }
}

// Ports DoReadConfig (:71-91). Accumulates: the set is never cleared by a read.
void ActiveLayersConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    for (const QString& key : src->valueNames()) {
        if (hasLayerPrefix(key)) {
            const QUuid guid = src->readUuid(key, QUuid());
            if (!guid.isNull()) {
                selectLayerByGuidLocked(guid);
            }
        }
    }
}

// Ports DoWriteConfig (:93-120). Deletes every prefixed key, then writes Layer0 ... Layer<n-1> in
// the set's own order. A null set writes nothing after the deletion.
void ActiveLayersConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // defensive; a null writer is a legacy access violation (see deviations.md)
        return;
    }
    for (const QString& key : dst->valueNames()) {
        if (hasLayerPrefix(key)) {
            dst->deleteValue(key);
        }
    }
    if (layerGuids_) {
        for (int i = 0; i < layerGuids_->count(); ++i) {
            dst->writeUuid(QStringLiteral("Layer") + QString::number(i), layerGuids_->item(i));
        }
    }
}

} // namespace cg::maptype
