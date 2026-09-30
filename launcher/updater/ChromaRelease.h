// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonArray>
#include <QStringList>
#include <QUrl>
#include <optional>

namespace ChromaUpdate {
struct Version {
    QString text;
    QStringList core;
    QStringList prerelease;
    static std::optional<Version> parse(QString text);
    int compare(const Version& other) const;
};

struct Asset {
    QString name;
    QUrl url;
    qint64 size = 0;
};
struct Release {
    Version version;
    QString tag;
    QString notes;
    bool prerelease = false;
    Asset package;
    Asset checksums;
    Asset manifest;
};

QString repository();
bool allowedDownloadUrl(const QUrl& url);
std::optional<Release> selectRelease(const QJsonArray& releases, const Version& current, bool portable, bool includePrereleases);
std::optional<QByteArray> checksumFor(const QByteArray& sums, const QString& name);
}  // namespace ChromaUpdate
