// SPDX-License-Identifier: GPL-3.0-only
#include "DiscordRpcClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QScopedValueRollback>
#include <QUuid>
#include <QtEndian>

#include <utility>

namespace {
constexpr quint32 Handshake = 0;
constexpr quint32 Frame = 1;
constexpr quint32 Close = 2;
constexpr quint32 Ping = 3;
constexpr quint32 Pong = 4;
constexpr quint32 MaximumPayload = 64 * 1024;
}

DiscordRpcClient::DiscordRpcClient(QString applicationId, QObject* parent)
    : DiscordRpcClient(std::move(applicationId), Options{}, parent)
{}

DiscordRpcClient::DiscordRpcClient(QString applicationId, Options options, QObject* parent)
    : QObject(parent), m_applicationId(applicationId.trimmed()), m_options(std::move(options))
{
    if (m_options.endpoints.isEmpty())
        m_options.endpoints = defaultEndpoints();
    m_options.reconnectInitialMs = qMax(1, m_options.reconnectInitialMs);
    m_options.reconnectMaximumMs = qMax(m_options.reconnectInitialMs, m_options.reconnectMaximumMs);
    m_options.handshakeTimeoutMs = qMax(1, m_options.handshakeTimeoutMs);
    m_options.commandTimeoutMs = qMax(1, m_options.commandTimeoutMs);
    m_options.activityIntervalMs = qMax(0, m_options.activityIntervalMs);
    m_options.activityRetryInitialMs = qMax(1, m_options.activityRetryInitialMs);
    m_options.activityRetryMaximumMs = qMax(m_options.activityRetryInitialMs, m_options.activityRetryMaximumMs);
    m_reconnectDelayMs = m_options.reconnectInitialMs;
    m_activityRetryDelayMs = m_options.activityRetryInitialMs;
    m_socket.setReadBufferSize(MaximumPayload + 8);
    for (auto* timer : { &m_reconnectTimer, &m_timeoutTimer, &m_activityTimer, &m_closeTimer })
        timer->setSingleShot(true);
    m_activityTimer.setTimerType(Qt::PreciseTimer);

    connect(&m_reconnectTimer, &QTimer::timeout, this, &DiscordRpcClient::connectNext);
    connect(&m_timeoutTimer, &QTimer::timeout, this, [this] { connectionFailed(Status::Error); });
    connect(&m_activityTimer, &QTimer::timeout, this, [this] { sendActivity(); });
    connect(&m_closeTimer, &QTimer::timeout, this, [this] {
        if (!m_enabled)
            resetTransport();
    });
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        if (!m_enabled || !m_attemptActive)
            return;
        const QJsonObject handshake{ { "v", 1 }, { "client_id", m_applicationId } };
        writeFrame(Handshake, QJsonDocument(handshake).toJson(QJsonDocument::Compact));
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &DiscordRpcClient::readFrames);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] { connectionFailed(); });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] { connectionFailed(); });
}

DiscordRpcClient::~DiscordRpcClient()
{
    setEnabled(false);
}

bool DiscordRpcClient::isConfigured() const
{
    if (m_applicationId.isEmpty() || m_applicationId.size() > 20)
        return false;
    for (auto c : m_applicationId) {
        if (c < u'0' || c > u'9')
            return false;
    }
    bool valid = false;
    const auto id = m_applicationId.toULongLong(&valid);
    return valid && id != 0;
}

QStringList DiscordRpcClient::defaultEndpoints()
{
    QStringList prefixes;
#ifdef Q_OS_WIN
    // QLocalSocket expands a bare name to the Windows named-pipe namespace.
    prefixes.append(QString());
#else
    for (const auto* variable : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" }) {
        const auto prefix = qEnvironmentVariable(variable);
        if (!prefix.isEmpty() && !prefixes.contains(prefix))
            prefixes.append(prefix);
    }
    if (!prefixes.contains(QStringLiteral("/tmp")))
        prefixes.append(QStringLiteral("/tmp"));
#endif
    QStringList endpoints;
    for (const auto& prefix : prefixes) {
        for (int index = 0; index < 10; ++index) {
            const auto name = QStringLiteral("discord-ipc-%1").arg(index);
            endpoints.append(prefix.isEmpty() ? name : QDir(prefix).filePath(name));
        }
    }
    return endpoints;
}

void DiscordRpcClient::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(status);
}

void DiscordRpcClient::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit connectedChanged(ready);
}

void DiscordRpcClient::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_reconnectTimer.stop();
    m_timeoutTimer.stop();
    m_activityTimer.stop();
    m_closeTimer.stop();

    if (!enabled) {
        // Privacy changes bypass both the activity throttle and any outstanding
        // reply. Disconnect as soon as Qt flushes this single complete packet.
        if (m_ready && m_socket.state() == QLocalSocket::ConnectedState) {
            const QJsonObject command{
                { "cmd", "SET_ACTIVITY" },
                { "nonce", QUuid::createUuid().toString(QUuid::WithoutBraces) },
                { "args", QJsonObject{ { "pid", QCoreApplication::applicationPid() }, { "activity", QJsonValue::Null } } }
            };
            writeFrame(Frame, QJsonDocument(command).toJson(QJsonDocument::Compact));
            m_socket.flush();
            m_socket.disconnectFromServer();
            m_closeTimer.start(200);
        } else {
            resetTransport();
        }
        m_attemptActive = false;
        setReady(false);
        setStatus(Status::Disabled);
        return;
    }

    resetTransport();
    if (!isConfigured()) {
        setStatus(Status::NotConfigured);
        return;
    }
    m_endpointIndex = 0;
    m_reconnectDelayMs = m_options.reconnectInitialMs;
    connectNext();
}

void DiscordRpcClient::setActivity(const QJsonObject& activity)
{
    m_activity = activity;
    m_hasActivity = true;
    scheduleActivity();
}

void DiscordRpcClient::clearActivity()
{
    m_activity = QJsonValue::Null;
    m_hasActivity = true;
    m_activityTimer.stop();
    sendActivity(true);
}

void DiscordRpcClient::resetTransport()
{
    QScopedValueRollback<bool> resetting(m_resetting, true);
    m_attemptActive = false;
    m_timeoutTimer.stop();
    m_activityTimer.stop();
    m_socket.abort();
    m_input.clear();
    m_pendingNonce.clear();
    m_hasSentActivity = false;
    m_lastActivityTime.invalidate();
    m_activityErrorTime.invalidate();
    m_activityRetryDelayMs = m_options.activityRetryInitialMs;
    setReady(false);
}

void DiscordRpcClient::connectNext()
{
    if (!m_enabled || !isConfigured())
        return;
    resetTransport();
    m_attemptActive = true;
    setStatus(Status::Connecting);
    m_timeoutTimer.start(m_options.handshakeTimeoutMs);
    m_socket.connectToServer(m_options.endpoints.at(m_endpointIndex), QIODevice::ReadWrite);
}

void DiscordRpcClient::connectionFailed(Status status)
{
    if (m_resetting || !m_enabled || !m_attemptActive)
        return;
    const bool wasReady = m_ready;
    resetTransport();
    setStatus(status);
    if (!m_enabled)
        return;

    int delay = 0;
    if (wasReady || ++m_endpointIndex >= m_options.endpoints.size()) {
        m_endpointIndex = 0;
        delay = m_reconnectDelayMs;
        m_reconnectDelayMs = static_cast<int>(qMin<qint64>(m_options.reconnectMaximumMs, qint64(m_reconnectDelayMs) * 2));
    }
    m_reconnectTimer.start(delay);
}

bool DiscordRpcClient::writeFrame(quint32 opcode, const QByteArray& payload)
{
    if (payload.size() > MaximumPayload || m_socket.state() != QLocalSocket::ConnectedState) {
        connectionFailed(Status::Error);
        return false;
    }
    QByteArray packet(8, Qt::Uninitialized);
    qToLittleEndian(opcode, packet.data());
    qToLittleEndian(static_cast<quint32>(payload.size()), packet.data() + 4);
    packet.append(payload);
    // The header and payload must be written together for Windows message pipes.
    if (m_socket.write(packet) != packet.size()) {
        connectionFailed(Status::Error);
        return false;
    }
    return true;
}

void DiscordRpcClient::readFrames()
{
    if (!m_enabled || !m_attemptActive)
        return;
    m_input.append(m_socket.readAll());
    while (m_input.size() >= 8) {
        const auto opcode = qFromLittleEndian<quint32>(m_input.constData());
        const auto length = qFromLittleEndian<quint32>(m_input.constData() + 4);
        if (length > MaximumPayload) {
            connectionFailed(Status::Error);
            return;
        }
        if (m_input.size() < 8 + length)
            return;
        const auto payload = m_input.mid(8, length);
        m_input.remove(0, 8 + length);
        if (!processFrame(opcode, payload))
            return;
    }
}

bool DiscordRpcClient::processFrame(quint32 opcode, const QByteArray& payload)
{
    if (opcode == Ping)
        return writeFrame(Pong, payload);
    if (opcode == Pong)
        return true;
    if (opcode != Frame) {
        connectionFailed(opcode == Close ? Status::Disconnected : Status::Error);
        return false;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        connectionFailed(Status::Error);
        return false;
    }
    const auto message = document.object();
    const auto event = message.value("evt").toString();
    if (!m_ready) {
        if (message.value("cmd") != QJsonValue("DISPATCH") || event != "READY") {
            connectionFailed(Status::Error);
            return false;
        }
        m_timeoutTimer.stop();
        m_reconnectDelayMs = m_options.reconnectInitialMs;
        setReady(true);
        setStatus(Status::Ready);
        scheduleActivity();
        return true;
    }

    // Never retain READY's user object, unsolicited events, or error messages.
    // Only the response to our own pending SET_ACTIVITY can alter its state.
    if (m_pendingNonce.isEmpty() || message.value("nonce").toString() != m_pendingNonce ||
        message.value("cmd") != QJsonValue("SET_ACTIVITY"))
        return true;
    m_pendingNonce.clear();
    m_timeoutTimer.stop();
    if (event == "ERROR") {
        if (m_activityErrorTime.isValid() && m_failedActivity == m_lastSentActivity) {
            m_activityRetryDelayMs = static_cast<int>(
                qMin<qint64>(m_options.activityRetryMaximumMs, qint64(m_activityRetryDelayMs) * 2));
        } else {
            m_activityRetryDelayMs = m_options.activityRetryInitialMs;
        }
        m_failedActivity = m_lastSentActivity;
        m_activityErrorTime.restart();
        m_hasSentActivity = false;
        setStatus(Status::Error);
    } else {
        m_activityErrorTime.invalidate();
        m_activityRetryDelayMs = m_options.activityRetryInitialMs;
        setStatus(Status::Ready);
    }
    scheduleActivity();
    return true;
}

void DiscordRpcClient::scheduleActivity()
{
    if (!m_enabled || !m_ready || !m_hasActivity || !m_pendingNonce.isEmpty())
        return;
    if (m_hasSentActivity && m_activity == m_lastSentActivity) {
        m_activityTimer.stop();
        return;
    }
    qint64 remaining = m_lastActivityTime.isValid() ? m_options.activityIntervalMs - m_lastActivityTime.elapsed() : 0;
    if (m_activityErrorTime.isValid() && m_failedActivity == m_activity)
        remaining = qMax(remaining, m_activityRetryDelayMs - m_activityErrorTime.elapsed());
    if (remaining > 0)
        m_activityTimer.start(static_cast<int>(remaining));
    else
        sendActivity();
}

void DiscordRpcClient::sendActivity(bool immediately)
{
    if (!m_enabled || !m_ready || !m_hasActivity)
        return;
    if (!immediately && (!m_pendingNonce.isEmpty() || (m_hasSentActivity && m_activity == m_lastSentActivity)))
        return;
    m_pendingNonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject command{
        { "cmd", "SET_ACTIVITY" },
        { "nonce", m_pendingNonce },
        { "args", QJsonObject{ { "pid", QCoreApplication::applicationPid() }, { "activity", m_activity } } }
    };
    if (!writeFrame(Frame, QJsonDocument(command).toJson(QJsonDocument::Compact)))
        return;
    m_lastSentActivity = m_activity;
    m_hasSentActivity = true;
    m_lastActivityTime.restart();
    m_timeoutTimer.start(m_options.commandTimeoutMs);
}
