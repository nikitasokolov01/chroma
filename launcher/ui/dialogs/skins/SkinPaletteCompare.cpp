// SPDX-License-Identifier: GPL-3.0-only
#include "SkinPaletteCompare.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"
#include "ui/widgets/ClayWidgets.h"

class SkinPaletteFallback : public QWidget {
   public:
    explicit SkinPaletteFallback(QWidget* parent) : QWidget(parent)
    {
        setMinimumSize(0, 0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    }
    void setTexture(const QImage& image, SkinModel::Model model)
    {
        m_texture = SkinModel::normalizeTexture(image);
        m_model = model;
        refresh();
    }
    void setVisibleParts(unsigned base, unsigned outer)
    {
        if (m_baseParts == base && m_outerParts == outer)
            return;
        m_baseParts = base;
        m_outerParts = outer;
        refresh();
    }
    QSize minimumSizeHint() const override { return { 0, 0 }; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        painter.setClipRect(rect());
        for (int y = 0; y < height(); y += 12)
            for (int x = 0; x < width(); x += 12)
                if ((x / 12 + y / 12) % 2)
                    painter.fillRect(QRect(x, y, 12, 12), palette().color(QPalette::AlternateBase));
        if (m_preview.isNull()) {
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, tr("Select a skin to preview."));
            return;
        }
        const auto size = m_preview.size().scaled(qMax(1, width() - 16), qMax(1, height() - 16), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(target, m_preview);
    }

   private:
    void refresh()
    {
        m_preview = {};
        if (m_texture.isNull()) {
            update();
            return;
        }
        if (m_baseParts == 0x3f && m_outerParts == 0x3f) {
            m_preview = SkinModel(m_texture, m_model).getPreview();
        } else {
            // Match SkinModel's existing front/back projection while honoring
            // the same per-part layer visibility as the two 3D views.
            m_preview = QImage(36, 36, QImage::Format_ARGB32);
            m_preview.fill(Qt::transparent);
            QPainter paint(&m_preview);
            const bool slim = m_model == SkinModel::SLIM;
            const int armWidth = slim ? 3 : 4;
            const int armX = slim ? 1 : 0;
            struct Face {
                SkinTextureDocument::Part part;
                QPoint destination;
                QRect base;
                QRect outer;
            };
            const Face faces[] = {
                { SkinTextureDocument::Head, { 4, 2 }, { 8, 8, 8, 8 }, { 40, 8, 8, 8 } },
                { SkinTextureDocument::Body, { 4, 10 }, { 20, 20, 8, 12 }, { 20, 36, 8, 12 } },
                { SkinTextureDocument::RightArm, { armX, 10 }, { 44, 20, armWidth, 12 }, { 44, 36, armWidth, 12 } },
                { SkinTextureDocument::LeftArm, { 12, 10 }, { 36, 52, armWidth, 12 }, { 52, 52, armWidth, 12 } },
                { SkinTextureDocument::RightLeg, { 4, 22 }, { 4, 20, 4, 12 }, { 4, 36, 4, 12 } },
                { SkinTextureDocument::LeftLeg, { 8, 22 }, { 20, 52, 4, 12 }, { 4, 52, 4, 12 } },
                { SkinTextureDocument::Head, { 24, 2 }, { 24, 8, 8, 8 }, { 56, 8, 8, 8 } },
                { SkinTextureDocument::Body, { 24, 10 }, { 32, 20, 8, 12 }, { 32, 36, 8, 12 } },
                { SkinTextureDocument::RightArm, { armX + 20, 10 }, { 48 + armWidth, 20, armWidth, 12 },
                  { 48 + armWidth, 36, armWidth, 12 } },
                { SkinTextureDocument::LeftArm, { 32, 10 }, { 40 + armWidth, 52, armWidth, 12 },
                  { 56 + armWidth, 52, armWidth, 12 } },
                { SkinTextureDocument::RightLeg, { 24, 22 }, { 12, 20, 4, 12 }, { 12, 36, 4, 12 } },
                { SkinTextureDocument::LeftLeg, { 28, 22 }, { 28, 52, 4, 12 }, { 12, 52, 4, 12 } },
            };
            for (const auto& face : faces) {
                const unsigned bit = 1u << face.part;
                if (m_baseParts & bit)
                    paint.drawImage(face.destination, m_texture, face.base);
                if (m_outerParts & bit)
                    paint.drawImage(face.destination, m_texture, face.outer);
            }
        }
        update();
    }
    QImage m_texture;
    QImage m_preview;
    SkinModel::Model m_model = SkinModel::CLASSIC;
    unsigned m_baseParts = 0x3f;
    unsigned m_outerParts = 0x3f;
};

SkinPaletteCompare::SkinPaletteCompare(QWidget* parent) : QWidget(parent)
{
    setObjectName("skinPaletteCompare");
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto* panes = new QHBoxLayout;
    panes->setSpacing(8);
    layout->addLayout(panes, 1);
    const bool useOpenGL = SkinOpenGLWindow::hasOpenGL();
    const auto addPane = [this, panes, useOpenGL](bool proposed, SkinOpenGLWindow*& view, SkinPaletteFallback*& fallback,
                                               QStackedLayout*& stack) {
        auto* pane = new ClayPanel(this);
        pane->setObjectName(proposed ? "palettePreviewPane" : "paletteCurrentPane");
        pane->setMinimumSize(0, 0);
        pane->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        auto* paneLayout = new QVBoxLayout(pane);
        paneLayout->setContentsMargins(6, 5, 6, 6);
        paneLayout->setSpacing(4);
        auto* label = new QLabel(proposed ? tr("Preview") : tr("Current"), pane);
        label->setObjectName(proposed ? "palettePreviewLabel" : "paletteCurrentLabel");
        label->setAlignment(Qt::AlignCenter);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto font = label->font();
        font.setBold(true);
        label->setFont(font);
        paneLayout->addWidget(label);
        auto* viewport = new QWidget(pane);
        viewport->setMinimumSize(0, 0);
        viewport->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        stack = new QStackedLayout(viewport);
        stack->setContentsMargins(0, 0, 0, 0);
        fallback = new SkinPaletteFallback(viewport);
        fallback->setObjectName(proposed ? "palettePreviewFallback" : "paletteCurrentFallback");
        fallback->setAccessibleName(proposed ? tr("Proposed skin, front and back") : tr("Current skin, front and back"));
        stack->addWidget(fallback);
        if (useOpenGL) {
            view = new SkinOpenGLWindow(nullptr, palette().color(QPalette::Base), viewport);
            view->setObjectName(proposed ? "palettePreviewView" : "paletteCurrentView");
            view->setMinimumSize(0, 0);
            view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
            view->setAccessibleName(proposed ? tr("Proposed palette, interactive 3D preview") : tr("Current skin, interactive 3D preview"));
            view->setEditingEnabled(false);
            view->setReadOnly(true);
            view->setGridVisible(false);
            stack->addWidget(view);
            stack->setCurrentWidget(view);
            connect(view, &SkinOpenGLWindow::renderingFailed, this, &SkinPaletteCompare::showFallback, Qt::QueuedConnection);
        }
        paneLayout->addWidget(viewport, 1);
        panes->addWidget(pane, 1);
    };
    addPane(false, m_currentView, m_currentFallback, m_currentStack);
    addPane(true, m_previewView, m_previewFallback, m_previewStack);
    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);
    m_hint = new QLabel(tr("Drag either skin to rotate both. Scroll to zoom."), this);
    m_hint->setObjectName("paletteCompareHint");
    m_hint->setWordWrap(true);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    toolbar->addWidget(m_hint, 1);
    m_reset = new QPushButton(tr("Reset view"), this);
    m_reset->setObjectName("paletteCompareReset");
    m_reset->setAutoDefault(false);
    m_reset->setCursor(Qt::PointingHandCursor);
    m_reset->setFixedHeight(26);
    m_reset->setStyleSheet("QPushButton#paletteCompareReset { min-height: 22px; max-height: 22px; padding: 1px 6px; border-radius: 6px; }");
    toolbar->addWidget(m_reset);
    layout->addLayout(toolbar);
    connect(m_reset, &QPushButton::clicked, this, &SkinPaletteCompare::resetView);
    if (useOpenGL) {
        connect(m_currentView, &SkinOpenGLWindow::cameraChanged, this, [this] {
            if (m_syncingCamera)
                return;
            m_autoFit = false;
            QScopedValueRollback<bool> guard(m_syncingCamera, true);
            m_previewView->setCameraState(m_currentView->cameraState());
        });
        connect(m_previewView, &SkinOpenGLWindow::cameraChanged, this, [this] {
            if (m_syncingCamera)
                return;
            m_autoFit = false;
            QScopedValueRollback<bool> guard(m_syncingCamera, true);
            m_currentView->setCameraState(m_previewView->cameraState());
        });
    } else {
        showFallback();
    }
}

void SkinPaletteCompare::setTextures(const QImage& current, const QImage& preview, SkinModel::Model model)
{
    // Updating color proposals must keep the chosen viewing angle and zoom.
    m_currentFallback->setTexture(current, model);
    m_previewFallback->setTexture(preview, model);
    if (m_currentView)
        m_currentView->setTexture(current, model);
    if (m_previewView)
        m_previewView->setTexture(preview, model);
}

void SkinPaletteCompare::resetView()
{
    m_autoFit = true;
    fitDefaultView();
}

void SkinPaletteCompare::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_autoFit) {
        // Let both stacked viewports receive their final geometry first.
        QTimer::singleShot(0, this, [this] {
            if (m_autoFit)
                fitDefaultView();
        });
    }
}

void SkinPaletteCompare::fitDefaultView()
{
    if (!m_currentView || !m_previewView)
        return;
    SkinOpenGLWindow::CameraState state;
    state.yaw = 68.f;
    state.pitch = 8.f;
    for (const auto* view : { m_currentView, m_previewView }) {
        const float aspect = float(qMax(1, view->width())) / qMax(1, view->height());
        // The renderer uses a 45-degree vertical field of view. A 20-pixel
        // horizontal span keeps the full body and outer layer inside both
        // narrow panes with a little breathing room around the arms.
        state.distance = qMax(state.distance, 20.f / (2.f * qTan(qDegreesToRadians(22.5f)) * aspect));
    }
    QScopedValueRollback<bool> guard(m_syncingCamera, true);
    m_currentView->setCameraState(state);
    m_previewView->setCameraState(state);
}

void SkinPaletteCompare::showFallback()
{
    // If either renderer fails, show the same projection on both sides so the
    // comparison remains useful and does not mix a live 3D view with a sprite.
    m_currentStack->setCurrentWidget(m_currentFallback);
    m_previewStack->setCurrentWidget(m_previewFallback);
    m_hint->setText(tr("3D is unavailable. Compare the front and back previews."));
    m_reset->setEnabled(false);
}

void SkinPaletteCompare::setLayersVisible(bool base, bool overlay)
{
    m_baseVisible = base;
    m_outerVisible = overlay;
    if (m_currentView)
        m_currentView->setLayersVisible(base, overlay);
    if (m_previewView)
        m_previewView->setLayersVisible(base, overlay);
    updateFallbackVisibility();
}

void SkinPaletteCompare::setPartLayerVisible(int part, SkinTextureDocument::Layer layer, bool visible)
{
    if (part < SkinTextureDocument::Head || part > SkinTextureDocument::LeftLeg)
        return;
    const unsigned bit = 1u << part;
    if (layer != SkinTextureDocument::Overlay)
        m_baseParts = visible ? m_baseParts | bit : m_baseParts & ~bit;
    if (layer != SkinTextureDocument::Base)
        m_outerParts = visible ? m_outerParts | bit : m_outerParts & ~bit;
    if (m_currentView)
        m_currentView->setPartLayerVisible(part, layer, visible);
    if (m_previewView)
        m_previewView->setPartLayerVisible(part, layer, visible);
    updateFallbackVisibility();
}

void SkinPaletteCompare::updateFallbackVisibility()
{
    const auto base = m_baseVisible ? m_baseParts : 0u;
    const auto outer = m_outerVisible ? m_outerParts : 0u;
    m_currentFallback->setVisibleParts(base, outer);
    m_previewFallback->setVisibleParts(base, outer);
}
