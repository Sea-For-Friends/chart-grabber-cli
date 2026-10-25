// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ExportMarks2KMLConfig.pas (TKmlSortingType, TKmlIconScaleType, IExportMarks2KMLConfigStatic,
// IExportMarks2KMLConfig), u_ExportMarks2KMLConfig.pas (TExportMarks2KMLConfigStatic,
// TExportMarks2KMLConfig).
//
// The settings of the KML marks exporter (spec vector.md section 2.12, V10). The element persists six keys
// in the parent's section and publishes an immutable snapshot through core::Snapshot, rebuilt before every
// change notification (T-0147 idiom).

#include "cg/core/snapshot.h"
#include "cg/vector/export_config.h"

#include <QString>

#include <cstdint>
#include <memory>

namespace cg::vector {

// TKmlSortingType (i_ExportMarks2KMLConfig.pas:30).
enum class KmlSortingType : std::uint8_t { None = 0, ByNameAsc = 1, ByNameDesc = 2 };

// TKmlIconScaleType (i_ExportMarks2KMLConfig.pas:32).
enum class KmlIconScaleType : std::uint8_t { Abs = 0, Small = 1, Medium = 2, Large = 3 };

// Ports IExportMarks2KMLConfigStatic (i_ExportMarks2KMLConfig.pas:34-53).
class IExportMarks2KmlConfigStatic {
public:
    virtual ~IExportMarks2KmlConfigStatic() = default;

    [[nodiscard]] virtual bool useCoordFormatting() const = 0;
    [[nodiscard]] virtual int coordPrecision() const = 0;
    [[nodiscard]] virtual KmlSortingType sortingType() const = 0;
    [[nodiscard]] virtual KmlIconScaleType iconScaleType() const = 0;
    [[nodiscard]] virtual bool useAbsPathToIcon() const = 0;
    [[nodiscard]] virtual QString absPathToIcon() const = 0;
};

using IExportMarks2KmlConfigStaticPtr = std::shared_ptr<const IExportMarks2KmlConfigStatic>;

// Ports TExportMarks2KMLConfigStatic (u_ExportMarks2KMLConfig.pas:318-366). The constructor takes the six
// fields in legacy order (:320-326), which is also the order of the getters in this header.
class ExportMarks2KmlConfigStatic final : public IExportMarks2KmlConfigStatic {
public:
    ExportMarks2KmlConfigStatic(bool useCoordFormatting, int coordPrecision, KmlSortingType sortingType,
                                KmlIconScaleType iconScaleType, bool useAbsPathToIcon, QString absPathToIcon);
    ~ExportMarks2KmlConfigStatic() override;

    [[nodiscard]] bool useCoordFormatting() const override;
    [[nodiscard]] int coordPrecision() const override;
    [[nodiscard]] KmlSortingType sortingType() const override;
    [[nodiscard]] KmlIconScaleType iconScaleType() const override;
    [[nodiscard]] bool useAbsPathToIcon() const override;
    [[nodiscard]] QString absPathToIcon() const override;

private:
    bool useCoordFormatting_;
    int coordPrecision_;
    KmlSortingType sortingType_;
    KmlIconScaleType iconScaleType_;
    bool useAbsPathToIcon_;
    QString absPathToIcon_;
};

// Ports IExportMarks2KMLConfig (i_ExportMarks2KMLConfig.pas:55-82): six getter/setter pairs and GetStatic.
class IExportMarks2KmlConfig : public IExportConfig {
public:
    [[nodiscard]] virtual bool useCoordFormatting() const = 0;
    virtual void setUseCoordFormatting(bool value) = 0;

    [[nodiscard]] virtual int coordPrecision() const = 0;
    virtual void setCoordPrecision(int value) = 0;

    [[nodiscard]] virtual KmlSortingType sortingType() const = 0;
    virtual void setSortingType(KmlSortingType value) = 0;

    [[nodiscard]] virtual KmlIconScaleType iconScaleType() const = 0;
    virtual void setIconScaleType(KmlIconScaleType value) = 0;

    [[nodiscard]] virtual bool useAbsPathToIcon() const = 0;
    virtual void setUseAbsPathToIcon(bool value) = 0;

    [[nodiscard]] virtual QString absPathToIcon() const = 0;
    virtual void setAbsPathToIcon(const QString& value) = 0;

    [[nodiscard]] virtual IExportMarks2KmlConfigStaticPtr getStatic() const = 0;
};

// Ports TExportMarks2KMLConfig (u_ExportMarks2KMLConfig.pas:61-316): a TConfigDataElementWithStaticBase, so
// the interface derives from core::ConfigElement through IExportConfig.
class ExportMarks2KmlConfig final : public IExportMarks2KmlConfig {
public:
    ExportMarks2KmlConfig();
    ~ExportMarks2KmlConfig() override;

    [[nodiscard]] bool useCoordFormatting() const override;
    void setUseCoordFormatting(bool value) override;

    [[nodiscard]] int coordPrecision() const override;
    void setCoordPrecision(int value) override;

    [[nodiscard]] KmlSortingType sortingType() const override;
    void setSortingType(KmlSortingType value) override;

    [[nodiscard]] KmlIconScaleType iconScaleType() const override;
    void setIconScaleType(KmlIconScaleType value) override;

    [[nodiscard]] bool useAbsPathToIcon() const override;
    void setUseAbsPathToIcon(bool value) override;

    [[nodiscard]] QString absPathToIcon() const override;
    void setAbsPathToIcon(const QString& value) override;

    [[nodiscard]] IExportMarks2KmlConfigStaticPtr getStatic() const override;

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override; // rebuilds the snapshot

private:
    bool useCoordFormatting_ = false;
    int coordPrecision_ = 6;
    KmlSortingType sortingType_ = KmlSortingType::None;
    KmlIconScaleType iconScaleType_ = KmlIconScaleType::Abs;
    bool useAbsPathToIcon_ = false;
    QString absPathToIcon_;
    core::Snapshot<IExportMarks2KmlConfigStatic> snapshot_;
};

} // namespace cg::vector
