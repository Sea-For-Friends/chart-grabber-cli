// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ProjectionSet.pas, u_ProjectionSetSimple.pas, i_ProjectionSetFactory.pas,
// u_ProjectionSetFactorySimple.pas, i_ProjectionSetList.pas, u_ProjectionSetListStatic.pas,
// u_ProjectionSetListStaticSimple.pas
//
// The 24-level pyramid (one IProjection per zoom, 0..23) for one projection type, plus the four
// legacy predefined sets and the settings-dialog list. Immutable snapshot, no locks (§8.1).

#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/projection_type_factory.h"

#include <QString>
#include <QtGlobal>

#include <memory>
#include <vector>

namespace cg::geo {

class IProjectionSet {
public:
    virtual ~IProjectionSet() = default;

    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual bool isSame(const IProjectionSet& other) const = 0;

    [[nodiscard]] virtual quint8 zoomCount() const noexcept = 0;             // always 24
    [[nodiscard]] virtual const ProjectionPtr& zoom(quint8 index) const = 0; // operator[] in legacy

    virtual void validateZoom(quint8& zoom) const noexcept = 0; // clamp to [0, 23]
    [[nodiscard]] virtual bool checkZoom(quint8 zoom) const noexcept = 0;

    // Cross-source reprojection: given a projection from *another* set, find the level of this
    // set whose ground resolution is closest. u_ProjectionSetSimple.pas.
    [[nodiscard]] virtual ProjectionPtr suitableProjection(const IProjection& other) const = 0;
    [[nodiscard]] virtual quint8 suitableZoom(const IProjection& other) const = 0;
    [[nodiscard]] virtual bool isProjectionFromThisSet(const IProjection& other) const = 0;
};
using ProjectionSetPtr = std::shared_ptr<const IProjectionSet>;

// u_ProjectionSetSimple.pas -- a plain 24-entry pyramid over one projection type.
class ProjectionSetSimple final : public IProjectionSet {
public:
    ProjectionSetSimple(core::HashValue hash, std::vector<ProjectionPtr> zooms);

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] bool isSame(const IProjectionSet& other) const override;

    [[nodiscard]] quint8 zoomCount() const noexcept override { return static_cast<quint8>(zooms_.size()); }
    [[nodiscard]] const ProjectionPtr& zoom(quint8 index) const override { return zooms_.at(index); }

    void validateZoom(quint8& zoom) const noexcept override;
    [[nodiscard]] bool checkZoom(quint8 zoom) const noexcept override;

    [[nodiscard]] ProjectionPtr suitableProjection(const IProjection& other) const override;
    [[nodiscard]] quint8 suitableZoom(const IProjection& other) const override;
    [[nodiscard]] bool isProjectionFromThisSet(const IProjection& other) const override;

private:
    core::HashValue hash_;
    std::vector<ProjectionPtr> zooms_;
};

// u_ProjectionSetFactorySimple.pas:67-174. Builds the four interned 24-level pyramids and any
// ad-hoc ones a non-standard ZMP needs.
class ProjectionSetFactory {
public:
    ProjectionSetFactory(HashSeed hash, std::shared_ptr<const DatumFactory> datums);

    [[nodiscard]] ProjectionSetPtr byConfig(const core::IConfigProvider* config) const;
    // asserts tileSplitCode == kTileSplitQuadrate256x256
    [[nodiscard]] ProjectionSetPtr byCode(int projectionEpsg, int tileSplitCode) const;

private:
    [[nodiscard]] ProjectionSetPtr createProjectionSet(const ProjectionTypePtr& type) const;

    HashSeed hash_;
    ProjectionTypeFactory types_;
    ProjectionSetPtr google_;
    ProjectionSetPtr esri53004_;
    ProjectionSetPtr yandex_;
    ProjectionSetPtr geographic_;
};

// u_ProjectionSetListStaticSimple.pas:45-58 -- three entries, legacy order (Q-4 ruling
// 2026-09-18: ESRI 53004 is reachable by code/config but was never in the selectable list).
// Captions are tr()-wrapped at the `ui` call site (geo has no tr(), core.md keeps user strings in
// ui); `caption` here is the untranslated legacy source string, gettext_NoOp's argument.
struct ProjectionSetListEntry {
    ProjectionSetPtr set;
    QString caption;
};
[[nodiscard]] std::vector<ProjectionSetListEntry>
standardProjectionSetList(const ProjectionSetFactory& factory);

} // namespace cg::geo
