#include "PerformanceMonitor.h"

#include <QQuickWindow>

PerformanceMonitor::PerformanceMonitor()
{
    _timer = new QTimer(this);

    // 刷新率
    connect(_timer, &QTimer::timeout, this, [this]
            {
        fps(_frameCount);
        _frameCount = 0; });

    connect(this, &QQuickItem::windowChanged, this, [this]
            {
        if (window()) {
            connect(window(), &QQuickWindow::afterRendering, this, [this] { _frameCount++; }, Qt::DirectConnection);
        } });

    connect(_timer, &QTimer::timeout, this, &PerformanceMonitor::updateUsage);
    _timer->start(1000); // 每秒更新一次
}

int PerformanceMonitor::setUpdateIntv(int ms)
{
    _updateIntv = ms >= 5 ? ms : 1000;
    _timer->start(_updateIntv); // 更新定时器间隔

    return _updateIntv;
}

void PerformanceMonitor::updateUsage()
{
}
