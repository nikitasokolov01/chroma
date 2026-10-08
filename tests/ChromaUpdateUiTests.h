// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <cstring>

#include "Application.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/skins/SkinEditorDialog.h"
#include "ui/widgets/InlineWorkspace.h"
#include "ui/widgets/LauncherHome.h"
#include "ui/widgets/UpdateNotice.h"
#include "updater/ChromaUpdater.h"

namespace ChromaUpdateUiTests {

class Reply : public QNetworkReply {
   public:
    Reply(const QNetworkRequest& request, QByteArray bytes, QObject* parent) : QNetworkReply(parent), m_bytes(std::move(bytes))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            if (m_done)
                return;
            m_available = true;
            emit readyRead();
            if (!m_done) {
                m_done = true;
                setFinished(true);
                emit downloadProgress(m_bytes.size(), m_bytes.size());
                emit finished();
            }
        });
    }
    void abort() override
    {
        if (m_done)
            return;
        m_done = true;
        setError(QNetworkReply::OperationCanceledError, "Fixture cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return (m_available ? m_bytes.size() - m_offset : 0) + QIODevice::bytesAvailable(); }

   protected:
    qint64 readData(char* destination, qint64 maximum) override
    {
        const auto count = qMin(maximum, m_available ? m_bytes.size() - m_offset : 0);
        if (count <= 0)
            return -1;
        std::memcpy(destination, m_bytes.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

   private:
    QByteArray m_bytes;
    qint64 m_offset = 0;
    bool m_available = false;
    bool m_done = false;
};

class Network : public QNetworkAccessManager {
   public:
    QList<QByteArray> responses;
    QList<QUrl> requested;

   protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        requested.append(request.url());
        return new Reply(request, responses.isEmpty() ? QByteArray() : responses.takeFirst(), this);
    }
};

inline QByteArray hash(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

inline bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

inline QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

inline QByteArray releaseList(const QString& version, const QString& notes)
{
    const QString base = "https://github.com/nikitasokolov01/chroma/releases/download/v" + version + "/";
    const auto asset = [&base](const QString& name) {
        return QJsonObject{ { "name", name }, { "browser_download_url", base + name }, { "size", 12 } };
    };
    return QJsonDocument(QJsonArray{ QJsonObject{
                             { "tag_name", "v" + version },
                             { "draft", false },
                             { "prerelease", false },
                             { "body", notes },
                             { "assets", QJsonArray{ asset("Chroma-" + version + "-Windows-x64.zip"), asset("SHA256SUMS.txt"),
                                                     asset("package-manifest.json") } } } })
        .toJson();
}

inline void backgroundNotice(MainWindow* window, const QString& root)
{
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    window->resize(1280, 820);
    QTemporaryDir fixture(QDir(root).filePath("update-notice-XXXXXX"));
    QVERIFY(fixture.isValid());
    const auto appDir = fixture.filePath("app");
    const auto dataDir = fixture.filePath("profile");
    QVERIFY(QDir().mkpath(appDir));
    QVERIFY(QDir().mkpath(dataDir));
    {
        QSettings settings(QDir(dataDir).filePath("chroma-updates.ini"), QSettings::IniFormat);
        settings.setValue("Automatic", false);
        settings.sync();
    }
    const QString notes = "## New features\n\n- **Outfit presets** save your favorite looks.\n- Palette swapping keeps your shading.\n\n"
                          "![External image](https://example.invalid/release.png)\n"
                          "![Local image](file:///C:/not-a-release-image.png)\n";
    const auto firstRelease = releaseList("99.0.0", notes);
    Network network;
    network.responses = { firstRelease };
    QImage texture(64, 64, QImage::Format_ARGB32);
    texture.fill(QColor("#86bdb5"));
    SkinModel skin(texture, SkinModel::CLASSIC);
    SkinEditorDialog editor(window, MinecraftAccountPtr(), skin);
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<SkinTextureDocument*>();
    QVERIFY(document);
    document->paintPixel(QPoint(8, 8), Qt::red, 1, SkinTextureDocument::Head, SkinTextureDocument::Base);
    const auto unsavedTexture = document->image();
    QSignalSpy editorFinished(&editor, &QDialog::finished);
    ChromaUpdater updater(window, appDir, dataDir, true, &network);
    auto* home = window->findChild<LauncherHome*>();
    QVERIFY(home);
    home->setUpdater(&updater);
    auto cleanup = qScopeGuard([&] {
        document->markSaved();
        window->inlineWorkspace()->closeAllPages();
        home->setUpdater(qobject_cast<ChromaUpdater*>(APPLICATION->updater().get()));
    });
    auto* notice = home->findChild<UpdateNotice*>("chromaUpdateNotice");
    auto* review = home->findChild<QPushButton*>("chromaUpdateWhatsNew");
    auto* later = home->findChild<QPushButton*>("chromaUpdateLater");
    QVERIFY(notice && review && later);
    QSignalSpy releaseAnnouncements(&updater, &ChromaUpdater::releaseAvailable);
    updater.checkBackgroundUpdates();
    QTRY_VERIFY(updater.hasAvailableUpdate());
    QTRY_VERIFY(notice->isVisible());
    QCOMPARE(updater.availableVersion(), QString("99.0.0"));
    QCOMPARE(releaseAnnouncements.count(), 1);
    QCOMPARE(network.requested.size(), 1);
    QCOMPARE(window->inlineWorkspace()->pageCount(), 1);
    QCOMPARE(window->inlineWorkspace()->currentPage(), static_cast<QWidget*>(&editor));
    QVERIFY(!window->findChild<QDialog*>("chromaUpdateDialog"));
    QCOMPARE(editorFinished.count(), 0);
    QVERIFY(document->isDirty());
    QCOMPARE(document->image(), unsavedTexture);
    QVERIFY(home->findChild<QLabel*>("chromaUpdateNoticeSummary")->text().contains("Outfit presets"));
    QVERIFY(home->findChild<QLabel*>("chromaUpdateNoticeSummary")->text().contains("Palette swapping"));

    window->resize(680, 640);
    QTRY_VERIFY(notice->rect().contains(QRect(review->mapTo(notice, QPoint()), review->size())));
    QTRY_VERIFY(notice->rect().contains(QRect(later->mapTo(notice, QPoint()), later->size())));
    QVERIFY(window->grab().save(QDir(root).filePath("chroma-update-notice-compact.png")));
    window->resize(1280, 820);
    review->click();
    QTRY_VERIFY(window->findChild<QDialog*>("chromaUpdateDialog"));
    QPointer<QDialog> dialog = window->findChild<QDialog*>("chromaUpdateDialog");
    QTRY_COMPARE(window->inlineWorkspace()->pageCount(), 2);
    QCOMPARE(window->inlineWorkspace()->currentPage(), static_cast<QWidget*>(dialog.data()));
    auto* browser = dialog->findChild<QTextBrowser*>("chromaUpdateNotes");
    QVERIFY(browser && browser->isReadOnly());
    QVERIFY(!browser->openExternalLinks());
    QVERIFY(!browser->openLinks());
    QVERIFY(browser->toPlainText().contains("Outfit presets"));
    QVERIFY(!browser->toPlainText().contains("**Outfit presets**"));
    QVERIFY(!browser->toPlainText().contains(QChar::ObjectReplacementCharacter));
    QVERIFY(browser->document()->resource(QTextDocument::ImageResource, QUrl("https://example.invalid/release.png")).value<QImage>().isNull());
    QVERIFY(browser->document()->resource(QTextDocument::ImageResource, QUrl("file:///C:/not-a-release-image.png")).value<QImage>().isNull());
    QCOMPARE(network.requested.size(), 1);
    QCOMPARE(document->image(), unsavedTexture);
    QVERIFY(document->isDirty());
    QCOMPARE(editorFinished.count(), 0);
    QVERIFY(window->grab().save(QDir(root).filePath("chroma-update-whats-new.png")));
    dialog->reject();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(window->inlineWorkspace()->currentPage(), static_cast<QWidget*>(&editor));
    QVERIFY(notice->isVisible());

    later->click();
    QVERIFY(!notice->isVisible());
    QVERIFY(updater.hasAvailableUpdate());
    network.responses = { firstRelease };
    updater.checkBackgroundUpdates();
    QTRY_COMPARE(network.requested.size(), 2);
    QTRY_VERIFY(updater.canCheckForUpdates());
    QVERIFY(!notice->isVisible());
    QCOMPARE(releaseAnnouncements.count(), 1);
    QCOMPARE(window->inlineWorkspace()->pageCount(), 1);

    // The menu's manual check restores a dismissed known offer even when the
    // latest request fails, retaining the valid notes and package information.
    network.responses = { "not a valid release list" };
    updater.checkForUpdates();
    QTRY_COMPARE(network.requested.size(), 3);
    QTRY_VERIFY(window->findChild<QDialog*>("chromaUpdateDialog"));
    dialog = window->findChild<QDialog*>("chromaUpdateDialog");
    QVERIFY(notice->isVisible());
    QCOMPARE(updater.availableVersion(), QString("99.0.0"));
    QCOMPARE(updater.availableNotes(), notes);
    QVERIFY(updater.status().contains("latest check failed"));
    QCOMPARE(editorFinished.count(), 0);
    QCOMPARE(document->image(), unsavedTexture);
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    QVERIFY(buttons);
    for (auto* button : buttons->buttons())
        if (buttons->buttonRole(button) == QDialogButtonBox::RejectRole) {
            button->click();
            break;
        }
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(!notice->isVisible());
    QCOMPARE(window->inlineWorkspace()->pageCount(), 1);

    network.responses = { releaseList("100.0.0", "## Features\n- A newer release should be announced.") };
    updater.checkBackgroundUpdates();
    QTRY_COMPARE(updater.availableVersion(), QString("100.0.0"));
    QVERIFY(notice->isVisible());
    QCOMPARE(releaseAnnouncements.count(), 2);
    QCOMPARE(window->inlineWorkspace()->currentPage(), static_cast<QWidget*>(&editor));
    network.responses = { "another failed check" };
    updater.checkBackgroundUpdates();
    QTRY_COMPARE(network.requested.size(), 5);
    QTRY_VERIFY(updater.canCheckForUpdates());
    QCOMPARE(updater.availableVersion(), QString("100.0.0"));
    QVERIFY(notice->isVisible());
    QCOMPARE(window->inlineWorkspace()->pageCount(), 1);
    QCOMPARE(editorFinished.count(), 0);
    QCOMPARE(document->image(), unsavedTexture);
    QVERIFY(document->isDirty());
    QVERIFY(document->canUndo());
    // A complete successful response can retire an offer that was withdrawn.
    network.responses = { "[]" };
    updater.checkBackgroundUpdates();
    QTRY_COMPARE(network.requested.size(), 6);
    QTRY_VERIFY(updater.canCheckForUpdates());
    QVERIFY(!updater.hasAvailableUpdate());
    QVERIFY(!notice->isVisible());
}
inline void verifiedDownload(MainWindow* window, const QString& root)
{
    QVERIFY(window->inlineWorkspace()->closeAllPages());
    window->resize(1280, 820);
    QTemporaryDir fixture(QDir(root).filePath("update-inline-XXXXXX"));
    QVERIFY(fixture.isValid());
    const auto appDir = fixture.filePath("app");
    const auto dataDir = fixture.filePath("profile");
    QVERIFY(QDir().mkpath(appDir));
    QVERIFY(QDir().mkpath(dataDir));
    const QByteArray oldApplication("Synthetic installed file; never executed.");
    const QByteArray profile("Synthetic profile contents; never used for authentication.");
    const auto appPath = QDir(appDir).filePath("chroma.exe");
    const auto profilePath = QDir(dataDir).filePath("prismlauncher.cfg");
    QVERIFY(write(appPath, oldApplication));
    QVERIFY(write(profilePath, profile));
    {
        QSettings settings(QDir(dataDir).filePath("chroma-updates.ini"), QSettings::IniFormat);
        settings.setValue("Automatic", false);
        settings.sync();
    }

    const QString base = "https://github.com/nikitasokolov01/chroma/releases/download/v99.0.0/";
    const QString packageName = "Chroma-99.0.0-Windows-x64.zip";
    // Opaque fixture bytes are sufficient for the download step. This test
    // deliberately never invokes the external installation helper.
    const QByteArray package("Synthetic future portable package.");
    const QByteArray manifest =
        QJsonDocument(QJsonObject{ { "version", "99.0.0" },
                                   { "portableFiles", QJsonArray{ QJsonObject{
                                                          { "path", "chroma.exe" }, { "size", 1 }, { "sha256", QString(64, 'a') } } } } })
            .toJson();
    const QByteArray checksums = hash(package) + "  " + packageName.toUtf8() + "\n" + hash(manifest) + "  package-manifest.json\n";
    const auto asset = [&base](const QString& name, qint64 size) {
        return QJsonObject{ { "name", name }, { "browser_download_url", base + name }, { "size", size } };
    };
    const QJsonObject release{ { "tag_name", "v99.0.0" },
                               { "draft", false },
                               { "prerelease", true },
                               { "body", "Synthetic updater test release." },
                               { "assets", QJsonArray{ asset(packageName, package.size()), asset("SHA256SUMS.txt", checksums.size()),
                                                       asset("package-manifest.json", manifest.size()) } } };
    Network network;
    network.responses = { QJsonDocument(QJsonArray{ release }).toJson(), checksums, manifest, package };

    QImage texture(64, 64, QImage::Format_ARGB32);
    texture.fill(QColor("#86bdb5"));
    SkinModel skin(texture, SkinModel::CLASSIC);
    SkinEditorDialog editor(window, MinecraftAccountPtr(), skin);
    window->openInlinePage(&editor, "Skin Studio");
    auto* document = editor.findChild<SkinTextureDocument*>();
    QVERIFY(document);
    document->paintPixel(QPoint(8, 8), Qt::red, 1, SkinTextureDocument::Head, SkinTextureDocument::Base);
    QVERIFY(document->isDirty());
    const auto unsavedTexture = document->image();
    QSignalSpy editorFinished(&editor, &QDialog::finished);
    QSignalSpy applicationQuit(APPLICATION, &QCoreApplication::aboutToQuit);
    ChromaUpdater updater(window, appDir, dataDir, true, &network);
    auto cleanup = qScopeGuard([&] {
        document->markSaved();
        window->inlineWorkspace()->closeAllPages();
    });
    QVERIFY(!updater.getAutomaticallyChecksForUpdates());
    updater.checkForUpdates();
    QTRY_COMPARE(network.requested.size(), 1);
    QTRY_VERIFY(window->findChild<QDialog*>("chromaUpdateDialog"));
    QPointer<QDialog> dialog = window->findChild<QDialog*>("chromaUpdateDialog");
    QTRY_COMPARE(window->inlineWorkspace()->currentPage(), static_cast<QWidget*>(dialog.data()));
    QCOMPARE(window->inlineWorkspace()->pageCount(), 2);
    QVERIFY(dialog->property("chromaInline").toBool());
    QCOMPARE(editorFinished.count(), 0);
    QVERIFY(document->isDirty());
    QCOMPARE(document->image(), unsavedTexture);
    bool hasHeading = false;
    for (auto* label : dialog->findChildren<QLabel*>())
        hasHeading |= label->text() == QString("Chroma 99.0.0 · Prerelease");
    QVERIFY(hasHeading);
    auto* action = dialog->findChild<QPushButton*>("chromaUpdateAction");
    QVERIFY(action && action->isEnabled());
    QCOMPARE(action->text(), QString("Download update"));
    // Merely checking and showing an offer must not fetch the package.
    QCOMPARE(network.requested.size(), 1);
    action->click();
    QTRY_COMPARE(action->text(), QString("Install and restart"));
    QVERIFY(action->isEnabled());
    QCOMPARE(network.requested.size(), 4);
    QCOMPARE(network.requested[0].host(), QString("api.github.com"));
    QCOMPARE(network.requested[0].path(), QString("/repos/nikitasokolov01/chroma/releases"));
    QCOMPARE(network.requested[1], QUrl(base + "SHA256SUMS.txt"));
    QCOMPARE(network.requested[2], QUrl(base + "package-manifest.json"));
    QCOMPARE(network.requested[3], QUrl(base + packageName));
    QVERIFY(network.responses.isEmpty());
    QCOMPARE(read(appPath), oldApplication);
    QCOMPARE(read(profilePath), profile);
    QCOMPARE(applicationQuit.count(), 0);
    QCOMPARE(editorFinished.count(), 0);
    QCOMPARE(document->image(), unsavedTexture);
    QVERIFY(document->isDirty());
    QVERIFY(window->grab().save(QDir(root).filePath("chroma-update-ready.png")));

    // Installation closes nested pages before asking about unsaved edits. A
    // timer or notice interaction during that nested confirmation must not
    // replace the release associated with the already verified package.
    bool cancelledInstall = false;
    bool releaseStayedLocked = false;
    QTimer::singleShot(40, window, [&] {
        auto* question = qobject_cast<QMessageBox*>(window->inlineWorkspace()->currentPage());
        if (!question || !question->button(QMessageBox::Cancel))
            return;
        updater.checkBackgroundUpdates();
        updater.checkForUpdates();
        updater.showAvailableUpdate();
        updater.dismissAvailableUpdate();
        releaseStayedLocked = !updater.canCheckForUpdates() && updater.availableVersion() == "99.0.0" &&
                              updater.shouldShowUpdateNotice() && network.requested.size() == 4;
        question->button(QMessageBox::Cancel)->click();
        cancelledInstall = true;
    });
    action->click();
    QVERIFY(cancelledInstall);
    QVERIFY(releaseStayedLocked);
    QTRY_VERIFY(dialog.isNull());
    dialog = window->findChild<QDialog*>("chromaUpdateDialog");
    QVERIFY(dialog);
    action = dialog->findChild<QPushButton*>("chromaUpdateAction");
    QVERIFY(action && action->isEnabled());
    QCOMPARE(action->text(), QString("Install and restart"));
    QCOMPARE(network.requested.size(), 4);
    QCOMPARE(editorFinished.count(), 0);
    QCOMPARE(applicationQuit.count(), 0);
    QCOMPARE(document->image(), unsavedTexture);
    QVERIFY(document->isDirty());

    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    QVERIFY(buttons);
    QAbstractButton* later = nullptr;
    for (auto* button : buttons->buttons())
        if (buttons->buttonRole(button) == QDialogButtonBox::RejectRole)
            later = button;
    QVERIFY(later);
    QCOMPARE(later->text(), QString("Later"));
    later->click();
    QTRY_COMPARE(window->inlineWorkspace()->pageCount(), 1);
    QCOMPARE(window->inlineWorkspace()->currentPage(), static_cast<QWidget*>(&editor));
    QVERIFY(editor.isEnabled());
    QVERIFY(editor.isVisible());
    QCOMPARE(editorFinished.count(), 0);
    QCOMPARE(document->image(), unsavedTexture);
    QVERIFY(document->isDirty());
    QVERIFY(document->canUndo());
    QCOMPARE(read(appPath), oldApplication);
    QCOMPARE(read(profilePath), profile);
    QCOMPARE(applicationQuit.count(), 0);
    QTRY_VERIFY(dialog.isNull());
    document->markSaved();
    editor.reject();
    QTRY_COMPARE(window->inlineWorkspace()->pageCount(), 0);
}
}  // namespace ChromaUpdateUiTests
