// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/export_marks_2_kml_config.h"

#include <utility>

// Ports u_ExportMarks2KMLConfig.pas (V10): defaults :108-117, CreateStatic :119-133, DoReadConfig :135-160,
// DoWriteConfig :162-173, getters :175-238, GetStatic :215-218, setters :240-316, static class :318-366.

namespace cg::vector {

namespace {

constexpr int kDefaultCoordPrecision = 6; // cDefaultPrecision (:102)
constexpr int kMinCoordPrecision = 4;     // read clamp, :145
constexpr int kMaxCoordPrecision = 12;    // read clamp, :145

} // namespace

ExportMarks2KmlConfigStatic::ExportMarks2KmlConfigStatic(bool useCoordFormatting, int coordPrecision,
                                                         KmlSortingType sortingType,
                                                         KmlIconScaleType iconScaleType,
                                                         bool useAbsPathToIcon, QString absPathToIcon)
    : useCoordFormatting_(useCoordFormatting), coordPrecision_(coordPrecision), sortingType_(sortingType),
      iconScaleType_(iconScaleType), useAbsPathToIcon_(useAbsPathToIcon),
      absPathToIcon_(std::move(absPathToIcon)) {}

ExportMarks2KmlConfigStatic::~ExportMarks2KmlConfigStatic() = default;

bool ExportMarks2KmlConfigStatic::useCoordFormatting() const {
    return useCoordFormatting_;
}

int ExportMarks2KmlConfigStatic::coordPrecision() const {
    return coordPrecision_;
}

KmlSortingType ExportMarks2KmlConfigStatic::sortingType() const {
    return sortingType_;
}

KmlIconScaleType ExportMarks2KmlConfigStatic::iconScaleType() const {
    return iconScaleType_;
}

bool ExportMarks2KmlConfigStatic::useAbsPathToIcon() const {
    return useAbsPathToIcon_;
}

QString ExportMarks2KmlConfigStatic::absPathToIcon() const {
    return absPathToIcon_;
}

ExportMarks2KmlConfig::ExportMarks2KmlConfig() {
    doBeforeChangeNotify(); // seed the snapshot from the defaults; no setChanged() (legacy:
                            // AfterConstruction)
}

ExportMarks2KmlConfig::~ExportMarks2KmlConfig() = default;

bool ExportMarks2KmlConfig::useCoordFormatting() const {
    const auto guard = readLock();
    return useCoordFormatting_;
}

void ExportMarks2KmlConfig::setUseCoordFormatting(bool value) {
    const auto guard = writeLock();
    if (useCoordFormatting_ != value) {
        useCoordFormatting_ = value;
        setChanged();
    }
}

int ExportMarks2KmlConfig::coordPrecision() const {
    const auto guard = readLock();
    return coordPrecision_;
}

// No clamp here: legacy :253-264 stores any value (the clamp is on read only).
void ExportMarks2KmlConfig::setCoordPrecision(int value) {
    const auto guard = writeLock();
    if (coordPrecision_ != value) {
        coordPrecision_ = value;
        setChanged();
    }
}

KmlSortingType ExportMarks2KmlConfig::sortingType() const {
    const auto guard = readLock();
    return sortingType_;
}

void ExportMarks2KmlConfig::setSortingType(KmlSortingType value) {
    const auto guard = writeLock();
    if (sortingType_ != value) {
        sortingType_ = value;
        setChanged();
    }
}

KmlIconScaleType ExportMarks2KmlConfig::iconScaleType() const {
    const auto guard = readLock();
    return iconScaleType_;
}

void ExportMarks2KmlConfig::setIconScaleType(KmlIconScaleType value) {
    const auto guard = writeLock();
    if (iconScaleType_ != value) {
        iconScaleType_ = value;
        setChanged();
    }
}

bool ExportMarks2KmlConfig::useAbsPathToIcon() const {
    const auto guard = readLock();
    return useAbsPathToIcon_;
}

void ExportMarks2KmlConfig::setUseAbsPathToIcon(bool value) {
    const auto guard = writeLock();
    if (useAbsPathToIcon_ != value) {
        useAbsPathToIcon_ = value;
        setChanged();
    }
}

QString ExportMarks2KmlConfig::absPathToIcon() const {
    const auto guard = readLock();
    return absPathToIcon_;
}

void ExportMarks2KmlConfig::setAbsPathToIcon(const QString& value) {
    const auto guard = writeLock();
    if (absPathToIcon_ != value) {
        absPathToIcon_ = value;
        setChanged();
    }
}

IExportMarks2KmlConfigStaticPtr ExportMarks2KmlConfig::getStatic() const {
    return snapshot_.get();
}

// :142-159: a null provider changes nothing and fires nothing. Each key takes the current value as its
// default. CoordPrecision is clamped AFTER the read, so an absent key also resets a current value out
// of 4..12 (e.g. 99 set through the setter) to 6. SortingType and IconScaleType are assigned only inside
// their enum range. SetChanged (:158) follows unconditionally.
void ExportMarks2KmlConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    useCoordFormatting_ = src->readBool(u"UseCoordFormatting", useCoordFormatting_);
    coordPrecision_ = src->readInt(u"CoordPrecision", coordPrecision_);
    if (coordPrecision_ > kMaxCoordPrecision || coordPrecision_ < kMinCoordPrecision) {
        coordPrecision_ = kDefaultCoordPrecision;
    }
    const auto sorting = src->readInt(u"SortingType", static_cast<qint32>(sortingType_));
    if (sorting >= static_cast<qint32>(KmlSortingType::None) &&
        sorting <= static_cast<qint32>(KmlSortingType::ByNameDesc)) {
        sortingType_ = static_cast<KmlSortingType>(sorting);
    }
    const auto iconScale = src->readInt(u"IconScaleType", static_cast<qint32>(iconScaleType_));
    if (iconScale >= static_cast<qint32>(KmlIconScaleType::Abs) &&
        iconScale <= static_cast<qint32>(KmlIconScaleType::Large)) {
        iconScaleType_ = static_cast<KmlIconScaleType>(iconScale);
    }
    useAbsPathToIcon_ = src->readBool(u"UseAbsPathToIcon", useAbsPathToIcon_);
    absPathToIcon_ = src->readString(u"AbsPathToIcon", absPathToIcon_);
    setChanged();
}

// :162-173: the six keys in this order, always written; the enums as their integers.
// A nil writer is ignored: unreachable in legacy (`u_GlobalState.pas:1277`); no register row.
void ExportMarks2KmlConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) {
        return;
    }
    dst->writeBool(u"UseCoordFormatting", useCoordFormatting_);
    dst->writeInt(u"CoordPrecision", coordPrecision_);
    dst->writeInt(u"SortingType", static_cast<qint32>(sortingType_));
    dst->writeInt(u"IconScaleType", static_cast<qint32>(iconScaleType_));
    dst->writeBool(u"UseAbsPathToIcon", useAbsPathToIcon_);
    dst->writeString(u"AbsPathToIcon", absPathToIcon_);
}

// :119-133: the snapshot is built from the six fields, in constructor order.
void ExportMarks2KmlConfig::doBeforeChangeNotify() {
    snapshot_.set(std::make_shared<const ExportMarks2KmlConfigStatic>(useCoordFormatting_, coordPrecision_,
                                                                      sortingType_, iconScaleType_,
                                                                      useAbsPathToIcon_, absPathToIcon_));
}

} // namespace cg::vector
