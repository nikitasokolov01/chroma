// SPDX-License-Identifier: GPL-3.0-only
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>
#include "updater/ChromaUpdater.h"

struct UpdateResponse {
    QByteArray body;
    int status = 200;
    QUrl redirect;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

class UpdateReply : public QNetworkReply {
   public:
    UpdateReply(const QNetworkRequest& request, UpdateResponse response, QObject* parent) : QNetworkReply(parent), m_response(response)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        if (!response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            if (m_finished)
                return;
            m_available = true;
            if (m_response.error != QNetworkReply::NoError)
                setError(m_response.error, "fixture network failure");
            emit readyRead();
            if (!m_finished) {
                m_finished = true;
                setFinished(true);
                emit finished();
            }
        });
    }
    void abort() override
    {
        if (m_finished)
            return;
        m_finished = true;
        setError(QNetworkReply::OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return (m_available ? m_response.body.size() - m_offset : 0) + QIODevice::bytesAvailable(); }

   protected:
    qint64 readData(char* data, qint64 maximum) override
    {
        const auto count = qMin(maximum, m_available ? m_response.body.size() - m_offset : 0);
        if (count <= 0)
            return -1;
        std::memcpy(data, m_response.body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

   private:
    UpdateResponse m_response;
    qint64 m_offset = 0;
    bool m_available = false, m_finished = false;
};

class UpdateNetwork : public QNetworkAccessManager {
   public:
    QList<UpdateResponse> responses;
    QList<QNetworkRequest> requests;

   protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        requests.append(request);
        if (responses.isEmpty())
            return new UpdateReply(request, { {}, 500, {}, QNetworkReply::UnknownNetworkError }, this);
        return new UpdateReply(request, responses.takeFirst(), this);
    }
};

class ChromaUpdateTest : public QObject {
    Q_OBJECT
   private:
    const QUrl m_url{ "https://github.com/nikitasokolov01/chroma/releases/download/v0.3.0/Chroma-0.3.0-Windows-x64.zip" };
    static QByteArray hash(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
    static QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
   private slots:
    void verifiedFileBecomesVisibleOnlyOnSuccess()
    {
        QTemporaryDir stage;
        QVERIFY(stage.isValid());
        const QString path = stage.filePath("update.zip");
        const QByteArray package(400000, 'x');
        UpdateNetwork network;
        network.responses = { { package } };
        ChromaUpdateTransfer task(&network);
        QSignalSpy success(&task, &ChromaUpdateTransfer::succeeded), failure(&task, &ChromaUpdateTransfer::failed);
        task.start(m_url, package.size(), package.size(), hash(package), path);
        QVERIFY(!QFile::exists(path));
        QTRY_COMPARE(success.count(), 1);
        QCOMPARE(failure.count(), 0);
        QCOMPARE(read(path), package);
        QCOMPARE(network.requests.first().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 int(QNetworkRequest::ManualRedirectPolicy));
    }
    void damagedDownloadsPreserveExistingFile_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<qint64>("limit");
        QTest::addColumn<qint64>("expectedSize");
        QTest::addColumn<QByteArray>("expectedHash");
        QTest::newRow("checksum mismatch") << QByteArray("bad") << qint64(30) << qint64(3) << hash("new");
        QTest::newRow("truncated") << QByteArray("ne") << qint64(30) << qint64(3) << hash("new");
        QTest::newRow("too large") << QByteArray("new extra") << qint64(3) << qint64(-1) << QByteArray();
        QTest::newRow("incorrect declared size") << QByteArray("new extra") << qint64(30) << qint64(3) << QByteArray();
    }
    void damagedDownloadsPreserveExistingFile()
    {
        QFETCH(QByteArray, body);
        QFETCH(qint64, limit);
        QFETCH(qint64, expectedSize);
        QFETCH(QByteArray, expectedHash);
        QTemporaryDir stage;
        const QString path = stage.filePath("update.zip");
        QFile old(path);
        QVERIFY(old.open(QIODevice::WriteOnly));
        QCOMPARE(old.write("existing"), qint64(8));
        old.close();
        UpdateNetwork network;
        network.responses = { { body } };
        ChromaUpdateTransfer task(&network);
        QSignalSpy success(&task, &ChromaUpdateTransfer::succeeded), failure(&task, &ChromaUpdateTransfer::failed);
        task.start(m_url, limit, expectedSize, expectedHash, path);
        QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(success.count(), 0);
        QCOMPARE(read(path), QByteArray("existing"));
    }
    void redirectsRemainOnTrustedHttpsHosts()
    {
        UpdateNetwork network;
        const QByteArray bytes("verified package");
        network.responses = { { "redirect body must be ignored", 302,
                                QUrl("https://release-assets.githubusercontent.com/github-production-release-asset/test") },
                              { bytes } };
        ChromaUpdateTransfer task(&network);
        QSignalSpy success(&task, &ChromaUpdateTransfer::succeeded), failure(&task, &ChromaUpdateTransfer::failed);
        task.start(m_url, bytes.size(), bytes.size(), hash(bytes));
        QTRY_COMPARE(success.count(), 1);
        QCOMPARE(failure.count(), 0);
        QCOMPARE(success.first().first().toByteArray(), bytes);
        QCOMPARE(network.requests.size(), 2);
    }
    void unsafeAddresses_data()
    {
        QTest::addColumn<QUrl>("destination");
        QTest::newRow("http") << QUrl("http://github.com/file");
        QTest::newRow("foreign host") << QUrl("https://example.com/update.zip");
        QTest::newRow("local file") << QUrl("file:///C:/Windows/win.ini");
        QTest::newRow("embedded user") << QUrl("https://user@github.com/file");
        QTest::newRow("alternate port") << QUrl("https://github.com:8443/file");
    }
    void unsafeAddresses()
    {
        QFETCH(QUrl, destination);
        UpdateNetwork network;
        network.responses = { { {}, 302, destination } };
        ChromaUpdateTransfer task(&network);
        QSignalSpy success(&task, &ChromaUpdateTransfer::succeeded), failure(&task, &ChromaUpdateTransfer::failed);
        task.start(m_url, 20);
        QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(success.count(), 0);
        QCOMPARE(network.requests.size(), 1);
        ChromaUpdateTransfer initial(&network);
        QSignalSpy rejected(&initial, &ChromaUpdateTransfer::failed);
        initial.start(destination, 20);
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(network.requests.size(), 1);
    }
    void apiRedirectCannotLeaveReleaseEndpoint()
    {
        UpdateNetwork network;
        network.responses = { { {}, 302, QUrl("https://github.com/other") } };
        ChromaUpdateTransfer task(&network);
        QSignalSpy failure(&task, &ChromaUpdateTransfer::failed);
        task.start(QUrl("https://api.github.com/repos/nikitasokolov01/chroma/releases?page=1"), 1000);
        QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests.first().rawHeader("X-GitHub-Api-Version"), QByteArray("2022-11-28"));
    }
    void cancellationAndTeardownIgnoreQueuedCompletion()
    {
        QTemporaryDir stage;
        UpdateNetwork network;
        network.responses = { { "new" }, { "new" } };
        const auto path = stage.filePath("cancelled.zip");
        ChromaUpdateTransfer task(&network);
        QSignalSpy success(&task, &ChromaUpdateTransfer::succeeded), failure(&task, &ChromaUpdateTransfer::failed);
        task.start(m_url, 3, 3, hash("new"), path);
        task.cancel();
        QCoreApplication::processEvents();
        QCOMPARE(success.count(), 0);
        QCOMPARE(failure.count(), 1);
        QVERIFY(!QFile::exists(path));
        QVERIFY(QDir(stage.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        auto* destroyed = new ChromaUpdateTransfer(&network);
        destroyed->start(m_url, 3, 3, hash("new"), stage.filePath("destroyed.zip"));
        delete destroyed;
        QCoreApplication::processEvents();
        QVERIFY(!QFile::exists(stage.filePath("destroyed.zip")));
    }
    void networkFailureNeverCommits()
    {
        QTemporaryDir stage;
        UpdateNetwork network;
        network.responses = { { "partial", 200, {}, QNetworkReply::RemoteHostClosedError } };
        ChromaUpdateTransfer task(&network);
        QSignalSpy success(&task, &ChromaUpdateTransfer::succeeded), failure(&task, &ChromaUpdateTransfer::failed);
        task.start(m_url, 100, -1, {}, stage.filePath("update.zip"));
        QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(success.count(), 0);
        QVERIFY(!QFile::exists(stage.filePath("update.zip")));
    }
    void completeReleaseDownloadRequiresExplicitActions()
    {
        QTemporaryDir installation, profile;
        QVERIFY(installation.isValid() && profile.isValid());
        const QString version = "999.999.999-rc.1";
        const QString packageName = "Chroma-" + version + "-Windows-x64.zip";
        const QByteArray package("fixture package; never executed");
        const auto manifest =
            QJsonDocument(QJsonObject{ { "version", version },
                                       { "portableFiles", QJsonArray{ QJsonObject{ { "path", "chroma.exe" },
                                                                                   { "size", 3 },
                                                                                   { "sha256", QString::fromLatin1(hash("exe")) } } } } })
                .toJson();
        const QByteArray sums = hash(package) + "  " + packageName.toUtf8() + "\n" + hash(manifest) + "  package-manifest.json\n";
        const auto asset = [&version](const QString& name, qint64 size) {
            return QJsonObject{ { "name", name },
                                { "size", size },
                                { "browser_download_url",
                                  "https://github.com/nikitasokolov01/chroma/releases/download/v" + version + "/" + name } };
        };
        const auto releases =
            QJsonDocument(
                QJsonArray{ QJsonObject{ { "tag_name", "v" + version },
                                         { "draft", false },
                                         { "prerelease", true },
                                         { "body", "Fixture release notes" },
                                         { "assets", QJsonArray{ asset(packageName, package.size()), asset("SHA256SUMS.txt", sums.size()),
                                                                 asset("package-manifest.json", manifest.size()) } } } })
                .toJson();
        UpdateNetwork network;
        network.responses = { { releases }, { sums }, { manifest }, { package } };
        QWidget window;
        ChromaUpdater updater(&window, installation.path(), profile.path(), true, &network);
        updater.setAutomaticallyChecksForUpdates(false);
        QVERIFY(updater.getBetaAllowed());
        QSignalSpy status(&updater, &ChromaUpdater::statusChanged);
        updater.checkForUpdates();
        QTRY_VERIFY(window.findChild<QDialog*>("chromaUpdateDialog"));
        auto* dialog = window.findChild<QDialog*>("chromaUpdateDialog");
        auto* action = dialog->findChild<QPushButton*>("chromaUpdateAction");
        QVERIFY(action);
        QCOMPARE(action->text(), QString("Download update"));
        QCOMPARE(network.requests.size(), 1);
        QTest::mouseClick(action, Qt::LeftButton);
        QTRY_COMPARE(action->text(), QString("Install and restart"));
        QCOMPARE(network.requests.size(), 4);
        QVERIFY(updater.status().contains("verified"));
        QVERIFY(!QFile::exists(installation.filePath("chroma.exe")));
        dialog->reject();  // Discard staging without executing the helper.
        QVERIFY(updater.canCheckForUpdates());
        updater.setBetaAllowed(false);
        {
            ChromaUpdater persisted(&window, installation.path(), profile.path(), true, &network);
            QVERIFY(!persisted.getAutomaticallyChecksForUpdates());
            QVERIFY(!persisted.getBetaAllowed());
        }
    }
};

QTEST_MAIN(ChromaUpdateTest)
#include "ChromaUpdate_test.moc"
