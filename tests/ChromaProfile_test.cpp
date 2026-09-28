// SPDX-License-Identifier: GPL-3.0-only
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>

#include "ChromaProfile.h"
#include "settings/ChromaSettingsObject.h"
#include "settings/INIFile.h"

namespace {
bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeSettings(const QString& path, const QVariantMap& values)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    INIFile ini;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        ini.set(it.key(), it.value());
    return ini.saveFile(path);
}

INIFile readSettings(const QString& path)
{
    INIFile ini;
    ini.loadFile(path);
    return ini;
}

QMap<QString, QByteArray> snapshot(const QString& root)
{
    QMap<QString, QByteArray> result;
    QDirIterator files(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto path = files.next();
        const auto relative = QDir(root).relativeFilePath(path);
        result.insert(relative, files.fileInfo().isDir() ? QByteArray("directory")
                                                         : QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256));
    }
    return result;
}
void registerSettings(ChromaSettingsObject& settings)
{
    settings.registerSetting("ApplicationTheme", "chroma");
    settings.registerSetting("AccentColor", "#b7a5f5");
    settings.registerSetting("MainWindowGeometry", "default-layout");
    settings.registerSetting("SelectedInstance", "");
    settings.registerSetting("InstanceDir", "instances");
    settings.registerSetting("IconsDir", "icons");
    settings.registerSetting("Language", "");
    settings.registerSetting("UseSystemLocale", false);
    settings.registerSetting("JavaPath", "java");
    settings.registerSetting({ "MaxMemAlloc", "MaxMemoryAlloc" }, 4096);
    settings.registerSetting("MSAClientIDOverride", "");
}
}  // namespace

class ChromaProfileTest : public QObject {
    Q_OBJECT

   private slots:
    void selectedProfileUsesExistingPathsWithoutCopies_data()
    {
        QTest::addColumn<bool>("absoluteLocations");
        QTest::newRow("relative custom locations") << false;
        QTest::newRow("absolute custom locations") << true;
    }

    void selectedProfileUsesExistingPathsWithoutCopies()
    {
        QFETCH(bool, absoluteLocations);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto sourceRoot = root.filePath("Prism profile");
        const auto stateRoot = root.filePath("Chroma state");
        const auto instancesRoot = absoluteLocations ? root.filePath("shared packs") : sourceRoot + "/custom-packs";
        const auto iconsRoot = absoluteLocations ? root.filePath("shared icons") : sourceRoot + "/custom-icons";
        QVERIFY(QDir().mkpath(sourceRoot));
        QVERIFY(QDir().mkpath(iconsRoot));
        QVERIFY(writeSettings(sourceRoot + "/prismlauncher.cfg",
                              { { "InstanceDir", absoluteLocations ? instancesRoot : QString("custom-packs") },
                                { "IconsDir", absoluteLocations ? iconsRoot : QString("custom-icons") },
                                { "MaxMemAlloc", 8192 } }));
        QVERIFY(writeSettings(instancesRoot + "/pack/instance.cfg", { { "InstanceType", "OneSix" }, { "name", "Existing pack" } }));
        QVERIFY(writeFile(instancesRoot + "/pack/.minecraft/mods/existing.jar", "existing mod bytes"));
        QVERIFY(writeFile(instancesRoot + "/pack/.minecraft/saves/world/level.dat", "existing world bytes"));
        QVERIFY(writeFile(instancesRoot + "/instgroups.json", "{\"formatVersion\":1,\"groups\":{}}"));
        QVERIFY(writeFile(sourceRoot + "/accounts.json", "{\"accounts\":[{\"synthetic\":true}]}"));
        const auto original = snapshot(sourceRoot);
        const auto originalInstances = snapshot(instancesRoot);
        const auto info = ChromaProfile::inspect(sourceRoot);
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        QVERIFY(ChromaProfile::sameProfilePath(info.root, sourceRoot));
        QVERIFY(ChromaProfile::sameProfilePath(info.instancesPath, instancesRoot));
        QVERIFY(ChromaProfile::sameProfilePath(info.iconsPath, iconsRoot));
        QCOMPARE(info.instanceCount, 1);

        QString error;
        QVERIFY(ChromaProfile::loadSelectedProfile(stateRoot, &error).isEmpty());
        QVERIFY(error.isEmpty());
        QVERIFY(!QFileInfo::exists(stateRoot));
        QVERIFY2(ChromaProfile::saveSelectedProfile(stateRoot, sourceRoot, &error), qPrintable(error));
        QVERIFY(ChromaProfile::sameProfilePath(ChromaProfile::loadSelectedProfile(stateRoot, &error), sourceRoot));
        QVERIFY(error.isEmpty());
        QVERIFY2(ChromaProfile::saveSelectedProfile(stateRoot, sourceRoot, &error), qPrintable(error));
        QCOMPARE(snapshot(sourceRoot), original);
        QCOMPARE(snapshot(instancesRoot), originalInstances);
        QVERIFY(!QFileInfo::exists(stateRoot + "/instances"));
        QVERIFY(!QFileInfo::exists(stateRoot + "/accounts.json"));
        QVERIFY(!QFileInfo::exists(stateRoot + "/prismlauncher.cfg"));
        QVERIFY(!ChromaProfile::saveSelectedProfile(instancesRoot + "/chroma-state", sourceRoot, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!ChromaProfile::saveSelectedProfile(iconsRoot + "/chroma-state", sourceRoot, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(snapshot(sourceRoot), original);
        QCOMPARE(snapshot(instancesRoot), originalInstances);
        QVERIFY(!QFileInfo::exists(iconsRoot + "/chroma-state"));
        QVERIFY2(ChromaProfile::saveSelectedProfile(stateRoot, QString(), &error), qPrintable(error));
        QVERIFY(ChromaProfile::loadSelectedProfile(stateRoot, &error).isEmpty());
        QVERIFY(error.isEmpty());
        QCOMPARE(snapshot(sourceRoot), original);
    }

    void profileValidationAndSelectionFailuresDoNotWriteSource()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto sourceRoot = root.filePath("Prism");
        QVERIFY(QDir().mkpath(sourceRoot));
        QVERIFY(!ChromaProfile::inspect(sourceRoot).error.isEmpty());
        QVERIFY(writeFile(sourceRoot + "/prismlauncher.cfg", "[broken section\nConfigVersion=1.3\n"));
        QVERIFY(!ChromaProfile::inspect(sourceRoot).error.isEmpty());
        QVERIFY(writeSettings(sourceRoot + "/prismlauncher.cfg", { { "InstanceDir", "instances" } }));
        const auto original = snapshot(sourceRoot);
        const auto info = ChromaProfile::inspect(sourceRoot);
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        QCOMPARE(info.instanceCount, 0);
        QVERIFY(!QFileInfo::exists(sourceRoot + "/instances"));
        QString error;
        QVERIFY(!ChromaProfile::saveSelectedProfile(sourceRoot, sourceRoot, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!ChromaProfile::saveSelectedProfile(sourceRoot + "/chroma-state", sourceRoot, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(snapshot(sourceRoot), original);
        QVERIFY(ChromaProfile::sameProfilePath(sourceRoot, sourceRoot + "/./"));
        QVERIFY(!ChromaProfile::sameProfilePath(sourceRoot, root.filePath("Prism-other")));
    }

    void malformedSelectionDoesNotSilentlyChooseAnotherProfile()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto sourceRoot = root.filePath("Prism");
        const auto stateRoot = root.filePath("Chroma state");
        QVERIFY(QDir().mkpath(sourceRoot));
        QVERIFY(writeSettings(sourceRoot + "/prismlauncher.cfg", { { "InstanceDir", "instances" } }));
        QString error;
        QVERIFY2(ChromaProfile::saveSelectedProfile(stateRoot, sourceRoot, &error), qPrintable(error));
        const auto stateFiles = QDir(stateRoot).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        QCOMPARE(stateFiles.size(), 1);
        QVERIFY(writeFile(stateFiles.first().absoluteFilePath(), "{not valid json"));
        QVERIFY(ChromaProfile::loadSelectedProfile(stateRoot, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY2(ChromaProfile::saveSelectedProfile(stateRoot, sourceRoot, &error), qPrintable(error));
        QVERIFY(QFile::remove(sourceRoot + "/prismlauncher.cfg"));
        QVERIFY(QDir().rmdir(sourceRoot));
        const auto unavailable = ChromaProfile::loadSelectedProfile(stateRoot, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(QDir::cleanPath(unavailable), QDir::cleanPath(sourceRoot));
        QVERIFY(!ChromaProfile::inspect(unavailable).error.isEmpty());
    }
    void existingSymlinksKeepTheirDirectPaths()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto sourceRoot = root.filePath("Prism");
        const auto sourceAlias = root.filePath("Prism alias");
        const auto externalPack = root.filePath("existing external pack");
        const auto linkedPack = sourceRoot + "/instances/linked-pack";
        QVERIFY(writeSettings(sourceRoot + "/prismlauncher.cfg", { { "InstanceDir", "instances" } }));
        QVERIFY(QDir().mkpath(sourceRoot + "/instances"));
        QVERIFY(writeSettings(externalPack + "/instance.cfg", { { "InstanceType", "OneSix" }, { "name", "Linked pack" } }));
        QVERIFY(writeFile(externalPack + "/.minecraft/saves/world/level.dat", "original world"));
        const auto nativePath = [](const QString& value) {
#ifdef Q_OS_WIN
            return std::filesystem::path(value.toStdWString());
#else
            return std::filesystem::path(value.toStdString());
#endif
        };
        const auto cleanup = qScopeGuard([&] {
            std::error_code ignored;
            std::filesystem::remove(nativePath(sourceAlias), ignored);
            std::filesystem::remove(nativePath(linkedPack), ignored);
        });
        std::error_code linkError;
        std::filesystem::create_directory_symlink(nativePath(sourceRoot), nativePath(sourceAlias), linkError);
        if (linkError)
            QSKIP("This machine does not permit creating directory symlinks.");
        std::filesystem::create_directory_symlink(nativePath(externalPack), nativePath(linkedPack), linkError);
        QVERIFY2(!linkError, linkError.message().c_str());
        const auto original = snapshot(sourceRoot);
        const auto originalPack = snapshot(externalPack);
        const auto info = ChromaProfile::inspect(sourceAlias);
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        QVERIFY(ChromaProfile::sameProfilePath(info.root, sourceRoot));
        QVERIFY(ChromaProfile::sameProfilePath(sourceAlias, sourceRoot));
        QCOMPARE(info.instanceCount, 1);
        QString error;
        QVERIFY(!ChromaProfile::saveSelectedProfile(sourceAlias + "/chroma-state", sourceRoot, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(snapshot(sourceRoot), original);
        QCOMPARE(snapshot(externalPack), originalPack);
        QVERIFY(QFileInfo(sourceAlias).isSymLink() || QFileInfo(sourceAlias).isJunction());
        QVERIFY(QFileInfo(linkedPack).isSymLink() || QFileInfo(linkedPack).isJunction());
    }
    void prismAppearanceDoesNotOverrideChroma()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath("prismlauncher.cfg");
        const QByteArray original =
            "# Existing Prism appearance must stay byte-for-byte intact\n[General]\nConfigVersion=1.3\n"
            "ApplicationTheme=light\nAccentColor=#00ff00\nMainWindowGeometry=prism-layout\n"
            "SelectedInstance=existing-pack\nMaxMemAlloc=8192\n";
        QVERIFY(writeFile(path, original));
        ChromaSettingsObject settings(path);
        registerSettings(settings);
        QCOMPARE(settings.get("ApplicationTheme").toString(), QString("chroma"));
        QCOMPARE(settings.get("AccentColor").toString(), QString("#b7a5f5"));
        QCOMPARE(settings.get("MainWindowGeometry").toString(), QString("default-layout"));
        QCOMPARE(settings.get("SelectedInstance").toString(), QString());
        QCOMPARE(settings.get("MaxMemAlloc").toInt(), 8192);
        QVERIFY(settings.reload());
        QCOMPARE(readFile(path), original);
        QVERIFY(!QFileInfo::exists(settings.uiFilePath()));

        QVERIFY(settings.set("ApplicationTheme", "dark"));
        QVERIFY(settings.set("AccentColor", "#ff8844"));
        QVERIFY(settings.set("MainWindowGeometry", "chroma-layout"));
        QVERIFY(settings.set("SelectedInstance", "another-pack"));
        QCOMPARE(readFile(path), original);
        QCOMPARE(QFileInfo(settings.uiFilePath()).fileName(), QString("chroma-ui.cfg"));

        ChromaSettingsObject reopened(path);
        registerSettings(reopened);
        QCOMPARE(reopened.get("ApplicationTheme").toString(), QString("dark"));
        QCOMPARE(reopened.get("AccentColor").toString(), QString("#ff8844"));
        QCOMPARE(reopened.get("MainWindowGeometry").toString(), QString("chroma-layout"));
        reopened.reset("ApplicationTheme");
        reopened.reset("AccentColor");
        QCOMPARE(reopened.get("ApplicationTheme").toString(), QString("chroma"));
        QCOMPARE(reopened.get("AccentColor").toString(), QString("#b7a5f5"));
        QCOMPARE(readFile(path), original);
    }

    void launchSettingsAndDataLocationsStayShared()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath("prismlauncher.cfg");
        QVERIFY(writeSettings(path, { { "ApplicationTheme", "light" },
                                      { "MaxMemoryAlloc", 6144 },
                                      { "InstanceDir", "custom-packs" },
                                      { "IconsDir", "custom-icons" },
                                      { "JavaPath", "java/bin/javaw.exe" },
                                      { "MSAClientIDOverride", "existing-client" },
                                      { "Language", "en_US" },
                                      { "UseSystemLocale", true },
                                      { "UnknownPrismPreference", "retain-me" } }));
        const QByteArray accounts = "{\"accounts\":[{\"synthetic\":true}]}";
        QVERIFY(writeFile(root.filePath("accounts.json"), accounts));
        QVERIFY(writeFile(root.filePath("custom-packs/pack/.minecraft/saves/world/level.dat"), QByteArray("world bytes\0\1", 13)));
        const auto world = readFile(root.filePath("custom-packs/pack/.minecraft/saves/world/level.dat"));
        ChromaSettingsObject settings(path);
        registerSettings(settings);
        QCOMPARE(settings.get("MaxMemAlloc").toInt(), 6144);
        QCOMPARE(settings.get("Language").toString(), QString("en_US"));
        QVERIFY(settings.get("UseSystemLocale").toBool());
        QCOMPARE(settings.get("InstanceDir").toString(), QString("custom-packs"));
        QCOMPARE(settings.get("IconsDir").toString(), QString("custom-icons"));
        QCOMPARE(settings.get("JavaPath").toString(), QString("java/bin/javaw.exe"));
        QCOMPARE(settings.get("MSAClientIDOverride").toString(), QString("existing-client"));
        QVERIFY(settings.set("MaxMemAlloc", 7168));
        const auto shared = readSettings(path);
        QCOMPARE(shared.value("MaxMemAlloc").toInt(), 7168);
        QVERIFY(!shared.contains("MaxMemoryAlloc"));
        QCOMPARE(shared.value("ApplicationTheme").toString(), QString("light"));
        QCOMPARE(shared.value("UnknownPrismPreference").toString(), QString("retain-me"));
        QCOMPARE(shared.value("InstanceDir").toString(), QString("custom-packs"));
        QCOMPARE(readFile(root.filePath("accounts.json")), accounts);
        QCOMPARE(readFile(root.filePath("custom-packs/pack/.minecraft/saves/world/level.dat")), world);
        QVERIFY(!QFileInfo::exists(root.filePath("instances")));
        QVERIFY(!QFileInfo::exists(settings.uiFilePath()));
    }

    void dynamicInterfacePreferencesAndAliasesAreIsolated()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath("prismlauncher.cfg");
        QVERIFY(writeSettings(path, { { "UnknownPrismPreference", "original" }, { "OldAccent", "source-value" } }));
        const auto original = readFile(path);
        QVERIFY(writeSettings(root.filePath("chroma-ui.cfg"), { { "OldAccent", "#123456" } }));
        ChromaSettingsObject settings(path);
        settings.registerSetting({ "AccentColor", "OldAccent" }, "#b7a5f5");
        QCOMPARE(settings.get("AccentColor").toString(), QString("#123456"));
        QVERIFY(settings.set("AccentColor", "#abcdef"));
        const QStringList uiKeys = { "ChromaHomeMode",
                                     "WideBarVisibility_instanceToolBar",
                                     "UI/Mods_Page/ColumnsVisibility",
                                     "SettingsGeometry",
                                     "NewInstanceGeometry",
                                     "ConsoleFont",
                                     "IconTheme",
                                     "InstSortMode" };
        for (const auto& key : uiKeys) {
            settings.registerSetting(key, "default");
            QVERIFY(settings.set(key, "chroma-preference"));
        }
        auto ui = readSettings(settings.uiFilePath());
        QVERIFY(!ui.contains("OldAccent"));
        QCOMPARE(ui.value("AccentColor").toString(), QString("#abcdef"));
        for (const auto& key : uiKeys)
            QCOMPARE(ui.value(key).toString(), QString("chroma-preference"));
        QVERIFY(settings.set("AccentColor", QVariant()));
        QCOMPARE(settings.get("AccentColor").toString(), QString("#b7a5f5"));
        settings.reset("SettingsGeometry");
        ui = readSettings(settings.uiFilePath());
        QVERIFY(!ui.contains("AccentColor"));
        QVERIFY(!ui.contains("SettingsGeometry"));
        QCOMPARE(readFile(path), original);
    }

    void suspensionFlushesOnlyChangedFiles()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath("prismlauncher.cfg");
        QVERIFY(writeSettings(path, { { "MaxMemAlloc", 4096 } }));
        ChromaSettingsObject settings(path);
        registerSettings(settings);
        const auto original = readFile(path);
        settings.suspendSave();
        QVERIFY(settings.set("AccentColor", "#123456"));
        QVERIFY(settings.set("MaxMemAlloc", 8192));
        QCOMPARE(readFile(path), original);
        QVERIFY(!QFileInfo::exists(settings.uiFilePath()));
        settings.resumeSave();
        QCOMPARE(readSettings(path).value("MaxMemAlloc").toInt(), 8192);
        QCOMPARE(readSettings(settings.uiFilePath()).value("AccentColor").toString(), QString("#123456"));

        // An interface-only batch must not rewrite a Prism file changed since
        // the prior shared batch. The launcher lock handles concurrent writers.
        const QByteArray marker = "[General]\nConfigVersion=1.3\nMaxMemAlloc=8192\n# preserve this comment\n";
        QVERIFY(writeFile(path, marker));
        settings.suspendSave();
        QVERIFY(settings.set("MainWindowGeometry", "layout"));
        settings.resumeSave();
        QCOMPARE(readFile(path), marker);
        settings.resumeSave();
        QCOMPARE(readFile(path), marker);
    }

    void reloadNotifiesWithoutRewritingAndDropsRemovedValues()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto path = root.filePath("prismlauncher.cfg");
        QVERIFY(writeSettings(path, { { "MaxMemAlloc", 8192 } }));
        QVERIFY(writeSettings(root.filePath("chroma-ui.cfg"), { { "AccentColor", "#123456" } }));
        ChromaSettingsObject settings(path);
        registerSettings(settings);
        int notifications = 0;
        connect(&settings, &SettingsObject::SettingChanged, &settings, [&notifications] { ++notifications; });
        const QByteArray newShared = "# retain shared formatting\n[General]\nConfigVersion=1.3\nJavaPath=java-new\n";
        const QByteArray newUi = "# retain UI formatting\n[General]\nConfigVersion=1.3\nApplicationTheme=dark\n";
        QVERIFY(writeFile(path, newShared));
        QVERIFY(writeFile(settings.uiFilePath(), newUi));
        QVERIFY(settings.reload());
        QVERIFY(notifications > 0);
        QCOMPARE(settings.get("MaxMemAlloc").toInt(), 4096);
        QCOMPARE(settings.get("JavaPath").toString(), QString("java-new"));
        QCOMPARE(settings.get("AccentColor").toString(), QString("#b7a5f5"));
        QCOMPARE(settings.get("ApplicationTheme").toString(), QString("dark"));
        QCOMPARE(readFile(path), newShared);
        QCOMPARE(readFile(settings.uiFilePath()), newUi);
        QVERIFY(QFile::remove(settings.uiFilePath()));
        QVERIFY(settings.reload());
        QCOMPARE(settings.get("ApplicationTheme").toString(), QString("chroma"));
        QVERIFY(!QFileInfo::exists(settings.uiFilePath()));
    }
};

QTEST_GUILESS_MAIN(ChromaProfileTest)
#include "ChromaProfile_test.moc"
