// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <memory>

#include "tasks/Task.h"

class BaseInstance;

namespace DiscordArtwork {
// Only anonymous, public images on the modpack catalogs' image CDNs are shareable.
QString publicUrl(const QString& value);
QString fromCatalog(const QString& provider, const QByteArray& json);
QString atLauncherUrl(const QString& packId);
}  // namespace DiscordArtwork

class DiscordArtworkResolver : public QObject {
    Q_OBJECT
   public:
    explicit DiscordArtworkResolver(QObject* parent = nullptr);
    ~DiscordArtworkResolver() override;

    // Call only for the playing instance while Discord presence is enabled.
    // Successful results update the instance and emit its propertiesChanged signal.
    void resolve(const std::shared_ptr<BaseInstance>& instance);
    void cancel();

   private:
    QHash<QString, Task::Ptr> m_requests;
    QHash<QString, QString> m_results;
    QSet<QString> m_attempted;
    quint64 m_generation = 0;
};
