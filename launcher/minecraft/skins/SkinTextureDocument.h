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
    enum Effect { Hue, Brightness, Grayscale, Invert };
    struct PixelPatch {
        QImage image;
        QRegion mask;   // Coordinates relative to image; pixels outside this mask are not pasted.
        QPoint origin;  // Original top-left position in the source texture.
    };

    explicit SkinTextureDocument(QObject* parent = nullptr);
    const QImage& image() const { return m_state.image; }
    SkinModel::Model model() const { return m_state.model; }
    bool isDirty() const;
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor + 1 < m_history.size(); }
    QRegion selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    void setSelection(QRegion selection);
    void clearSelection();

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
    void floodFill(QPoint seed, QColor color, const QRegion& allowed);
    void applyEffect(Effect effect, int amount, const QRegion& allowed);
    void setColorAdjustments(int hue, int brightness, const QRegion& allowed);
    void finishColorAdjustments();
    int hueAdjustment() const { return m_hueAdjustment; }
    int brightnessAdjustment() const { return m_brightnessAdjustment; }
    PixelPatch copyPixels(const QRegion& allowed) const;
    void pastePixels(const PixelPatch& patch, QPoint destination, const QRegion& allowed);
    static QByteArray encodePatch(const PixelPatch& patch);
    static PixelPatch decodePatch(const QByteArray& bytes);
    static bool writeClipboard(const PixelPatch& patch);
    static PixelPatch readClipboard();

   public slots:
    void undo();
    void redo();
    void reset();

   signals:
    void changed();
    void selectionChanged();
    void adjustmentsChanged();

   private:
    struct State {
        QImage image;
        SkinModel::Model model = SkinModel::CLASSIC;
        QImage adjustmentBase;
        QRegion adjustmentRegion;
        int hue = 0;
        int brightness = 0;
        bool operator==(const State& other) const { return model == other.model && image == other.image; }
    };
    void record();
    void restoreColorAdjustments();
    QRegion editableRegion(const QRegion& allowed) const;
    QRegion m_selection;
    State m_state;
    State m_original;
    State m_saved;
    QVector<State> m_history;
    int m_cursor = 0;
    bool m_stroke = false;
    QImage m_adjustmentBase;
    QRegion m_adjustmentRegion;
    int m_adjustmentHistory = -1;
    int m_hueAdjustment = 0;
    int m_brightnessAdjustment = 0;
};
