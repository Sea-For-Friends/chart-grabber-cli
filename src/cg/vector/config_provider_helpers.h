// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/ConfigData/u_ConfigProviderHelpers.pas:451-631 -- the appearance part (ReadAppearancePoint/
// Line/Polygon, WriteAppearancePoint/Line/Polygon). The colour and GUID helpers are in cg::core,
// ReadPolygon in geo. FEAT-GEO-037

#include "cg/core/config_writer.h"
#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/mark_picture.h"

namespace cg::vector {

// Each reader starts from the facets of `defaultAppearance` (built-in defaults for a missing facet or a
// null default), reads the keys when `config` is non-null, and always returns a new appearance.
// Point keys: TextColor, ShadowColor (colours), FontSize, IconSize, IconName.
[[nodiscard]] AppearancePtr readAppearancePoint(const core::IConfigProviderPtr& config,
                                                const MarkPictureListPtr& markPictureList,
                                                const IAppearanceOfMarkFactory& factory,
                                                const AppearancePtr& defaultAppearance);
// Writers write only the facets the appearance has: point TextColor, ShadowColor, FontSize, IconName,
// IconSize; line LineColor, LineWidth; polygon LineColor, LineWidth, FillColor. Colours as <id>Hex.
// A null `config` writes nothing (defensive; unreachable in legacy, whose callers dereference the writer
// first).
void writeAppearancePoint(const core::IConfigWriterPtr& config, const AppearancePtr& appearance);

[[nodiscard]] AppearancePtr readAppearanceLine(const core::IConfigProviderPtr& config,
                                               const IAppearanceOfMarkFactory& factory,
                                               const AppearancePtr& defaultAppearance);
void writeAppearanceLine(const core::IConfigWriterPtr& config, const AppearancePtr& appearance);

[[nodiscard]] AppearancePtr readAppearancePolygon(const core::IConfigProviderPtr& config,
                                                  const IAppearanceOfMarkFactory& factory,
                                                  const AppearancePtr& defaultAppearance);
void writeAppearancePolygon(const core::IConfigWriterPtr& config, const AppearancePtr& appearance);

} // namespace cg::vector
