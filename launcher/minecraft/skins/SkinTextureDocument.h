// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QRegion>
#include <QVector>

#include "minecraft/skins/SkinModel.h"

// A small, independent texture document. History is bounded and each brush
// stroke is a single operation, regardless of the number of mouse events.
class SkinTextureDocument : public QObject {
    Q_OBJECT
   public:
    enum Part { All = -1, Head, Body, RightArm, LeftArm, RightLeg, LeftLeg };
    enum Layer { Both, Base, Overlay };

    explicit SkinTextureDocument(QObject* parent = nullptr);
    const QImage& image() const { return m_state.image; }
    SkinModel::Model model() const { return m_state.model; }
    bool isDirty() const;
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor + 1 < m_history.size(); }

    static QImage readPng(const QString& path, QString* error = nullptr);
    static QRegion uvRegion(Part part, Layer layer, SkinModel::Model model);
    bool load(const QImage& image, SkinModel::Model model = SkinModel::CLASSIC);
    bool importPng(const QString& path, QString* error = nullptr);
    bool exportPng(const QString& path, QString* error = nullptr) const;
    void markSaved();
    void beginStroke();
    void paintPixel(QPoint pixel, QColor color, int brushSize, Part part, Layer layer, bool erase = false);
    void endStroke();
    void setModel(SkinModel::Model model);

   public slots:
    void undo();
    void redo();
    void reset();

   signals:
    void changed();

   private:
    struct State {
        QImage image;
        SkinModel::Model model = SkinModel::CLASSIC;
        bool operator==(const State& other) const { return model == other.model && image == other.image; }
    };
    void record();
    State m_state;
    State m_original;
    State m_saved;
    QVector<State> m_history;
    int m_cursor = 0;
    bool m_stroke = false;
};
