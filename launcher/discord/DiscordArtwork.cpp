// SPDX-License-Identifier: GPL-3.0-only
#include "DiscordArtwork.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include "BaseInstance.h"
#include "BuildConfig.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"

QString DiscordArtwork::publicUrl(const QString& value)
{
    if (value.size() > 300)
        return {};
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (url.port() != -1 && url.port() != 443))
        return {};

    const auto host = url.host();
    const auto path = url.path();
    if (path.contains("..") || path.contains('\\') || path.contains(QChar::Null))
        return {};
    const bool knownImage = (host == "cdn.modrinth.com" && path.startsWith("/data/")) ||
                            ((host == "media.forgecdn.net" || host == "mediafilez.forgecdn.net") && path.startsWith("/avatars/")) ||
                            (host == "download.nodecdn.net" && path.startsWith("/containers/atl/launcher/images/")) ||
                            (host == "cdn.technicpack.net" && path.startsWith("/platform2/pack-icons/"));
    const auto encoded = url.toString(QUrl::FullyEncoded);
    // Discord's asset identifier / external image URL limit is 300 characters.
    return knownImage && encoded.size() <= 300 ? encoded : QString();
}

QString DiscordArtwork::fromCatalog(const QString& provider, const QByteArray& json)
{
    auto document = QJsonDocument::fromJson(json);
    if (!document.isObject())
        return {};
    auto object = document.object();
    if (provider == "modrinth")
        return publicUrl(object.value("icon_url").toString());
    if (provider == "flame") {
        const auto logo = object.value("data").toObject().value("logo").toObject();
        const auto thumbnail = publicUrl(logo.value("thumbnailUrl").toString());
        return thumbnail.isEmpty() ? publicUrl(logo.value("url").toString()) : thumbnail;
    }
    return {};
}

QString DiscordArtwork::atLauncherUrl(const QString& packId)
{
    static const QRegularExpression idPattern("^[A-Za-z0-9_-]+$");
    if (!idPattern.match(packId).hasMatch())
        return {};
    return publicUrl(BuildConfig.ATL_DOWNLOAD_SERVER_URL + "launcher/images/" + packId);
}

DiscordArtworkResolver::DiscordArtworkResolver(QObject* parent) : QObject(parent) {}

DiscordArtworkResolver::~DiscordArtworkResolver()
{
    cancel();
}

void DiscordArtworkResolver::cancel()
{
    ++m_generation;
    const auto requests = m_requests;
    m_requests.clear();
    for (auto it = requests.cbegin(); it != requests.cend(); ++it) {
        m_attempted.remove(it.key());
        const auto& request = it.value();
        disconnect(request.get(), nullptr, this, nullptr);
        request->abort();
    }
}

void DiscordArtworkResolver::resolve(const std::shared_ptr<BaseInstance>& instance)
{
    if (!instance || !instance->isMinecraftRunning() || !instance->discordArtworkUrl().isEmpty() || !instance->isManagedPack())
        return;

    const auto provider = instance->getManagedPackType();
    const auto id = instance->getManagedPackID();
    static const QRegularExpression idPattern("^[A-Za-z0-9_-]+$");
    if (!idPattern.match(id).hasMatch())
        return;

    if (provider == "atlauncher") {
        instance->setDiscordArtworkUrl(DiscordArtwork::atLauncherUrl(id));
        return;
    }
    if (provider != "modrinth" && provider != "flame")
        return;

    const auto key = provider + ':' + id;
    if (m_results.contains(key)) {
        instance->setDiscordArtworkUrl(m_results.value(key));
        return;
    }
    if (m_attempted.contains(key))
        return;
    m_attempted.insert(key);

    static const ModrinthAPI modrinth;
    static const FlameAPI flame;
    const ResourceAPI& api = provider == "modrinth" ? static_cast<const ResourceAPI&>(modrinth) : static_cast<const ResourceAPI&>(flame);
    const auto endpoint = api.getInfoURL(id);
    if (!endpoint)
        return;

    auto bytes = std::make_shared<QByteArray>();
    auto request = ResourceAPI::cachedRequest(QUrl(*endpoint), bytes);
    m_requests.insert(key, request);
    const auto generation = m_generation;
    const std::weak_ptr<BaseInstance> weakInstance = instance;
    connect(request.get(), &Task::succeeded, this, [this, weakInstance, key, provider, id, bytes, generation] {
        if (generation != m_generation)
            return;
        const auto artwork = DiscordArtwork::fromCatalog(provider, *bytes);
        if (artwork.isEmpty())
            return;
        m_results.insert(key, artwork);
        if (auto instance = weakInstance.lock(); instance && instance->currentStatus() == BaseInstance::Status::Present &&
                                                 instance->getManagedPackType() == provider && instance->getManagedPackID() == id)
            instance->setDiscordArtworkUrl(artwork);
    });
    // A completing task must remain alive until all of its direct signal handlers finish.
    connect(
        request.get(), &Task::finished, this,
        [this, key, generation] {
            if (generation == m_generation)
                m_requests.remove(key);
        },
        Qt::QueuedConnection);
    request->start();
}
