// SPDX-License-Identifier: GPL-3.0-only
#include "SkinOutfitLibrary.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace {
constexpr int MaximumFileBytes = 128 * 1024;
bool validName(const QString& name)
{
    return !name.trimmed().isEmpty() && name.size() <= 80 && !name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]"));
}
bool validCapeId(const QString& id)
{
    // IDs are opaque service values, never paths. Permit UUIDs and test IDs.
    static const QRegularExpression pattern("\\A[A-Za-z0-9_-]{1,128}\\z");
    return id.isEmpty() || pattern.match(id).hasMatch();
}
void fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
}
QString encode(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    return image.save(&buffer, "PNG") ? QString::fromLatin1(bytes.toBase64()) : QString();
}
QImage decode(const QString& encoded, bool cape)
{
    auto bytes = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!bytes || bytes.decoded.left(8) != QByteArray("\x89PNG\r\n\x1a\n", 8))
        return {};
    QBuffer buffer(&bytes.decoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    reader.setAutoDetectImageFormat(false);
    if (reader.size() != (cape ? QSize(64, 32) : QSize(64, 64)))
        return {};
    return reader.read().convertToFormat(QImage::Format_ARGB32);
}
bool valid(const SkinOutfitLibrary::Entry& entry)
{
    return validName(entry.name) && entry.image.size() == QSize(64, 64) &&
           (entry.model == SkinModel::CLASSIC || entry.model == SkinModel::SLIM) &&
           (!entry.capeId || validCapeId(*entry.capeId)) &&
           (entry.capeImage.isNull() || (entry.capeId && !entry.capeId->isEmpty() && entry.capeImage.size() == QSize(64, 32)));
}
}  // namespace

SkinOutfitLibrary::SkinOutfitLibrary(QString directory) : m_directory(std::move(directory)) {}

QString SkinOutfitLibrary::path(const QString& id) const
{
    static const QRegularExpression pattern("\\A[0-9a-f]{32}\\z");
    return pattern.match(id).hasMatch() ? QDir(m_directory).filePath(id + ".skinoutfit") : QString();
}

bool SkinOutfitLibrary::write(const Entry& entry, QString* error)
{
    const auto filename = path(entry.id);
    if (filename.isEmpty() || !valid(entry) || QFileInfo(filename).isSymLink()) {
        fail(error, QObject::tr("Choose a valid skin and a name of up to 80 characters for this outfit."));
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        fail(error, QObject::tr("The outfit folder could not be created."));
        return false;
    }
    const auto skinPng = encode(entry.image);
    const auto capePng = entry.capeImage.isNull() ? QString() : encode(entry.capeImage);
    if (skinPng.isEmpty() || (!entry.capeImage.isNull() && capePng.isEmpty())) {
        fail(error, QObject::tr("The outfit images could not be saved."));
        return false;
    }
    QJsonObject object{ { "format", 1 }, { "name", entry.name.trimmed() }, { "model", int(entry.model) }, { "skin", skinPng } };
    if (entry.capeId)
        object.insert("cape", *entry.capeId);
    if (!capePng.isEmpty())
        object.insert("capeImage", capePng);
    const auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QSaveFile file(filename);
    if (data.size() > MaximumFileBytes || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        fail(error, QObject::tr("The outfit could not be saved: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

QString SkinOutfitLibrary::save(const Entry& entry, QString* error)
{
    if (error)
        error->clear();
    auto snapshot = entry;
    snapshot.id = QUuid::createUuid().toString(QUuid::Id128);
    return write(snapshot, error) ? snapshot.id : QString();
}

std::optional<SkinOutfitLibrary::Entry> SkinOutfitLibrary::load(const QString& id, QString* error) const
{
    if (error)
        error->clear();
    const auto filename = path(id);
    QFile file(filename);
    if (filename.isEmpty() || QFileInfo(filename).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > MaximumFileBytes) {
        fail(error, QObject::tr("This outfit could not be read."));
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto object = document.object();
    const auto model = object.value("model").toInt(-1);
    Entry entry{ id, object.value("name").toString(), decode(object.value("skin").toString(), false),
                 static_cast<SkinModel::Model>(model), std::nullopt, {} };
    if (object.contains("cape"))
        entry.capeId = object.value("cape").toString();
    if (object.contains("capeImage"))
        entry.capeImage = decode(object.value("capeImage").toString(), true);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || object.value("format").toInt() != 1 ||
        (object.contains("cape") && !object.value("cape").isString()) ||
        (object.contains("capeImage") && entry.capeImage.isNull()) || !valid(entry)) {
        fail(error, QObject::tr("This outfit is damaged or uses an unsupported format."));
        return {};
    }
    return entry;
}

QList<SkinOutfitLibrary::Entry> SkinOutfitLibrary::entries(QString* warning) const
{
    if (warning)
        warning->clear();
    QList<Entry> result;
    int skipped = 0;
    for (const auto& file : QDir(m_directory).entryInfoList({ "*.skinoutfit" }, QDir::Files | QDir::NoSymLinks)) {
        auto entry = load(file.completeBaseName());
        if (entry)
            result.append(std::move(*entry));
        else
            ++skipped;
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        const int order = QString::localeAwareCompare(left.name, right.name);
        return order ? order < 0 : left.id < right.id;
    });
    if (skipped)
        fail(warning, QObject::tr("%n unreadable outfit(s) were skipped.", nullptr, skipped));
    return result;
}

bool SkinOutfitLibrary::rename(const QString& id, const QString& name, QString* error)
{
    auto entry = load(id, error);
    if (!entry)
        return false;
    entry->name = name;
    return write(*entry, error);
}

bool SkinOutfitLibrary::remove(const QString& id, QString* error)
{
    if (error)
        error->clear();
    const auto filename = path(id);
    if (filename.isEmpty() || QFileInfo(filename).isSymLink() || !QFile::remove(filename)) {
        fail(error, QObject::tr("This outfit could not be deleted."));
        return false;
    }
    return true;
}
