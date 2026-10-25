// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// NEW (T-0239, T-0291, app-cli.md 1.6.1); no legacy counterpart: legacy reads one maps directory
// (u_ZmpInfoSet.pas:126-185). Ports: nothing.
//
// The ZMP sets of the --maps folders as one: a GUID defined in several folders takes the definition of the
// LAST folder that has it and stands at its first appearance; the sink is told once per such GUID.

#include "cg/maptype/zmp_info_set.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <vector>

namespace cg::app {

class MergedZmpInfoSet final : public maptype::IZmpInfoSet {
public:
    struct Folder {
        QString dir;                     // the absolute --maps path, as given (for messages)
        const maptype::IZmpInfoSet* set; // not owned; read only inside the constructor
    };
    // Called once per GUID defined in more than one folder, after the merge, in the order of guids():
    // winner = the definition in use (the last folder that has it), replaced = the others, in folder order.
    using ReplaceSink =
        std::function<void(const QUuid& guid, const QString& winnerPath, const QStringList& replacedPaths)>;
    MergedZmpInfoSet(const std::vector<Folder>& folders, const ReplaceSink& onReplaced);
    [[nodiscard]] maptype::IZmpInfoPtr zmpByGuid(const QUuid& guid) const override; // nullptr if absent
    [[nodiscard]] const std::vector<QUuid>& guids() const override { return guids_; }

private:
    QHash<QUuid, maptype::IZmpInfoPtr> byGuid_;
    std::vector<QUuid> guids_;
};

} // namespace cg::app
