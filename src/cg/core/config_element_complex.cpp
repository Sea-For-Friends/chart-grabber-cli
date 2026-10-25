// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_element_complex.h"

namespace cg::core {

ConfigElementComplex::~ConfigElementComplex() = default;

void ConfigElementComplex::addChild(std::shared_ptr<ConfigElement> child, SaveLoadSlot slot,
                                    ChildFlags flags) {
    if (flags.listenToChanges && child) {
        subscriptions_.push_back(child->changed().subscribe([this] {
            // Ports OnItemChange (u_ConfigDataElementComplexBase.pas:352-361): suspend this
            // parent's own notification, run the hook, mark dirty, resume -- BatchToken never
            // touches the mutex, so this is safe even when called re-entrantly while a parent
            // WriteGuard is already open on this thread (e.g. from inside doReadConfig()).
            BatchToken token(*this);
            doSubItemChange();
            setChanged();
        }));
    }
    children_.push_back(Child{std::move(child), std::move(slot), flags});
}

void ConfigElementComplex::addChangeable(Changeable& child) {
    subscriptions_.push_back(child.changed().subscribe([this] {
        BatchToken token(*this);
        doSubItemChange();
        setChanged();
    }));
}

void ConfigElementComplex::doReadConfig(const IConfigProviderPtr& src) {
    for (const Child& child : children_) {
        if (!child.element) {
            continue;
        }
        IConfigProviderPtr childSrc = src;
        if (!child.slot.subItem.isEmpty()) {
            childSrc = src ? src->subItem(child.slot.subItem) : nullptr;
        }
        child.element->readConfig(childSrc);
    }
}

void ConfigElementComplex::doWriteConfig(const IConfigWriterPtr& dst) const {
    for (const Child& child : children_) {
        if (!child.element) {
            continue;
        }
        IConfigWriterPtr childDst = dst;
        if (!child.slot.subItem.isEmpty()) {
            childDst = dst ? dst->orCreateSubItem(child.slot.subItem) : nullptr;
        }
        child.element->writeConfig(childDst);
    }
}

ConfigElementComplex::ReadGuard ConfigElementComplex::readLock() const {
    ConfigElement::ReadGuard self = ConfigElement::readLock(); // parent first
    std::vector<ConfigElement::ReadGuard> children;
    for (const Child& child : children_) {
        if (child.flags.propagateReadLock && child.element) {
            children.push_back(child.element->readLock()); // children, in add order
        }
    }
    return ReadGuard(std::move(self), std::move(children));
}

ConfigElementComplex::WriteGuard ConfigElementComplex::writeLock() {
    ConfigElement::WriteGuard self = ConfigElement::writeLock(); // parent first
    std::vector<ConfigElement::WriteGuard> locked;
    std::vector<ConfigElement::BatchToken> batted;
    for (Child& child : children_) {
        if (!child.element) {
            continue;
        }
        if (child.flags.propagateWriteLock) {
            locked.push_back(child.element->writeLock());
        } else if (child.flags.propagateBatch) {
            batted.push_back(ConfigElement::BatchToken(*child.element));
        }
    }
    return WriteGuard(std::move(self), std::move(locked), std::move(batted));
}

} // namespace cg::core
