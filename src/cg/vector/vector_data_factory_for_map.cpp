// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/vector_data_factory_for_map.h"

#include "cg/core/hash.h"

#include <QtGlobal>

#include <stdexcept>
#include <utility>

namespace cg::vector {

namespace {

// TVectorDataItemMainInfoOfMap (u_VectorDataFactoryForMap.pas:67-182). Own fields and methods;
// the legacy class does not derive from the simple main info.
class VectorDataItemMainInfoOfMap final : public IVectorDataItemMainInfo {
public:
    VectorDataItemMainInfoOfMap(core::HashValue hash, HtmlToHintTextConverterPtr hintConverter,
                                StringProviderPtr urlPrefix, qint32 index, QString name, QString desc)
        : hash_(hash), hintConverter_(std::move(hintConverter)), urlPrefix_(std::move(urlPrefix)),
          index_(index), name_(std::move(name)), desc_(std::move(desc)) {}

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; } // :119-122
    [[nodiscard]] QString name() const override { return name_; }                  // :152-155
    [[nodiscard]] QString desc() const override { return desc_; }                  // :114-117

    // :157-182
    [[nodiscard]] bool isEqual(const IVectorDataItemMainInfo* item) const override {
        if (item == nullptr) {
            return false;
        }
        if (item == this) { // :165 identity rung
            return true;
        }
        if (item->hash() != 0 && hash_ != 0 && item->hash() != hash_) {
            return false;
        }
        if (name_ != item->name()) {
            return false;
        }
        if (desc_ != item->desc()) {
            return false;
        }
        return true;
    }

    // :124-130: the converter is asked for the name alone first, then for name and description.
    [[nodiscard]] QString hintText() const override {
        QString result = hintConverter_->convert(name_, QString());
        if (result.isEmpty()) {
            result = hintConverter_->convert(name_, desc_);
        }
        return result;
    }

    // :147-150: read at call time, so a changed prefix shows up at once.
    [[nodiscard]] QString infoUrl() const override {
        return urlPrefix_->value() + QString::number(index_) + QStringLiteral("/");
    }

    // :137-145: empty for an empty description, else the description wrapped in HTML tags.
    [[nodiscard]] QString infoHtml() const override {
        if (desc_.isEmpty()) {
            return QString();
        }
        return QStringLiteral("<HTML><BODY>") + desc_ + QStringLiteral("</BODY></HTML>");
    }

    [[nodiscard]] QString infoCaption() const override { return name_; } // :132-135

private:
    core::HashValue hash_;
    HtmlToHintTextConverterPtr hintConverter_;
    StringProviderPtr urlPrefix_;
    qint32 index_;
    QString name_;
    QString desc_;
};

} // namespace

// Legacy Create (:186-196): Assert(Assigned(...)) is a no-op in the shipped build; the port asserts.
VectorDataItemMainInfoFactoryForMap::VectorDataItemMainInfoFactoryForMap(
    HtmlToHintTextConverterPtr hintConverter)
    : hintConverter_(std::move(hintConverter)) {
    Q_ASSERT(hintConverter_ != nullptr);
}

VectorDataItemMainInfoPtr VectorDataItemMainInfoFactoryForMap::buildMainInfo(IdData* idData,
                                                                             const QString& name,
                                                                             const QString& desc) const {
    // Legacy BuildMainInfo (:206-208): Assert(AIdData <> nil) is a no-op in the shipped build
    // (SASPlanet.dproj:92), then Result := nil. Trace: the map factory is created only at
    // u_MapType.pas:433; its two legacy loader calls set IdData first (u_MapType.pas:542,
    // u_VectorTileProviderByInfoTileProvider.pas:144). Full trace in the handoff.
    if (idData == nullptr) {
        return nullptr;
    }
    // Precondition, unreachable from legacy: both callers set urlPrefix before the loader runs
    // (u_MapType.pas:538, u_VectorTileProviderByInfoTileProvider.pas:142). Without it legacy fails
    // later, in GetInfoUrl; the port refuses at once instead of dereferencing null.
    if (!idData->urlPrefix) {
        throw std::invalid_argument(
            "VectorDataItemMainInfoFactoryForMap::buildMainInfo: IdData without urlPrefix");
    }
    // Legacy: VIndex := InterlockedIncrement(NextIndex) - 1, i.e. the value before the increment.
    const qint32 index = idData->nextIndex.fetch_add(1);
    // Legacy (:209-211): VHash := CalcHashByString(AName); UpdateHashByString(VHash, ADesc).
    core::HashValue hash = core::hashOf(QStringView{name});
    core::hashUpdate(hash, QStringView{desc});
    return std::make_shared<VectorDataItemMainInfoOfMap>(hash, hintConverter_, idData->urlPrefix, index, name,
                                                         desc);
}

} // namespace cg::vector
