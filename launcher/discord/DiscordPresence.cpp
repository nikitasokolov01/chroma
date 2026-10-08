// SPDX-License-Identifier: GPL-3.0-only
#include "DiscordPresence.h"

#include <QDateTime>
#include "DiscordArtwork.h"
#include "DiscordRpcClient.h"
#include "settings/Setting.h"

namespace {
const QString chromaArtwork = QStringLiteral("https://raw.githubusercontent.com/nikitasokolov01/chroma/v1.0.0/program_info/chroma_256.png");

QString activityTitle(const QString& name)
{
    QString title = name.simplified();
    if (title.isEmpty())
        title = QStringLiteral("Minecraft");
    if (title.size() == 1)
        title = QStringLiteral("Playing %1").arg(title);
    // Discord's text fields have a 128-byte limit. Keep Unicode code points
    // intact, including surrogate pairs, when an instance has a long name.
    while (title.toUtf8().size() > 128) {
        const bool pair = title.back().isLowSurrogate() && title.size() > 1 && title.at(title.size() - 2).isHighSurrogate();
        title.chop(pair ? 2 : 1);
    }
    return title;
}
}  // namespace

DiscordPresence::DiscordPresence(SettingsObjectPtr settings, const QString& applicationId, QObject* parent)
    : QObject(parent)
    , m_settings(std::move(settings))
    , m_client(new DiscordRpcClient(applicationId, this))
    , m_artwork(new DiscordArtworkResolver(this))
{
    connect(m_client, &DiscordRpcClient::statusChanged, this, [this] {
        emit statusChanged();
        if (m_client->isConnected())
            refresh();
    });
    const auto setting = m_settings->getSetting("ChromaDiscordPresence");
    connect(setting.get(), &Setting::SettingChanged, this, [this] { applySetting(); });
    connect(setting.get(), &Setting::settingReset, this, [this] { applySetting(); });
    refresh();
    applySetting();
}

DiscordPresence::~DiscordPresence()
{
    shutdown();
}

void DiscordPresence::observeInstance(const InstancePtr& instance)
{
    if (!instance || m_observed.contains(instance.get()))
        return;
    auto* raw = instance.get();
    m_observed.insert(raw);
    std::weak_ptr<BaseInstance> weak = instance;
    connect(raw, &BaseInstance::minecraftRunningChanged, this, [this, weak] {
        if (auto locked = weak.lock())
            updateRunning(locked);
    });
    connect(raw, &BaseInstance::propertiesChanged, this, [this] { refresh(); });
    connect(raw, &QObject::destroyed, this, [this, raw] {
        m_observed.remove(raw);
        m_sessions.remove(raw);
        refresh();
    });
    updateRunning(instance);
}

void DiscordPresence::updateRunning(const InstancePtr& instance)
{
    if (instance->isMinecraftRunning()) {
        if (!m_sessions.contains(instance.get()))
            m_sessions.insert(instance.get(), { instance, QDateTime::currentSecsSinceEpoch(), ++m_order });
    } else {
        m_sessions.remove(instance.get());
    }
    refresh();
}

void DiscordPresence::applySetting()
{
    if (m_stopped)
        return;
    m_enabled = m_settings->get("ChromaDiscordPresence").toBool();
    if (!m_enabled)
        m_artwork->cancel();
    refresh();
    m_client->setEnabled(m_enabled);
    emit statusChanged();
}

void DiscordPresence::refresh()
{
    if (m_stopped)
        return;
    InstancePtr active;
    Session latest{};
    for (const auto& session : std::as_const(m_sessions)) {
        if (session.order > latest.order) {
            if (auto instance = session.instance.lock()) {
                active = std::move(instance);
                latest = session;
            }
        }
    }
    QJsonObject activity{ { "type", 0 }, { "status_display_type", 0 } };
    QJsonObject assets;
    if (active) {
        const auto title = activityTitle(active->name());
        activity.insert("details", title);
        activity.insert("timestamps", QJsonObject{ { "start", latest.started } });
        const auto artwork = active->discordArtworkUrl();
        assets.insert("large_image", artwork.isEmpty() ? chromaArtwork : artwork);
        assets.insert("large_text", title);
        if (!artwork.isEmpty()) {
            assets.insert("small_image", chromaArtwork);
            assets.insert("small_text", "Chroma");
        }
    } else {
        activity.insert("details", "Browsing for modpacks");
        assets.insert("large_image", chromaArtwork);
        assets.insert("large_text", "Chroma");
    }
    activity.insert("assets", assets);
    if (m_activity != activity) {
        m_activity = activity;
        m_client->setActivity(activity);
        emit activityChanged(activity);
    }
    if (active && m_enabled && m_client->isConnected())
        m_artwork->resolve(active);
}

QString DiscordPresence::statusText() const
{
    if (!m_enabled || m_stopped)
        return tr("Activity sharing is off.");
    switch (m_client->status()) {
        case DiscordRpcClient::Status::NotConfigured:
            return tr("Discord activity is not configured in this build.");
        case DiscordRpcClient::Status::Ready:
            return tr("Connected to Discord. Your Discord activity privacy settings still apply.");
        case DiscordRpcClient::Status::Error:
            return tr("Discord could not update your activity. Chroma will try again automatically.");
        default:
            return tr("Waiting for the Discord desktop app. Chroma will connect automatically.");
    }
}

void DiscordPresence::shutdown()
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_artwork->cancel();
    m_client->setEnabled(false);
    emit statusChanged();
}
