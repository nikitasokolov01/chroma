// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QCryptographicHash>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <memory>
#include "ChromaRelease.h"
#include "ExternalUpdater.h"

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
class QTemporaryDir;
class QDialog;
class QLabel;
class QPushButton;
class QProgressBar;

// A bounded, cancellable HTTPS transfer. Files become visible only after their size and SHA-256 match.
class ChromaUpdateTransfer : public QObject {
    Q_OBJECT
   public:
    ChromaUpdateTransfer(QNetworkAccessManager* network, QObject* parent = nullptr);
    ~ChromaUpdateTransfer() override;
    void start(const QUrl& url, qint64 maximum, qint64 expectedSize = -1, const QByteArray& sha256 = {}, const QString& path = {});
    void cancel();
   signals:
    void succeeded(QByteArray data);
    void failed(QString reason);
    void progress(qint64 received, qint64 total);

   private:
    void request(const QUrl& url);
    void readAvailable();
    void fail(const QString& reason);
    QPointer<QNetworkAccessManager> m_network;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QSaveFile> m_file;
    QCryptographicHash m_hash{ QCryptographicHash::Sha256 };
    QByteArray m_data, m_expectedHash;
    qint64 m_maximum = 0, m_expectedSize = -1, m_received = 0;
    int m_redirects = 0;
    bool m_active = false, m_api = false;
};

class ChromaUpdater : public ExternalUpdater {
    Q_OBJECT
   public:
    ChromaUpdater(QWidget* parent, const QString& appDir, const QString& dataDir, bool portable, QNetworkAccessManager* network);
    ~ChromaUpdater() override;
    void checkForUpdates() override;
    bool getAutomaticallyChecksForUpdates() override;
    double getUpdateCheckInterval() override;
    bool getBetaAllowed() override;
    void setAutomaticallyChecksForUpdates(bool check) override;
    void setUpdateCheckInterval(double seconds) override;
    void setBetaAllowed(bool allowed) override;
    QString status() const { return m_status; }
    bool canCheckForUpdates() const { return !m_busy; }
    void setWindow(QWidget* window);
   signals:
    void statusChanged(QString status);

   private:
    void check(bool manual);
    void fetchPage(int page);
    void finishCheck();
    void schedule(bool startup = false);
    void setStatus(const QString& text);
    void fail(const QString& reason);
    void offer();
    void download();
    void downloadManifest();
    void downloadPackage();
    void install();
    void cancelTransfer();
    ChromaUpdateTransfer* transfer();
    QPointer<QWidget> m_window;
    QPointer<QNetworkAccessManager> m_network;
    QSettings m_settings;
    QTimer m_timer;
    QString m_appDir, m_dataDir, m_status;
    bool m_portable, m_manual = false, m_busy = false, m_ready = false, m_keepStage = false;
    QJsonArray m_releases;
    std::optional<ChromaUpdate::Release> m_release;
    QPointer<ChromaUpdateTransfer> m_transfer;
    std::unique_ptr<QTemporaryDir> m_stage;
    QByteArray m_packageHash, m_manifestHash;
    QPointer<QDialog> m_dialog;
    QPointer<QLabel> m_message;
    QPointer<QProgressBar> m_progress;
    QPointer<QPushButton> m_action;
};
