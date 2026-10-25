// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/app/proj_converter_factory_adapter.h"

#include <QString>

#include <optional>
#include <stdexcept>
#include <utility>

namespace cg::app {

namespace {

// The wrapped converter. A conversion that yields std::nullopt throws std::runtime_error with the text of
// u_ProjConverterByProj4.pas:143-145 / :167-170 (EProjConverterByDllError); the ": <pj_strerrno>" tail of
// legacy is not available from geo::IProjConverter and is left out.
class WrappedProjConverter final : public script::IProjConverter {
public:
    explicit WrappedProjConverter(geo::ProjConverterPtr inner) : inner_(std::move(inner)) {}

    [[nodiscard]] script::DoublePoint lonLat2XY(script::DoublePoint projLonLat) const override {
        const std::optional<geo::DoublePoint> xy = inner_->lonLat2XY({projLonLat.x, projLonLat.y});
        if (!xy) {
            throw std::runtime_error(
                QString::asprintf("LonLat2XY(%.6f; %.6f) failed", projLonLat.x, projLonLat.y).toStdString());
        }
        return {xy->x(), xy->y()};
    }

    [[nodiscard]] script::DoublePoint xy2LonLat(script::DoublePoint projXY) const override {
        const std::optional<geo::LonLat> ll = inner_->xy2LonLat({projXY.x, projXY.y});
        if (!ll) {
            throw std::runtime_error(
                QString::asprintf("XY2LonLat(%.6f; %.6f) failed", projXY.x, projXY.y).toStdString());
        }
        return {ll->lon, ll->lat};
    }

private:
    geo::ProjConverterPtr inner_;
};

std::shared_ptr<const script::IProjConverter> wrap(geo::ProjConverterPtr converter) {
    if (!converter) {
        return nullptr;
    }
    return std::make_shared<const WrappedProjConverter>(std::move(converter));
}

} // namespace

ProjConverterFactoryAdapter::ProjConverterFactoryAdapter(
    std::shared_ptr<const geo::ProjConverterFactory> factory)
    : factory_(std::move(factory)) {}

std::shared_ptr<const script::IProjConverter> ProjConverterFactoryAdapter::byEpsg(int epsg) const {
    return wrap(factory_->byEpsg(epsg));
}

std::shared_ptr<const script::IProjConverter>
ProjConverterFactoryAdapter::byInitString(const QByteArray& args) const {
    return wrap(factory_->byInitString(args));
}

} // namespace cg::app
