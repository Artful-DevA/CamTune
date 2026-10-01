// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreviewWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>

namespace ui {

namespace {

const char *kVertex = R"(
attribute vec2 a_pos;
attribute vec2 a_tex;
varying vec2 v_tex;
void main() {
    v_tex = a_tex;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// Limited-range BT.601 YUV -> RGB.
const char *kFragment = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_tex;
uniform sampler2D u_y;
uniform sampler2D u_u;
uniform sampler2D u_v;
void main() {
    float y = 1.16438 * (texture2D(u_y, v_tex).r - 0.0627451);
    float u = texture2D(u_u, v_tex).r - 0.5;
    float v = texture2D(u_v, v_tex).r - 0.5;
    gl_FragColor = vec4(y + 1.59603 * v,
                        y - 0.39176 * u - 0.81297 * v,
                        y + 2.01723 * u,
                        1.0);
}
)";

} // namespace

PreviewWidget::PreviewWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 180);
    setMouseTracking(false);
    setFocusPolicy(Qt::ClickFocus);
    setToolTip(tr("Scroll to zoom, drag to pan, double-click to reset framing"));
}

PreviewWidget::~PreviewWidget()
{
    makeCurrent();
    releaseGL();
    doneCurrent();
}

void PreviewWidget::releaseGL()
{
    if (!m_glReady)
        return;
    glDeleteTextures(3, m_tex);
    m_tex[0] = m_tex[1] = m_tex[2] = 0;
    m_vbo.destroy();
    m_program.reset();
    m_glReady = false;
}

void PreviewWidget::setFrame(cam::FramePtr frame)
{
    if (!frame || frame->format != cam::PixelFormat::I420)
        return;
    m_frame = std::move(frame);
    m_dirty = true;
    update(); // coalesced to the display refresh
}

void PreviewWidget::clearFrame()
{
    m_frame.reset();
    m_texW = m_texH = 0;
    update();
}

void PreviewWidget::setMessage(const QString &message)
{
    if (message == m_message)
        return;
    m_message = message;
    update();
}

void PreviewWidget::setInteraction(Interaction mode)
{
    m_mode = mode;
    setCursor(mode == Interaction::Navigate ? Qt::ArrowCursor : Qt::CrossCursor);
    if (mode == Interaction::Navigate)
        setToolTip(tr("Scroll to zoom, drag to pan, double-click to reset framing"));
    else if (mode == Interaction::DrawRect)
        setToolTip(tr("Drag to draw a region"));
    else
        setToolTip(tr("Click to pick a color"));
}

void PreviewWidget::setOverlayRects(const QList<QRectF> &rects, bool ellipse)
{
    m_overlay = rects;
    m_overlayEllipse = ellipse;
    update();
}

void PreviewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        makeCurrent();
        releaseGL();
        doneCurrent();
    });

    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertex) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragment) || !m_program->link()) {
        m_message = tr("OpenGL preview unavailable: %1").arg(m_program->log());
        m_program.reset();
        return;
    }
    glGenTextures(3, m_tex);
    for (GLuint t : m_tex) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    m_vbo.create();
    m_texW = m_texH = 0;
    m_dirty = true;
    m_glReady = true;
}

void PreviewWidget::uploadFrame()
{
    const cam::Frame &f = *m_frame;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool realloc = f.width != m_texW || f.height != m_texH;
    for (int p = 0; p < 3; ++p) {
        const int w = p == 0 ? f.width : f.width / 2;
        const int h = p == 0 ? f.height : f.height / 2;
        glBindTexture(GL_TEXTURE_2D, m_tex[p]);
        // Pooled I420 frames are tightly packed, so no row-length parameter is
        // needed (that keeps this working on GLES2 too).
        if (realloc)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, f.plane[p]);
        else
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE, f.plane[p]);
    }
    m_texW = f.width;
    m_texH = f.height;
}

QRectF PreviewWidget::pictureRect() const
{
    if (m_texW <= 0 || m_texH <= 0)
        return QRectF();
    const double ww = width(), wh = height();
    const double aspect = double(m_texW) / m_texH;
    double w = ww, h = ww / aspect;
    if (h > wh) {
        h = wh;
        w = wh * aspect;
    }
    return QRectF((ww - w) / 2, (wh - h) / 2, w, h);
}

QPointF PreviewWidget::toNormalized(const QPointF &pos) const
{
    QRectF r = pictureRect();
    if (r.isEmpty())
        return QPointF(-1, -1);
    return QPointF((pos.x() - r.x()) / r.width(), (pos.y() - r.y()) / r.height());
}

void PreviewWidget::paintGL()
{
    QPainter painter(this);
    painter.beginNativePainting();
    const QColor bg = palette().color(QPalette::Window).darker(140);
    glClearColor(bg.redF(), bg.greenF(), bg.blueF(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_glReady && m_frame && m_dirty) {
        uploadFrame();
        m_dirty = false;
        // Drop our reference right away: the pipeline can reuse the buffer.
        m_frame.reset();
    }

    const QRectF pr = pictureRect();
    if (m_glReady && m_program && !pr.isEmpty()) {
        const double dpr = devicePixelRatioF();
        glViewport(0, 0, GLsizei(width() * dpr), GLsizei(height() * dpr));
        const float x0 = float(pr.left() / width() * 2 - 1), x1 = float(pr.right() / width() * 2 - 1);
        const float y0 = float(1 - pr.top() / height() * 2), y1 = float(1 - pr.bottom() / height() * 2);
        const GLfloat verts[] = {x0, y0, 0, 0, x1, y0, 1, 0, x0, y1, 0, 1, x1, y1, 1, 1};
        m_vbo.bind();
        m_vbo.allocate(verts, sizeof verts);
        m_program->bind();
        const int posLoc = m_program->attributeLocation("a_pos");
        const int texLoc = m_program->attributeLocation("a_tex");
        m_program->enableAttributeArray(posLoc);
        m_program->enableAttributeArray(texLoc);
        m_program->setAttributeBuffer(posLoc, GL_FLOAT, 0, 2, 4 * sizeof(GLfloat));
        m_program->setAttributeBuffer(texLoc, GL_FLOAT, 2 * sizeof(GLfloat), 2, 4 * sizeof(GLfloat));
        const char *names[3] = {"u_y", "u_u", "u_v"};
        for (int p = 0; p < 3; ++p) {
            glActiveTexture(GLenum(GL_TEXTURE0 + p));
            glBindTexture(GL_TEXTURE_2D, m_tex[p]);
            m_program->setUniformValue(names[p], p);
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_program->disableAttributeArray(posLoc);
        m_program->disableAttributeArray(texLoc);
        m_program->release();
        m_vbo.release();
        glActiveTexture(GL_TEXTURE0);
    }
    painter.endNativePainting();

    painter.setRenderHint(QPainter::Antialiasing);
    if (!pr.isEmpty() && !m_overlay.isEmpty()) {
        QPen pen(QColor(255, 210, 60), 2, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 210, 60, 40));
        for (const QRectF &n : m_overlay) {
            QRectF r(pr.x() + n.x() * pr.width(), pr.y() + n.y() * pr.height(), n.width() * pr.width(),
                     n.height() * pr.height());
            if (m_overlayEllipse)
                painter.drawEllipse(r);
            else
                painter.drawRect(r);
        }
    }
    if (m_dragging && m_mode == Interaction::DrawRect) {
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.setBrush(QColor(255, 255, 255, 40));
        painter.drawRect(QRectF(m_dragStart, m_dragLast).normalized());
    }
    if (!m_message.isEmpty()) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        painter.setFont(f);
        QRectF box = rect().adjusted(24, 0, -24, 0);
        QRectF textRect = painter.boundingRect(box, Qt::AlignCenter | Qt::TextWordWrap, m_message);
        textRect.adjust(-14, -10, 14, 10);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRoundedRect(textRect, 8, 8);
        painter.setPen(Qt::white);
        painter.drawText(box, Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
}

void PreviewWidget::wheelEvent(QWheelEvent *e)
{
    if (m_mode != Interaction::Navigate)
        return;
    const double steps = e->angleDelta().y() / 120.0;
    if (steps != 0)
        Q_EMIT zoomRequested(steps);
    e->accept();
}

void PreviewWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_dragStart = m_dragLast = e->position();
    if (m_mode == Interaction::PickPoint) {
        m_dragging = false;
        QPointF n = toNormalized(e->position());
        if (n.x() >= 0 && n.x() <= 1 && n.y() >= 0 && n.y() <= 1)
            Q_EMIT pointPicked(n);
        return;
    }
    if (m_mode == Interaction::Navigate)
        setCursor(Qt::ClosedHandCursor);
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    const QPointF pos = e->position();
    if (m_mode == Interaction::Navigate) {
        QRectF pr = pictureRect();
        if (!pr.isEmpty()) {
            // Dragging moves the picture with the mouse, i.e. the view the other way.
            Q_EMIT panRequested(-(pos.x() - m_dragLast.x()) / pr.width(), -(pos.y() - m_dragLast.y()) / pr.height());
        }
    }
    m_dragLast = pos;
    if (m_mode == Interaction::DrawRect)
        update();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    if (m_mode == Interaction::Navigate) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_mode == Interaction::DrawRect) {
        QPointF a = toNormalized(m_dragStart), b = toNormalized(e->position());
        QRectF r = QRectF(a, b).normalized().intersected(QRectF(0, 0, 1, 1));
        if (r.width() > 0.01 && r.height() > 0.01)
            Q_EMIT rectDrawn(r);
        update();
    }
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_mode == Interaction::Navigate && e->button() == Qt::LeftButton)
        Q_EMIT doubleClicked();
}

} // namespace ui
