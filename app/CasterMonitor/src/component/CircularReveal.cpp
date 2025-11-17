#include "CircularReveal.h"
#include <QGuiApplication>
#include <QQuickItemGrabResult>
#include <QPainterPath>

CircularReveal::CircularReveal(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    m_target = nullptr;
    m_radius = 0;
    m_anim = new QPropertyAnimation(this, "radius", this);
    m_anim->setDuration(358);
    m_anim->setEasingCurve(QEasingCurve::InOutCubic);
    setVisible(false);
    connect(m_anim, &QPropertyAnimation::finished, this, [=]()
            {
        update();
        setVisible(false);
        Q_EMIT animationFinished(); });
    connect(this, &CircularReveal::radiusChanged, this, [=]()
            { update(); });
}

void CircularReveal::paint(QPainter *painter)
{
    painter->save();
    painter->drawImage(QRect(0, 0, static_cast<int>(width()), static_cast<int>(height())),
                       m_source);
    QPainterPath path;
    path.moveTo(m_center.x(), m_center.y());
    path.addEllipse(QPointF(m_center.x(), m_center.y()), m_radius, m_radius);
    painter->setCompositionMode(QPainter::CompositionMode_Clear);
    QPainterPath rectPath;
    rectPath.addRect(0, 0, width(), height());
    if (m_dark)
    {
        path = rectPath.subtracted(path);
    }
    painter->setBrush(Qt::black);
    painter->drawPath(path);
    painter->restore();
}

void CircularReveal::start(int w, int h, const QPoint &center, int radius, bool dark)
{
    if (dark)
    {
        m_anim->setStartValue(radius);
        m_anim->setEndValue(0);
    }
    else
    {
        m_anim->setStartValue(0);
        m_anim->setEndValue(radius);
    }
    m_center = center;
    m_dark = dark;
    m_grabResult = m_target->grabToImage(QSize(w, h));
    connect(m_grabResult.data(), &QQuickItemGrabResult::ready, this,
            &CircularReveal::handleGrabResult);
}

void CircularReveal::handleGrabResult()
{
    m_grabResult.data()->image().swap(m_source);
    update();
    setVisible(true);
    Q_EMIT imageChanged();
    m_anim->start();
}
