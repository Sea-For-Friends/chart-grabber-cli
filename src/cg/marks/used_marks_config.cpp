// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/used_marks_config.h"

// Ports u_UsedMarksConfig.pas (T-0147): constructor :65-71, CreateStatic :73-84, DoReadConfig :86-95,
// DoWriteConfig :97-103, getters :105-133, GetStatic :135-138, setters :140-177.

namespace cg::marks {

UsedMarksConfig::UsedMarksConfig() {
    doBeforeChangeNotify(); // seed the snapshot (legacy: TConfigDataElementWithStaticBase.AfterConstruction);
                            // no setChanged()
}

UsedMarksConfig::~UsedMarksConfig() = default;

bool UsedMarksConfig::isUseMarks() const {
    const auto guard = readLock();
    return isUseMarks_;
}

void UsedMarksConfig::setIsUseMarks(bool value) {
    const auto guard = writeLock();
    if (isUseMarks_ != value) {
        isUseMarks_ = value;
        setChanged();
    }
}

bool UsedMarksConfig::ignoreCategoriesVisible() const {
    const auto guard = readLock();
    return ignoreCategoriesVisible_;
}

void UsedMarksConfig::setIgnoreCategoriesVisible(bool value) {
    const auto guard = writeLock();
    if (ignoreCategoriesVisible_ != value) {
        ignoreCategoriesVisible_ = value;
        setChanged();
    }
}

bool UsedMarksConfig::ignoreMarksVisible() const {
    const auto guard = readLock();
    return ignoreMarksVisible_;
}

void UsedMarksConfig::setIgnoreMarksVisible(bool value) {
    const auto guard = writeLock();
    if (ignoreMarksVisible_ != value) {
        ignoreMarksVisible_ = value;
        setChanged();
    }
}

IUsedMarksConfigStaticPtr UsedMarksConfig::getStatic() const {
    return snapshot_.get();
}

// u_UsedMarksConfig.pas:89-94: a null provider changes nothing and fires nothing; otherwise the three keys
// are read in this order (:90-92), each with the current value as default, and SetChanged (:93) follows even
// when nothing changed.
void UsedMarksConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    isUseMarks_ = src->readBool(u"IsUseMarks", isUseMarks_);
    ignoreCategoriesVisible_ = src->readBool(u"IgnoreCategoriesVisible", ignoreCategoriesVisible_);
    ignoreMarksVisible_ = src->readBool(u"IgnoreMarksVisible", ignoreMarksVisible_);
    setChanged();
}

// u_UsedMarksConfig.pas:100-102: the same key order as the read; always written.
void UsedMarksConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // defensive: legacy DoWriteConfig (:97-103) has no nil check and would raise an access
                // violation
        return;
    }
    dst->writeBool(u"IsUseMarks", isUseMarks_);
    dst->writeBool(u"IgnoreCategoriesVisible", ignoreCategoriesVisible_);
    dst->writeBool(u"IgnoreMarksVisible", ignoreMarksVisible_);
}

// u_UsedMarksConfig.pas:73-84: built from the fields in the constructor's order (:79-81), which differs from
// the key order above.
void UsedMarksConfig::doBeforeChangeNotify() {
    snapshot_.set(std::make_shared<const UsedMarksConfigStatic>(isUseMarks_, ignoreMarksVisible_,
                                                                ignoreCategoriesVisible_));
}

} // namespace cg::marks
