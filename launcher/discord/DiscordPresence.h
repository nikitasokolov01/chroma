// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <memory>

#include "BaseInstance.h"
#include "settings/SettingsObject.h"

class DiscordArtworkResolver;
class DiscordRpcClient;

// One launcher-wide activity. The most recently started game wins when several
// instances are running, then the previous game resumes when that game exits.
class DiscordPresence : public QObject {
    Q_OBJECT
   public:
    DiscordPresence(SettingsObjectPtr settings, const QString& applicationId, QObject* parent = nullptr);
    ~DiscordPresence() override;
    void observeInstance(const InstancePtr& instance);
    QString statusText() const;
    QJsonObject currentActivity() const { return m_activity; }
    void shutdown();

   signals:
    void statusChanged();
    void activityChanged(const QJsonObject& activity);

   private:
    void applySetting();
    void updateRunning(const InstancePtr& instance);
    void refresh();
    struct Session {
        std::weak_ptr<BaseInstance> instance;
        qint64 started;
        quint64 order;
    };
    SettingsObjectPtr m_settings;
    DiscordRpcClient* m_client;
    DiscordArtworkResolver* m_artwork;
    QSet<BaseInstance*> m_observed;
    QHash<BaseInstance*, Session> m_sessions;
    QJsonObject m_activity;
    quint64 m_order = 0;
    bool m_enabled = false;
    bool m_stopped = false;
};
