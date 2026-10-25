// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/string_config_data_element_with_def_base.h"

#include <utility>

namespace cg::core {

StringConfigDataElementWithDefBase::StringConfigDataElementWithDefBase(
    std::shared_ptr<ILanguageManager> languageManager, bool useStore, QString storeIdentifier,
    bool isStoreDefault)
    : languageManager_(std::move(languageManager)), useStore_(useStore),
      storeIdentifier_(std::move(storeIdentifier)), isStoreDefault_(isStoreDefault) {}

StringConfigDataElementWithDefBase::~StringConfigDataElementWithDefBase() = default;

void StringConfigDataElementWithDefBase::init() {
    defValue_ = getDefValueForCurrentLang();
    value_ = defValue_;
    langChangeSubscription_ = languageManager_->changed().subscribe([this] { onLangChange(); });
}

void StringConfigDataElementWithDefBase::doReadConfig(const IConfigProviderPtr& src) {
    if (useStore_ && src != nullptr) {
        setValueLocked(src->readString(storeIdentifier_, value_));
    }
}

void StringConfigDataElementWithDefBase::doWriteConfig(const IConfigWriterPtr& dst) const {
    // Defensive; no legacy path found that passes a nil writer to a string element (legacy would
    // raise EAccessViolation here; readConfig has the nil check).
    if (!useStore_ || dst == nullptr) {
        return;
    }
    const QString suffixed = storeIdentifier_ + QLatin1Char('_');
    if (isStoreDefault_) {
        if (value_ != defValue_) {
            dst->writeString(storeIdentifier_, value_);
            dst->deleteValue(suffixed);
        } else {
            dst->writeString(suffixed, value_);
            dst->deleteValue(storeIdentifier_);
        }
    } else if (value_ != defValue_) {
        dst->writeString(storeIdentifier_, value_);
    } else {
        dst->deleteValue(storeIdentifier_);
    }
}

QString StringConfigDataElementWithDefBase::defaultValue() const {
    const ReadGuard guard = readLock();
    return defValue_;
}

QString StringConfigDataElementWithDefBase::value() const {
    const ReadGuard guard = readLock();
    return value_;
}

void StringConfigDataElementWithDefBase::onLangChange() {
    const WriteGuard guard = writeLock();
    const QString defValueNew = getDefValueForCurrentLang();
    if (value_ == defValue_) {
        setValueLocked(defValueNew);
    }
    defValue_ = defValueNew;
}

void StringConfigDataElementWithDefBase::setValue(const QString& value) {
    const WriteGuard guard = writeLock();
    setValueLocked(value);
}

void StringConfigDataElementWithDefBase::setValueLocked(const QString& value) {
    if (value_ != value) {
        value_ = value;
        setChanged();
    }
}

} // namespace cg::core
