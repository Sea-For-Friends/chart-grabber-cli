// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataElementComplexBase.pas, i_ConfigSaveLoadStrategy.pas and its two
// implementations (u_ConfigSaveLoadStrategyBasicUseProvider.pas,
// u_ConfigSaveLoadStrategyBasicProviderSubItem.pas)
//
// A ConfigElement that aggregates other ConfigElements: reading/writing forwards to every child in
// add order, and a child's own change is folded into exactly one parent notification. Real call
// sites (u_GlobalConfig.pas:224-266) pass all four ChildFlags false, so lock/batch propagation is
// a rarely-exercised corner: keep it correct, not clever.

#include "cg/core/changeable.h"
#include "cg/core/config_element.h"

#include <memory>
#include <utility>
#include <vector>

namespace cg::core {

// Where a child persists itself. Empty subItem => use the parent's own provider directly (ports
// TConfigSaveLoadStrategyBasicUseProvider); non-empty => provider.subItem(name) on read and
// provider.orCreateSubItem(name) on write (ports TConfigSaveLoadStrategyBasicProviderSubItem).
struct SaveLoadSlot {
    QString subItem;
};

// Flags map 1:1 onto the four booleans of TConfigDataElementComplexBase.Add(item, strategy,
// ANeedReadLock, ANeedWriteLock, ANeedStopNotify, ANeedChangedListen). Declared at namespace scope,
// not nested in ConfigElementComplex: a default function argument of `{}` cannot use a nested
// type's own default member initializers before its enclosing class is complete (GCC 16 correctly
// diagnoses this, see docs/handoffs/T-0002B.md).
// deadlock: all-false by default. Every real legacy call site (u_GlobalConfig.pas:224-266, ~30 of
// them) passes False,False,False,False; the legacy two-argument Add() overload that defaults to
// all-true only works because TMultiReadExclusiveWriteSynchronizer is re-entrant. Our writeLock()
// is a non-recursive shared_mutex (tech-lead-rulings.md, 2026-09-17), so all-true propagation
// self-deadlocks the instant a caller touches a child's own setter/getter, or calls
// child->readConfig(), from inside the parent's writeLock() scope. Callers that genuinely want
// propagation opt in explicitly.
struct ConfigElementChildFlags {
    bool propagateReadLock = false;
    bool propagateWriteLock = false;
    bool propagateBatch = false;
    bool listenToChanges = false;
};

// FEAT-UI-206
class ConfigElementComplex : public ConfigElement {
public:
    ~ConfigElementComplex() override;

    using ChildFlags = ConfigElementChildFlags;

    // Locks self first, then every child flagged propagateReadLock/WriteLock, in add order;
    // unlocks in reverse (core.md 3: "the only nested lock pair in the whole design"). Shadows
    // (does not virtually override) ConfigElement::readLock()/writeLock(): call through a
    // ConfigElementComplex reference/pointer to get propagation.
    class ReadGuard {
    public:
        ReadGuard(ReadGuard&&) noexcept = default;
        ReadGuard(const ReadGuard&) = delete;

    private:
        friend class ConfigElementComplex;
        ReadGuard(ConfigElement::ReadGuard self, std::vector<ConfigElement::ReadGuard> children)
            : self_(std::move(self)), children_(std::move(children)) {}
        ConfigElement::ReadGuard self_;
        std::vector<ConfigElement::ReadGuard> children_;
    };

    class WriteGuard {
    public:
        WriteGuard(WriteGuard&&) noexcept = default;
        WriteGuard(const WriteGuard&) = delete;

    private:
        friend class ConfigElementComplex;
        WriteGuard(ConfigElement::WriteGuard self, std::vector<ConfigElement::WriteGuard> lockedChildren,
                   std::vector<ConfigElement::BatchToken> battedChildren)
            : self_(std::move(self)), lockedChildren_(std::move(lockedChildren)),
              battedChildren_(std::move(battedChildren)) {}
        ConfigElement::WriteGuard self_;
        std::vector<ConfigElement::WriteGuard> lockedChildren_;
        std::vector<ConfigElement::BatchToken> battedChildren_;
    };

    // NOLINTBEGIN(bugprone-derived-method-shadowing-base-method): deliberate, documented shadow
    // (not an override -- the return types differ), per the comment above.
    [[nodiscard]] ReadGuard readLock() const;
    [[nodiscard]] WriteGuard writeLock();
    // NOLINTEND(bugprone-derived-method-shadowing-base-method)

protected:
    void addChild(std::shared_ptr<ConfigElement> child, SaveLoadSlot slot, ChildFlags flags = {});
    void addChangeable(Changeable& child); // listen only; ports the IChangeable overload

    void doReadConfig(const IConfigProviderPtr& src) override; // forwards to every child, in add order
    void doWriteConfig(const IConfigWriterPtr& dst) const override;

    // Called with the parent's own batch open, before setChanged(), whenever a flagged child fires
    // (ports OnItemChange, u_ConfigDataElementComplexBase.pas:352-361).
    virtual void doSubItemChange() {}

private:
    struct Child {
        std::shared_ptr<ConfigElement> element;
        SaveLoadSlot slot;
        ChildFlags flags;
    };
    std::vector<Child> children_;
    std::vector<Subscription> subscriptions_;
};

} // namespace cg::core
