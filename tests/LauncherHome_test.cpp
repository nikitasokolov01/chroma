// SPDX-License-Identifier: GPL-3.0-only
// Build explicitly with --target ChromaUiSmoke. The fixture and screenshots are
// kept under CHROMA_UI_TEST_ROOT (default: <working directory>/.chroma-test/ui).

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QWizard>
#include <cstdio>

#include "Application.h"
#include "BuildConfig.h"
#include "ChromaProfile.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "net/HttpMetaCache.h"
#include "tasks/Task.h"
#include "ui/InstanceWindow.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/IconPickerDialog.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/PrismProfileDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/instanceview/InstanceProxyModel.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/pagedialog/PageDialog.h"
#include "ui/pages/global/APIPage.h"
#include "ui/pages/global/AccountListPage.h"
#include "ui/pages/modplatform/modrinth/ModrinthModel.h"
#include "ui/pages/modplatform/modrinth/ModrinthPage.h"
#include "ui/themes/ThemeManager.h"
#include "ui/themes/AccentColor.h"
#include "ui/themes/ClayStyle.h"
#include "ui/widgets/AppearanceWidget.h"
#include "ui/widgets/ClayWidgets.h"
#include "ui/widgets/InlineWorkspace.h"
#include "ui/widgets/LauncherHome.h"
#include "ui/widgets/ModFilterWidget.h"
#include "ui/widgets/ModpackBrowser.h"
#include "ui/widgets/ModpackCardDelegate.h"

namespace {

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

bool prepareFixture(const QString& root)
{
    QDir directory(root);
    const QString marker = directory.filePath("chroma-ui-smoke-fixture.txt");
    if (directory.exists() && !directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty() && !QFile::exists(marker)) {
        std::fprintf(stderr, "Refusing to overwrite an existing directory without the Chroma UI fixture marker.\n");
        return false;
    }
    if (!directory.mkpath(".") || !writeFile(marker, "Synthetic launcher UI test data; no game or account files.\n"))
        return false;

    QSettings settings(directory.filePath(BuildConfig.LAUNCHER_CONFIGFILE), QSettings::IniFormat);
    settings.clear();
    settings.setValue("ConfigVersion", "1.3");
    settings.setValue("ApplicationTheme", "bright");
    settings.setValue("AccentColor", "#118833");
    settings.setValue("IconTheme", "breeze_dark");
    settings.setValue("Language", "en_US");
    settings.setValue("IgnoreJavaWizard", true);
    settings.setValue("UserAskedAboutAutomaticJavaDownload", true);
    settings.setValue("AutomaticJavaDownload", false);
    settings.setValue("TheCat", false);
    settings.setValue("InstSortMode", "Name");
    settings.setValue("InstanceDir", "Prism Library");
    settings.setValue("MaxMemAlloc", 3456);
    settings.setValue("DownloadsDir", directory.filePath("downloads"));
    // Prevent background translation/news requests from leaving the machine.
    settings.setValue("ProxyType", "HTTP");
    settings.setValue("ProxyAddr", "127.0.0.1");
    settings.setValue("ProxyPort", 9);
    settings.setValue("RequestTimeout", 1);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return false;
    QSettings ui(directory.filePath("chroma-ui.cfg"), QSettings::IniFormat);
    ui.clear();
    ui.setValue("ConfigVersion", "1.3");
    ui.setValue("Language", "en_US");
    ui.sync();
    if (ui.status() != QSettings::NoError)
        return false;
    for (const auto* source : { "PolyMC", "MultiMC" }) {
        if (!writeFile(directory.filePath(QString::fromLatin1(source) + "_nomigrate.txt"), "UI test profile\n"))
            return false;
    }
    if (!writeFile(directory.filePath("accounts.json"), "{\"accounts\":[],\"formatVersion\":3}"))
        return false;

    struct Fixture {
        const char* id;
        const char* name;
        const char* icon;
        const char* loader;
        const char* loaderVersion;
        int daysAgo;
    };
    const Fixture fixtures[] = {
        { "cobblemon", "Cobblemon", "chicken", "net.fabricmc.fabric-loader", "0.16.14", 2 },
        { "modrinth-smp", "Modrinth SMP", "modrinth", "net.neoforged", "21.1.172", 0 },
        { "quality-of-life", "Quality of Life", "diamond", "net.fabricmc.fabric-loader", "0.16.14", 0 },
        { "redstone", "Redstone Playground", "tnt", "", "", 1 },
        { "wynncraft", "Wynncraft", "grass", "", "", 3 },
    };
    // Local metadata makes this exercise the form without network retries.
    if (!directory.mkpath("meta/net.minecraft") ||
        !writeFile(directory.filePath("meta/index.json"),
                   R"({"formatVersion":1,"packages":[{"uid":"net.minecraft","name":"Minecraft"}]})") ||
        !writeFile(
            directory.filePath("meta/net.minecraft/index.json"),
            R"({"formatVersion":1,"uid":"net.minecraft","name":"Minecraft","versions":[{"version":"1.21.1","releaseTime":"2024-08-08T00:00:00Z","type":"release","recommended":true}]})"))
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Fixture& fixture : fixtures) {
        const QString relative = QString("Prism Library/%1").arg(fixture.id);
        if (!directory.mkpath(relative))
            return false;
        QDir instance(directory.filePath(relative));
        QSettings instanceSettings(instance.filePath("instance.cfg"), QSettings::IniFormat);
        instanceSettings.clear();
        instanceSettings.setValue("ConfigVersion", "1.3");
        instanceSettings.setValue("InstanceType", "OneSix");
        instanceSettings.setValue("name", fixture.name);
        instanceSettings.setValue("iconKey", fixture.icon);
        instanceSettings.setValue("lastLaunchTime", fixture.daysAgo ? now - qint64(fixture.daysAgo) * 86400000 : 0);
        instanceSettings.setValue("totalTimePlayed", fixture.daysAgo ? 7260 : 0);
        instanceSettings.sync();
        if (instanceSettings.status() != QSettings::NoError)
            return false;
        QJsonArray components{ QJsonObject{ { "uid", "net.minecraft" },
                                            { "version", "1.21.1" },
                                            { "cachedVersion", "1.21.1" },
                                            { "cachedName", "Minecraft" },
                                            { "important", true } } };
        if (*fixture.loader) {
            components.append(QJsonObject{ { "uid", fixture.loader },
                                           { "version", fixture.loaderVersion },
                                           { "cachedVersion", fixture.loaderVersion },
                                           { "cachedName", "Mod loader" } });
        }
        if (!writeFile(instance.filePath("mmc-pack.json"),
                       QJsonDocument(QJsonObject{ { "formatVersion", 1 }, { "components", components } }).toJson()))
            return false;
    }
    return true;
}

QLabel* labelWithText(QWidget* parent, const QString& text)
{
    for (auto* label : parent->findChildren<QLabel*>()) {
        if (label->text() == text)
            return label;
    }
    return nullptr;
}

QToolButton* navigationButton(QWidget* parent, const QString& name)
{
    for (auto* button : parent->findChildren<QToolButton*>()) {
        if (button->accessibleName() == name)
            return button;
    }
    return nullptr;
}

class HeldTask : public Task {
   public:
    HeldTask() : Task(false) {}
    void complete() { emitSucceeded(); }

   protected:
    void executeTask() override { setStatus("Synthetic task in progress"); }
};

}  // namespace

class LauncherHomeTest : public QObject {
    Q_OBJECT

   public:
    LauncherHomeTest(MainWindow* window, const QString& root) : m_window(window), m_root(root) {}

   private:
    MainWindow* m_window;
    QString m_root;
    LauncherHome* m_home = nullptr;
    InstanceView* m_view = nullptr;
    QLineEdit* m_search = nullptr;
    int m_popupPaints = 0;
    QPointer<NewInstanceDialog> m_creationDialog;
    bool m_creationTaskObserved = false;
    bool m_creationPromptFinished = false;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && m_creationDialog && !m_creationTaskObserved) {
            auto* progress = qobject_cast<ProgressDialog*>(watched);
            if (progress && progress->getTask()) {
                m_creationTaskObserved = true;
                connect(
                    progress->getTask(), &Task::started, this,
                    [this, progress] {
                        QVERIFY(m_creationDialog);
                        QTimer::singleShot(30, progress, [this] {
                            auto* confirmation = qobject_cast<QMessageBox*>(m_window->inlineWorkspace()->currentPage());
                            QVERIFY(confirmation);
                            QVERIFY(m_creationDialog);
                            QVERIFY(!confirmation->isWindow());
                            QTest::mouseClick(confirmation->button(QMessageBox::Yes), Qt::LeftButton);
                        });
                        QCOMPARE(QMessageBox::question(progress, "Creation task fixture", "Continue creating this fixture?",
                                                       QMessageBox::Yes | QMessageBox::No),
                                 QMessageBox::Yes);
                        QVERIFY(m_creationDialog);
                        m_creationPromptFinished = true;
                    },
                    Qt::SingleShotConnection);
            }
        }
        if (event->type() == QEvent::Paint) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && widget != m_window && widget->isWindow() &&
                (qobject_cast<QDialog*>(widget) || qobject_cast<QMainWindow*>(widget)))
                ++m_popupPaints;
        }
        return false;
    }

    void verifyInline(QWidget* page, int depth)
    {
        QVERIFY(page);
        QVERIFY(page->isVisible());
        QVERIFY(!page->isWindow());
        QCOMPARE(page->window(), m_window);
        QCOMPARE(m_window->inlineWorkspace()->currentPage(), page);
        QCOMPARE(m_window->inlineWorkspace()->pageCount(), depth);
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QVERIFY(m_window->isEnabled());
        for (auto* top : QApplication::topLevelWidgets()) {
            if (top != m_window && top->isVisible() && (qobject_cast<QDialog*>(top) || qobject_cast<QMainWindow*>(top)))
                QFAIL(qPrintable(QString("Unexpected top-level feature window: %1").arg(top->metaObject()->className())));
        }
        QCOMPARE(m_popupPaints, 0);
    }

    void verifyPageGeometry(QWidget* page)
    {
        QTest::qWait(100);
        auto* container = page->findChild<QWidget*>("pageContainer");
        if (!container)
            return;
        auto* nav = container->findChild<QWidget*>("pageNavigation");
        QVERIFY(nav);
        const QRect navBounds(nav->mapTo(m_window, QPoint()), nav->size());
        auto* footer = container->findChild<QWidget*>("pageFooter");
        qInfo() << "Page minima" << container->minimumSizeHint() << container->sizeHint() << page->minimumSizeHint() << "layout"
                << container->layout()->minimumSize() << "footer" << (footer ? footer->geometry() : QRect())
                << (footer ? footer->minimumSizeHint() : QSize()) << "parent" << page->parentWidget()->geometry() << "height-for-width"
                << container->hasHeightForWidth() << container->heightForWidth(container->width()) << page->hasHeightForWidth()
                << page->heightForWidth(page->width());
        for (auto* scroll : container->findChildren<QScrollArea*>()) {
            if (!scroll->isVisible() || !scroll->objectName().startsWith("pageScroll_"))
                continue;
            const QRect bodyBounds(scroll->mapTo(m_window, QPoint()), scroll->size());
            qInfo() << "Page geometry" << container->geometry() << navBounds << scroll->objectName() << bodyBounds << "viewport"
                    << scroll->viewport()->geometry() << "body" << scroll->widget()->geometry() << "scroll minimum"
                    << scroll->minimumSizeHint();
            QVERIFY2(!nav->isVisible() || !navBounds.intersects(bodyBounds), "Page body overlaps its navigation");
        }
    }

    QModelIndex indexFor(const QString& id) const
    {
        for (int row = 0; row < m_view->model()->rowCount(); ++row) {
            const auto index = m_view->model()->index(row, 0);
            if (index.data(InstanceList::InstanceIDRole).toString() == id)
                return index;
        }
        return {};
    }

    QList<ModPlatform::IndexedPack::Ptr> catalogFixtures()
    {
        struct CatalogFixture {
            const char* title;
            const char* description;
            const char* icon;
        };
        const CatalogFixture fixtures[] = {
            { "Meadowcraft", "A relaxed adventure with new biomes, cozy buildings, and room to explore.", "grass" },
            { "Create Workshop", "Build connected factories and bring a growing mechanical world to life.", "diamond" },
            { "Skybound Isles", "Start above the clouds and connect floating islands through exploration.", "chicken" },
            { "Redstone Laboratory", "Experiment with intricate machines, automation, and engineering challenges.", "tnt" },
            { "Vanilla Plus", "Thoughtful improvements to the familiar Minecraft experience.", "default" },
            { "A Very Long Adventure Pack Name for Responsive Card Layouts",
              "A longer description checks wrapping and clipping across card sizes.", "modrinth" },
        };
        QList<ModPlatform::IndexedPack::Ptr> packs;
        for (int i = 0; i < int(std::size(fixtures)); ++i) {
            auto pack = std::make_shared<ModPlatform::IndexedPack>();
            pack->addonId = QString("fixture-%1").arg(i);
            pack->provider = ModPlatform::ResourceProvider::MODRINTH;
            pack->name = fixtures[i].title;
            pack->slug = pack->addonId.toString();
            pack->description = fixtures[i].description;
            pack->authors = { { "Chroma Test Studio", {} } };
            pack->logoName = pack->slug + ".png";
            pack->extraDataLoaded = true;
            pack->extraData.body =
                "## Explore at your own pace\nA synthetic catalog entry used to verify the launcher UI.\n\n"
                "- Build with friends\n- Discover new landscapes\n- Choose a compatible version";
            pack->versionsLoaded = true;
            for (int release = 0; release < 2; ++release) {
                ModPlatform::IndexedVersion version;
                version.addonId = pack->addonId;
                version.fileId = pack->slug + QString("-release-%1").arg(release);
                version.version = release ? "1.0.0" : "1.1.0";
                version.version_number = version.version;
                version.version_type = ModPlatform::IndexedVersionType::Release;
                version.mcVersion = { "1.21.1" };
                version.loaders = ModPlatform::Fabric;
                version.date = "2026-09-20T12:00:00Z";
                version.downloadUrl = QUrl::fromLocalFile(QDir(m_root).filePath("catalog-fixture.zip")).toString();
                pack->versions.append(version);
            }
            packs.append(pack);
        }
        return packs;
    }

    void seedCatalog(ModrinthPage* page, const QList<ModPlatform::IndexedPack::Ptr>& packs, const QString& term = {})
    {
        auto* model = page->findChild<Modrinth::ModpackListModel*>();
        auto* filter = page->findChild<ModFilterWidget*>();
        QVERIFY(model && filter);
        QVERIFY(!model->hasActiveSearchJob());
        // Prime the same state as a completed API response. The friend accessor
        // exists only for this harness; no runtime fixture switch is exposed.
        model->m_currentSearchTerm = term.isNull() ? page->findChild<QLineEdit*>("searchEdit")->text() : term;
        model->m_currentSort = "relevance";
        model->m_filter = filter->getFilter();
        model->beginResetModel();
        model->m_modpacks.clear();
        model->m_nextSearchOffset = 0;
        model->m_searchState = Modrinth::ModpackListModel::Finished;
        model->endResetModel();
        for (int i = 0; i < packs.size(); ++i) {
            const auto pack = packs.at(i);
            static const char* icons[] = { "grass", "diamond", "chicken", "tnt", "default", "modrinth" };
            const QIcon icon = APPLICATION->icons()->getIcon(icons[i % int(std::size(icons))]);
            model->m_logoMap.insert(pack->logoName, icon);
            const QString path = APPLICATION->metacache()->resolveEntry(page->metaEntryBase(), "logos/" + pack->logoName)->getFullPath();
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QVERIFY(icon.pixmap(192, 192).save(path, "PNG"));
        }
        auto response = packs;
        model->searchRequestFinished(response);
        QCOMPARE(model->rowCount({}), packs.size());
    }

    void checkCardGeometry()
    {
        m_view->doItemsLayout();
        QList<QRect> rectangles;
        for (int row = 0; row < m_view->model()->rowCount(); ++row) {
            const auto index = m_view->model()->index(row, 0);
            const QRect geometry = m_view->geometryRect(index);
            QVERIFY2(geometry.isValid(), qPrintable(QString("Invalid card rectangle at row %1").arg(row)));
            for (const auto& other : rectangles)
                QVERIFY2(!geometry.intersects(other), "Instance cards overlap");
            rectangles.append(geometry);
            m_view->scrollTo(index, QAbstractItemView::PositionAtCenter);
            QCoreApplication::processEvents();
            const QRect visual = m_view->visualRect(index);
            QVERIFY2(m_view->viewport()->rect().contains(visual.center()),
                     qPrintable(QString("row %1, window %2x%3, viewport %4x%5, card %6,%7 %8x%9")
                                    .arg(row).arg(m_window->width()).arg(m_window->height())
                                    .arg(m_view->viewport()->width()).arg(m_view->viewport()->height())
                                    .arg(visual.x()).arg(visual.y()).arg(visual.width()).arg(visual.height())));
            QCOMPARE(m_view->indexAt(visual.center()), index);
            auto* scroll = m_home->findChild<QScrollArea*>("homeContentScroll");
            QVERIFY(scroll);
            QVERIFY(scroll->viewport()->rect().contains(m_view->viewport()->mapTo(scroll->viewport(), visual.center())));
        }
        QCOMPARE(m_view->verticalScrollBar()->maximum(), 0);
        auto* scroll = m_home->findChild<QScrollArea*>("homeContentScroll");
        QVERIFY(scroll);
        scroll->verticalScrollBar()->setValue(0);
    }

    void checkRecentTextGeometry()
    {
        auto* libraryTitle = m_home->findChild<QLabel*>("libraryTitle");
        QVERIFY(libraryTitle);
        for (auto* row : m_home->findChildren<QFrame*>("recentInstance")) {
            if (!row->isVisible())
                continue;
            QVERIFY2(row->parentWidget()->rect().contains(row->geometry()),
                     qPrintable(QString("Recent row y=%1 h=%2 exceeds section h=%3 at window %4x%5")
                                    .arg(row->y())
                                    .arg(row->height())
                                    .arg(row->parentWidget()->height())
                                    .arg(m_window->width())
                                    .arg(m_window->height())));
            QVERIFY(row->mapTo(m_home, QPoint(0, row->height())).y() <= libraryTitle->mapTo(m_home, QPoint()).y());
            for (auto* label : row->findChildren<QLabel*>()) {
                if (label->text().isEmpty())
                    continue;
                const QByteArray text = label->text().toUtf8();
                QVERIFY2(label->height() >= label->fontMetrics().height(), text.constData());
                const QRect bounds(label->mapTo(row, QPoint()), label->size());
                QVERIFY2(row->rect().contains(bounds), text.constData());
            }
        }
    }

   private slots:
    void initTestCase()
    {
        qApp->installEventFilter(this);
        const auto metadata = APPLICATION->metadataIndex();
        auto indexLoad = metadata->loadTask(Net::Mode::Offline);
        indexLoad->start();
        QTRY_VERIFY(indexLoad->isFinished());
        QVERIFY(indexLoad->wasSuccessful());
        auto minecraftLoad = metadata->get("net.minecraft")->loadTask(Net::Mode::Offline);
        minecraftLoad->start();
        QTRY_VERIFY(minecraftLoad->isFinished());
        QVERIFY(minecraftLoad->wasSuccessful());
        for (Meta::BaseEntity* entity :
             { static_cast<Meta::BaseEntity*>(metadata.get()), static_cast<Meta::BaseEntity*>(metadata->get("net.minecraft").get()) }) {
            QFile cached(QDir(m_root).filePath("meta/" + entity->localFilename()));
            QVERIFY(cached.open(QIODevice::ReadOnly));
            entity->setSha256(QString::fromLatin1(QCryptographicHash::hash(cached.readAll(), QCryptographicHash::Sha256).toHex()));
            QVERIFY(entity->isLoaded());
        }
        if (BuildConfig.LAUNCHER_APP_BINARY_NAME == "chroma") {
            QCOMPARE(QCoreApplication::applicationName(), QString("Chroma"));
            QCOMPARE(QCoreApplication::organizationName(), QString("Chroma"));
        }
        m_home = m_window->findChild<LauncherHome*>("launcherHome");
        m_view = m_window->findChild<InstanceView*>("instanceLibrary");
        QVERIFY(m_home);
        QVERIFY(m_view);
        m_search = m_home->findChild<QLineEdit*>("librarySearch");
        QVERIFY(m_search);
        m_window->resize(1280, 820);
        m_window->show();
        m_window->activateWindow();
        QTest::qWait(150);
        QCOMPARE(APPLICATION->instances()->count(), 5);
        QCOMPARE(m_view->model()->rowCount(), 5);
        QCOMPARE(m_home->findChildren<QFrame*>("recentInstance").size(), 3);
        QCOMPARE(APPLICATION->palette().highlight().color(), QColor("#7c3aed"));
        QVERIFY(labelWithText(m_home, "5 / 5"));
    }

    void freshInstancesExposeCachedSummaries()
    {
        // This runs before any card is selected or its pack profile is loaded.
        QVERIFY(APPLICATION->settings()->get("SelectedInstance").toString().isEmpty());
        QCOMPARE(indexFor("cobblemon").data(InstanceProxyModel::InstanceSummaryRole).toString(), QString("Fabric 1.21.1"));
        QCOMPARE(indexFor("modrinth-smp").data(InstanceProxyModel::InstanceSummaryRole).toString(), QString("NeoForge 1.21.1"));
        QCOMPARE(indexFor("redstone").data(InstanceProxyModel::InstanceSummaryRole).toString(), QString("Vanilla 1.21.1"));
    }

    void searchAndReset()
    {
        QTest::mouseClick(m_search, Qt::LeftButton);
        QTest::keyClicks(m_search, "no-such-instance");
        QCOMPARE(m_view->model()->rowCount(), 0);
        QVERIFY(labelWithText(m_home, "0 / 5"));
        auto* empty = labelWithText(m_home, "No instances match your search.\nTry a different name or clear the search.");
        QVERIFY(empty);
        QVERIFY(empty->isVisible());
        QTest::keyClick(m_search, Qt::Key_Escape);
        QTRY_COMPARE(m_view->model()->rowCount(), 5);
        QVERIFY(!empty->isVisible());
        QTest::keyClicks(m_search, "COBBLE");
        QCOMPARE(m_view->model()->rowCount(), 1);
        QCOMPARE(m_view->model()->index(0, 0).data(InstanceList::InstanceIDRole).toString(), QString("cobblemon"));
        m_home->clearSearch();
        QCOMPARE(m_view->model()->rowCount(), 5);
    }

    void selectionUpdatesDetailsAndActions()
    {
        const auto index = indexFor("cobblemon");
        QVERIFY(index.isValid());
        m_view->scrollTo(index);
        QCoreApplication::processEvents();
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, Qt::NoModifier, m_view->visualRect(index).center());
        QCOMPARE(m_view->currentIndex(), index);
        QCOMPARE(APPLICATION->settings()->get("SelectedInstance").toString(), QString("cobblemon"));
        auto* details = m_home->findChild<QScrollArea*>("homeDetails");
        QVERIFY(details);
        QVERIFY(labelWithText(details, "Cobblemon"));
        for (const auto* name : { "actionLaunchInstance", "actionEditInstance", "actionViewSelectedInstFolder" }) {
            auto* action = m_window->findChild<QAction*>(name);
            QVERIFY2(action, name);
            QVERIFY2(action->isEnabled(), name);
        }
        auto* stop = m_window->findChild<QAction*>("actionKillInstance");
        QVERIFY(stop);
        QVERIFY(!stop->isEnabled());
    }

    void sortAndLibraryNavigation()
    {
        const auto combos = m_home->findChildren<QComboBox*>();
        QCOMPARE(combos.size(), 1);
        auto* sort = combos.constFirst();
        sort->setFocus();
        QTest::keyClick(sort, Qt::Key_End);
        QCOMPARE(APPLICATION->settings()->get("InstSortMode").toString(), QString("LastLaunch"));
        QCOMPARE(m_view->model()->index(0, 0).data(InstanceList::InstanceIDRole).toString(), QString("redstone"));
        QTest::keyClick(sort, Qt::Key_Home);
        QCOMPARE(m_view->model()->index(0, 0).data(InstanceList::InstanceIDRole).toString(), QString("cobblemon"));

        auto* library = navigationButton(m_home, "Library");
        auto* home = navigationButton(m_home, "Home");
        auto* recentTitle = m_home->findChild<QLabel*>("recentTitle");
        QVERIFY(library);
        QVERIFY(home);
        QVERIFY(recentTitle);
        QTest::mouseClick(library, Qt::LeftButton);
        QVERIFY(library->isChecked());
        QVERIFY(!recentTitle->isVisible());
        QTest::mouseClick(home, Qt::LeftButton);
        QVERIFY(home->isChecked());
        QVERIFY(recentTitle->isVisible());
    }

    void customAccentPersistsAndReapplies()
    {
        AppearanceWidget appearance(false, m_window);
        m_window->openInlinePage(&appearance, "Appearance");
        verifyInline(&appearance, 1);
        auto* custom = appearance.findChild<QPushButton*>("customAccentButton");
        auto* presets = appearance.findChild<QComboBox*>("accentComboBox");
        QVERIFY(custom);
        QVERIFY(presets);
        QVERIFY(custom->isEnabled());
        bool accepted = false;
        QTimer::singleShot(20, &appearance, [&] {
            auto* dialog = qobject_cast<QColorDialog*>(m_window->inlineWorkspace()->currentPage());
            if (dialog) {
                verifyInline(dialog, 2);
                dialog->setCurrentColor(QColor("#24304f"));
                accepted = true;
                dialog->accept();
            }
        });
        QTest::mouseClick(custom, Qt::LeftButton);
        QVERIFY(accepted);
        QCOMPARE(APPLICATION->settings()->get("AccentColor").toString(), QString("#24304f"));
        QCOMPARE(APPLICATION->palette().highlight().color(), QColor("#24304f"));
        QCOMPARE(APPLICATION->palette().highlightedText().color(), QColor(Qt::white));
        QSettings persisted(QDir(m_root).filePath("chroma-ui.cfg"), QSettings::IniFormat);
        QCOMPARE(persisted.value("AccentColor").toString(), QString("#24304f"));
        QSettings prism(QDir(m_root).filePath(BuildConfig.LAUNCHER_CONFIGFILE), QSettings::IniFormat);
        QCOMPARE(prism.value("ApplicationTheme").toString(), QString("bright"));
        QCOMPARE(prism.value("AccentColor").toString(), QString("#118833"));
        APPLICATION->themeManager()->applyCurrentlySelectedTheme();
        QCOMPARE(APPLICATION->palette().highlight().color(), QColor("#24304f"));
        presets->setCurrentIndex(presets->findData("#7c3aed"));
        QCOMPARE(APPLICATION->palette().highlight().color(), QColor("#7c3aed"));
        appearance.close();
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
    }

    void clayAccessibilityAndThemeSwitching()
    {
        const auto previousMotion = qgetenv("CHROMA_REDUCED_MOTION");
        const auto restore = qScopeGuard([&] {
            if (previousMotion.isNull())
                qunsetenv("CHROMA_REDUCED_MOTION");
            else
                qputenv("CHROMA_REDUCED_MOTION", previousMotion);
            APPLICATION->settings()->set("ApplicationTheme", "chroma");
            APPLICATION->themeManager()->applyCurrentlySelectedTheme();
        });
        qputenv("CHROMA_REDUCED_MOTION", "1");
        QVERIFY(!Clay::motionAllowed());
        QVERIFY(Clay::enabled());
        QCOMPARE(APPLICATION->palette().color(QPalette::Window), QColor("#f4f1fa"));
        QCOMPARE(APPLICATION->palette().color(QPalette::PlaceholderText), QColor("#635f69"));
        QVERIFY(QFontDatabase::families().contains("Nunito"));
        QVERIFY(QFontDatabase::families().contains("DM Sans"));
        for (auto* button : m_home->findChildren<QToolButton*>()) {
            if (button->isVisible() && !button->accessibleName().isEmpty())
                QVERIFY2(button->height() >= 44 && button->width() >= 44, qPrintable(button->accessibleName()));
        }
        m_search->setFocus();
        QVERIFY(m_search->hasFocus());
        for (const auto* theme : { "dark", "bright", "chroma-dark", "chroma" }) {
            APPLICATION->settings()->set("ApplicationTheme", theme);
            APPLICATION->themeManager()->applyCurrentlySelectedTheme();
            QCoreApplication::processEvents();
            QCOMPARE(Clay::enabled(), QString(theme).startsWith("chroma"));
            QCOMPARE(Clay::dark(), QString(theme) == "chroma-dark");
            m_search->setText("COBBLE");
            QCOMPARE(m_view->model()->rowCount(), 1);
            m_search->clear();
            QCOMPARE(m_view->model()->rowCount(), 5);
        }
    }

    void chromaDarkUsesAppearanceSelector()
    {
        const auto previousTheme = APPLICATION->settings()->get("ApplicationTheme");
        const auto previousAccent = APPLICATION->settings()->get("AccentColor");
        const auto previousIconTheme = APPLICATION->settings()->get("IconTheme");
        const bool pairedIcons = previousIconTheme.toString() == "breeze_light" || previousIconTheme.toString() == "breeze_dark";
        const QSize previousSize = m_window->size();
        AppearanceWidget appearance(false, m_window);
        const auto restore = qScopeGuard([&] {
            appearance.close();
            m_window->inlineWorkspace()->closeAllPages();
            m_home->showHomePage();
            m_home->clearSearch();
            APPLICATION->settings()->set("ApplicationTheme", previousTheme);
            APPLICATION->settings()->set("AccentColor", previousAccent);
            APPLICATION->settings()->set("IconTheme", previousIconTheme);
            APPLICATION->themeManager()->applyCurrentlySelectedTheme();
            m_window->resize(previousSize);
            QCoreApplication::processEvents();
        });
        m_window->resize(1280, 820);
        m_window->openInlinePage(&appearance, "Appearance");
        verifyInline(&appearance, 1);
        auto* themes = appearance.findChild<QComboBox*>("widgetStyleComboBox");
        auto* accents = appearance.findChild<QComboBox*>("accentComboBox");
        auto* custom = appearance.findChild<QPushButton*>("customAccentButton");
        QVERIFY(themes);
        QVERIFY(accents);
        QVERIFY(custom);
        const int darkIndex = themes->findData("chroma-dark");
        const int lightIndex = themes->findData("chroma");
        QVERIFY(darkIndex >= 0);
        QVERIFY(lightIndex >= 0);

        // Exercise the real Appearance signal and persistence path, including
        // returning to light and selecting dark again without restarting.
        for (const int selected : { darkIndex, lightIndex, darkIndex }) {
            themes->setCurrentIndex(selected);
            QCoreApplication::processEvents();
            const bool dark = selected == darkIndex;
            QCOMPARE(APPLICATION->settings()->get("ApplicationTheme").toString(), dark ? QString("chroma-dark") : QString("chroma"));
            QCOMPARE(Clay::dark(), dark);
            QVERIFY(Clay::enabled());
            QVERIFY(accents->isEnabled());
            QVERIFY(custom->isEnabled());
            QCOMPARE(APPLICATION->palette().color(QPalette::Window), Clay::colors().Canvas);
            QCOMPARE(APPLICATION->palette().color(QPalette::Base), Clay::colors().Surface);
            QCOMPARE(APPLICATION->palette().color(QPalette::PlaceholderText), Clay::colors().Muted);
            QCOMPARE(APPLICATION->settings()->get("AccentColor"), previousAccent);
            QCOMPARE(APPLICATION->settings()->get("IconTheme").toString(),
                     pairedIcons ? (dark ? QString("breeze_dark") : QString("breeze_light")) : previousIconTheme.toString());
        }

        const auto contrast = [](const QColor& first, const QColor& second) {
            const double firstLight = AccentColor::luminance(first);
            const double secondLight = AccentColor::luminance(second);
            return (qMax(firstLight, secondLight) + 0.05) / (qMin(firstLight, secondLight) + 0.05);
        };
        const auto palette = APPLICATION->palette();
        QCOMPARE(palette.color(QPalette::Window), QColor("#191622"));
        QCOMPARE(palette.color(QPalette::Base), QColor("#292333"));
        QCOMPARE(palette.color(QPalette::Text), QColor("#f4effa"));
        QCOMPARE(palette.color(QPalette::PlaceholderText), QColor("#beb4cb"));
        QCOMPARE(m_search->palette().color(QPalette::PlaceholderText), Clay::colors().Muted);
        QSettings persisted(QDir(m_root).filePath("chroma-ui.cfg"), QSettings::IniFormat);
        QCOMPARE(persisted.value("ApplicationTheme").toString(), QString("chroma-dark"));
        QVERIFY(AccentColor::luminance(palette.color(QPalette::Window)) < 0.04);
        QVERIFY(contrast(palette.color(QPalette::Text), palette.color(QPalette::Base)) >= 7);
        QVERIFY(contrast(palette.color(QPalette::PlaceholderText), palette.color(QPalette::Base)) >= 4.5);
        QVERIFY(contrast(palette.color(QPalette::Link), palette.color(QPalette::Window)) >= 4.5);
        const int skyAccent = accents->findData("#8ecbff");
        QVERIFY(skyAccent >= 0);
        accents->setCurrentIndex(skyAccent);
        QCOMPARE(APPLICATION->settings()->get("AccentColor").toString(), QString("#8ecbff"));
        QCOMPARE(APPLICATION->palette().color(QPalette::Highlight), QColor("#8ecbff"));
        QVERIFY(contrast(APPLICATION->palette().color(QPalette::HighlightedText), QColor("#8ecbff")) >= 4.5);
        const int originalAccent = accents->findData(previousAccent.toString());
        QVERIFY(originalAccent >= 0);
        accents->setCurrentIndex(originalAccent);
        QTest::qWait(100);
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("dark-settings.png")));

        appearance.close();
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        auto* settingsAction = m_window->findChild<QAction*>("actionSettings");
        QVERIFY(settingsAction);
        settingsAction->trigger();
        QTRY_VERIFY(qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage()));
        auto* settings = qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage());
        verifyInline(settings, 1);
        verifyPageGeometry(settings);
        QVERIFY(Clay::dark());
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("dark-launcher-settings.png")));
        auto* back = m_window->findChild<QToolButton*>("inlineBackButton");
        QVERIFY(back);
        QTest::mouseClick(back, Qt::LeftButton);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QTRY_VERIFY(m_search->isVisible());
        QTest::qWait(100);
        checkCardGeometry();
        checkRecentTextGeometry();
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("dark-home.png")));
        m_window->resize(680, 640);
        QTest::qWait(100);
        QCOMPARE(m_window->size(), QSize(680, 640));
        checkCardGeometry();
        checkRecentTextGeometry();
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("dark-compact.png")));
        m_search->setText("COBBLE");
        QCOMPARE(m_view->model()->rowCount(), 1);
        m_home->clearSearch();
        QCOMPARE(m_view->model()->rowCount(), 5);
    }

    void claySplitMenuKeepsNativeActions()
    {
        QAction action(tr("Play"), m_home);
        QMenu menu(m_home);
        menu.addAction(tr("Launch options"));
        class MenuButtonProbe : public ClayToolButton {
           public:
            using ClayToolButton::ClayToolButton;
            QStyleOptionToolButton options() const
            {
                QStyleOptionToolButton option;
                initStyleOption(&option);
                return option;
            }
        };
        MenuButtonProbe button(m_home);
        button.setDefaultAction(&action);
        button.setMenu(&menu);
        button.setPopupMode(QToolButton::MenuButtonPopup);
        button.setToolButtonStyle(Qt::ToolButtonTextOnly);
        button.setProperty("role", "primary");
        button.setGeometry(100, 100, 200, 56);
        button.show();
        QSignalSpy triggered(&action, &QAction::triggered);
        for (const auto direction : { Qt::LeftToRight, Qt::RightToLeft }) {
            button.setLayoutDirection(direction);
            bool opened = false;
            const int arrowX = direction == Qt::LeftToRight ? button.width() - 20 : 20;
            const QPoint arrowPosition(arrowX, button.height() / 2);
            const QStyleOptionToolButton option = button.options();
            const QRect menuRect = button.style()->subControlRect(QStyle::CC_ToolButton, &option, QStyle::SC_ToolButtonMenu, &button);
            QVERIFY2(menuRect.contains(arrowPosition), qPrintable(QString("Menu rectangle %1,%2 %3x%4 misses target %5,%6")
                                                                      .arg(menuRect.x()).arg(menuRect.y())
                                                                      .arg(menuRect.width()).arg(menuRect.height())
                                                                      .arg(arrowPosition.x()).arg(arrowPosition.y())));
            QTimer::singleShot(30, &menu, [&] {
                opened = menu.isVisible();
                menu.hide();
            });
            QTest::mouseClick(&button, Qt::LeftButton, Qt::NoModifier, arrowPosition);
            QTRY_VERIFY(opened);
            QCOMPARE(triggered.count(), 0);
        }
        button.setLayoutDirection(Qt::LeftToRight);
        QTest::mouseClick(&button, Qt::LeftButton, Qt::NoModifier, QPoint(50, button.height() / 2));
        QCOMPARE(triggered.count(), 1);
        button.setFocus();
        QTest::keyClick(&button, Qt::Key_Space);
        QCOMPARE(triggered.count(), 2);
    }

    void responsiveGeometryAndScreenshots()
    {
        m_window->activateWindow();
        auto* details = m_home->findChild<QScrollArea*>("homeDetails");
        QVERIFY(details);
        m_window->resize(1280, 820);
        QTest::qWait(100);
        QVERIFY(details->isVisible());
        checkCardGeometry();
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("home.png")));
        checkRecentTextGeometry();

        m_window->resize(800, 820);
        QTest::qWait(100);
        QCOMPARE(m_window->width(), 800);
        QVERIFY(!details->isVisible());
        checkCardGeometry();
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("narrow.png")));
        checkRecentTextGeometry();

        m_window->resize(680, 640);
        QTest::qWait(100);
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("minimum.png")));
        QCOMPARE(m_window->width(), 680);
        QCOMPARE(m_window->height(), 640);
        QCOMPARE(m_home->findChildren<QFrame*>("recentInstance").size(), 3);
        checkCardGeometry();
        checkRecentTextGeometry();
    }

    void prismProfileEntryAndFlow()
    {
        auto* action = m_window->findChild<QAction*>("actionUsePrismFolder");
        auto* entry = m_home->findChild<QToolButton*>("usePrismFolderButton");
        QVERIFY(action);
        QVERIFY(entry);
        QCOMPARE(entry->defaultAction(), action);
        QVERIFY(!entry->isVisible());

        QTemporaryDir source(QDir(m_root).filePath("../prism-profile-source-XXXXXX"));
        QTemporaryDir destination(QDir(m_root).filePath("empty-library-XXXXXX"));
        QVERIFY(source.isValid());
        QVERIFY(destination.isValid());
        const auto settings = APPLICATION->settings();
        const auto originalInstances = settings->get("InstanceDir");
        const auto originalMemory = settings->get("MaxMemAlloc");
        const auto restore = qScopeGuard([&] {
            settings->set("InstanceDir", originalInstances);
            settings->set("MaxMemAlloc", originalMemory);
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            // Upstream watchers can retain the temporary profile after a directory switch.
            // Release only handles inside this owned fixture before QTemporaryDir removes it.
            const QString fixtureRoot = QDir::cleanPath(destination.path());
            for (auto* watcher : APPLICATION->findChildren<QFileSystemWatcher*>()) {
                const auto paths = watcher->directories() + watcher->files();
                for (const auto& path : paths) {
                    const QString clean = QDir::cleanPath(path);
                    if (clean == fixtureRoot || clean.startsWith(fixtureRoot + '/'))
                        watcher->removePath(path);
                }
            }
        });
        settings->set("InstanceDir", destination.path());
        QTRY_COMPARE(APPLICATION->instances()->count(), 0);
        QTRY_VERIFY(entry->isVisible());
        m_window->resize(680, 640);
        QTest::qWait(50);
        QCOMPARE(m_window->size(), QSize(680, 640));
        QVERIFY(m_home->rect().contains(QRect(entry->mapTo(m_home, QPoint()), entry->size())));
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("empty-profile.png")));

        QDir prism(source.path());
        QVERIFY(prism.mkpath("instances/fixture-pack/.minecraft/saves/Test World"));
        QVERIFY(writeFile(prism.filePath("prismlauncher.cfg"),
                          "[General]\nConfigVersion=1.3\nInstanceDir=instances\nMaxMemAlloc=3456\nApplicationTheme=bright\n"));
        QVERIFY(writeFile(prism.filePath("instances/fixture-pack/instance.cfg"),
                          "[General]\nConfigVersion=1.3\nInstanceType=OneSix\nname=Prism World\niconKey=grass\n"));
        QVERIFY(writeFile(prism.filePath("instances/fixture-pack/mmc-pack.json"),
                          "{\"formatVersion\":1,\"components\":[{\"uid\":\"net.minecraft\",\"version\":\"1.21.1\"}]}"));
        QVERIFY(writeFile(prism.filePath("instances/fixture-pack/.minecraft/saves/Test World/level.dat"), "world fixture"));
        QVERIFY(writeFile(prism.filePath("instances/instgroups.json"),
                          "{\"formatVersion\":\"1\",\"groups\":{\"From Prism\":{\"instances\":[\"fixture-pack\"]}}}"));

        const auto readBytes = [](const QString& path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        const auto before = readBytes(prism.filePath("prismlauncher.cfg"));
        PrismProfileDialog dialog(m_window, source.path());
        dialog.show();
        auto* summary = dialog.findChild<QLabel*>("prismProfileSummary");
        auto* use = dialog.findChild<QPushButton*>("prismUseProfileButton");
        QVERIFY(summary);
        QVERIFY(use);
        QVERIFY(summary->text().contains("1 instance"));
        QVERIFY(use->isEnabled());
        QTRY_VERIFY(dialog.isVisible());
        verifyInline(&dialog, 1);
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("prism-profile.png")));
        QTest::mouseClick(use, Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(ChromaProfile::sameProfilePath(dialog.selectedProfilePath(), source.path()));
        QCOMPARE(readBytes(prism.filePath("prismlauncher.cfg")), before);
        QCOMPARE(readBytes(prism.filePath("instances/fixture-pack/.minecraft/saves/Test World/level.dat")), QByteArray("world fixture"));
        QCOMPARE(APPLICATION->instances()->count(), 0);
        QVERIFY(QDir(destination.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QCOMPARE(settings->get("MaxMemAlloc"), originalMemory);

        PrismProfileDialog same(m_window, m_root);
        QVERIFY(!same.findChild<QPushButton*>("prismUseProfileButton")->isEnabled());
        PrismProfileDialog invalid(m_window, prism.filePath("missing"));
        QVERIFY(!invalid.findChild<QPushButton*>("prismUseProfileButton")->isEnabled());
    }

    void wholePageScrollAndKeyboardNavigation()
    {
        m_home->showHomePage(false);
        m_window->resize(680, 640);
        auto* scroll = m_home->findChild<QScrollArea*>("homeContentScroll");
        auto* recent = m_home->findChild<QLabel*>("recentTitle");
        auto* library = m_home->findChild<QLabel*>("libraryTitle");
        auto* header = m_home->findChild<QWidget*>("homeHeader");
        QVERIFY(scroll && recent && library && header);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 100);
        QCOMPARE(m_view->verticalScrollBar()->maximum(), 0);
        scroll->verticalScrollBar()->setValue(0);
        const int recentTop = recent->mapTo(scroll->viewport(), QPoint()).y();
        const int libraryTop = library->mapTo(scroll->viewport(), QPoint()).y();
        const int searchTop = m_search->mapTo(scroll->viewport(), QPoint()).y();
        const QPoint headerPosition = header->mapTo(m_window, QPoint());
        scroll->verticalScrollBar()->setValue(100);
        QCOMPARE(recent->mapTo(scroll->viewport(), QPoint()).y(), recentTop - 100);
        QCOMPARE(library->mapTo(scroll->viewport(), QPoint()).y(), libraryTop - 100);
        QCOMPARE(m_search->mapTo(scroll->viewport(), QPoint()).y(), searchTop - 100);
        QCOMPARE(header->mapTo(m_window, QPoint()), headerPosition);
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("whole-page-scrolled.png")));

        scroll->verticalScrollBar()->setValue(0);
        QWheelEvent wheel(QPointF(20, 20), QPointF(m_view->viewport()->mapToGlobal(QPoint(20, 20))), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(m_view->viewport(), &wheel);
        QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);
        m_view->selectionModel()->setCurrentIndex(indexFor("cobblemon"), QItemSelectionModel::ClearAndSelect);
        m_view->setFocus();
        QTest::keyClick(m_view, Qt::Key_PageDown);
        QTest::keyClick(m_view, Qt::Key_End);
        QTRY_COMPARE(m_view->currentIndex(), indexFor("wynncraft"));
        const auto lastCenter = m_view->viewport()->mapTo(scroll->viewport(), m_view->visualRect(m_view->currentIndex()).center());
        QVERIFY(scroll->viewport()->rect().contains(lastCenter));
        scroll->verticalScrollBar()->setValue(0);
    }

    void pinnedInstancesPersistAndOpenInline()
    {
        m_window->resize(1280, 820);
        m_home->showHomePage(false);
        const auto index = indexFor("cobblemon");
        m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        QTRY_COMPARE(APPLICATION->settings()->get("SelectedInstance").toString(), QString("cobblemon"));
        auto* pin = m_home->findChild<QToolButton*>("pinSelectedInstance");
        QVERIFY(pin);
        QVERIFY(!m_home->selectedInstancePinned());
        QTest::mouseClick(pin, Qt::LeftButton);
        QTRY_VERIFY(m_home->selectedInstancePinned());
        QCOMPARE(APPLICATION->settings()->get("ChromaPinnedInstances").toStringList(), QStringList{ "cobblemon" });
        QSettings ui(QDir(m_root).filePath("chroma-ui.cfg"), QSettings::IniFormat);
        QCOMPARE(ui.value("ChromaPinnedInstances").toStringList(), QStringList{ "cobblemon" });
        QSettings shared(QDir(m_root).filePath(BuildConfig.LAUNCHER_CONFIGFILE), QSettings::IniFormat);
        QVERIFY(!shared.contains("ChromaPinnedInstances"));
        auto* pinned = m_home->findChild<QToolButton*>("pinnedInstance");
        QVERIFY(pinned);
        QCOMPARE(pinned->property("instanceId").toString(), QString("cobblemon"));
        QVERIFY(pinned->isVisible());
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("pinned-library.png")));
        QTest::mouseClick(pinned, Qt::LeftButton);
        QTRY_VERIFY(qobject_cast<InstanceWindow*>(m_window->inlineWorkspace()->currentPage()));
        verifyInline(m_window->inlineWorkspace()->currentPage(), 1);
        auto* editor = qobject_cast<InstanceWindow*>(m_window->inlineWorkspace()->currentPage());
        QCOMPARE(editor->instanceId(), QString("cobblemon"));
        verifyPageGeometry(editor);
        QVERIFY(pinned->isVisible());
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-instance.png")));
        auto* back = m_window->findChild<QToolButton*>("inlineBackButton");
        QVERIFY(back);
        QTest::mouseClick(back, Qt::LeftButton);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QVERIFY(m_home->selectedInstancePinned());
        m_home->toggleSelectedPin();
        QTRY_VERIFY(!m_home->selectedInstancePinned());
        QTRY_VERIFY(m_home->findChildren<QToolButton*>("pinnedInstance").isEmpty());
    }

    void settingsAndNestedConfirmationStayInline()
    {
        m_window->resize(1280, 820);
        auto* settingsAction = m_window->findChild<QAction*>("actionSettings");
        QVERIFY(settingsAction);
        bool visited = false;
        QTimer::singleShot(50, m_window, [&] {
            const auto cleanup = qScopeGuard([&] { m_window->inlineWorkspace()->closeAllPages(); });
            auto* settings = qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage());
            QVERIFY(settings);
            verifyInline(settings, 1);
            verifyPageGeometry(settings);
            QVERIFY(settings->findChild<QPushButton*>("savePageButton"));
            QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-settings.png")));
            bool confirmed = false;
            QTimer::singleShot(30, settings, [&] {
                auto* nested = qobject_cast<QMessageBox*>(m_window->inlineWorkspace()->currentPage());
                QVERIFY(nested);
                verifyInline(nested, 2);
                QVERIFY(settings->isVisible());
                QVERIFY(!settings->isEnabled());
                QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-confirmation.png")));
                confirmed = true;
                QTest::mouseClick(nested->button(QMessageBox::Yes), Qt::LeftButton);
            });
            QCOMPARE(QMessageBox::question(settings, "Inline confirmation", "Keep this test setting?", QMessageBox::Yes | QMessageBox::No),
                     QMessageBox::Yes);
            QVERIFY(confirmed);
            verifyInline(settings, 1);
            QVERIFY(settings->isEnabled());
            auto* back = m_window->findChild<QToolButton*>("inlineBackButton");
            QVERIFY(back);
            QTest::mouseClick(back, Qt::LeftButton);
            visited = true;
        });
        settingsAction->trigger();
        QTRY_VERIFY(visited);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QCOMPARE(m_popupPaints, 0);
    }

    void providerGalleryAndCatalogBrowsing()
    {
        const auto cleanup = qScopeGuard([&] {
            m_window->inlineWorkspace()->closeAllPages();
            m_window->resize(1280, 820);
        });
        m_window->resize(1280, 820);
        m_window->findChild<QAction*>("actionAddInstance")->trigger();
        QTRY_VERIFY(qobject_cast<NewInstanceDialog*>(m_window->inlineWorkspace()->currentPage()));
        auto* add = qobject_cast<NewInstanceDialog*>(m_window->inlineWorkspace()->currentPage());
        verifyInline(add, 1);
        auto* hub = add->findChild<QWidget*>("providerHub");
        auto* hubScroll = add->findChild<QScrollArea*>("providerHubScroll");
        auto* create = add->findChild<QPushButton*>("createInstanceButton");
        QVERIFY(hub && hubScroll && create);
        QTRY_VERIFY(hub->isVisible());
        QVERIFY(!create->isVisible());
        for (const QSize size : { QSize(1280, 820), QSize(680, 640) }) {
            m_window->resize(size);
            QTest::qWait(100);
            QCOMPARE(m_window->size(), size);
            QList<QRect> cards;
            for (const auto* id : { "modrinth", "flame", "atl", "technic", "legacy_ftb" }) {
                auto* card = hub->findChild<QPushButton*>("providerCard_" + QString::fromLatin1(id));
                QVERIFY(card && card->isVisible());
                QVERIFY(!card->accessibleName().isEmpty());
                for (const QRect& rect : cards)
                    QVERIFY(!card->geometry().intersects(rect));
                cards.append(card->geometry());
                hubScroll->ensureWidgetVisible(card);
                QVERIFY(hubScroll->viewport()->rect().contains(card->mapTo(hubScroll->viewport(), card->rect().center())));
            }
            hubScroll->verticalScrollBar()->setValue(0);
            QVERIFY(m_window->grab().save(
                QDir(m_root).filePath(size.width() == 1280 ? "provider-gallery.png" : "provider-gallery-compact.png")));
        }
        auto* curseForge = hub->findChild<QPushButton*>("providerCard_flame");
        QCOMPARE(curseForge->isEnabled(), bool(APPLICATION->capabilities() & Application::SupportsFlame));
        auto* custom = hub->findChild<QPushButton*>("createCustomInstance");
        QVERIFY(custom);
        hubScroll->ensureWidgetVisible(custom);
        custom->setFocus(Qt::TabFocusReason);
        QTest::keyClick(custom, Qt::Key_Return);
        QTRY_VERIFY(!hub->isVisible());
        auto* provider = add->findChild<QComboBox*>("providerSelector");
        QVERIFY(provider);
        QCOMPARE(provider->currentData().toString(), QString("vanilla"));
        auto* allProviders = add->findChild<QPushButton*>("browseProvidersButton");
        QVERIFY(allProviders);
        QTest::mouseClick(allProviders, Qt::LeftButton);
        QTRY_VERIFY(hub->isVisible());
        QVERIFY(!create->isVisible());

        auto* page = add->findChild<ModrinthPage*>();
        QVERIFY(page);
        const auto packs = catalogFixtures();
        seedCatalog(page, packs);
        m_window->resize(1280, 820);
        hubScroll->verticalScrollBar()->setValue(0);
        QTest::mouseClick(hub->findChild<QPushButton*>("providerCard_modrinth"), Qt::LeftButton);
        QTRY_VERIFY(page->isVisible());
        auto* browser = page->findChild<ModpackBrowser*>();
        auto* view = page->findChild<QListView*>("packView");
        auto* search = page->findChild<QLineEdit*>("searchEdit");
        auto* versions = page->findChild<QComboBox*>("versionSelectionBox");
        auto* details = page->findChild<QWidget*>("catalogDetails");
        auto* title = page->findChild<QLabel*>("catalogTitle");
        auto* model = page->findChild<Modrinth::ModpackListModel*>();
        QVERIFY(browser && view && search && versions && details && title && model);
        QCOMPARE(provider->currentData().toString(), QString("modrinth"));
        QCOMPARE(model->rowCount({}), packs.size());
        QCOMPARE(view->viewMode(), QListView::IconMode);
        QVERIFY(qobject_cast<ModpackCardDelegate*>(view->itemDelegate()));
        QVERIFY(!details->isVisible());
        QVERIFY(!create->isEnabled());
        for (const QSize size : { QSize(1280, 820), QSize(680, 640) }) {
            m_window->resize(size);
            QTest::qWait(120);
            view->doItemsLayout();
            QCOMPARE(m_window->size(), size);
            QCOMPARE(view->horizontalScrollBar()->maximum(), 0);
            QCOMPARE(model->rowCount({}), packs.size());
            QCOMPARE(view->visualRect(model->index(0, 0)).top(), view->visualRect(model->index(1, 0)).top());
            QVERIFY(view->visualRect(model->index(0, 0)).left() < view->visualRect(model->index(1, 0)).left());
            qInfo() << "Catalog geometry" << size << view->geometry() << view->viewport()->size() << view->gridSize()
                    << view->visualRect(model->index(0, 0)) << view->visualRect(model->index(1, 0)) << view->visualRect(model->index(2, 0))
                    << view->itemDelegate()->sizeHint(QStyleOptionViewItem(), model->index(0, 0));
            QList<QRect> cards;
            for (int row = 0; row < model->rowCount({}); ++row) {
                const QModelIndex index = model->index(row, 0);
                QCOMPARE(index.data(Qt::DisplayRole).toString(), packs.at(row)->name);
                QCOMPARE(index.data(ModpackCardRoles::AuthorRole).toString(), QString("Chroma Test Studio"));
                QVERIFY(!index.data(ModpackCardRoles::SummaryRole).toString().isEmpty());
                view->scrollTo(index, QAbstractItemView::PositionAtCenter);
                const QRect rect = view->visualRect(index);
                QVERIFY(rect.isValid() && rect.height() > 180);
                QVERIFY(view->viewport()->rect().contains(rect.center()));
                QCOMPARE(view->indexAt(rect.center()), index);
                const QRect content = rect.translated(0, view->verticalScrollBar()->value());
                for (const QRect& other : cards)
                    QVERIFY(!content.intersects(other));
                cards.append(content);
            }
            view->scrollToTop();
            QVERIFY(m_window->grab().save(
                QDir(m_root).filePath(size.width() == 1280 ? "modrinth-catalog.png" : "modrinth-catalog-compact.png")));
        }
        auto* filtersButton = page->findChild<QPushButton*>("filterButton");
        auto* filterDrawer = page->findChild<ModFilterWidget*>();
        QVERIFY(filtersButton && filterDrawer);
        QTest::mouseClick(filtersButton, Qt::LeftButton);
        QTest::qWait(100);
        QVERIFY(filterDrawer->isVisible() && !view->isVisible());
        QVERIFY(search->isVisible() && filtersButton->isVisible());
        QVERIFY(m_window->rect().contains(QRect(filterDrawer->mapTo(m_window, QPoint()), filterDrawer->size())));
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("modrinth-filters-compact.png")));
        QTest::mouseClick(filtersButton, Qt::LeftButton);
        QTRY_VERIFY(!filterDrawer->isVisible() && view->isVisible());
        // Clicking a real model card opens its cached detail/version data.
        const auto first = model->index(0, 0);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(first).center());
        QTRY_VERIFY(details->isVisible());
        QCOMPARE(title->text(), packs.first()->name);
        QCOMPARE(versions->count(), 2);
        QVERIFY(create->isEnabled());
        QVERIFY(!view->isVisible());
        QTest::qWait(100);
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("modrinth-details-compact.png")));
        for (QWidget* widget : { static_cast<QWidget*>(add), static_cast<QWidget*>(page), static_cast<QWidget*>(browser),
                                 static_cast<QWidget*>(details), static_cast<QWidget*>(create) }) {
            qInfo() << "Detail sizing" << widget->objectName() << widget->geometry() << widget->mapTo(m_window, QPoint()) << "min"
                    << widget->minimumSize() << widget->minimumSizeHint() << "hint" << widget->sizeHint() << "hfw"
                    << widget->hasHeightForWidth() << widget->heightForWidth(widget->width()) << widget->sizePolicy();
        }
        QVERIFY(m_window->rect().contains(QRect(create->mapTo(m_window, QPoint()), create->size())));
        auto* options = add->findChild<QPushButton*>("instanceOptionsButton");
        auto* instanceOptions = add->findChild<QWidget*>("instanceOptions");
        QVERIFY(options && instanceOptions);
        QTest::mouseClick(options, Qt::LeftButton);
        QTest::qWait(100);
        QVERIFY(instanceOptions->isVisible());
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("modrinth-options-compact.png")));
        for (QWidget* control : { static_cast<QWidget*>(add->findChild<QLineEdit*>("instNameTextBox")),
                                  static_cast<QWidget*>(add->findChild<QComboBox*>("groupBox")), static_cast<QWidget*>(create) }) {
            QVERIFY(control && control->isVisible());
            const QRect bounds(control->mapTo(m_window, QPoint()), control->size());
            QVERIFY2(m_window->rect().contains(bounds), qPrintable(QString("%1 bounds %2,%3 %4x%5 outside window %6x%7")
                                                                       .arg(control->objectName())
                                                                       .arg(bounds.x())
                                                                       .arg(bounds.y())
                                                                       .arg(bounds.width())
                                                                       .arg(bounds.height())
                                                                       .arg(m_window->width())
                                                                       .arg(m_window->height())));
        }
        QTest::mouseClick(options, Qt::LeftButton);
        m_window->resize(1280, 820);
        QTest::qWait(100);
        QVERIFY(view->isVisible());
        qInfo() << "Wide details" << details->isVisible() << details->geometry() << details->minimumSize() << details->maximumSize()
                << browser->geometry();
        QVERIFY(details->isVisible());
        QVERIFY(details->width() >= 300);
        QVERIFY(m_window->rect().contains(QRect(details->mapTo(m_window, QPoint()), details->size())));
        const QRect detailBounds(details->mapTo(browser, QPoint()), details->size());
        const QRect resultBounds(view->mapTo(browser, QPoint()), view->size());
        QVERIFY(browser->rect().contains(detailBounds));
        QVERIFY(!detailBounds.intersects(resultBounds));
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("modrinth-details.png")));
        versions->setCurrentIndex(1);
        QVERIFY(create->isEnabled());
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(title->text(), packs.at(1)->name);
        QCOMPARE(page->getCurrent()->addonId, packs.at(1)->addonId);
        QVERIFY(create->isEnabled());
        search->setFocus();
        QTest::keyClick(search, Qt::Key_Left);
        QTest::qWait(400);
        QVERIFY(create->isEnabled() && details->isVisible());
        QCOMPARE(page->getCurrent()->addonId, packs.at(1)->addonId);

        // The real search handler invalidates the old selection. Inject the
        // corresponding server response through the model's public callback.
        model->m_currentSearchTerm = "Create";
        search->setFocus();
        QTest::keyClicks(search, "Create");
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_VERIFY(!details->isVisible());
        QVERIFY(!create->isEnabled());
        QVERIFY(!page->getCurrent());
        seedCatalog(page, { packs.at(1) }, "Create");
        QTest::qWait(400);
        QCOMPARE(page->getSerachTerm(), QString("Create"));
        QCOMPARE(model->rowCount({}), 1);
        QCOMPARE(model->index(0, 0).data(Qt::DisplayRole).toString(), QString("Create Workshop"));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(model->index(0, 0)).center());
        QTRY_VERIFY(details->isVisible());
        QCOMPARE(title->text(), QString("Create Workshop"));
        QVERIFY(create->isEnabled());
        QVERIFY(!model->hasActiveSearchJob());
        auto* back = page->findChild<QPushButton*>("catalogBackToResults");
        QVERIFY(back);
        QTest::mouseClick(back, Qt::LeftButton);
        QVERIFY(view->isVisible() && !details->isVisible());
        QTest::mouseClick(filtersButton, Qt::LeftButton);
        QTest::qWait(100);
        QVERIFY(filterDrawer->isVisible() && view->isVisible());
        const QRect filterBounds(filterDrawer->mapTo(browser, QPoint()), filterDrawer->size());
        QVERIFY(browser->rect().contains(filterBounds));
        QVERIFY(!filterBounds.intersects(QRect(view->mapTo(browser, QPoint()), view->size())));
        QTest::mouseClick(filtersButton, Qt::LeftButton);

        // An empty result/reset must remove the prior ready-to-install task.
        seedCatalog(page, {}, "Create");
        QTRY_VERIFY(!create->isEnabled());
        QVERIFY(!page->getCurrent());
        QVERIFY(!details->isVisible());
        auto* empty = page->findChild<QLabel*>("catalogEmptyState");
        QVERIFY(empty && empty->isVisible());
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("modrinth-empty-results.png")));
        // Switching services must discard an actual ready installer as well.
        seedCatalog(page, { packs.at(1) }, "Create");
        view->doItemsLayout();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(model->index(0, 0)).center());
        QTRY_VERIFY(create->isEnabled() && details->isVisible());
        const int importIndex = provider->findData("import");
        QVERIFY(importIndex > 0);
        provider->setCurrentIndex(importIndex - 1);
        provider->setFocus();
        QTest::keyClick(provider, Qt::Key_Down);
        QTRY_VERIFY(!page->isVisible());
        QVERIFY(!create->isEnabled());
        QCOMPARE(provider->currentData().toString(), QString("import"));
        QTest::mouseClick(allProviders, Qt::LeftButton);
        QTRY_VERIFY(hub->isVisible());
        QVERIFY(!create->isVisible());
        QCOMPARE(m_popupPaints, 0);
    }

    void newInstanceAndNestedIconPickerStayInline()
    {
        auto* addAction = m_window->findChild<QAction*>("actionAddInstance");
        QVERIFY(addAction);
        bool visited = false;
        QTimer::singleShot(50, m_window, [&] {
            const auto cleanup = qScopeGuard([&] { m_window->inlineWorkspace()->closeAllPages(); });
            auto* add = qobject_cast<NewInstanceDialog*>(m_window->inlineWorkspace()->currentPage());
            QVERIFY(add);
            verifyInline(add, 1);
            add->selectProvider("vanilla");
            verifyPageGeometry(add);
            QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-new-instance.png")));
            auto* icon = add->findChild<QToolButton*>("iconButton");
            QVERIFY(icon);
            bool nestedVisited = false;
            QTimer::singleShot(30, add, [&] {
                auto* picker = qobject_cast<IconPickerDialog*>(m_window->inlineWorkspace()->currentPage());
                QVERIFY(picker);
                verifyInline(picker, 2);
                QVERIFY(add->isVisible());
                QVERIFY(!add->isEnabled());
                QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-icon-picker.png")));
                nestedVisited = true;
                QTest::mouseClick(m_window->findChild<QToolButton*>("inlineBackButton"), Qt::LeftButton);
            });
            QTest::mouseClick(icon, Qt::LeftButton);
            QVERIFY(nestedVisited);
            verifyInline(add, 1);
            QVERIFY(add->isEnabled());
            QTest::mouseClick(m_window->findChild<QToolButton*>("inlineBackButton"), Qt::LeftButton);
            visited = true;
        });
        addAction->trigger();
        QTRY_VERIFY(visited);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QCOMPARE(APPLICATION->instances()->count(), 5);
        QCOMPARE(m_popupPaints, 0);
    }

    void staticFileChooserStaysInline()
    {
        bool visited = false;
        QTimer::singleShot(30, m_window, [&] {
            auto* chooser = qobject_cast<QFileDialog*>(m_window->inlineWorkspace()->currentPage());
            QVERIFY(chooser);
            verifyInline(chooser, 1);
            visited = true;
            chooser->reject();
        });
        QVERIFY(QFileDialog::getOpenFileName(m_window, "Choose fixture", m_root).isEmpty());
        QVERIFY(visited);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
    }

    void primaryNavigationReplacesPagesAtMinimumSize()
    {
        const auto close = qScopeGuard([&] { m_window->inlineWorkspace()->closeAllPages(); });
        m_window->resize(680, 640);
        m_window->findChild<QAction*>("actionSettings")->trigger();
        QTRY_VERIFY(qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage()));
        QPointer<QWidget> settings = m_window->inlineWorkspace()->currentPage();
        verifyInline(settings, 1);
        verifyPageGeometry(settings);
        QCOMPARE(m_window->size(), QSize(680, 640));
        auto* selector = settings->findChild<QComboBox*>("pageSelector");
        QVERIFY(selector && selector->isVisible());
        auto* save = settings->findChild<QPushButton*>("savePageButton");
        QVERIFY(save && save->isVisible());
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-settings-compact.png")));
        QVERIFY(m_window->rect().contains(QRect(save->mapTo(m_window, QPoint()), save->size())));

        m_window->findChild<QAction*>("actionAddInstance")->trigger();
        QTRY_VERIFY(qobject_cast<NewInstanceDialog*>(m_window->inlineWorkspace()->currentPage()));
        QVERIFY(!settings || !settings->isVisible());
        auto* add = m_window->inlineWorkspace()->currentPage();
        verifyInline(add, 1);
        qobject_cast<NewInstanceDialog*>(add)->selectProvider("vanilla");
        verifyPageGeometry(add);
        QCOMPARE(m_window->size(), QSize(680, 640));
        selector = add->findChild<QComboBox*>("providerSelector");
        QVERIFY(selector && selector->isVisible());
        auto* create = add->findChild<QPushButton*>("createInstanceButton");
        QVERIFY(create && create->isVisible());
        QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-new-instance-compact.png")));
        QVERIFY(m_window->rect().contains(QRect(create->mapTo(m_window, QPoint()), create->size())));
        // Closing the settings page also releases its batching lock immediately.
        const auto memory = APPLICATION->settings()->get("MaxMemAlloc");
        APPLICATION->settings()->set("MaxMemAlloc", 4570);
        QSettings shared(QDir(m_root).filePath(BuildConfig.LAUNCHER_CONFIGFILE), QSettings::IniFormat);
        QCOMPARE(shared.value("MaxMemAlloc").toInt(), 4570);
        APPLICATION->settings()->set("MaxMemAlloc", memory);
        QTest::mouseClick(navigationButton(m_home, "Home"), Qt::LeftButton);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QVERIFY(m_home->findChild<QLabel*>("recentTitle")->isVisible());
        m_window->resize(1280, 820);
    }

    void newInstanceAcceptanceCreatesInSharedLibrary()
    {
        m_window->resize(1280, 820);
        const QString name = "Chroma UI Created Fixture";
        const QString groupName = "Chroma UI fixture group";
        QString createdId;
        const auto cleanup = qScopeGuard([&] {
            m_window->inlineWorkspace()->closeAllPages();
            if (!createdId.isEmpty())
                APPLICATION->instances()->deleteInstance(createdId);
        });
        m_window->findChild<QAction*>("actionAddInstance")->trigger();
        QTRY_VERIFY(qobject_cast<NewInstanceDialog*>(m_window->inlineWorkspace()->currentPage()));
        auto* add = m_window->inlineWorkspace()->currentPage();
        verifyInline(add, 1);
        qobject_cast<NewInstanceDialog*>(add)->selectProvider("vanilla");
        auto* nameField = add->findChild<QLineEdit*>("instNameTextBox");
        auto* group = add->findChild<QComboBox*>("groupBox");
        auto* create = add->findChild<QPushButton*>("createInstanceButton");
        QVERIFY(nameField && group && create);
        nameField->setText(name);
        group->setEditText(groupName);
        QVERIFY(create->isEnabled());
        m_creationDialog = qobject_cast<NewInstanceDialog*>(add);
        m_creationTaskObserved = false;
        m_creationPromptFinished = false;
        QTest::mouseClick(create, Qt::LeftButton);
        QVERIFY(m_creationTaskObserved);
        QVERIFY(m_creationPromptFinished);
        QTRY_COMPARE(APPLICATION->instances()->count(), 6);
        for (int row = 0; row < m_view->model()->rowCount(); ++row) {
            auto index = m_view->model()->index(row, 0);
            if (index.data(Qt::DisplayRole).toString() == name)
                createdId = index.data(InstanceList::InstanceIDRole).toString();
        }
        QVERIFY(!createdId.isEmpty());
        auto instance = APPLICATION->instances()->getInstanceById(createdId);
        QVERIFY(instance);
        QCOMPARE(instance->name(), name);
        QCOMPARE(APPLICATION->instances()->getInstanceGroup(createdId), groupName);
        const QString instanceRoot = QDir(m_root).filePath("Prism Library/" + createdId);
        QVERIFY(ChromaProfile::sameProfilePath(instance->instanceRoot(), instanceRoot));
        QVERIFY(QFile::exists(QDir(instanceRoot).filePath("instance.cfg")));
        QFile pack(QDir(instanceRoot).filePath("mmc-pack.json"));
        QVERIFY(pack.open(QIODevice::ReadOnly));
        const auto components = QJsonDocument::fromJson(pack.readAll()).object().value("components").toArray();
        QVERIFY(std::any_of(components.begin(), components.end(), [](const QJsonValue& value) {
            return value.toObject().value("uid") == "net.minecraft" && value.toObject().value("version") == "1.21.1";
        }));
        pack.close();
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QCOMPARE(m_popupPaints, 0);
        APPLICATION->instances()->deleteInstance(createdId);
        createdId.clear();
        QTRY_COMPARE(APPLICATION->instances()->count(), 5);
        QVERIFY(!QDir(instanceRoot).exists());
    }

    void instanceSettingsPreserveUnderlyingEditor()
    {
        const auto close = qScopeGuard([&] { m_window->inlineWorkspace()->closeAllPages(); });
        auto* editor = APPLICATION->showInstanceWindow(APPLICATION->instances()->getInstanceById("cobblemon"));
        QTRY_COMPARE(m_window->inlineWorkspace()->currentPage(), editor);
        verifyInline(editor, 1);
        APPLICATION->ShowGlobalSettings(editor, "java");
        QTRY_VERIFY(qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage()));
        auto* settings = m_window->inlineWorkspace()->currentPage();
        verifyInline(settings, 2);
        QVERIFY(editor->isVisible() && !editor->isEnabled());
        APPLICATION->ShowGlobalSettings(editor, "java");
        QCOMPARE(m_window->inlineWorkspace()->pageCount(), 2);
        QTest::mouseClick(m_window->findChild<QToolButton*>("inlineBackButton"), Qt::LeftButton);
        QTRY_COMPARE(m_window->inlineWorkspace()->currentPage(), editor);
        QVERIFY(editor->isVisible() && editor->isEnabled());
        QTest::mouseClick(m_window->findChild<QToolButton*>("inlineBackButton"), Qt::LeftButton);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
    }

    void multipleLocalPacksAreOfferedInOrder()
    {
        QTemporaryDir archives(QDir(m_root).filePath("local-pack-fixtures-XXXXXX"));
        QVERIFY(archives.isValid());
        const QString first = QDir(archives.path()).filePath("first-pack.zip");
        const QString second = QDir(archives.path()).filePath("second-pack.zip");
        const QByteArray emptyZip = QByteArray::fromHex("504b0506000000000000000000000000000000000000");
        QVERIFY(writeFile(first, emptyZip));
        QVERIFY(writeFile(second, emptyZip));
        bool returned = false;
        int visited = 0;
        QPointer<NewInstanceDialog> firstDialog;
        QTimer poll;
        connect(&poll, &QTimer::timeout, this, [&] {
            auto* page = qobject_cast<NewInstanceDialog*>(m_window->inlineWorkspace()->currentPage());
            if (!page || !page->isVisible())
                return;
            auto* input = page->findChild<QLineEdit*>("modpackEdit");
            if (!input || input->text().isEmpty())
                return;
            QVERIFY(!returned);
            verifyInline(page, 1);
            if (visited == 0) {
                QCOMPARE(QDir::fromNativeSeparators(input->text()), first);
                firstDialog = page;
                visited = 1;
                QTimer::singleShot(60, page, [&] {
                    QCOMPARE(m_window->inlineWorkspace()->currentPage(), firstDialog.data());
                    firstDialog->reject();
                });
            } else if (page != firstDialog) {
                QCOMPARE(visited, 1);
                QVERIFY(!firstDialog || !firstDialog->isVisible());
                QCOMPARE(QDir::fromNativeSeparators(input->text()), second);
                visited = 2;
                page->reject();
                poll.stop();
            }
        });
        poll.start(30);
        m_window->processURLs({ QUrl::fromLocalFile(first), QUrl::fromLocalFile(second) });
        returned = true;
        poll.stop();
        QCOMPARE(visited, 2);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QCOMPARE(APPLICATION->instances()->count(), 5);
    }

    void microsoftAccountConfigurationIsVisibleAndRefreshes()
    {
        const QString originalClient = APPLICATION->settings()->get("MSAClientIDOverride").toString();
        const bool clientConfigured = !APPLICATION->getMSAClientID().trimmed().isEmpty();
        const int accountCount = APPLICATION->accounts()->count();
        QPointer<PageDialog> settings;
        QPointer<QLineEdit> clientField;
        const auto restore = qScopeGuard([&] {
            if (clientField)
                clientField->setText(originalClient);
            APPLICATION->settings()->set("MSAClientIDOverride", originalClient);
            if (settings)
                settings->reject();
            m_window->inlineWorkspace()->closeAllPages();
        });

        m_window->resize(1280, 820);
        APPLICATION->ShowGlobalSettings(m_window, "accounts");
        QTRY_VERIFY(qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage()));
        settings = qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage());
        auto* accounts = settings->findChild<AccountListPage*>();
        QVERIFY(accounts && accounts->isVisible());
        auto* addMicrosoft = accounts->findChild<QAction*>("actionAddMicrosoft");
        auto* notice = accounts->findChild<QFrame*>("microsoftSignInNotice");
        auto* configure = accounts->findChild<QPushButton*>("configureMicrosoftSignInButton");
        QVERIFY(addMicrosoft && notice && configure);
        QTest::qWait(100);
        verifyInline(settings, 1);
        QVERIFY(addMicrosoft->isVisible());
        QCOMPARE(addMicrosoft->isEnabled(), clientConfigured);
        QCOMPARE(notice->isVisible(), !clientConfigured);
        bool buttonVisible = false;
        for (auto* button : accounts->findChildren<QToolButton*>()) {
            if (button->text() == addMicrosoft->text())
                buttonVisible = button->isVisible();
        }
        QVERIFY(buttonVisible);
        QVERIFY(m_window->grab().save(QDir(m_root).filePath(clientConfigured ? "accounts-configured.png" : "accounts-unconfigured.png")));

        if (!clientConfigured) {
            QVERIFY(configure->isVisible());
            QTest::mouseClick(configure, Qt::LeftButton);
        } else {
            QVERIFY(settings->selectPage("apis"));
        }
        auto* services = settings->findChild<APIPage*>();
        QVERIFY(services);
        QTRY_VERIFY(services->isVisible());
        QCOMPARE(m_window->inlineWorkspace()->currentPage(), settings.data());
        QCOMPARE(m_window->inlineWorkspace()->pageCount(), 1);
        clientField = services->findChild<QLineEdit*>("msaClientID");
        QVERIFY(clientField);
        if (!clientConfigured)
            QTRY_VERIFY(clientField->hasFocus());
        // A synthetic public UUID exercises configuration refresh only. Never
        // click Add Microsoft or start authentication with this fixture value.
        clientField->setText("11111111-1111-4111-8111-111111111111");
        QVERIFY(clientField->hasAcceptableInput());
        QVERIFY(services->apply());
        QVERIFY(settings->selectPage("accounts"));
        QTRY_VERIFY(addMicrosoft->isEnabled());
        QVERIFY(!notice->isVisible());
        QCOMPARE(APPLICATION->accounts()->count(), accountCount);
        QCOMPARE(m_popupPaints, 0);

        clientField->setText(originalClient);
        QVERIFY(services->apply());
        QVERIFY(settings->selectPage("apis"));
        QVERIFY(settings->selectPage("accounts"));
        QCOMPARE(addMicrosoft->isEnabled(), clientConfigured);
        QCOMPARE(notice->isVisible(), !clientConfigured);
    }

    void accountSetupWaitsForSettingsToClose()
    {
        for (bool reuse : { false, true }) {
            QPointer<QWidget> original;
            if (reuse) {
                APPLICATION->ShowGlobalSettings(m_window, "accounts");
                QTRY_VERIFY(qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage()));
                original = m_window->inlineWorkspace()->currentPage();
            }
            bool returned = false;
            bool closed = false;
            QTimer::singleShot(100, m_window, [&] {
                const auto cleanup = qScopeGuard([&] { m_window->inlineWorkspace()->closeAllPages(); });
                QVERIFY(!returned);
                auto* settings = qobject_cast<PageDialog*>(m_window->inlineWorkspace()->currentPage());
                QVERIFY(settings);
                verifyInline(settings, 1);
                if (reuse)
                    QCOMPARE(settings, original.data());
                closed = true;
                QTest::mouseClick(m_window->findChild<QToolButton*>("inlineBackButton"), Qt::LeftButton);
            });
            APPLICATION->ShowGlobalSettings(m_window, "accounts", true);
            returned = true;
            QVERIFY(closed);
            QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        }
    }

    void runningTaskLocksNavigationAndSupportsCancellation()
    {
        HeldTask task;
        ProgressDialog progress(m_window);
        progress.setSkipButton(true, "Cancel");
        bool visited = false;
        QTimer::singleShot(100, &progress, [&] {
            const auto finish = qScopeGuard([&] {
                if (task.isRunning())
                    task.complete();
            });
            QVERIFY(task.isRunning());
            task.setAbortable(false);
            verifyInline(&progress, 1);
            QVERIFY(m_window->inlineWorkspace()->navigationBlocked());
            auto* home = navigationButton(m_home, "Home");
            auto* settings = m_window->findChild<QAction*>("actionSettings");
            QVERIFY(home && settings);
            QVERIFY(!home->isEnabled());
            QTest::mouseClick(m_window->findChild<QToolButton*>("inlineBackButton"), Qt::LeftButton);
            QCOMPARE(m_window->inlineWorkspace()->currentPage(), &progress);
            QTest::keyClick(&progress, Qt::Key_Escape);
            QVERIFY(task.isRunning());
            settings->trigger();
            QCOMPARE(m_window->inlineWorkspace()->currentPage(), &progress);
            QVERIFY(!m_window->inlineWorkspace()->closeAllPages());
            QVERIFY(m_window->grab().save(QDir(m_root).filePath("inline-progress.png")));
            task.setAbortable(true);
            QTest::keyClick(&progress, Qt::Key_Escape);
            QCOMPARE(task.getState(), Task::State::AbortedByUser);
            visited = true;
        });
        QCOMPARE(progress.execWithTask(&task), int(QDialog::Rejected));
        QVERIFY(visited);
        QTRY_COMPARE(m_window->inlineWorkspace()->pageCount(), 0);
        QVERIFY(!m_window->inlineWorkspace()->navigationBlocked());
        QVERIFY(navigationButton(m_home, "Home")->isEnabled());
    }

    void destroyingWorkspaceDisconnectsRetainedPages()
    {
        QMainWindow temporaryWindow;
        auto* workspace = new InlineWorkspace(&temporaryWindow, &temporaryWindow);
        QPointer<QDialog> page = new QDialog(&temporaryWindow);
        workspace->present(page);
        QCOMPARE(workspace->pageCount(), 1);
        delete workspace;
        QVERIFY(page.isNull());
        QCOMPARE(m_window->inlineWorkspace()->pageCount(), 0);
    }

    void cleanupTestCase()
    {
        QVERIFY(m_window->inlineWorkspace()->closeAllPages());
        QCOMPARE(m_popupPaints, 0);
        qApp->removeEventFilter(this);
    }
    void applicationUsesSharedProfileInPlace()
    {
        QVERIFY(ChromaProfile::sameProfilePath(APPLICATION->dataRoot(), m_root));
        QCOMPARE(APPLICATION->instances()->count(), 5);
        QCOMPARE(APPLICATION->settings()->get("MaxMemAlloc").toInt(), 3456);
        auto instance = APPLICATION->instances()->getInstanceById("cobblemon");
        QVERIFY(instance);
        QVERIFY(ChromaProfile::sameProfilePath(instance->instanceRoot(), QDir(m_root).filePath("Prism Library/cobblemon")));
        APPLICATION->settings()->set("MaxMemAlloc", 4567);
        QSettings shared(QDir(m_root).filePath("prismlauncher.cfg"), QSettings::IniFormat);
        QCOMPARE(shared.value("MaxMemAlloc").toInt(), 4567);
        QCOMPARE(shared.value("InstanceDir").toString(), QString("Prism Library"));
        QCOMPARE(shared.value("ApplicationTheme").toString(), QString("bright"));
        APPLICATION->settings()->set("MaxMemAlloc", 3456);
    }
};

int main(int argc, char** argv)
{
    const QString root = QDir::cleanPath(
        QFileInfo(qEnvironmentVariable("CHROMA_UI_TEST_ROOT", QDir::currentPath() + "/.chroma-test/ui")).absoluteFilePath());
    if (!prepareFixture(root))
        return 1;
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    Q_INIT_RESOURCE(multimc);
    Q_INIT_RESOURCE(backgrounds);
    Q_INIT_RESOURCE(documents);
    Q_INIT_RESOURCE(prismlauncher);
    Q_INIT_RESOURCE(pe_dark);
    Q_INIT_RESOURCE(pe_light);
    Q_INIT_RESOURCE(pe_blue);
    Q_INIT_RESOURCE(pe_colored);
    Q_INIT_RESOURCE(breeze_dark);
    Q_INIT_RESOURCE(breeze_light);
    Q_INIT_RESOURCE(OSX);
    Q_INIT_RESOURCE(iOS);
    Q_INIT_RESOURCE(flat);
    Q_INIT_RESOURCE(flat_white);
    Q_INIT_RESOURCE(shaders);

    QByteArray executable = argv[0];
    QByteArray dirOption = "--dir";
    QByteArray profile = QFile::encodeName(root);
    char* appArguments[] = { executable.data(), dirOption.data(), profile.data(), nullptr };
    int appArgumentCount = 3;
    if (qEnvironmentVariableIsSet("CHROMA_UI_USE_SAVED_PROFILE")) {
        // Run a copy of this executable in an isolated fixture folder. Its
        // portable UserData directory exercises the actual startup pointer path.
        const QString executableRoot = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
        if (!executableRoot.startsWith(QFileInfo(root).absolutePath() + '/'))
            return 1;
        QString error;
        if (!ChromaProfile::saveSelectedProfile(QDir(executableRoot).filePath("UserData"), root, &error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        appArguments[1] = nullptr;
        appArgumentCount = 1;
    }
    Application app(appArgumentCount, appArguments);
    if (app.status() == Application::Failed || app.status() == Application::Succeeded)
        return 1;
#ifdef Q_OS_WIN
    // Qt's offscreen Windows plugin does not enumerate installed system fonts.
    // Load the normal Windows UI family for faithful text metrics/screenshots.
    if (QGuiApplication::platformName() == "offscreen") {
        const QDir fonts(qEnvironmentVariable("SystemRoot", "C:/Windows") + "/Fonts");
        for (const auto* file : { "segoeui.ttf", "seguisb.ttf", "segoeuib.ttf" }) {
            if (QFontDatabase::addApplicationFont(fonts.filePath(file)) < 0) {
                std::fprintf(stderr, "Could not load the Windows UI font for offscreen rendering.\n");
                return 1;
            }
        }
        QApplication::setFont(QFont("Segoe UI", 10));
    }
#endif
    // This empty fixture has no accounts. Dismiss only its startup wizard,
    // including an already embedded wizard, without beginning authentication.
    for (auto* widget : QApplication::allWidgets()) {
        if (auto* wizard = qobject_cast<QWizard*>(widget))
            wizard->reject();
    }
    app.themeManager()->applyCurrentlySelectedTheme();
    auto* window = app.showMainWindow();
    LauncherHomeTest test(window, root);
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::fprintf(stderr, "Native UI smoke timed out; an inline dialog event loop did not return.\n");
        std::exit(2);
    });
    watchdog.start(60000);
    const int result = QTest::qExec(&test, argc, argv);
    window->hide();
    return result;
}

#include "LauncherHome_test.moc"
