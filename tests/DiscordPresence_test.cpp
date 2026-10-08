// SPDX-License-Identifier: GPL-3.0-only
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "NullInstance.h"
#include "discord/DiscordPresence.h"
#include "settings/ChromaSettingsObject.h"
#include "settings/INISettingsObject.h"

class DiscordPresenceTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_directory;
    SettingsObjectPtr settings(const QString& suffix)
    {
        auto result = std::make_shared<ChromaSettingsObject>(m_directory.filePath(suffix + ".cfg"));
        result->registerSetting("ChromaDiscordPresence", false);
        result->registerSetting("ShowGameTime", true);
        result->registerSetting("RecordGameTime", false);
        for (const auto* key : { "PreLaunchCommand", "WrapperCommand", "PostExitCommand" })
            result->registerSetting(key, "");
        for (const auto* key : { "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput", "ConsoleOverflowStop" })
            result->registerSetting(key, false);
        result->registerSetting("ConsoleMaxLines", 1000);
        return result;
    }
    InstancePtr instance(const SettingsObjectPtr& global, const QString& id)
    {
        auto local = std::make_shared<INISettingsObject>(m_directory.filePath(id + ".cfg"));
        auto result = std::make_shared<NullInstance>(global, local, m_directory.filePath(id));
        result->setName(id);
        return result;
    }

   private slots:
    void browsingUntilGameActuallyStartsAndAfterExit()
    {
        auto global = settings("lifecycle");
        DiscordPresence presence(global, "");  // No real Discord connection, even when enabled.
        auto pack = instance(global, "Cobblemon");
        presence.observeInstance(pack);
        QCOMPARE(presence.currentActivity().value("details").toString(), "Browsing for modpacks");
        pack->setRunning(true);  // Auth, downloads and launch preparation are not playing.
        QCOMPARE(presence.currentActivity().value("details").toString(), "Browsing for modpacks");
        pack->setMinecraftRunning(true);
        QCOMPARE(presence.currentActivity().value("type").toInt(), 0);
        QCOMPARE(presence.currentActivity().value("details").toString(), "Cobblemon");
        QVERIFY(presence.currentActivity().value("timestamps").toObject().value("start").toInteger() > 0);
        const auto started = presence.currentActivity().value("timestamps");
        pack->setMinecraftRunning(true);
        QCOMPARE(presence.currentActivity().value("timestamps"), started);
        pack->setName("Cobblemon Together");
        QCOMPARE(presence.currentActivity().value("details").toString(), "Cobblemon Together");
        QCOMPARE(presence.currentActivity().value("timestamps"), started);
        QCOMPARE(pack->lastLaunch(), 0);  // Works with game-time recording disabled.
        pack->setRunning(false);          // Launch task failure/stop also clears process presence.
        QCOMPARE(presence.currentActivity().value("details").toString(), "Browsing for modpacks");
        QVERIFY(!presence.currentActivity().contains("timestamps"));
    }

    void latestRunningPackWinsAndEarlierPackResumes()
    {
        auto global = settings("multiple");
        DiscordPresence presence(global, "");
        auto first = instance(global, "First");
        auto second = instance(global, "Second");
        presence.observeInstance(first);
        presence.observeInstance(first);  // Repeated InstanceList updates do not duplicate subscriptions.
        presence.observeInstance(second);
        first->setMinecraftRunning(true);
        const auto firstStart = presence.currentActivity().value("timestamps");
        second->setMinecraftRunning(true);
        QCOMPARE(presence.currentActivity().value("details").toString(), "Second");
        second->setMinecraftRunning(false);
        QCOMPARE(presence.currentActivity().value("details").toString(), "First");
        QCOMPARE(presence.currentActivity().value("timestamps"), firstStart);
        first.reset();
        QCOMPARE(presence.currentActivity().value("details").toString(), "Browsing for modpacks");
    }

    void publicArtworkUpdatesWhilePlayingAndLocalPathsStayPrivate()
    {
        auto global = settings("artwork");
        DiscordPresence presence(global, "");
        auto pack = instance(global, "Picture Pack");
        pack->setDiscordArtworkUrl("file:///C:/Users/person/private.png");
        presence.observeInstance(pack);
        pack->setMinecraftRunning(true);
        QVERIFY(presence.currentActivity()
                    .value("assets")
                    .toObject()
                    .value("large_image")
                    .toString()
                    .startsWith("https://raw.githubusercontent.com/"));
        const QString picture = "https://cdn.modrinth.com/data/abcd/icon.png";
        pack->setDiscordArtworkUrl(picture);
        QCOMPARE(presence.currentActivity().value("assets").toObject().value("large_image").toString(), picture);
        QCOMPARE(presence.currentActivity().value("assets").toObject().value("small_text").toString(), "Chroma");
    }

    void sharingPreferenceIsIsolatedPersistentAndShutdownIsFinal()
    {
        auto global = settings("preferences");
        global->set("ChromaDiscordPresence", false);
        DiscordPresence presence(global, "");
        QVERIFY(presence.statusText().contains("off"));
        global->set("ChromaDiscordPresence", true);
        QVERIFY(presence.statusText().contains("not configured"));
        auto reloaded = settings("preferences");
        QVERIFY(reloaded->get("ChromaDiscordPresence").toBool());
        INIFile shared;
        shared.loadFile(m_directory.filePath("preferences.cfg"));
        QVERIFY(!shared.contains("ChromaDiscordPresence"));
        auto pack = instance(global, "Shutdown");
        presence.observeInstance(pack);
        presence.shutdown();
        QSignalSpy changed(&presence, &DiscordPresence::activityChanged);
        pack->setMinecraftRunning(true);
        global->set("ChromaDiscordPresence", false);
        global->set("ChromaDiscordPresence", true);
        QCOMPARE(changed.count(), 0);
        QVERIFY(presence.statusText().contains("off"));
    }

    void longUnicodeTitlesStayValid()
    {
        auto global = settings("unicode");
        DiscordPresence presence(global, "");
        auto pack = instance(global, "Long");
        pack->setName(QString::fromUtf8("🌸").repeated(100));
        presence.observeInstance(pack);
        pack->setMinecraftRunning(true);
        const auto title = presence.currentActivity().value("details").toString();
        QCOMPARE(title.toUtf8().size(), 128);
        QCOMPARE(title, QString::fromUtf8("🌸").repeated(32));
    }
};

QTEST_GUILESS_MAIN(DiscordPresenceTest)
#include "DiscordPresence_test.moc"
