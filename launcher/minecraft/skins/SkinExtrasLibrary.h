// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "SkinTextureDocument.h"

class SkinExtrasLibrary {
   public:
    struct Entry {
        QString id;
        QString name;
        SkinModel::Model model;
        SkinTextureDocument::PixelPatch pixels;
    };
    explicit SkinExtrasLibrary(QString directory);
    QList<Entry> entries(QString* warning = nullptr) const;
    std::optional<Entry> load(const QString& id, QString* error = nullptr) const;
    QString save(const QString& name, const SkinTextureDocument::PixelPatch& pixels, SkinModel::Model model, QString* error = nullptr);
    bool remove(const QString& id, QString* error = nullptr);
    static SkinTextureDocument::PixelPatch forModel(const Entry& entry, SkinModel::Model model);

   private:
    QString path(const QString& id) const;
    QString m_directory;
};
