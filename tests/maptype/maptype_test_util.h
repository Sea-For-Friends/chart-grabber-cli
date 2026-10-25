// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared by the T-0044 config-element tests: an in-memory INI file as a read/write provider tree.

#include "cg/bitmap/bitmap32_buffer_factory_simple.h"
#include "cg/bitmap/bitmap32_static_factory.h"
#include "cg/bitmap/bitmap_tile_save_load_factory.h"
#include "cg/bitmap/content_type_manager_bitmap.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/content_type.h"
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/content_converters.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info_set.h"
#include "cg/vector/appearance_of_mark_factory.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace cg::maptype::test {

// The converter matrix over the standard registry and the real bitmap codecs (built once).
inline std::shared_ptr<const ContentConverters> testContentConverters() {
    static const auto instance = std::make_shared<const ContentConverters>(
        core::ContentTypeRegistry::standard(),
        std::make_shared<const bitmap::ContentTypeManagerBitmap>(
            std::make_shared<const bitmap::BitmapTileSaveLoadFactory>(
                std::make_shared<const bitmap::Bitmap32StaticFactory>(
                    std::make_shared<bitmap::Bitmap32BufferFactorySimple>()))));
    return instance;
}

// The real bitmap codec factory (the tests make their input images with it).
inline std::shared_ptr<const bitmap::IBitmapTileSaveLoadFactory> testBitmapFactory() {
    return std::make_shared<const bitmap::BitmapTileSaveLoadFactory>(
        std::make_shared<const bitmap::Bitmap32StaticFactory>(
            std::make_shared<bitmap::Bitmap32BufferFactorySimple>()));
}

// A w x h bitmap of one colour ($AARRGGBB), or with `gradient` a deterministic opaque picture.
inline bitmap::Bitmap32StaticPtr testBitmap(int w, int h, core::Color32 colour, bool gradient = false) {
    std::vector<core::Color32> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), colour);
    if (gradient) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                px[static_cast<std::size_t>(y * w + x)] =
                    0xFF000000U | (static_cast<core::Color32>(x * 15) << 16) |
                    (static_cast<core::Color32>(y * 15) << 8) | static_cast<core::Color32>((x * y) & 0xFF);
            }
        }
    }
    return std::make_shared<const bitmap::Bitmap32StaticFactory>(
               std::make_shared<bitmap::Bitmap32BufferFactorySimple>())
        ->build(QPoint(w, h), px.data());
}

// The n-th fake GUID (distinct for n >= 1, never null).
inline QUuid guidN(int n) {
    return QUuid(static_cast<uint>(n), 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
}

// A map-type set of the given maps in order (a duplicate GUID keeps its first item).
inline IMapTypeSetPtr buildSet(const std::vector<IMapTypePtr>& maps) {
    auto b = MapTypeSetBuilderFactory().build(false);
    for (const auto& m : maps) {
        b->add(m);
    }
    return b->makeAndClear();
}

inline std::shared_ptr<core::ConfigProviderIni> iniFrom(const QByteArray& text) {
    auto ini = core::IniFile::fromBytes(text);
    return core::ConfigProviderIni::wrap(std::make_shared<core::IniFile>(std::move(ini.value())));
}

// tests/fixtures: the CMake build passes CG_FIXTURES_DIR; the tools/nocmake_build.sh fallback
// passes nothing, so the directory is found by walking up from the working directory.
inline QString fixtureDir() {
#ifdef CG_FIXTURES_DIR
    return QStringLiteral(CG_FIXTURES_DIR);
#else
    QDir dir = QDir::current();
    for (int level = 0; level < 8; ++level) {
        if (dir.exists(QStringLiteral("tests/fixtures"))) {
            return dir.filePath(QStringLiteral("tests/fixtures"));
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QStringLiteral("tests/fixtures");
#endif
}

// The default icon draws text, which needs a (headless) GUI application for fonts. One per process, shared by
// every test file through these inline functions. T-0101: it is owned by a gtest Environment that destroys it
// in TearDown, i.e. before main returns; a function-local `static QGuiApplication` is destroyed too late for
// LeakSanitizer, and Qt frees its process-global font cache only when the application object is destroyed.
inline std::unique_ptr<QGuiApplication>& guiAppSlot() {
    static std::unique_ptr<QGuiApplication> app;
    return app;
}

inline void ensureGuiApp() {
    static int argc = 1;
    static char arg0[] = "maptype_test";
    static char* argv[] = {arg0, nullptr};
    if (!guiAppSlot()) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        guiAppSlot() = std::make_unique<QGuiApplication>(argc, argv);
    }
}

class GuiAppEnvironment final : public ::testing::Environment {
public:
    void TearDown() override { guiAppSlot().reset(); }
};

// gtest takes ownership of the environment (hence release()).
inline ::testing::Environment* const kGuiAppEnvironment =
    ::testing::AddGlobalTestEnvironment(std::make_unique<GuiAppEnvironment>().release());

// The environment a ZMP set loads in: a language directory with the stock (empty) .mo files, a projection
// factory and a ZmpConfig. Shared by zmp_info_set_test.cpp and download_checker_stuped_test.cpp.
struct ZmpEnv {
    QTemporaryDir langDir;
    std::unique_ptr<core::LanguageManager> languages;
    geo::ProjectionSetFactory projections{geo::HashSeed{},
                                          std::make_shared<const geo::DatumFactory>(geo::HashSeed{})};
    ZmpConfig zmpConfig;
    vector::AppearanceOfMarkFactory appearanceFactory;

    ZmpEnv() {
        ensureGuiApp(); // the one stock 24.bmp that is really a PNG gets the default icon, which draws text
        for (const char* code : {"uk", "ru", "es", "tr", "fa", "fr"}) {
            QFile f(langDir.filePath(QString::fromLatin1(code) + QStringLiteral(".mo")));
            EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        }
        languages = std::make_unique<core::LanguageManager>(langDir.path());
    }

    // Loads `relativeNames` under `root`; the messages the set reported go to `errors`.
    std::unique_ptr<ZmpInfoSet> load(const QString& root, const QStringList& relativeNames,
                                     QStringList& errors) const {
        return std::make_unique<ZmpInfoSet>(
            zmpConfig, *languages, projections, core::ContentTypeRegistry::standard(), appearanceFactory,
            nullptr, root, relativeNames, [&errors](const QString& message) { errors.append(message); });
    }
    std::unique_ptr<ZmpInfoSet> loadAll(const QString& root, QStringList& errors) const {
        return load(root, zmpFileNames(root), errors);
    }
};

} // namespace cg::maptype::test

// T-0321: skip a test that reads the private map catalogue when it is absent (tests/corpus_skip.h).
#include "../corpus_skip.h"
#define CG_SKIP_IF_NO_CORPUS() CG_SKIP_IF_NO_CORPUS_IN(cg::maptype::test::fixtureDir())
