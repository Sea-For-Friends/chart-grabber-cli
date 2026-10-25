// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/i_RegionProcessProgressInfo.pas:30-68 (the four interfaces),
//        Src/RegionProcess/u_RegionProcessProgressInfo.pas:70-141 (RegionProcessProgressInfo)
//
// Legacy's (CancelNotifier, OperationID) pair is one std::stop_token here.

#include "cg/core/sync.h"

#include <QString>

#include <atomic>
#include <stop_token>

namespace cg::region {

class IProgressInfoBase { // :30-37
public:
    virtual ~IProgressInfoBase() = default;
    [[nodiscard]] virtual double processedRatio() const = 0;
    [[nodiscard]] virtual bool finished() const = 0;
};

class IProgressInfoInternalBase { // :39-49
public:
    virtual ~IProgressInfoInternalBase() = default;
    [[nodiscard]] virtual std::stop_token stopToken() const = 0;
    virtual void setProcessedRatio(double value) = 0;
    virtual void finish() = 0;
};

class IRegionProcessProgressInfo : public IProgressInfoBase { // :51-61
public:
    [[nodiscard]] virtual QString caption() const = 0;
    [[nodiscard]] virtual QString firstLine() const = 0;
    [[nodiscard]] virtual QString secondLine() const = 0;
};

class IRegionProcessProgressInfoInternal : public IProgressInfoInternalBase { // :63-68
public:
    virtual void setCaption(const QString& value) = 0;
    virtual void setFirstLine(const QString& value) = 0;
    virtual void setSecondLine(const QString& value) = 0;
};

// Written by the worker thread, polled by the UI timer. Legacy has no lock; the port uses atomics
// for the two scalars and one mutex over the three strings (mechanism, not behaviour).
class RegionProcessProgressInfo final : public IRegionProcessProgressInfo,
                                        public IRegionProcessProgressInfoInternal {
public:
    explicit RegionProcessProgressInfo(std::stop_token stop);

    [[nodiscard]] double processedRatio() const override;
    [[nodiscard]] bool finished() const override;
    [[nodiscard]] QString caption() const override;
    [[nodiscard]] QString firstLine() const override;
    [[nodiscard]] QString secondLine() const override;

    [[nodiscard]] std::stop_token stopToken() const override;
    void setProcessedRatio(double value) override;
    void finish() override;
    void setCaption(const QString& value) override;
    void setFirstLine(const QString& value) override;
    void setSecondLine(const QString& value) override;

private:
    std::stop_token stop_;
    std::atomic<double> processedRatio_{0.0};
    std::atomic<bool> finished_{false};
    mutable core::Mutex stringsMutex_;
    QString caption_;
    QString firstLine_;
    QString secondLine_;
};

} // namespace cg::region
