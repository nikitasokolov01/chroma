// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "ui/themes/AccentColor.h"

namespace {

double contrastRatio(const QColor& first, const QColor& second)
{
    const double firstLight = AccentColor::luminance(first);
    const double secondLight = AccentColor::luminance(second);
    return (qMax(firstLight, secondLight) + 0.05) / (qMin(firstLight, secondLight) + 0.05);
}

}  // namespace

class AccentColorTest : public QObject {
    Q_OBJECT

   private slots:
    void acceptsOpaqueRgb_data()
    {
        QTest::addColumn<QString>("setting");
        QTest::addColumn<QColor>("expected");

        QTest::newRow("default violet") << "#7c3aed" << QColor("#7c3aed");
        QTest::newRow("saved lavender") << "#b7a5f5" << QColor("#b7a5f5");
        QTest::newRow("uppercase") << "#B7A5F5" << QColor("#b7a5f5");
        QTest::newRow("surrounding whitespace") << "  #B7A5F5\t" << QColor("#b7a5f5");
        QTest::newRow("black") << "#000000" << QColor(Qt::black);
        QTest::newRow("white") << "#ffffff" << QColor(Qt::white);
        QTest::newRow("custom rgb") << "#14293e" << QColor(20, 41, 62);
    }

    void acceptsOpaqueRgb()
    {
        QFETCH(QString, setting);
        QFETCH(QColor, expected);

        const QColor color = AccentColor::fromSetting(setting);
        QCOMPARE(color, expected);
        QCOMPARE(color.alpha(), 255);
    }

    void invalidSettingsUseViolet_data()
    {
        QTest::addColumn<QString>("setting");

        QTest::newRow("empty") << "";
        QTest::newRow("whitespace") << " \t";
        QTest::newRow("unrecognized value") << "invalid";
        QTest::newRow("named color") << "red";
        QTest::newRow("transparent") << "transparent";
        QTest::newRow("short hex") << "#abc";
        QTest::newRow("missing hash") << "b7a5f5";
        QTest::newRow("missing channel") << "#1234";
        QTest::newRow("extra channel") << "#1234567";
        QTest::newRow("invalid hex digit") << "#12X456";
        QTest::newRow("transparent alpha") << "#00123456";
        QTest::newRow("translucent alpha") << "#80123456";
        QTest::newRow("opaque alpha") << "#ff123456";
    }

    void invalidSettingsUseViolet()
    {
        QFETCH(QString, setting);
        QCOMPARE(AccentColor::fromSetting(setting), QColor("#7c3aed"));
    }

    void luminanceUsesLinearSrgb()
    {
        QCOMPARE(AccentColor::luminance(QColor(Qt::black)), 0.0);
        QCOMPARE(AccentColor::luminance(QColor(Qt::white)), 1.0);
        QCOMPARE(AccentColor::luminance(QColor(Qt::red)), 0.2126);
        QCOMPARE(AccentColor::luminance(QColor(Qt::green)), 0.7152);
        QCOMPARE(AccentColor::luminance(QColor(Qt::blue)), 0.0722);
        // Mid-gray is not 50% luminance: sRGB channels need gamma correction.
        QVERIFY(qAbs(AccentColor::luminance(QColor("#808080")) - 0.2158605) < 0.000001);
    }

    void foregroundFollowsContrast()
    {
        QCOMPARE(AccentColor::foreground(QColor("#7c3aed")), QColor(Qt::white));
        QCOMPARE(AccentColor::foreground(QColor("#b7a5f5")), QColor(Qt::black));
        QCOMPARE(AccentColor::foreground(QColor("#161124")), QColor(Qt::white));
        QCOMPARE(AccentColor::foreground(QColor(Qt::black)), QColor(Qt::white));
        QCOMPARE(AccentColor::foreground(QColor(Qt::white)), QColor(Qt::black));
        // Neighboring gray values around the contrast crossover catch a
        // simple brightness threshold that can leave mid-tones unreadable.
        QCOMPARE(AccentColor::foreground(QColor("#757575")), QColor(Qt::white));
        QCOMPARE(AccentColor::foreground(QColor("#767676")), QColor(Qt::black));
    }

    void readableLinksPreserveTheAccent()
    {
        const QColor dark("#202329");
        const QColor light("#F4F1FA");
        const QColor lavender("#b7a5f5");
        const QColor violet("#7c3aed");
        QCOMPARE(AccentColor::link(lavender, dark), lavender);
        QCOMPARE(AccentColor::link(QColor(Qt::white), dark), QColor(Qt::white));
        QVERIFY(AccentColor::link(QColor(Qt::black), dark) != QColor(Qt::black));
        QCOMPARE(AccentColor::link(violet, light), violet);
        QCOMPARE(AccentColor::link(QColor(Qt::black), light), QColor(Qt::black));
        QVERIFY(AccentColor::link(QColor(Qt::white), light) != QColor(Qt::white));
        QVERIFY(AccentColor::luminance(AccentColor::link(lavender, light)) < AccentColor::luminance(lavender));
    }

    void foregroundAndLinksMeetContrastAcrossRgb_data()
    {
        QTest::addColumn<QColor>("window");
        QTest::addColumn<QColor>("content");

        QTest::newRow("dark surfaces") << QColor("#202329") << QColor("#17191d");
        QTest::newRow("clay surfaces") << QColor("#F4F1FA") << QColor("#FAF8FF");
        QTest::newRow("white surface") << QColor(Qt::white) << QColor(Qt::white);
        QTest::newRow("black surface") << QColor(Qt::black) << QColor(Qt::black);
        // Mid-tone backgrounds exercise the endpoint choice near the
        // black/white contrast crossover, not only at theme extremes.
        QTest::newRow("mid gray") << QColor("#757575") << QColor("#757575");
        QTest::newRow("lighter mid gray") << QColor("#767676") << QColor("#767676");
    }

    void foregroundAndLinksMeetContrastAcrossRgb()
    {
        QFETCH(QColor, window);
        QFETCH(QColor, content);
        int colorsChecked = 0;
        for (int red = 0; red <= 255; red += 17) {
            for (int green = 0; green <= 255; green += 17) {
                for (int blue = 0; blue <= 255; blue += 17) {
                    const QColor accent(red, green, blue);
                    const QColor foreground = AccentColor::foreground(accent);
                    const QColor link = AccentColor::link(accent, window);
                    const QByteArray description = accent.name().toUtf8();

                    QVERIFY2(contrastRatio(accent, foreground) >= 4.5, description.constData());
                    QVERIFY2(contrastRatio(window, link) >= 4.5, description.constData());
                    QVERIFY2(contrastRatio(content, link) >= 4.5, description.constData());
                    QCOMPARE(link.alpha(), 255);
                    ++colorsChecked;
                }
            }
        }
        QCOMPARE(colorsChecked, 4096);
    }
};

QTEST_GUILESS_MAIN(AccentColorTest)

#include "AccentColor_test.moc"
