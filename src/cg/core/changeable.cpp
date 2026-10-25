// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/changeable.h"

namespace cg::core {

void Changeable::fireChanged() {
    doBeforeChangeNotify();
    before_.notify();
    doChangeNotify();
    changed_.notify();
    doAfterChangeNotify();
    after_.notify();
}

} // namespace cg::core
