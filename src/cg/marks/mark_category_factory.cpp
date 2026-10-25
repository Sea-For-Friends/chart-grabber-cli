// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_category_factory.h"

#include <stdexcept>
#include <utility>

// Ports u_MarkCategoryFactory.pas (T-0091). The legacy reads the default name and both scales under one
// read lock (:78-90). Here each getter takes its own lock: the config's lock is not re-entrant, so the
// three reads cannot share one guard. A writer between them is the only difference (UI thread only today).

using namespace Qt::StringLiterals;

namespace cg::marks {

MarkCategoryFactory::MarkCategoryFactory(std::shared_ptr<IMarkCategoryFactoryConfig> config)
    : config_(std::move(config)) {
    if (config_ == nullptr) {
        throw std::invalid_argument("MarkCategoryFactory: nil config");
    }
}

// u_MarkCategoryFactory.pas:72-99: "" -> the default name; "X\" -> "X\" + the default name; any other name
// is kept. The category is visible and takes the two configured scales.
MarkCategoryPtr MarkCategoryFactory::createNew(const QString& name) const {
    QString value = name;
    if (value.isEmpty()) {
        value = config_->defaultName()->value();
    } else if (value.endsWith(QLatin1Char('\\'))) { // RightStr(VName, 1) = '\'
        value += config_->defaultName()->value();
    }
    const int afterScale = config_->afterScale();
    const int beforeScale = config_->beforeScale();
    return std::make_shared<const MarkCategory>(value, true, afterScale, beforeScale);
}

// u_MarkCategoryFactory.pas:106-132: only "" is replaced by the default name (no backslash rule here).
MarkCategoryPtr MarkCategoryFactory::modify(const MarkCategoryPtr& /*source*/, const QString& name,
                                            bool visible, int afterScale, int beforeScale) const {
    QString value = name;
    if (value.isEmpty()) {
        value = config_->defaultName()->value();
    }
    return std::make_shared<const MarkCategory>(value, visible, afterScale, beforeScale);
}

// u_MarkCategoryFactory.pas:134-146: name and both scales are the source's, visibility is the argument.
MarkCategoryPtr MarkCategoryFactory::modifyVisible(const MarkCategoryPtr& source, bool visible) const {
    if (source == nullptr) {
        throw std::invalid_argument("MarkCategoryFactory::modifyVisible: nil source");
    }
    return std::make_shared<const MarkCategory>(source->name(), visible, source->afterScale(),
                                                source->beforeScale());
}

std::shared_ptr<IMarkCategoryFactoryConfig> MarkCategoryFactory::config() const {
    return config_;
}

} // namespace cg::marks
