// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_AppearanceHelper.pas / i_AppearanceHelper.pas (T-0142, spec vector.md §2.14). Expected values are
// literals taken from u_AppearanceHelper.pas and Bitmap32/c_Color32.pas, not read from the port.
#include "cg/vector/appearance_helper.h"
#include "cg/vector/appearance_of_mark_line.h"
#include "cg/vector/appearance_of_mark_point.h"
#include "cg/vector/appearance_of_mark_polygon.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <utility>

#include "fake_mark_picture.h"

using namespace cg::vector;
using cg::vector::test::FakeMarkPicture;
using cg::vector::test::FakeMarkPictureList;

namespace {

// ---- fakes: params with independent flags (the real Import*Params tie two flags together, L-130) ----
struct CaptionFacet final : IAppearancePointCaption {
    cg::core::HashValue hashCaption() const noexcept override { return 0; }
    Color32 textColor() const override { return 0xFF010101U; }
    Color32 textBgColor() const override { return 0xFF020202U; }
    int fontSize() const override { return 11; }
};
struct IconFacet final : IAppearancePointIcon {
    MarkPicturePtr paramPic = std::make_shared<FakeMarkPicture>(QStringLiteral("paramPic"), 1);
    cg::core::HashValue hashIcon() const noexcept override { return 0; }
    int markerSize() const override { return 21; }
    QString picName() const override { return QStringLiteral("paramPicName"); }
    MarkPicturePtr pic() const override { return paramPic; }
};
struct LineFacet final : IAppearanceLine {
    Color32 lineColor() const override { return 0xFF030303U; }
    int lineWidth() const override { return 5; }
};
struct BorderFacet final : IAppearancePolygonBorder {
    cg::core::HashValue hashBorder() const noexcept override { return 0; }
    Color32 lineColor() const override { return 0xFF040404U; }
    int lineWidth() const override { return 6; }
};
struct FillFacet final : IAppearancePolygonFill {
    cg::core::HashValue hashFill() const noexcept override { return 0; }
    Color32 fillColor() const override { return 0xFF050505U; }
};

struct PointParams final : IImportPointParams {
    std::shared_ptr<IconFacet> icon = std::make_shared<IconFacet>();
    bool fTextColor = false, fBg = false, fFont = false, fMarker = false, fPic = false;
    AppearancePtr appearance() const override { return nullptr; }
    AppearancePointCaptionPtr captionAppearance() const override { return std::make_shared<CaptionFacet>(); }
    AppearancePointIconPtr iconAppearance() const override { return icon; }
    bool isForceTextColor() const override { return fTextColor; }
    bool isForceTextBgColor() const override { return fBg; }
    bool isForceFontSize() const override { return fFont; }
    bool isForceMarkerSize() const override { return fMarker; }
    bool isForcePicName() const override { return fPic; }
};
struct LineParams final : IImportLineParams {
    bool fColor = false, fWidth = false;
    AppearancePtr appearance() const override { return nullptr; }
    AppearanceLinePtr lineAppearance() const override { return std::make_shared<LineFacet>(); }
    bool isForceLineColor() const override { return fColor; }
    bool isForceLineWidth() const override { return fWidth; }
};
struct PolyParams final : IImportPolyParams {
    bool fColor = false, fWidth = false, fFill = false;
    AppearancePtr appearance() const override { return nullptr; }
    AppearancePolygonBorderPtr borderAppearance() const override { return std::make_shared<BorderFacet>(); }
    AppearancePolygonFillPtr fillAppearance() const override { return std::make_shared<FillFacet>(); }
    bool isForceLineColor() const override { return fColor; }
    bool isForceLineWidth() const override { return fWidth; }
    bool isForceFillColor() const override { return fFill; }
};

// Records the factory arguments; returns a sentinel so the result identity is checked too.
class RecordingFactory final : public IAppearanceOfMarkFactory {
public:
    mutable Color32 textColor = 0, textBg = 0, lineColor = 0, fillColor = 0;
    mutable int fontSize = 0, markerSize = 0, lineWidth = 0;
    mutable QString picName;
    mutable MarkPicturePtr pic;
    AppearancePtr sentinel = std::make_shared<AppearanceOfMarkLine>(99, 0xFF000099U, 99);

    AppearancePtr createPointAppearance(Color32 tc, Color32 bg, int fs, const QString& pn,
                                        const MarkPicturePtr& p, int ms) const override {
        textColor = tc;
        textBg = bg;
        fontSize = fs;
        picName = pn;
        pic = p;
        markerSize = ms;
        return sentinel;
    }
    AppearancePtr createLineAppearance(Color32 lc, int lw) const override {
        lineColor = lc;
        lineWidth = lw;
        return sentinel;
    }
    AppearancePtr createPolygonAppearance(Color32 lc, int lw, Color32 fc) const override {
        lineColor = lc;
        lineWidth = lw;
        fillColor = fc;
        return sentinel;
    }
};

// Helper-side values, all distinct from the params' and from each other.
constexpr Color32 kHText = 0xFF0C0C0CU, kHFill = 0xFF0B0B0BU, kHLine = 0xFF111111U;
constexpr int kHTextSize = 31, kHIconSize = 41, kHWidth = 51;

struct Rig {
    std::shared_ptr<PointParams> point = std::make_shared<PointParams>();
    std::shared_ptr<LineParams> line = std::make_shared<LineParams>();
    std::shared_ptr<PolyParams> poly = std::make_shared<PolyParams>();
    std::shared_ptr<RecordingFactory> factory = std::make_shared<RecordingFactory>();
    MarkPicturePtr helperPic = std::make_shared<FakeMarkPicture>(QStringLiteral("helperPic"), 2);
    AppearanceHelper helper{point, line, poly, nullptr, factory};

    Rig() {
        helper.textColor().setColor(kHText);
        helper.fillColor().setColor(kHFill);
        helper.lineColor().setColor(kHLine);
        helper.textSize().setValue(kHTextSize);
        helper.iconSize().setValue(kHIconSize);
        helper.lineWidth().setValue(kHWidth);
        helper.icon().setIcon(helperPic);
    }
};

struct CaseRow {
    bool force;
    bool found;
    bool helperWins; // literal from `not Force and Found`, u_AppearanceHelper.pas:407-451
};
constexpr std::array<CaseRow, 4> kCases{
    {{false, true, true}, {true, true, false}, {false, false, false}, {true, false, false}}};

// ---- redefine truth tables ----
TEST(AppearanceHelperRedefine, PointEveryPropertyForceAndFound) {
    for (int prop = 0; prop < 5; ++prop) {
        for (const CaseRow& c : kCases) {
            Rig r;
            // icon (0), text colour (1), fill -> bg (2), text size (3), icon size (4)
            switch (prop) {
            case 0:
                r.point->fPic = c.force;
                if (!c.found)
                    r.helper.icon().reset();
                break;
            case 1:
                r.point->fTextColor = c.force;
                if (!c.found)
                    r.helper.textColor().reset();
                break;
            case 2:
                r.point->fBg = c.force;
                if (!c.found)
                    r.helper.fillColor().reset();
                break;
            case 3:
                r.point->fFont = c.force;
                if (!c.found)
                    r.helper.textSize().reset();
                break;
            default:
                r.point->fMarker = c.force;
                if (!c.found)
                    r.helper.iconSize().reset();
                break;
            }
            const AppearancePtr res = r.helper.redefinePointAppearance();
            SCOPED_TRACE(testing::Message()
                         << "prop " << prop << " force " << c.force << " found " << c.found);
            EXPECT_EQ(res, r.factory->sentinel);
            const RecordingFactory& f = *r.factory;
            const bool h0 = prop != 0 || c.helperWins;
            EXPECT_EQ(f.picName, h0 ? QStringLiteral("helperPic") : QStringLiteral("paramPicName"));
            ASSERT_NE(f.pic, nullptr);
            EXPECT_EQ(f.pic->name(), h0 ? QStringLiteral("helperPic") : QStringLiteral("paramPic"));
            EXPECT_EQ(f.textColor, (prop != 1 || c.helperWins) ? kHText : 0xFF010101U);
            EXPECT_EQ(f.textBg, (prop != 2 || c.helperWins) ? kHFill : 0xFF020202U);
            EXPECT_EQ(f.fontSize, (prop != 3 || c.helperWins) ? kHTextSize : 11);
            EXPECT_EQ(f.markerSize, (prop != 4 || c.helperWins) ? kHIconSize : 21);
        }
    }
}

TEST(AppearanceHelperRedefine, PointIconFoundButNullGivesNullPicAndEmptyName) {
    Rig r;
    r.helper.icon().setIcon(nullptr);
    ASSERT_NE(r.helper.redefinePointAppearance(), nullptr);
    EXPECT_EQ(r.factory->pic, nullptr);
    EXPECT_EQ(r.factory->picName, QString());
}

TEST(AppearanceHelperRedefine, LineEveryPropertyForceAndFound) {
    for (int prop = 0; prop < 2; ++prop) {
        for (const CaseRow& c : kCases) {
            Rig r;
            if (prop == 0) {
                r.line->fColor = c.force;
                if (!c.found)
                    r.helper.lineColor().reset();
            } else {
                r.line->fWidth = c.force;
                if (!c.found)
                    r.helper.lineWidth().reset();
            }
            SCOPED_TRACE(testing::Message()
                         << "prop " << prop << " force " << c.force << " found " << c.found);
            EXPECT_EQ(r.helper.redefineLineAppearance(), r.factory->sentinel);
            EXPECT_EQ(r.factory->lineColor, (prop != 0 || c.helperWins) ? kHLine : 0xFF030303U);
            EXPECT_EQ(r.factory->lineWidth, (prop != 1 || c.helperWins) ? kHWidth : 5);
        }
    }
}

TEST(AppearanceHelperRedefine, PolygonEveryPropertyForceAndFound) {
    for (int prop = 0; prop < 3; ++prop) {
        for (const CaseRow& c : kCases) {
            Rig r;
            if (prop == 0) {
                r.poly->fColor = c.force;
                if (!c.found)
                    r.helper.lineColor().reset();
            } else if (prop == 1) {
                r.poly->fWidth = c.force;
                if (!c.found)
                    r.helper.lineWidth().reset();
            } else {
                r.poly->fFill = c.force;
                if (!c.found)
                    r.helper.fillColor().reset();
            }
            SCOPED_TRACE(testing::Message()
                         << "prop " << prop << " force " << c.force << " found " << c.found);
            EXPECT_EQ(r.helper.redefinePolygonAppearance(), r.factory->sentinel);
            EXPECT_EQ(r.factory->lineColor, (prop != 0 || c.helperWins) ? kHLine : 0xFF040404U);
            EXPECT_EQ(r.factory->lineWidth, (prop != 1 || c.helperWins) ? kHWidth : 6);
            EXPECT_EQ(r.factory->fillColor, (prop != 2 || c.helperWins) ? kHFill : 0xFF050505U);
        }
    }
}

TEST(AppearanceHelperRedefine, NullParamsGiveNull) {
    const auto factory = std::make_shared<RecordingFactory>();
    AppearanceHelper none(nullptr, nullptr, nullptr, nullptr, factory);
    EXPECT_EQ(none.redefinePointAppearance(), nullptr);
    EXPECT_EQ(none.redefineLineAppearance(), nullptr);
    EXPECT_EQ(none.redefinePolygonAppearance(), nullptr);

    // each variant depends on its own params only
    AppearanceHelper onlyLine(nullptr, std::make_shared<LineParams>(), nullptr, nullptr, factory);
    EXPECT_EQ(onlyLine.redefinePointAppearance(), nullptr);
    EXPECT_NE(onlyLine.redefineLineAppearance(), nullptr);
    EXPECT_EQ(onlyLine.redefinePolygonAppearance(), nullptr);
    AppearanceHelper onlyPoly(nullptr, nullptr, std::make_shared<PolyParams>(), nullptr, factory);
    EXPECT_EQ(onlyPoly.redefineLineAppearance(), nullptr);
    EXPECT_NE(onlyPoly.redefinePolygonAppearance(), nullptr);
    AppearanceHelper onlyPoint(std::make_shared<PointParams>(), nullptr, nullptr, nullptr, factory);
    EXPECT_NE(onlyPoint.redefinePointAppearance(), nullptr);
    EXPECT_EQ(onlyPoint.redefineLineAppearance(), nullptr);
}

// ---- colour names (u_AppearanceHelper.pas:145-237, values from c_Color32.pas) ----
TEST(AppearanceHelperColor, EveryGpxNameMapsToItsLegacyColor) {
    struct Row {
        const char* name;
        Color32 color;
    };
    // The odd mappings (yellow, magenta, darkyellow) are legacy (LV-6).
    const Row rows[] = {
        {"red", 0xFFFF0000U},         // clRed32 :146
        {"blue", 0xFF0000FFU},        // clBlue32 c_Color32.pas:45
        {"cyan", 0xFF00FFFFU},        // clAqua32
        {"black", 0xFF000000U},       // clBlack32
        {"green", 0xFF007F00U},       // clGreen32 :37
        {"white", 0xFFFFFFFFU},       // clWhite32
        {"yellow", 0xFFFFFFE0U},      // clLightYellow32
        {"darkred", 0xFF8B0000U},     // clDarkRed32 :77
        {"magenta", 0xFF8B008BU},     // clDarkMagenta32
        {"darkblue", 0xFF00008BU},    // clDarkBlue32 :66
        {"darkcyan", 0xFF008B8BU},    // clDarkCyan32 :67
        {"darkgray", 0xFFA9A9A9U},    // clDarkGray32 :69
        {"darkgreen", 0xFF006400U},   // clDarkGreen32 :70
        {"lightgray", 0xFFBFBFBFU},   // clLightGray32 :34
        {"darkyellow", 0xFFFFFF00U},  // clYellow32
        {"darkmagenta", 0xFF8B008BU}, // clDarkMagenta32
        {"transparent", 0x00000000U}, // literal $00000000 :236
    };
    Rig r;
    for (const Row& row : rows) {
        for (const QString& spelling :
             {QString::fromLatin1(row.name), QString::fromLatin1(row.name).toUpper(),
              QString::fromLatin1(row.name).left(1).toUpper() + QString::fromLatin1(row.name).mid(1)}) {
            SCOPED_TRACE(spelling.toStdString());
            r.helper.lineColor().reset();
            EXPECT_TRUE(r.helper.lineColor().setGpxColorName(spelling));
            EXPECT_TRUE(r.helper.lineColor().found());
            EXPECT_EQ(r.helper.lineColor().color(), row.color);
        }
    }
}

TEST(AppearanceHelperColor, UnknownAndNonAsciiNamesGiveBlackNotFound) {
    Rig r;
    // lengths 3 and 5 are the lengths of "red" and "black": a wrong name of the right length must miss too
    for (const QString& name :
         {QStringLiteral("orange"), QStringLiteral("reD1"), QStringLiteral("rex"), QStringLiteral("blacc"),
          QStringLiteral(""), QStringLiteral("КРАСНый"), QStringLiteral("РЕД")}) {
        SCOPED_TRACE(name.toStdString());
        r.helper.lineColor().setColor(0x12345678U);
        EXPECT_FALSE(r.helper.lineColor().setGpxColorName(name));
        EXPECT_FALSE(r.helper.lineColor().found());
        EXPECT_EQ(r.helper.lineColor().color(), 0xFF000000U);
    }
}

TEST(AppearanceHelperColor, FailedLookupAfterSuccessClearsTheEarlierColor) {
    Rig r;
    ASSERT_TRUE(r.helper.textColor().setGpxColorName(QStringLiteral("Red")));
    EXPECT_EQ(r.helper.textColor().color(), 0xFFFF0000U);
    EXPECT_FALSE(r.helper.textColor().setGpxColorName(QStringLiteral("nosuch")));
    EXPECT_FALSE(r.helper.textColor().found());
    EXPECT_EQ(r.helper.textColor().color(), 0xFF000000U);
}

TEST(AppearanceHelperColor, KmlValueSwapsRedAndBlueKeepsAlphaAndGreen) {
    Rig r;
    r.helper.fillColor().reset();
    r.helper.fillColor().setKmlColorValue(0x80112233U);
    EXPECT_EQ(r.helper.fillColor().color(), 0x80332211U);
    EXPECT_TRUE(r.helper.fillColor().found());
    r.helper.fillColor().setKmlColorValue(0xAABBCCDDU);
    EXPECT_EQ(r.helper.fillColor().color(), 0xAADDCCBBU);
}

// ---- set helpers ----
TEST(AppearanceHelperSetHelpers, ColorSetResetAndInitialState) {
    const auto factory = std::make_shared<RecordingFactory>();
    AppearanceHelper h(nullptr, nullptr, nullptr, nullptr, factory);
    EXPECT_FALSE(h.textColor().found());
    EXPECT_EQ(h.textColor().color(), 0U); // transparent, not black
    h.textColor().setColor(0x12345678U);
    EXPECT_TRUE(h.textColor().found());
    EXPECT_EQ(h.textColor().color(), 0x12345678U);
    h.textColor().reset();
    EXPECT_FALSE(h.textColor().found());
    EXPECT_EQ(h.textColor().color(), 0U);
}

TEST(AppearanceHelperSetHelpers, IntegerSetResetAndInitialState) {
    const auto factory = std::make_shared<RecordingFactory>();
    AppearanceHelper h(nullptr, nullptr, nullptr, nullptr, factory);
    EXPECT_FALSE(h.lineWidth().found());
    EXPECT_EQ(h.lineWidth().value(), 0);
    h.lineWidth().setValue(-7);
    EXPECT_TRUE(h.lineWidth().found());
    EXPECT_EQ(h.lineWidth().value(), -7);
    h.lineWidth().reset();
    EXPECT_FALSE(h.lineWidth().found());
    EXPECT_EQ(h.lineWidth().value(), 0);
}

TEST(AppearanceHelperSetHelpers, IconSetResetAndNullIcon) {
    const auto factory = std::make_shared<RecordingFactory>();
    AppearanceHelper h(nullptr, nullptr, nullptr, nullptr, factory);
    EXPECT_FALSE(h.icon().found());
    EXPECT_EQ(h.icon().icon(), nullptr);
    const MarkPicturePtr p = std::make_shared<FakeMarkPicture>(QStringLiteral("p"), 3);
    h.icon().setIcon(p);
    EXPECT_TRUE(h.icon().found());
    EXPECT_EQ(h.icon().icon(), p);
    h.icon().setIcon(nullptr); // found is set even for a null picture (:365-369)
    EXPECT_TRUE(h.icon().found());
    EXPECT_EQ(h.icon().icon(), nullptr);
    h.icon().setIcon(p);
    h.icon().reset();
    EXPECT_FALSE(h.icon().found());
    EXPECT_EQ(h.icon().icon(), nullptr);
}

TEST(AppearanceHelperSetHelpers, IconSetByNameHitMissKeepsPreviousAndNoList) {
    const auto factory = std::make_shared<RecordingFactory>();
    AppearanceHelper h(nullptr, nullptr, nullptr, std::make_shared<FakeMarkPictureList>(), factory);
    EXPECT_FALSE(h.icon().setByName(QStringLiteral("zzz"))); // miss on a fresh helper: not found
    EXPECT_FALSE(h.icon().found());
    EXPECT_EQ(h.icon().icon(), nullptr);
    EXPECT_TRUE(h.icon().setByName(QStringLiteral("a")));
    EXPECT_TRUE(h.icon().found());
    ASSERT_NE(h.icon().icon(), nullptr);
    EXPECT_EQ(h.icon().icon()->name(), QStringLiteral("a"));
    EXPECT_FALSE(h.icon().setByName(QStringLiteral("zzz"))); // miss after a hit: previous kept
    EXPECT_TRUE(h.icon().found());
    ASSERT_NE(h.icon().icon(), nullptr);
    EXPECT_EQ(h.icon().icon()->name(), QStringLiteral("a"));
    EXPECT_TRUE(h.icon().setByName(QStringLiteral("b")));
    EXPECT_EQ(h.icon().icon()->name(), QStringLiteral("b"));

    AppearanceHelper noList(nullptr, nullptr, nullptr, nullptr, factory);
    EXPECT_FALSE(noList.icon().setByName(QStringLiteral("a")));
    EXPECT_FALSE(noList.icon().found());
    const MarkPicturePtr p = std::make_shared<FakeMarkPicture>(QStringLiteral("p"), 3);
    noList.icon().setIcon(p);
    EXPECT_FALSE(noList.icon().setByName(QStringLiteral("a")));
    EXPECT_TRUE(noList.icon().found());
    EXPECT_EQ(noList.icon().icon(), p);
}

// ---- has...Appearance (u_AppearanceHelper.pas:525-548) and reset (:580-589) ----
TEST(AppearanceHelperHas, EachHelperAloneSwitchesExactlyItsPredicates) {
    struct Row {
        int helper; // 0 text colour, 1 line colour, 2 fill colour, 3 line width, 4 text size, 5 icon size, 6
                    // icon
        bool point, line, poly;
    };
    const Row rows[] = {{0, true, false, false}, {1, false, true, true},  {2, true, false, true},
                        {3, false, true, true},  {4, true, false, false}, {5, true, false, false},
                        {6, true, false, false}};
    for (const Row& row : rows) {
        SCOPED_TRACE(row.helper);
        const auto factory = std::make_shared<RecordingFactory>();
        AppearanceHelper h(nullptr, nullptr, nullptr, nullptr, factory);
        EXPECT_FALSE(h.hasPointAppearance());
        EXPECT_FALSE(h.hasLineAppearance());
        EXPECT_FALSE(h.hasPolygonAppearance());
        switch (row.helper) {
        case 0:
            h.textColor().setColor(1);
            break;
        case 1:
            h.lineColor().setColor(1);
            break;
        case 2:
            h.fillColor().setColor(1);
            break;
        case 3:
            h.lineWidth().setValue(1);
            break;
        case 4:
            h.textSize().setValue(1);
            break;
        case 5:
            h.iconSize().setValue(1);
            break;
        default:
            h.icon().setIcon(nullptr);
            break;
        }
        EXPECT_EQ(h.hasPointAppearance(), row.point);
        EXPECT_EQ(h.hasLineAppearance(), row.line);
        EXPECT_EQ(h.hasPolygonAppearance(), row.poly);
    }
}

TEST(AppearanceHelperHas, ResetClearsAllSevenHelpers) {
    Rig r;
    ASSERT_TRUE(r.helper.hasPointAppearance());
    ASSERT_TRUE(r.helper.hasLineAppearance());
    r.helper.reset();
    EXPECT_FALSE(r.helper.textColor().found());
    EXPECT_FALSE(r.helper.lineColor().found());
    EXPECT_FALSE(r.helper.fillColor().found());
    EXPECT_FALSE(r.helper.lineWidth().found());
    EXPECT_FALSE(r.helper.textSize().found());
    EXPECT_FALSE(r.helper.iconSize().found());
    EXPECT_FALSE(r.helper.icon().found());
    EXPECT_EQ(r.helper.textColor().color(), 0U);
    EXPECT_EQ(r.helper.lineWidth().value(), 0);
    EXPECT_EQ(r.helper.icon().icon(), nullptr);
    EXPECT_FALSE(r.helper.hasPointAppearance());
    EXPECT_FALSE(r.helper.hasLineAppearance());
    EXPECT_FALSE(r.helper.hasPolygonAppearance());
}

} // namespace
