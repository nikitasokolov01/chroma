// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

// A small, unauthenticated Rich Presence transport. It only talks to the locally
// running Discord desktop client; account tokens and user data are never needed.
class DiscordRpcClient : public QObject {
    Q_OBJECT

   public:
    enum class Status { Disabled, NotConfigured, Connecting, Ready, Disconnected, Error };
    Q_ENUM(Status)

    struct Options {
        // Empty selects Discord's standard local endpoints. Tests supply their
        // own server name so fixture activity can never reach a real account.
        QStringList endpoints;
        int reconnectInitialMs = 1000;
        int reconnectMaximumMs = 60000;
        int handshakeTimeoutMs = 10000;
        int commandTimeoutMs = 10000;
        int activityIntervalMs = 15000;
        int activityRetryInitialMs = 15000;
        int activityRetryMaximumMs = 60000;
    };

    explicit DiscordRpcClient(QString applicationId, QObject* parent = nullptr);
    DiscordRpcClient(QString applicationId, Options options, QObject* parent = nullptr);
    ~DiscordRpcClient() override;

    bool isConfigured() const;
    bool isEnabled() const { return m_enabled; }
    bool isConnected() const { return m_ready; }
    Status status() const { return m_status; }

    void setEnabled(bool enabled);
    void setActivity(const QJsonObject& activity);
    void clearActivity();

   signals:
    void statusChanged(DiscordRpcClient::Status status);
    void connectedChanged(bool connected);

   private:
    static QStringList defaultEndpoints();
    void setStatus(Status status);
    void setReady(bool ready);
    void connectNext();
    void connectionFailed(Status status = Status::Disconnected);
    void resetTransport();
    void readFrames();
    bool processFrame(quint32 opcode, const QByteArray& payload);
    bool writeFrame(quint32 opcode, const QByteArray& payload);
    void scheduleActivity();
    void sendActivity(bool immediately = false);

    QString m_applicationId;
    Options m_options;
    QLocalSocket m_socket;
    QTimer m_reconnectTimer;
    QTimer m_timeoutTimer;
    QTimer m_activityTimer;
    QTimer m_closeTimer;
    QElapsedTimer m_lastActivityTime;
    QElapsedTimer m_activityErrorTime;
    QByteArray m_input;
    QJsonValue m_activity = QJsonValue::Null;
    QJsonValue m_lastSentActivity;
    QJsonValue m_failedActivity;
    QString m_pendingNonce;
    Status m_status = Status::Disabled;
    int m_endpointIndex = 0;
    int m_reconnectDelayMs = 1000;
    int m_activityRetryDelayMs = 15000;
    bool m_enabled = false;
    bool m_ready = false;
    bool m_hasActivity = false;
    bool m_hasSentActivity = false;
    bool m_attemptActive = false;
    bool m_resetting = false;
};
