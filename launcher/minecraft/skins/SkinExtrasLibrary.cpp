// SPDX-License-Identifier: GPL-3.0-only
#include "SkinExtrasLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

#include "ui/dialogs/skins/draw/SkinGeometry.h"

namespace {
using D = SkinTextureDocument;
bool validPatch(const D::PixelPatch& patch, SkinModel::Model model)
{
    return !D::encodePatch(patch).isEmpty() &&
           patch.mask.translated(patch.origin).subtracted(D::uvRegion(D::All, D::Both, model)).isEmpty();
}
bool validName(const QString& name)
{
    return !name.trimmed().isEmpty() && name.size() <= 80 && !name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]"));
}
void fail(QString* error, const QString& text)
{
    if (error)
        *error = text;
}
}  // namespace

SkinExtrasLibrary::SkinExtrasLibrary(QString directory) : m_directory(std::move(directory)) {}

QString SkinExtrasLibrary::path(const QString& id) const
{
    static const QRegularExpression pattern("\\A[0-9a-f]{32}\\z");
    return pattern.match(id).hasMatch() ? QDir(m_directory).filePath(id + ".skinextra") : QString();
}

QString SkinExtrasLibrary::save(const QString& name, const D::PixelPatch& pixels, SkinModel::Model model, QString* error)
{
    if (!validName(name) || (model != SkinModel::CLASSIC && model != SkinModel::SLIM) || !validPatch(pixels, model)) {
        fail(error, QObject::tr("Enter a name of up to 80 characters and select some skin pixels to save."));
        return {};
    }
    if (!QDir().mkpath(m_directory)) {
        fail(error, QObject::tr("The Skin Extras folder could not be created."));
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::Id128);
    QSaveFile file(path(id));
    const auto data = QJsonDocument(QJsonObject{ { "format", 1 },
                                                 { "name", name.trimmed() },
                                                 { "model", int(model) },
                                                 { "pixels", QString::fromLatin1(D::encodePatch(pixels).toBase64()) } })
                          .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        fail(error, QObject::tr("The skin extra could not be saved: %1").arg(file.errorString()));
        return {};
    }
    return id;
}

std::optional<SkinExtrasLibrary::Entry> SkinExtrasLibrary::load(const QString& id, QString* error) const
{
    const auto filename = path(id);
    QFile file(filename);
    if (filename.isEmpty() || QFileInfo(filename).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 32768) {
        fail(error, QObject::tr("This skin extra could not be read."));
        return {};
    }
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const int model = object.value("model").toInt(-1);
    const auto name = object.value("name").toString();
    const auto encoded = object.value("pixels").toString().toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (object.value("format").toInt() != 1 || !validName(name) || (model != SkinModel::CLASSIC && model != SkinModel::SLIM) || !decoded) {
        fail(error, QObject::tr("This skin extra is damaged or uses an unsupported format."));
        return {};
    }
    const auto pixels = D::decodePatch(decoded.decoded);
    if (!validPatch(pixels, static_cast<SkinModel::Model>(model))) {
        fail(error, QObject::tr("This skin extra contains invalid skin pixels."));
        return {};
    }
    return Entry{ id, name, static_cast<SkinModel::Model>(model), pixels };
}

QList<SkinExtrasLibrary::Entry> SkinExtrasLibrary::entries(QString* warning) const
{
    QList<Entry> entries;
    int skipped = 0;
    for (const auto& file : QDir(m_directory).entryInfoList({ "*.skinextra" }, QDir::Files | QDir::NoSymLinks)) {
        auto entry = load(file.completeBaseName());
        if (entry)
            entries.append(std::move(*entry));
        else
            ++skipped;
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        const int order = QString::localeAwareCompare(left.name, right.name);
        return order ? order < 0 : left.id < right.id;
    });
    if (skipped)
        fail(warning, QObject::tr("%n unreadable skin extra(s) were skipped.", nullptr, skipped));
    return entries;
}

bool SkinExtrasLibrary::remove(const QString& id, QString* error)
{
    const auto filename = path(id);
    if (filename.isEmpty() || QFileInfo(filename).isSymLink() || !QFile::remove(filename)) {
        fail(error, QObject::tr("This skin extra could not be removed."));
        return false;
    }
    return true;
}

D::PixelPatch SkinExtrasLibrary::forModel(const Entry& entry, SkinModel::Model model)
{
    if (!validPatch(entry.pixels, entry.model) || (model != SkinModel::CLASSIC && model != SkinModel::SLIM))
        return {};
    if (entry.model == model)
        return entry.pixels;
    // Reuse the model's face layout so narrow arms keep the right face and
    // orientation when a saved Classic extra is applied to a Slim skin.
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QRegion mask;
    const auto source = opengl::skinBoxes(entry.model == SkinModel::SLIM);
    const auto target = opengl::skinBoxes(model == SkinModel::SLIM);
    for (size_t box = 0; box < source.size(); ++box) {
        const auto& from = source[box];
        const auto& to = target[box];
        const auto fromFaces = opengl::boxFaces(from.size, from.center, from.uv, from.textureSize);
        const auto toFaces = opengl::boxFaces(to.size, to.center, to.uv, to.textureSize);
        for (size_t face = 0; face < fromFaces.size(); ++face) {
            const auto a = fromFaces[face].pixels;
            const auto b = toFaces[face].pixels;
            for (int y = 0; y < b.height(); ++y) {
                for (int x = 0; x < b.width(); ++x) {
                    const QPoint sourcePixel(a.x() + (2 * x + 1) * a.width() / (2 * b.width()),
                                             a.y() + (2 * y + 1) * a.height() / (2 * b.height()));
                    const auto relative = sourcePixel - entry.pixels.origin;
                    if (!entry.pixels.mask.contains(relative))
                        continue;
                    const auto destination = b.topLeft() + QPoint(x, y);
                    image.setPixelColor(destination, entry.pixels.image.pixelColor(relative));
                    mask += QRect(destination, QSize(1, 1));
                }
            }
        }
    }
    const auto bounds = mask.boundingRect();
    return mask.isEmpty() ? D::PixelPatch{} : D::PixelPatch{ image.copy(bounds), mask.translated(-bounds.topLeft()), bounds.topLeft() };
}
