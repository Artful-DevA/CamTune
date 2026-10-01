// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Frame.h"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPointF>
#include <QRectF>

#include <memory>

namespace ui {

// GPU preview of processed I420 frames. The three planes are uploaded as
// single-channel textures and converted to RGB in a fragment shader, so the
// CPU never converts colorspace for display. Rendering is driven by frame
// arrival and coalesced by Qt's update(), independent of the pipeline.
class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum class Interaction { Navigate, DrawRect, PickPoint };

    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;

    void setFrame(cam::FramePtr frame);
    void clearFrame();
    void setMessage(const QString &message);
    void setInteraction(Interaction mode);
    Interaction interaction() const { return m_mode; }
    // Normalized rectangles drawn on top of the picture (e.g. effect regions).
    void setOverlayRects(const QList<QRectF> &rects, bool ellipse = false);
    cam::FramePtr currentFrame() const { return m_frame; }

Q_SIGNALS:
    void zoomRequested(double steps);         // mouse wheel
    void panRequested(double dx, double dy);  // drag, in output-normalized units
    void rectDrawn(const QRectF &normalized);
    void pointPicked(const QPointF &normalized);
    void doubleClicked();

protected:
    void initializeGL() override;
    void paintGL() override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    QRectF pictureRect() const;
    QPointF toNormalized(const QPointF &widgetPos) const;
    void uploadFrame();
    void releaseGL();

    cam::FramePtr m_frame;
    bool m_dirty = false;
    QString m_message;
    Interaction m_mode = Interaction::Navigate;
    QList<QRectF> m_overlay;
    bool m_overlayEllipse = false;

    bool m_glReady = false;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QOpenGLBuffer m_vbo;
    GLuint m_tex[3] = {0, 0, 0};
    int m_texW = 0, m_texH = 0;

    bool m_dragging = false;
    QPointF m_dragStart, m_dragLast;
};

} // namespace ui
