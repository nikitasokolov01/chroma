// SPDX-License-Identifier: GPL-3.0-only
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "minecraft/skins/SkinOutfitLibrary.h"

class SkinOutfitTest : public QObject {
    Q_OBJECT
   private:
    SkinOutfitLibrary::Entry outfit() const
    {
        QImage skin(64, 64, QImage::Format_ARGB32);
        skin.fill(Qt::cyan);
        skin.setPixelColor(40, 8, QColor(80, 120, 160, 90));
        QImage cape(64, 32, QImage::Format_ARGB32);
        cape.fill(Qt::magenta);
        return { {}, "Winter / blue", skin, SkinModel::SLIM, QString("owned-cape"), cape };
    }

   private slots:
    void snapshotsPersistIndependently()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SkinOutfitLibrary library(directory.path());
        auto source = outfit();
        const auto original = source;
        QString error;
        const auto id = library.save(source, &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        source.image.fill(Qt::black);
        source.capeImage.fill(Qt::white);
        source.model = SkinModel::CLASSIC;
        source.name = "changed";
        const SkinOutfitLibrary reopened(directory.path());
        const auto saved = reopened.load(id, &error);
        QVERIFY2(saved, qPrintable(error));
        QCOMPARE(saved->name, original.name);
        QCOMPARE(saved->model, original.model);
        QCOMPARE(saved->image, original.image);
        QCOMPARE(saved->capeImage, original.capeImage);
        QVERIFY(saved->capeId == original.capeId);
        QCOMPARE(reopened.entries().size(), 1);
        const auto duplicate = library.save(original);
        QVERIFY(!duplicate.isEmpty() && duplicate != id);
        QVERIFY(library.rename(id, "  Survival  "));
        QCOMPARE(library.load(id)->name, QString("Survival"));
        QCOMPARE(library.load(id)->image, original.image);
        QVERIFY(library.remove(id));
        QVERIFY(!library.load(id));
        QVERIFY(library.load(duplicate));
    }

    void capeKeepAndRemoveAreDistinct()
    {
        QTemporaryDir directory;
        SkinOutfitLibrary library(directory.path());
        auto entry = outfit();
        entry.capeImage = {};
        entry.capeId.reset();
        const auto keep = library.save(entry);
        QVERIFY(!keep.isEmpty());
        QVERIFY(!library.load(keep)->capeId);
        entry.capeId = QString();
        const auto remove = library.save(entry);
        QVERIFY(!remove.isEmpty());
        QVERIFY(library.load(remove)->capeId);
        QVERIFY(library.load(remove)->capeId->isEmpty());
    }

    void rejectsUnsafeIdentifiersAndInvalidContent()
    {
        QTemporaryDir directory;
        SkinOutfitLibrary library(directory.path());
        auto entry = outfit();
        const auto id = library.save(entry);
        QVERIFY(!id.isEmpty());
        for (const auto& bad : { QString("../accounts"), id + "/../" + id, id + ".skinoutfit", QString() }) {
            QVERIFY(!library.load(bad));
            QVERIFY(!library.remove(bad));
            QVERIFY(!library.rename(bad, "New name"));
        }
        QVERIFY(!library.rename(id, "\n"));
        QCOMPARE(library.load(id)->name, entry.name);
        for (const auto& name : { QString(), QString(81, 'a'), QString("bad\nname") }) {
            entry.name = name;
            QString error;
            QVERIFY(library.save(entry, &error).isEmpty());
            QVERIFY(!error.isEmpty());
        }
        entry = outfit();
        entry.image = QImage(32, 32, QImage::Format_ARGB32);
        QVERIFY(library.save(entry).isEmpty());
        entry = outfit();
        entry.model = static_cast<SkinModel::Model>(-1);
        QVERIFY(library.save(entry).isEmpty());
        entry = outfit();
        entry.capeId = "../outside";
        QVERIFY(library.save(entry).isEmpty());
        entry = outfit();
        entry.capeId.reset();
        QVERIFY(library.save(entry).isEmpty());
        entry = outfit();
        entry.capeImage = QImage(1024, 1024, QImage::Format_ARGB32);
        QVERIFY(library.save(entry).isEmpty());
    }

    void damagedFilesDoNotHideHealthyOutfits()
    {
        QTemporaryDir directory;
        SkinOutfitLibrary library(directory.path());
        const auto healthy = library.save(outfit());
        const auto broken = library.save(outfit());
        QFile file(directory.filePath(broken + ".skinoutfit"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        const auto overwrite = [&file](const QJsonObject& object) {
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            const auto bytes = QJsonDocument(object).toJson();
            const bool success = file.write(bytes) == bytes.size();
            file.close();
            return success;
        };
        for (const auto& key : { QString("skin"), QString("capeImage") }) {
            auto invalid = original;
            invalid.insert(key, "not valid base64!");
            QVERIFY(overwrite(invalid));
            QVERIFY(!library.load(broken));
        }
        auto invalid = original;
        invalid.insert("format", 2);
        QVERIFY(overwrite(invalid));
        QVERIFY(!library.load(broken));
        invalid = original;
        invalid.insert("cape", QJsonObject{});
        QVERIFY(overwrite(invalid));
        QVERIFY(!library.load(broken));
        QString warning;
        const auto entries = library.entries(&warning);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().id, healthy);
        QVERIFY(!warning.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray(128 * 1024 + 1, ' '));
        file.close();
        QVERIFY(!library.load(broken));
        QVERIFY(library.load(healthy));
    }
};

QTEST_GUILESS_MAIN(SkinOutfitTest)
#include "SkinOutfit_test.moc"
