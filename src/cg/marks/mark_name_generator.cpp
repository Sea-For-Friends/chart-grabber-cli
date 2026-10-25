// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_name_generator.h"

#include "cg/core/delphi_format.h"

#include <QByteArray>

#include <span>
#include <stdexcept>
#include <utility>

// Ports u_MarkNameGenerator.pas (T-0091). The format element is a child added with all four flags on
// (u_MarkNameGenerator.pas:65, Add(item, BasicUseProvider)), as maptype/map_type_gui_config.cpp does.
// The 'Name' sub-item slot (u_MarkTemplateConfigBase.pas:67, all four flags off) belongs to the owner,
// TMarkTemplateConfigBase (M2b), not to the generator.

using namespace Qt::StringLiterals;

namespace cg::marks {

MarkNameGenerator::MarkNameGenerator(std::shared_ptr<core::IStringConfigDataElement> formatString)
    : formatString_(std::move(formatString)) {
    if (formatString_ == nullptr) {
        throw std::invalid_argument("MarkNameGenerator: nil format string");
    }
    addChild(formatString_, core::SaveLoadSlot{}, ChildFlags{true, true, true, true});
}

MarkNameGenerator::~MarkNameGenerator() = default;

std::shared_ptr<core::IStringConfigDataElement> MarkNameGenerator::formatString() const {
    return formatString_;
}

int MarkNameGenerator::counter() const {
    const auto guard = readLock();
    return counter_;
}

// u_MarkNameGenerator.pas:113-124.
void MarkNameGenerator::setCounter(int value) {
    const auto guard = writeLock();
    if (counter_ != value) {
        counter_ = value;
        setChanged();
    }
}

// u_MarkNameGenerator.pas:101-111: under the write lock Format(FormatString, [Counter]), then Inc,
// SetChanged. A format error (a malformed format, %s, a missing argument) leaves before Inc: the counter is
// unchanged, nothing is notified and the guard releases the lock. The plain ConfigElement lock is taken (not
// the propagating one): the legacy lock is re-entrant, ours is not, and Value takes the child's own lock.
QString MarkNameGenerator::newName() {
    const auto guard = ConfigElement::writeLock();
    const core::FormatArg arg = static_cast<qint64>(counter_);
    const auto text = core::delphiFormat(formatString_->value().toUtf8(), std::span(&arg, 1));
    if (!text) {
        throw std::runtime_error(text.error().what().toUtf8().toStdString());
    }
    ++counter_;
    setChanged();
    return QString::fromUtf8(*text);
}

// u_MarkNameGenerator.pas:69-76 (:73 Counter, :74 SetChanged): the child first (inherited), then Counter when
// the provider is not nil.
void MarkNameGenerator::doReadConfig(const core::IConfigProviderPtr& src) {
    ConfigElementComplex::doReadConfig(src);
    if (src) {
        counter_ = src->readInt(u"Counter"_s, counter_);
        setChanged();
    }
}

// u_MarkNameGenerator.pas:78-84: the child first, then Counter.
void MarkNameGenerator::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    ConfigElementComplex::doWriteConfig(dst);
    if (dst == nullptr) {
        return; // defensive, as string_config_data_element_with_def_base.cpp; no legacy caller traced
    }
    dst->writeInt(u"Counter"_s, counter_);
}

} // namespace cg::marks
