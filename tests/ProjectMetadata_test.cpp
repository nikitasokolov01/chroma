// SPDX-License-Identifier: GPL-3.0-only
#include <QImage>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include "modplatform/flame/FlameModIndex.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "ui/widgets/ProjectDescriptionPage.h"

#ifndef Q_MOC_RUN
static const char* fixture0 =
    "{\"id\":\"pack123\",\"title\":\"A <great> pack\",\"slug\":\"sample\",\"project_type\":\"modpack\",\n            "
    "\"description\":\"Summary\",\"author\":\"Author\",\"downloads\":3200000000,\"followers\":42,\"categories\":[\"adventure\"],\n         "
    "   \"additional_categories\":[\"adventure\",\"technology\"],\"game_versions\":[\"1.21.1\"],\"loaders\":[\"fabric\"],\n            "
    "\"license\":{\"id\":\"MIT\",\"url\":\"https://example.org/license\"},\"client_side\":\"required\",\"server_side\":\"optional\",\n     "
    "       \"published\":\"2024-01-01T00:00:00Z\",\"updated\":\"2026-01-01T00:00:00Z\",\"body\":\"# About\",\"status\":\"approved\",\n    "
    "        "
    "\"gallery\":[{\"url\":\"https://example.org/b.png\",\"title\":\"Second\"},{\"url\":\"https://example.org/"
    "a.png\",\"title\":\"First\",\"featured\":true}],\n            "
    "\"donation_urls\":[{\"id\":\"site\",\"platform\":\"Support\",\"url\":\"https://example.org/support\"}]}";
static const char* fixture1 =
    "{\"id\":42,\"name\":\"Pack\",\"slug\":\"pack\",\"summary\":\"Summary\",\"status\":8,\n            "
    "\"downloadCount\":12000000000,\"dateCreated\":\"2022-01-01T00:00:00Z\",\"dateModified\":\"2026-01-01T00:00:00Z\",\n            "
    "\"authors\":[{\"name\":\"Author\",\"url\":\"https://example.org/author\"}],\"categories\":[{\"name\":\"Adventure\"}],\n            "
    "\"links\":{\"websiteUrl\":\"https://example.org/pack\",\"sourceUrl\":\"https://example.org/source\"},\n            "
    "\"screenshots\":[{\"url\":\"https://example.org/a.png\",\"title\":\"Gallery\",\"description\":\"Caption\"}],\n            "
    "\"latestFiles\":[{\"gameVersions\":[\"1.21.1\",\"NeoForge\"]}],\"latestFilesIndexes\":[{\"gameVersion\":\"1.21.1\",\"modLoader\":6}]}";
static const char* fixture2 = "{\"id\":\"id\",\"title\":\"Title\"}";
static const char* fixture3 = "{\"id\":7,\"name\":\"Title\",\"slug\":\"title\"}";
static const char* fixture4 =
    "{\"modId\":42,\"id\":43,\"displayName\":\"Release\",\"fileName\":\"a.jar\",\"fileDate\":\"2026-01-01T00:00:00Z\",\n            "
    "\"releaseType\":1,\"gameVersions\":[\"Client\",\"Server\",\"1.21.1\",\"Fabric\"],\n            "
    "\"dependencies\":[{\"modId\":100,\"relationType\":3},{\"modId\":101,\"relationType\":5}]}";

#endif

class ProjectMetadataTest : public QObject {
    Q_OBJECT
   private slots:
    void cancellationBeforeQueuedStart()
    {
        Task::Ptr request;
        {
            ModrinthAPI temporaryApi;
            auto pack = std::make_shared<ModPlatform::IndexedPack>();
            pack->addonId = "never-requested";
            request = temporaryApi.getProjectInfo({ pack }, {});
            QMetaObject::invokeMethod(request.get(), &Task::start, Qt::QueuedConnection);
            QVERIFY(request->abort());
        }  // The API is gone before its already queued task begins.
        QSignalSpy aborted(request.get(), &Task::aborted);
        QSignalSpy succeeded(request.get(), &Task::succeeded);
        QSignalSpy failed(request.get(), &Task::failed);
        QTRY_COMPARE(aborted.size(), 1);
        QCOMPARE(succeeded.size(), 0);
        QCOMPARE(failed.size(), 0);
        QVERIFY(request->abort());
        QCOMPARE(aborted.size(), 1);

        auto bytes = std::make_shared<QByteArray>();
        auto cached = ResourceAPI::cachedRequest(QUrl("https://catalog.invalid/never-requested"), bytes);
        QVERIFY(cached->abort());
        QSignalSpy cacheAborted(cached.get(), &Task::aborted);
        cached->start();
        QCOMPARE(cacheAborted.size(), 1);
        QVERIFY(bytes->isEmpty());
    }

    void modrinthMetadata()
    {
        auto obj = QJsonDocument::fromJson(fixture0).object();
        ModPlatform::IndexedPack pack;
        Modrinth::loadIndexedPack(pack, obj);
        Modrinth::loadExtraPackData(pack, obj);
        QCOMPARE(pack.provider, ModPlatform::ResourceProvider::MODRINTH);
        QCOMPARE(pack.websiteUrl, QString("https://modrinth.com/modpack/sample"));
        QCOMPARE(pack.extraData.downloads, qint64(3200000000));
        QCOMPARE(pack.extraData.followers, qint64(42));
        QCOMPARE(pack.extraData.categories.size(), 2);
        QCOMPARE(pack.extraData.gallery.front().title, QString("First"));
        QCOMPARE(pack.extraData.license, QString("MIT"));
        QCOMPARE(pack.extraData.body, QString("# About"));
        QVERIFY(!pack.extraData.bodyIsHtml);
        QVERIFY(pack.extraDataLoaded);
        Modrinth::loadExtraPackData(pack, obj);
        QCOMPARE(pack.extraData.donate.size(), 1);
        QCOMPARE(pack.extraData.gallery.size(), 2);
    }

    void curseforgeMetadata()
    {
        auto obj = QJsonDocument::fromJson(fixture1).object();
        ModPlatform::IndexedPack pack;
        FlameMod::loadIndexedPack(pack, obj);
        QCOMPARE(pack.provider, ModPlatform::ResourceProvider::FLAME);
        QCOMPARE(pack.extraData.downloads, qint64(12000000000));
        QCOMPARE(pack.extraData.followers, qint64(-1));
        QVERIFY(pack.extraData.license.isEmpty());
        QVERIFY(pack.extraData.bodyIsHtml);
        QCOMPARE(pack.extraData.status, QString("abandoned"));
        QCOMPARE(pack.extraData.gallery.front().description, QString("Caption"));
        QCOMPARE(pack.extraData.gameVersions, QStringList{ "1.21.1" });
        QCOMPARE(pack.extraData.loaders, QStringList{ "neoforge" });
        QCOMPARE(pack.extraData.sourceUrl, QString("https://example.org/source"));
        FlameMod::loadIndexedPack(pack, obj);
        QCOMPARE(pack.authors.size(), 1);
        QCOMPARE(pack.extraData.gallery.size(), 1);
    }

    void missingOptionalMetadata()
    {
        auto modrinth = QJsonDocument::fromJson(fixture2).object();
        ModPlatform::IndexedPack pack;
        Modrinth::loadIndexedPack(pack, modrinth);
        Modrinth::loadExtraPackData(pack, modrinth);
        QCOMPARE(pack.side, ModPlatform::Side::NoSide);
        QCOMPARE(pack.extraData.downloads, qint64(-1));
        QVERIFY(pack.extraData.gallery.isEmpty());
        QVERIFY(pack.extraData.license.isEmpty());
        auto flame = QJsonDocument::fromJson(fixture3).object();
        ModPlatform::IndexedPack cf;
        FlameMod::loadIndexedPack(cf, flame);
        QCOMPARE(cf.side, ModPlatform::Side::NoSide);
        QCOMPARE(cf.extraData.followers, qint64(-1));
        QVERIFY(cf.authors.isEmpty());
    }

    void nullableModrinthChangelog()
    {
        auto obj = QJsonDocument::fromJson(
                       "{\"id\":\"version\",\"project_id\":\"project\",\"date_published\":\"2026-01-01T00:00:00Z\",\"game_versions\":[\"1."
                       "21.1\"],\"loaders\":[\"fabric\"],\"name\":\"Release\",\"version_number\":\"1.0\",\"version_type\":\"release\","
                       "\"changelog\":null,\"files\":[{\"url\":\"https://example.org/"
                       "a.mrpack\",\"filename\":\"a.mrpack\",\"primary\":true,\"hashes\":{\"sha512\":\"abcd\"}}]}")
                       .object();
        const auto version = Modrinth::loadIndexedPackVersion(obj);
        QCOMPARE(version.fileId.toString(), QString("version"));
        QVERIFY(version.changelog.isEmpty());
        QVERIFY(version.dependencies.isEmpty());
        QCOMPARE(version.side, ModPlatform::Side::NoSide);
    }

    void releaseInspectionAndPagination()
    {
        auto pack = std::make_shared<ModPlatform::IndexedPack>();
        pack->addonId = "project";
        pack->name = "Project";
        pack->versionsLoaded = true;
        for (int i = 0; i < 150; ++i) {
            ModPlatform::IndexedVersion version;
            version.fileId = QString::number(i);
            version.version = QString("Release %1").arg(i);
            version.changelog = QString("Changes for release %1").arg(i);
            pack->versions.append(version);
        }
        ProjectDescriptionPage page;
        page.setProject(pack, 0);
        auto* tabs = page.findChild<QTabBar*>("projectInformationTabs");
        QVERIFY(tabs);
        tabs->setCurrentIndex(2);
        QVERIFY(page.toPlainText().contains("Show more releases"));
        QVERIFY(!page.toPlainText().contains("Release 149"));
        QSignalSpy selection(&page, &ProjectDescriptionPage::projectVersionSelected);
        page.anchorClicked(QUrl("chroma-version:4"));
        QCOMPARE(selection.size(), 1);
        QCOMPARE(selection.front().front().toInt(), 4);
        QVERIFY(page.toPlainText().contains("Changes for release 4"));
        QVERIFY(page.toPlainText().indexOf("Changes for release 4") < page.toPlainText().indexOf("Available releases"));
        page.anchorClicked(QUrl("chroma-more:releases"));
        QVERIFY(page.toPlainText().contains("Release 149"));
        QVERIFY(!page.toPlainText().contains("Show more releases"));
    }

    void dependenciesAndSides()
    {
        auto obj = QJsonDocument::fromJson(fixture4).object();
        const auto version = FlameMod::loadIndexedPackVersion(obj);
        QCOMPARE(version.side, ModPlatform::Side::UniversalSide);
        QCOMPARE(version.dependencies.size(), 2);
        QCOMPARE(version.dependencies[0].type, ModPlatform::DependencyType::REQUIRED);
        QCOMPARE(version.dependencies[1].type, ModPlatform::DependencyType::INCOMPATIBLE);
    }

    void sharedPresentation()
    {
        auto pack = std::make_shared<ModPlatform::IndexedPack>();
        pack->addonId = "test";
        pack->provider = ModPlatform::ResourceProvider::FLAME;
        pack->name = "<script>name</script>";
        pack->description = "Summary & details";
        pack->extraData.body = "<p><b>HTML body</b></p>";
        pack->extraData.bodyIsHtml = true;
        pack->extraDataLoaded = true;
        pack->versionsLoaded = true;
        pack->extraData.downloads = 123;
        ProjectDescriptionPage page;
        page.setProject(pack);
        QVERIFY(page.toPlainText().contains("<script>name</script>"));
        QVERIFY(page.toPlainText().contains("HTML body"));
        QVERIFY(page.toPlainText().contains("CurseForge"));
        auto* tabs = page.findChild<QTabBar*>("projectInformationTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);
        tabs->setCurrentIndex(1);
        QVERIFY(page.toPlainText().contains("no gallery images"));
        tabs->setCurrentIndex(2);
        QVERIFY(page.toPlainText().contains("No compatible releases"));
        pack->versionsLoaded = false;
        pack->versionsError = "Releases unavailable";
        page.setProject(pack);
        QVERIFY(page.toPlainText().contains("Releases unavailable"));
        QVERIFY(!page.toPlainText().contains("Loading releases"));
        page.setProject({});
        QVERIFY(page.toPlainText().isEmpty());
        QVERIFY(tabs->isHidden());
    }

    void providerHtmlCannotLoadLocalResources()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::red);
        const auto path = directory.filePath("local.png");
        QVERIFY(image.save(path));
        ProjectDescriptionPage page;
        // QTextDocument delegates resource reads to the browser. A provider
        // description must not fall back to Qt's local-file image loader.
        const auto resource = page.document()->resource(QTextDocument::ImageResource, QUrl::fromLocalFile(path));
        QCOMPARE(resource.metaType(), QMetaType::fromType<QByteArray>());
        QVERIFY(resource.toByteArray().isEmpty());
        const auto stylesheet = page.document()->resource(QTextDocument::StyleSheetResource, QUrl::fromLocalFile(path));
        QCOMPARE(stylesheet.metaType(), QMetaType::fromType<QByteArray>());
        QVERIFY(stylesheet.toByteArray().isEmpty());
    }
};
QTEST_MAIN(ProjectMetadataTest)
#include "ProjectMetadata_test.moc"
