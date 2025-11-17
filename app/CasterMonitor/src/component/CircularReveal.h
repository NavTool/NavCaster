#pragma once

#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QPainter>
#include <QPropertyAnimation>
#include "stdafx.h"

class CircularReveal : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY_AUTO_P(QQuickItem *, target)
    Q_PROPERTY_AUTO(int, radius)
    QML_NAMED_ELEMENT(CircularReveal)
public:
    explicit CircularReveal(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    Q_INVOKABLE void start(int w, int h, const QPoint &center, int radius, bool dark);
    Q_SIGNAL void imageChanged();
    Q_SIGNAL void animationFinished();
    Q_SLOT void handleGrabResult();

private:
    QPropertyAnimation *m_anim = nullptr;
    QImage m_source;
    QPoint m_center;
    QSharedPointer<QQuickItemGrabResult> m_grabResult;
    bool m_dark;
};
