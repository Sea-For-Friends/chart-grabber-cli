// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_impl_config_orm.h"

#include "cg/core/delphi_text.h"

#include <utility>

namespace cg::marks {

MarkSystemImplConfigORM::MarkSystemImplConfigORM(QString fileName, bool isReadOnly, const QString& userName,
                                                 const QString& passwordPlain, const QString& password,
                                                 quint32 cacheSizeMb, const QString& forcedSchemaName)
    : MarkSystemImplConfigBase(std::move(fileName), isReadOnly), userName_(userName),
      password_(passwordPlain.isEmpty() ? password : passwordPlain), cacheSizeMb_(cacheSizeMb),
      forcedSchemaName_(core::delphiTrim(forcedSchemaName)) {}

} // namespace cg::marks
