// SPDX-License-Identifier: GPL-3.0-only
#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocalServer>
#include <QPointer>
#include <QTest>
#include <QUuid>
#include <QtEndian>

#include "discord/DiscordRpcClient.h"

namespace {
struct Packet {
    quint32 opcode;
    QByteArray body;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

QByteArray packet(quint32 opcode, const QByteArray& body)
{
    QByteArray result(8, Qt::Uninitialized);
    qToLittleEndian(opcode, result.data());
    qToLittleEndian(static_cast<quint32>(body.size()), result.data() + 4);
    result.append(body);
    return result;
}

class FakeDiscord : public QObject {
   public:
    FakeDiscord()
    {
        name = "chroma-rpc-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        listening = server.listen(name);
        connect(&server, &QLocalServer::newConnection, this, [this] {
            peer = server.nextPendingConnection();
            input.clear();
            ++connections;
            connect(peer, &QLocalSocket::readyRead, this, [this, socket = peer] {
                if (socket != peer)
                    return;
                input.append(socket->readAll());
                while (input.size() >= 8) {
                    const auto size = qFromLittleEndian<quint32>(input.constData() + 4);
                    if (input.size() < size + 8)
                        return;
                    received.append(Packet{ qFromLittleEndian<quint32>(input.constData()), input.mid(8, size) });
                    input.remove(0, size + 8);
                }
            });
        });
    }

    DiscordRpcClient::Options options(int interval = 80) const
    {
        DiscordRpcClient::Options options;
        options.endpoints = { name };
        options.reconnectInitialMs = 30;
        options.reconnectMaximumMs = 100;
        options.handshakeTimeoutMs = 1000;
        options.commandTimeoutMs = 1000;
        options.activityIntervalMs = interval;
        return options;
    }

    void send(quint32 opcode, const QByteArray& body)
    {
        peer->write(packet(opcode, body));
        peer->flush();
    }
    void sendJson(const QJsonObject& object) { send(1, QJsonDocument(object).toJson(QJsonDocument::Compact)); }
    void ready() { sendJson(QJsonObject{ { "cmd", "DISPATCH" }, { "evt", "READY" }, { "data", QJsonObject{} } }); }
    void reply(const Packet& request, bool error = false, const QString& nonceOverride = {})
    {
        const auto nonce = nonceOverride.isEmpty() ? request.json().value("nonce").toString() : nonceOverride;
        sendJson(QJsonObject{ { "cmd", "SET_ACTIVITY" }, { "nonce", nonce },
                             { "evt", error ? QJsonValue("ERROR") : QJsonValue(QJsonValue::Null) },
                             { "data", error ? QJsonValue(QJsonObject{ { "code", 4000 } }) : QJsonValue(QJsonValue::Null) } });
    }

    QString name;
    bool listening;
    int connections = 0;
    QLocalServer server;
    QPointer<QLocalSocket> peer;
    QByteArray input;
    QList<Packet> received;
};

QJsonObject activity(const QString& details)
{
    return QJsonObject{ { "type", 0 }, { "details", details } };
}
}

class DiscordRpcClientTest : public QObject {
    Q_OBJECT
   private slots:
    void missingConfigurationNeverConnects()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        for (const auto& id : { QString(), QString("not-an-id"), QString("0"), QString("18446744073709551616") }) {
            DiscordRpcClient client(id, discord.options());
            QVERIFY(!client.isConfigured());
            client.setActivity(activity("Browsing for modpacks"));
            client.setEnabled(true);
            QCOMPARE(client.status(), DiscordRpcClient::Status::NotConfigured);
            QTest::qWait(5);
            QCOMPARE(discord.connections, 0);
        }
    }

    void handshakeAndFragmentedReady()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        DiscordRpcClient client("123456789012345678", discord.options());
        bool readySignalHasConnection = false;
        connect(&client, &DiscordRpcClient::statusChanged, this, [&](DiscordRpcClient::Status status) {
            if (status == DiscordRpcClient::Status::Ready)
                readySignalHasConnection = client.isConnected();
        });
        const auto browsing = activity("Browsing for modpacks");
        client.setActivity(browsing);
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        QCOMPARE(discord.received[0].opcode, quint32(0));
        QCOMPARE(discord.received[0].json(), (QJsonObject{ { "v", 1 }, { "client_id", "123456789012345678" } }));
        QVERIFY(!client.isConnected());

        const auto ready = packet(1, R"({"cmd":"DISPATCH","evt":"READY","data":{"user":{"id":"fixture-only"}}})");
        discord.peer->write(ready.left(5));
        discord.peer->flush();
        QTest::qWait(10);
        QVERIFY(!client.isConnected());
        discord.peer->write(ready.mid(5, 8));
        discord.peer->flush();
        QTest::qWait(10);
        QVERIFY(!client.isConnected());
        discord.peer->write(ready.mid(13));
        discord.peer->flush();
        QTRY_COMPARE(discord.received.size(), 2);
        QVERIFY(client.isConnected());
        QCOMPARE(client.status(), DiscordRpcClient::Status::Ready);
        QVERIFY(readySignalHasConnection);
        const auto command = discord.received[1].json();
        QCOMPARE(command.value("cmd").toString(), QString("SET_ACTIVITY"));
        QCOMPARE(command.value("args").toObject().value("pid").toInteger(), QCoreApplication::applicationPid());
        QCOMPARE(command.value("args").toObject().value("activity").toObject(), browsing);
        QVERIFY(!command.value("nonce").toString().isEmpty());
    }

    void pingPongPreservesBinaryPayload()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        DiscordRpcClient client("123456789012345678", discord.options());
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        const QByteArray ping("ping\0data", 9);
        discord.send(3, ping);
        QTRY_COMPARE(discord.received.size(), 2);
        QCOMPARE(discord.received[1].opcode, quint32(4));
        QCOMPARE(discord.received[1].body, ping);
        discord.ready();
        QTRY_VERIFY(client.isConnected());
    }

    void coalescesChangesAndDoesNotRepeatIdenticalActivity()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        DiscordRpcClient client("123456789012345678", discord.options(180));
        client.setActivity(activity("Browsing for modpacks"));
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 2);
        discord.reply(discord.received.last());
        client.setActivity(activity("Pack one"));
        client.setActivity(activity("Pack two"));
        QTest::qWait(30);
        QCOMPARE(discord.received.size(), 2);
        QTRY_COMPARE(discord.received.size(), 3);
        QCOMPARE(discord.received.last().json().value("args").toObject().value("activity").toObject(), activity("Pack two"));
        discord.reply(discord.received.last());
        client.setActivity(activity("Pack two"));
        QTest::qWait(220);
        QCOMPARE(discord.received.size(), 3);
    }

    void disableClearsImmediatelyAndStopsReconnecting()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        DiscordRpcClient client("123456789012345678", discord.options(15000));
        client.setActivity(activity("Active pack"));
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 2);
        // The first request is deliberately still awaiting its reply.
        client.setEnabled(false);
        QCOMPARE(client.status(), DiscordRpcClient::Status::Disabled);
        QVERIFY(!client.isConnected());
        QTRY_COMPARE(discord.received.size(), 3);
        QVERIFY(discord.received.last().json().value("args").toObject().value("activity").isNull());
        QTRY_COMPARE(discord.peer->state(), QLocalSocket::UnconnectedState);
        client.setActivity(activity("Private pack"));
        QTest::qWait(150);
        QCOMPARE(discord.connections, 1);
        QCOMPARE(discord.received.size(), 3);
    }

    void clearBypassesThrottleAndIgnoresPreviousReply()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        DiscordRpcClient client("123456789012345678", discord.options(15000));
        client.setActivity(activity("Active pack"));
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 2);
        const auto previous = discord.received.last();
        client.clearActivity();
        QTRY_COMPARE(discord.received.size(), 3);
        QVERIFY(discord.received.last().json().value("args").toObject().value("activity").isNull());
        discord.reply(previous, true);
        QTest::qWait(10);
        QCOMPARE(client.status(), DiscordRpcClient::Status::Ready);
        discord.reply(discord.received.last());
        QTest::qWait(10);
        QVERIFY(client.isConnected());
    }

    void reconnectRestoresLatestActivity()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        DiscordRpcClient client("123456789012345678", discord.options());
        client.setActivity(activity("Old pack"));
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 2);
        discord.peer->abort();
        client.setActivity(activity("Latest pack"));
        QTRY_COMPARE(discord.connections, 2);
        QTRY_COMPARE(discord.received.size(), 3);
        QCOMPARE(discord.received.last().opcode, quint32(0));
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 4);
        QCOMPARE(discord.received.last().json().value("args").toObject().value("activity").toObject(), activity("Latest pack"));
    }

    void rejectedActivityRetriesWithBackoffAndIgnoresWrongNonce()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        auto options = discord.options(0);
        options.activityRetryInitialMs = 180;
        options.activityRetryMaximumMs = 360;
        DiscordRpcClient client("123456789012345678", options);
        client.setActivity(activity("Rejected pack"));
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 2);
        discord.reply(discord.received.last(), true, "unrelated-nonce");
        QTest::qWait(10);
        QCOMPARE(client.status(), DiscordRpcClient::Status::Ready);
        discord.reply(discord.received.last(), true);
        QTRY_COMPARE(client.status(), DiscordRpcClient::Status::Error);
        QTest::qWait(30);
        QCOMPARE(discord.received.size(), 2);
        QTRY_COMPARE(discord.received.size(), 3);
        QCOMPARE(discord.received.last().json().value("args").toObject().value("activity").toObject(), activity("Rejected pack"));
        discord.reply(discord.received.last(), true);
        QTest::qWait(210);
        QCOMPARE(discord.received.size(), 3);
        // A new payload is not held back by an error for the old one.
        client.setActivity(activity("Valid pack"));
        QTRY_COMPARE(discord.received.size(), 4);
        discord.reply(discord.received.last());
        QTRY_COMPARE(client.status(), DiscordRpcClient::Status::Ready);
    }

    void malformedFramesDisconnect_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid-json") << packet(1, "not json");
        QTest::newRow("array-json") << packet(1, "[]");
        QTest::newRow("unknown-opcode") << packet(99, "{}");
        QTest::newRow("activity-before-ready") << packet(1, R"({"cmd":"SET_ACTIVITY","nonce":"unknown"})");
        QByteArray oversized(8, Qt::Uninitialized);
        qToLittleEndian(quint32(1), oversized.data());
        qToLittleEndian(quint32(65537), oversized.data() + 4);
        QTest::newRow("oversized-payload") << oversized;
    }

    void malformedFramesDisconnect()
    {
        QFETCH(QByteArray, bytes);
        FakeDiscord discord;
        QVERIFY(discord.listening);
        auto options = discord.options();
        options.reconnectInitialMs = 1000;
        DiscordRpcClient client("123456789012345678", options);
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        discord.peer->write(bytes);
        discord.peer->flush();
        QTRY_COMPARE(client.status(), DiscordRpcClient::Status::Error);
        QVERIFY(!client.isConnected());
        QTRY_COMPARE(discord.peer->state(), QLocalSocket::UnconnectedState);
    }

    void handshakeTimeoutReconnects()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        auto options = discord.options();
        options.handshakeTimeoutMs = 150;
        DiscordRpcClient client("123456789012345678", options);
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        // A listening endpoint that never sends READY cannot stall the client.
        QTRY_VERIFY(discord.connections >= 2);
        client.setEnabled(false);
    }

    void unansweredCommandReconnects()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        auto options = discord.options();
        options.commandTimeoutMs = 150;
        DiscordRpcClient client("123456789012345678", options);
        client.setActivity(activity("Pack"));
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 2);
        QTRY_COMPARE(discord.connections, 2);
        QTRY_COMPARE(discord.received.size(), 3);
        discord.ready();
        QTRY_COMPARE(discord.received.size(), 4);
        discord.reply(discord.received.last());
        QTRY_VERIFY(client.isConnected());
    }

    void searchesEndpointsWithoutConnectingToRealDiscord()
    {
        FakeDiscord discord;
        QVERIFY(discord.listening);
        auto options = discord.options();
        options.endpoints.prepend("missing-" + discord.name);
        DiscordRpcClient client("123456789012345678", options);
        client.setEnabled(true);
        QTRY_COMPARE(discord.received.size(), 1);
        QCOMPARE(discord.connections, 1);
        discord.ready();
        QTRY_VERIFY(client.isConnected());
    }
};

QTEST_GUILESS_MAIN(DiscordRpcClientTest)
#include "DiscordRpcClient_test.moc"
