#pragma once

#include <QQuickItem>
#include "src/stdafx.h"
#include <QTimer>

class PerformanceMonitor : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY_AUTO(int, fps)
    Q_PROPERTY_AUTO(int, cpu)
    Q_PROPERTY_AUTO(int, memory)
    QML_ELEMENT
public:
    PerformanceMonitor();
    int setUpdateIntv(int ms);

private slots:
    void updateUsage();

private:
    int _frameCount = 0;

    int _updateIntv = 1000;

    QTimer *_timer;
};
