#include "modplatform/ResourceAPI.h"

#include "Application.h"
#include "Json.h"
#include "net/NetJob.h"

#include "modplatform/ModIndex.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimer>
#include "BuildConfig.h"
#include "net/ApiDownload.h"

namespace {
// Fetch only public catalog JSON. Never cache authentication responses or headers.
class CatalogRequest final : public Task {
   public:
    CatalogRequest(QUrl url, std::shared_ptr<QByteArray> response) : m_url(std::move(url)), m_response(std::move(response))
    {
        setAbortable(true);
    }
    bool abort() override
    {
        m_cancelled = true;
        if (!isRunning())
            return true;
        if (m_job && m_job->isRunning())
            m_job->abort();
        if (isRunning())
            emitAborted();
        return true;
    }

   protected:
    void executeTask() override
    {
        if (m_cancelled) {
            emitAborted();
            return;
        }
        const auto base = m_url.host() == QUrl(BuildConfig.FLAME_BASE_URL).host() ? "FlamePacks" : "ModrinthPacks";
        const auto key = QString::fromLatin1(QCryptographicHash::hash(m_url.toEncoded(), QCryptographicHash::Sha256).toHex());
        m_path = APPLICATION->metacache()->resolveEntry(base, "metadata/" + key + ".json")->getFullPath();
        QFile cached(m_path);
        if (cached.size() <= 32 * 1024 * 1024 && cached.open(QIODevice::ReadOnly)) {
            m_cached = cached.readAll();
            if (QJsonDocument::fromJson(m_cached).isNull())
                m_cached.clear();
        }
        // Complete on the event loop so callers can keep normal task lifetimes.
        const auto age = QFileInfo(m_path).lastModified().secsTo(QDateTime::currentDateTime());
        if (!m_cached.isEmpty() && age >= 0 && age < 900) {
            *m_response = m_cached;
            QTimer::singleShot(0, this, [this] {
                if (isRunning())
                    emitSucceeded();
            });
            return;
        }
        m_job = makeShared<NetJob>("Project metadata", APPLICATION->network());
        m_job->setAskRetry(false);
        m_job->addNetAction(Net::ApiDownload::makeByteArray(m_url, m_response));
        connect(m_job.get(), &Task::succeeded, this, [this] {
            if (!isRunning())
                return;
            if (QJsonDocument::fromJson(*m_response).isNull()) {
                fallback(tr("The provider returned invalid metadata."));
                return;
            }
            QDir().mkpath(QFileInfo(m_path).absolutePath());
            QSaveFile cache(m_path);
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(*m_response);
                cache.commit();
            }
            emitSucceeded();
        });
        connect(m_job.get(), &Task::failed, this, [this](const QString& reason) { fallback(reason); });
        connect(m_job.get(), &Task::aborted, this, [this] {
            if (isRunning())
                emitAborted();
        });
        m_job->start();
    }

   private:
    void fallback(const QString& reason)
    {
        if (!isRunning())
            return;
        if (!m_cached.isEmpty()) {
            *m_response = m_cached;
            logWarning(tr("Showing saved project information; the provider is unavailable."));
            emitSucceeded();
        } else
            emitFailed(reason);
    }
    QUrl m_url;
    QString m_path;
    QByteArray m_cached;
    std::shared_ptr<QByteArray> m_response;
    NetJob::Ptr m_job;
    bool m_cancelled = false;
};

class ProjectInfoRequest final : public Task {
   public:
    ProjectInfoRequest(const ResourceAPI* api, ModPlatform::IndexedPack::Ptr pack) : m_api(api), m_pack(std::move(pack))
    {
        setAbortable(true);
    }
    bool abort() override
    {
        m_cancelled = true;
        if (!isRunning())
            return true;
        if (m_request && m_request->isRunning())
            m_request->abort();
        if (isRunning())
            emitAborted();
        return true;
    }

   protected:
    void executeTask() override
    {
        if (m_cancelled) {
            emitAborted();
            return;
        }
        const auto url = m_api->getInfoURL(m_pack->addonId.toString());
        if (!url) {
            emitFailed(tr("This provider has no project information endpoint."));
            return;
        }
        auto response = std::make_shared<QByteArray>();
        m_request = ResourceAPI::cachedRequest(QUrl(*url), response);
        connect(m_request.get(), &Task::failed, this, [this](const QString& reason) {
            if (isRunning())
                emitFailed(reason);
        });
        connect(m_request.get(), &Task::aborted, this, [this] {
            if (isRunning())
                emitAborted();
        });
        connect(m_request.get(), &Task::succeeded, this, [this, response] {
            if (!isRunning())
                return;
            try {
                auto obj = Json::requireObject(QJsonDocument::fromJson(*response));
                if (obj.contains("data"))
                    obj = Json::requireObject(obj, "data");
                m_api->loadIndexedPack(*m_pack, obj);
                m_api->loadExtraPackInfo(*m_pack, obj);
                m_pack->extraData.notice = m_request->warnings().join(' ');
            } catch (const JSONValidationError& e) {
                emitFailed(e.cause());
                return;
            }
            const auto description = m_api->getDescriptionURL(m_pack->addonId.toString());
            if (!description) {
                m_pack->extraDataLoaded = true;
                emitSucceeded();
                return;
            }
            // Keep the completing request alive until its signal dispatch finishes.
            const auto previous = m_request;
            QTimer::singleShot(0, this, [this, description, previous] { loadDescription(*description); });
        });
        m_request->start();
    }

   private:
    void loadDescription(const QString& url)
    {
        if (!isRunning())
            return;
        auto response = std::make_shared<QByteArray>();
        m_request = ResourceAPI::cachedRequest(QUrl(url), response);
        connect(m_request.get(), &Task::succeeded, this, [this, response] {
            if (!isRunning())
                return;
            m_pack->extraData.body = QJsonDocument::fromJson(*response).object()["data"].toString();
            m_pack->extraData.bodyIsHtml = true;
            m_pack->extraDataLoaded = true;
            if (!m_request->warnings().isEmpty())
                m_pack->extraData.notice = m_request->warnings().join(' ');
            emitSucceeded();
        });
        connect(m_request.get(), &Task::failed, this, [this](const QString&) {
            if (!isRunning())
                return;
            m_pack->extraData.notice = tr("The full description is unavailable. Project details and installation remain available.");
            m_pack->extraDataLoaded = false;  // A later visit can retry.
            emitSucceeded();
        });
        connect(m_request.get(), &Task::aborted, this, [this] {
            if (isRunning())
                emitAborted();
        });
        m_request->start();
    }
    const ResourceAPI* m_api;
    ModPlatform::IndexedPack::Ptr m_pack;
    Task::Ptr m_request;
    bool m_cancelled = false;
};
}  // namespace

Task::Ptr ResourceAPI::cachedRequest(const QUrl& url, std::shared_ptr<QByteArray> response)
{
    return makeShared<CatalogRequest>(url, std::move(response));
}

Task::Ptr ResourceAPI::searchProjects(SearchArgs&& args, Callback<QList<ModPlatform::IndexedPack::Ptr>>&& callbacks) const
{
    auto search_url_optional = getSearchURL(args);
    if (!search_url_optional.has_value()) {
        callbacks.on_fail("Failed to create search URL", -1);
        return nullptr;
    }

    auto search_url = search_url_optional.value();

    auto response = std::make_shared<QByteArray>();
    auto netJob = makeShared<NetJob>(QString("%1::Search").arg(debugName()), APPLICATION->network());

    netJob->addNetAction(Net::ApiDownload::makeByteArray(QUrl(search_url), response));

    QObject::connect(netJob.get(), &NetJob::succeeded, [this, response, callbacks] {
        QJsonParseError parse_error{};
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response from" << debugName() << "at" << parse_error.offset
                       << "reason:" << parse_error.errorString();
            qWarning() << *response;

            callbacks.on_fail(parse_error.errorString(), -1);

            return;
        }

        QList<ModPlatform::IndexedPack::Ptr> newList;
        auto packs = documentToArray(doc);

        for (auto packRaw : packs) {
            auto packObj = packRaw.toObject();

            ModPlatform::IndexedPack::Ptr pack = std::make_shared<ModPlatform::IndexedPack>();
            try {
                loadIndexedPack(*pack, packObj);
                newList << pack;
            } catch (const JSONValidationError& e) {
                qWarning().nospace() << "Error while loading resource from " << debugName() << ": " << e.cause();
                continue;
            }
        }

        callbacks.on_succeed(newList);
    });

    // Capture a weak_ptr instead of a shared_ptr to avoid circular dependency issues.
    // This prevents the lambda from extending the lifetime of the shared resource,
    // as it only temporarily locks the resource when needed.
    auto weak = netJob.toWeakRef();
    QObject::connect(netJob.get(), &NetJob::failed, [weak, callbacks](const QString& reason) {
        int network_error_code = -1;
        if (auto netJob = weak.lock()) {
            if (auto* failed_action = netJob->getFailedActions().at(0); failed_action)
                network_error_code = failed_action->replyStatusCode();
        }
        callbacks.on_fail(reason, network_error_code);
    });
    QObject::connect(netJob.get(), &NetJob::aborted, [callbacks] {
        if (callbacks.on_abort != nullptr)
            callbacks.on_abort();
    });

    return netJob;
}

Task::Ptr ResourceAPI::getProjectVersions(VersionSearchArgs&& args, Callback<QVector<ModPlatform::IndexedVersion>>&& callbacks) const
{
    auto versions_url_optional = getVersionsURL(args);
    if (!versions_url_optional.has_value())
        return nullptr;

    auto versions_url = versions_url_optional.value();

    auto response = std::make_shared<QByteArray>();
    auto netJob = cachedRequest(QUrl(versions_url), response);

    QObject::connect(netJob.get(), &Task::succeeded, [this, response, callbacks, args] {
        QJsonParseError parse_error{};
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response for getting versions at" << parse_error.offset
                       << "reason:" << parse_error.errorString();
            if (callbacks.on_fail)
                callbacks.on_fail(parse_error.errorString(), -1);
            return;
        }

        args.pack->versionsError.clear();
        if (!doc.isArray() && (!doc.isObject() || !doc.object()["data"].isArray())) {
            args.pack->versionsError = QObject::tr("The provider returned an invalid release list.");
            if (callbacks.on_fail)
                callbacks.on_fail(args.pack->versionsError, -1);
            return;
        }
        QVector<ModPlatform::IndexedVersion> unsortedVersions;
        try {
            auto arr = doc.isObject() ? doc.object()["data"].toArray() : doc.array();

            for (auto versionIter : arr) {
                auto obj = versionIter.toObject();

                auto file = loadIndexedPackVersion(obj, args.resourceType);
                if (!file.addonId.isValid())
                    file.addonId = args.pack->addonId;

                if (file.fileId.isValid() && !file.downloadUrl.isEmpty())  // Heuristic to check if the returned value is valid
                    unsortedVersions.append(file);
            }

            auto orderSortPredicate = [](const ModPlatform::IndexedVersion& a, const ModPlatform::IndexedVersion& b) -> bool {
                // dates are in RFC 3339 format
                return a.date > b.date;
            };
            std::sort(unsortedVersions.begin(), unsortedVersions.end(), orderSortPredicate);
        } catch (const JSONValidationError& e) {
            qDebug() << doc;
            qWarning() << "Error while reading" << debugName() << "resource version:" << e.cause();
            args.pack->versionsError = QObject::tr("The provider returned an invalid release list.");
            if (callbacks.on_fail)
                callbacks.on_fail(args.pack->versionsError, -1);
            return;
        }

        callbacks.on_succeed(unsortedVersions);
    });

    // Capture a weak_ptr instead of a shared_ptr to avoid circular dependency issues.
    // This prevents the lambda from extending the lifetime of the shared resource,
    // as it only temporarily locks the resource when needed.
    QObject::connect(netJob.get(), &Task::failed, [callbacks, pack = args.pack](const QString& reason) {
        pack->versionsError = QObject::tr("Releases could not be loaded. Reopen this project to try again.");
        if (callbacks.on_fail)
            callbacks.on_fail(reason, -1);
    });
    QObject::connect(netJob.get(), &Task::aborted, [callbacks] {
        if (callbacks.on_abort != nullptr)
            callbacks.on_abort();
    });

    return netJob;
}

Task::Ptr ResourceAPI::getProjectInfo(ProjectInfoArgs&& args, Callback<ModPlatform::IndexedPack::Ptr>&& callbacks) const
{
    auto job = makeShared<ProjectInfoRequest>(this, args.pack);
    QObject::connect(job.get(), &Task::succeeded, [callbacks, pack = args.pack]() mutable {
        if (callbacks.on_succeed)
            callbacks.on_succeed(pack);
    });
    QObject::connect(job.get(), &Task::failed, [callbacks](const QString& reason) {
        if (callbacks.on_fail)
            callbacks.on_fail(reason, -1);
    });
    QObject::connect(job.get(), &Task::aborted, [callbacks] {
        if (callbacks.on_abort)
            callbacks.on_abort();
    });
    return job;
}

Task::Ptr ResourceAPI::getDependencyVersion(DependencySearchArgs&& args, Callback<ModPlatform::IndexedVersion>&& callbacks) const
{
    auto versions_url_optional = getDependencyURL(args);
    if (!versions_url_optional.has_value())
        return nullptr;

    auto versions_url = versions_url_optional.value();

    auto netJob = makeShared<NetJob>(QString("%1::Dependency").arg(args.dependency.addonId.toString()), APPLICATION->network());
    auto response = std::make_shared<QByteArray>();

    netJob->addNetAction(Net::ApiDownload::makeByteArray(versions_url, response));

    QObject::connect(netJob.get(), &NetJob::succeeded, [this, response, callbacks, args] {
        QJsonParseError parse_error{};
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response for getting dependency version at" << parse_error.offset
                       << "reason:" << parse_error.errorString();
            qWarning() << *response;
            return;
        }

        QJsonArray arr;
        if (args.dependency.version.length() != 0 && doc.isObject()) {
            arr.append(doc.object());
        } else {
            arr = doc.isObject() ? doc.object()["data"].toArray() : doc.array();
        }

        QVector<ModPlatform::IndexedVersion> versions;
        for (auto versionIter : arr) {
            auto obj = versionIter.toObject();

            auto file = loadIndexedPackVersion(obj, ModPlatform::ResourceType::Mod);
            if (!file.addonId.isValid())
                file.addonId = args.dependency.addonId;

            if (file.fileId.isValid() &&
                (!file.loaders || args.loader & file.loaders))  // Heuristic to check if the returned value is valid
                versions.append(file);
        }

        auto orderSortPredicate = [](const ModPlatform::IndexedVersion& a, const ModPlatform::IndexedVersion& b) -> bool {
            // dates are in RFC 3339 format
            return a.date > b.date;
        };
        std::sort(versions.begin(), versions.end(), orderSortPredicate);
        auto bestMatch = versions.size() != 0 ? versions.front() : ModPlatform::IndexedVersion();
        callbacks.on_succeed(bestMatch);
    });

    // Capture a weak_ptr instead of a shared_ptr to avoid circular dependency issues.
    // This prevents the lambda from extending the lifetime of the shared resource,
    // as it only temporarily locks the resource when needed.
    auto weak = netJob.toWeakRef();
    QObject::connect(netJob.get(), &NetJob::failed, [weak, callbacks](const QString& reason) {
        int network_error_code = -1;
        if (auto netJob = weak.lock()) {
            if (auto* failed_action = netJob->getFailedActions().at(0); failed_action)
                network_error_code = failed_action->replyStatusCode();
        }
        callbacks.on_fail(reason, network_error_code);
    });
    return netJob;
}

QString ResourceAPI::getGameVersionsString(std::list<Version> mcVersions) const
{
    QString s;
    for (auto& ver : mcVersions) {
        s += QString("\"%1\",").arg(mapMCVersionToModrinth(ver));
    }
    s.remove(s.length() - 1, 1);  // remove last comma
    return s;
}

QString ResourceAPI::mapMCVersionToModrinth(Version v) const
{
    static const QString preString = " Pre-Release ";
    auto verStr = v.toString();

    if (verStr.contains(preString)) {
        verStr.replace(preString, "-pre");
    }
    verStr.replace(" ", "-");
    return verStr;
}

Task::Ptr ResourceAPI::getProject(QString addonId, std::shared_ptr<QByteArray> response) const
{
    auto project_url_optional = getInfoURL(addonId);
    if (!project_url_optional.has_value())
        return nullptr;

    auto project_url = project_url_optional.value();

    auto netJob = makeShared<NetJob>(QString("%1::GetProject").arg(addonId), APPLICATION->network());

    netJob->addNetAction(Net::ApiDownload::makeByteArray(QUrl(project_url), response));

    return netJob;
}
