// SPDX-License-Identifier: GPL-3.0-only
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "NullInstance.h"
#include "discord/DiscordArtwork.h"
#include "settings/INISettingsObject.h"

namespace {
SettingsObjectPtr globalSettings(const QString& path)
{
    auto settings = std::make_shared<INISettingsObject>(path);
    for (const auto* name : { "ShowGameTime", "RecordGameTime", "PreLaunchCommand", "WrapperCommand", "PostExitCommand", "ShowConsole",
                              "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput", "ConsoleMaxLines", "ConsoleOverflowStop" })
        settings->registerSetting(name, false);
    return settings;
}
}  // namespace

class DiscordArtworkTest : public QObject {
    Q_OBJECT
   private slots:
    void publicImages_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("modrinth") << QStringLiteral("https://cdn.modrinth.com/data/pack/icon.png") << true;
        QTest::newRow("curseforge") << QStringLiteral("https://media.forgecdn.net/avatars/123/456/pack.png") << true;
        QTest::newRow("curseforge-new-cdn") << QStringLiteral("https://mediafilez.forgecdn.net/avatars/123/456/pack.png") << true;
        QTest::newRow("atlauncher") << QStringLiteral("https://download.nodecdn.net/containers/atl/launcher/images/PackName") << true;
        QTest::newRow("technic") << QStringLiteral("https://cdn.technicpack.net/platform2/pack-icons/123.png") << true;
        QTest::newRow("disk") << QStringLiteral("file:///C:/Users/Player/icon.png") << false;
        QTest::newRow("windows-path") << QStringLiteral("C:\\Users\\Player\\icon.png") << false;
        QTest::newRow("data") << QStringLiteral("data:image/png;base64,AAAA") << false;
        QTest::newRow("private") << QStringLiteral("https://192.168.1.5/icon.png") << false;
        QTest::newRow("localhost") << QStringLiteral("https://localhost/icon.png") << false;
        QTest::newRow("unknown-host") << QStringLiteral("https://example.com/icon.png") << false;
        QTest::newRow("subdomain-spoof") << QStringLiteral("https://cdn.modrinth.com.example.com/data/icon.png") << false;
        QTest::newRow("insecure") << QStringLiteral("http://cdn.modrinth.com/data/icon.png") << false;
        QTest::newRow("credentials") << QStringLiteral("https://user:secret@cdn.modrinth.com/data/icon.png") << false;
        QTest::newRow("signed-query") << QStringLiteral("https://cdn.modrinth.com/data/icon.png?token=secret") << false;
        QTest::newRow("fragment") << QStringLiteral("https://cdn.modrinth.com/data/icon.png#secret") << false;
        QTest::newRow("port") << QStringLiteral("https://cdn.modrinth.com:8443/data/icon.png") << false;
        QTest::newRow("wrong-cdn-path") << QStringLiteral("https://download.nodecdn.net/containers/private/icon.png") << false;
        QTest::newRow("traversal") << QStringLiteral("https://cdn.modrinth.com/data/%2e%2e/private.png") << false;
        QTest::newRow("oversize") << (QStringLiteral("https://cdn.modrinth.com/data/") + QString(300, 'a') + ".png") << false;
        QTest::newRow("encoded-oversize") << (QStringLiteral("https://cdn.modrinth.com/data/") + QString(60, QChar(0x00E9)) + ".png")
                                          << false;
    }

    void publicImages()
    {
        QFETCH(QString, url);
        QFETCH(bool, allowed);
        QCOMPARE(!DiscordArtwork::publicUrl(url).isEmpty(), allowed);
    }

    void catalogArtwork()
    {
        QCOMPARE(DiscordArtwork::fromCatalog("modrinth", "{\"icon_url\":\"https://cdn.modrinth.com/data/pack/icon.png\"}"),
                 QString("https://cdn.modrinth.com/data/pack/icon.png"));
        QCOMPARE(
            DiscordArtwork::fromCatalog("flame", "{\"data\":{\"logo\":{\"url\":\"https://media.forgecdn.net/avatars/1/2/icon.png\"}}}"),
            QString("https://media.forgecdn.net/avatars/1/2/icon.png"));
        QVERIFY(DiscordArtwork::fromCatalog("modrinth", "{\"icon_url\":\"file:///private.png\"}").isEmpty());
        QVERIFY(DiscordArtwork::fromCatalog("modrinth", "invalid").isEmpty());
        QVERIFY(DiscordArtwork::fromCatalog("unknown", "{\"icon_url\":\"https://cdn.modrinth.com/data/pack/icon.png\"}").isEmpty());
        QVERIFY(DiscordArtwork::atLauncherUrl("../private").isEmpty());
        QVERIFY(DiscordArtwork::atLauncherUrl("pack?token=secret").isEmpty());
    }

    void artworkPersistenceAndPackChange()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto globals = globalSettings(directory.filePath("global.ini"));
        const auto path = directory.filePath("instance.cfg");
        const auto artwork = QString("https://cdn.modrinth.com/data/pack/icon.png");
        {
            auto settings = std::make_shared<INISettingsObject>(path);
            NullInstance instance(globals, settings, directory.path());
            instance.setManagedPack("modrinth", "pack", "A pack", "version", "1.0");
            QSignalSpy properties(&instance, &BaseInstance::propertiesChanged);
            instance.setDiscordArtworkUrl(artwork);
            instance.setDiscordArtworkUrl(artwork);
            QCOMPARE(properties.size(), 1);
            instance.setManagedPack("modrinth", "pack", "A pack", "version2", "2.0");
            QCOMPARE(instance.discordArtworkUrl(), artwork);
        }
        auto settings = std::make_shared<INISettingsObject>(path);
        NullInstance instance(globals, settings, directory.path());
        QCOMPARE(instance.discordArtworkUrl(), artwork);
        instance.setManagedPack("modrinth", "another", "Another pack", "version", "1.0");
        QVERIFY(instance.discordArtworkUrl().isEmpty());
        settings->set("ChromaDiscordArtworkUrl", "https://private.example/icon.png?secret=yes");
        QVERIFY(instance.discordArtworkUrl().isEmpty());
    }

    void runningSignalsWithoutRecordingPlaytime()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto globals = globalSettings(directory.filePath("global.ini"));
        auto instance = std::make_shared<NullInstance>(globals, std::make_shared<INISettingsObject>(directory.filePath("instance.cfg")),
                                                       directory.path());
        QSignalSpy running(instance.get(), &BaseInstance::minecraftRunningChanged);
        instance->setRunning(true);
        QVERIFY(!instance->isMinecraftRunning());
        instance->setMinecraftRunning(true);
        instance->setMinecraftRunning(true);
        QVERIFY(instance->isMinecraftRunning());
        QCOMPARE(running.size(), 1);
        QCOMPARE(instance->lastLaunch(), qint64(0));
        instance->setRunning(false);
        QVERIFY(!instance->isMinecraftRunning());
        QCOMPARE(running.size(), 2);
        instance->setMinecraftRunning(false);
        QCOMPARE(running.size(), 2);
    }

    void localArtworkNeedsNoNetwork()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto globals = globalSettings(directory.filePath("global.ini"));
        auto instance = std::make_shared<NullInstance>(globals, std::make_shared<INISettingsObject>(directory.filePath("instance.cfg")),
                                                       directory.path());
        DiscordArtworkResolver resolver;
        instance->setManagedPack("modrinth", "sample", "Sample", "version", "1.0");
        resolver.resolve(instance);  // An idle managed pack cannot make a request.
        QVERIFY(instance->discordArtworkUrl().isEmpty());
        instance->setMinecraftRunning(true);
        instance->setManagedPack("atlauncher", "SamplePack", "Sample", "version", "1.0");
        resolver.resolve(instance);  // ATLauncher has a deterministic public catalog image.
        QCOMPARE(instance->discordArtworkUrl(), DiscordArtwork::atLauncherUrl("SamplePack"));
        resolver.resolve(instance);  // Already known artwork needs no provider request.
        resolver.cancel();
    }
};

QTEST_GUILESS_MAIN(DiscordArtworkTest)
#include "DiscordArtwork_test.moc"
