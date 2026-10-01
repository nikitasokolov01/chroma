// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QNetworkAccessManager>
#include <functional>
#include <optional>

#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/skins/SkinModel.h"
#include "tasks/Task.h"

// Uses the launcher's existing authentication and Minecraft Services request.
// The only account data written after upload is public skin/cape profile data.
class SkinApplyTask : public Task {
    Q_OBJECT
   public:
    SkinApplyTask(MinecraftAccountPtr account, QString path, SkinModel::Model model, std::optional<QString> cape = std::nullopt);
    static shared_qobject_ptr<SkinApplyTask> forCape(
        MinecraftAccountPtr account,
        QString cape,
        shared_qobject_ptr<QNetworkAccessManager> network = shared_qobject_ptr<QNetworkAccessManager>());
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    void upload();
    void applyCape();
    void runStep(Task::Ptr task, std::function<void()> success);
    MinecraftAccountPtr m_account;
    QString m_path;
    SkinModel::Model m_model;
    std::optional<QString> m_cape;
    QByteArray m_png;
    Task::Ptr m_step;
    QList<Task::Ptr> m_steps;
    bool m_capeOnly = false;
    shared_qobject_ptr<QNetworkAccessManager> m_capeNetwork;
};
