#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "stdafx.h"
#include "NavCore.h"

class GnssResourceController : public QObject
{
    Q_OBJECT

    Q_PROPERTY_AUTO(QList<QVariantMap>, baseline_data)  // 动态基线信息
    Q_PROPERTY_AUTO(QList<QVariantMap>, navfile_data)   // 星历文件信息
    Q_PROPERTY_AUTO(QList<QVariantMap>, obsfile_data)   // 观测文件信息
    Q_PROPERTY_AUTO(QList<QVariantMap>, quality_data)   // 质检分析信息
    Q_PROPERTY_AUTO(QList<QVariantMap>, solution_data)  // 结果（非详细的历元解信息）
    Q_PROPERTY_AUTO(QList<QVariantMap>, station_data)   // 站点信息
    Q_PROPERTY_AUTO(QList<QVariantMap>, vector_data)    // 静态基线
    Q_PROPERTY_AUTO(QList<QVariantMap>, closeloop_data) // 闭合环
    Q_PROPERTY_AUTO(QList<QVariantMap>, coord_data)     // 坐标
    Q_PROPERTY_AUTO(QList<QVariantMap>, controlpoint_data) // 坐标

    Q_PROPERTY_AUTO(QList<QVariantMap>, resource_data)  // 资源视图

    Q_PROPERTY_AUTO(QList<QVariantMap>, maplayer_data)  // 图层视图

    Q_PROPERTY_AUTO(QList<QVariantMap>, task_data)      // 任务队列



    QML_SINGLETON
    QML_ELEMENT
private:
    explicit GnssResourceController(QObject *parent = nullptr);

public:
    SINGLETON(GnssResourceController)
    static GnssResourceController *create(QQmlEngine *, QJSEngine *)
    {
        return getInstance();
    }

    // 更新所有信息
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData();

    // 更新站点信息
    Q_SIGNAL void updateStationDataStart();
    Q_SIGNAL void updateStationDataSuccess();
    Q_INVOKABLE void updateStationData();

    //更新控制点信息
    Q_SIGNAL void updateControlPointDataStart();
    Q_SIGNAL void updateControlPointDataSuccess();
    Q_INVOKABLE void updateControlPointData();

    // 更新基线信息
    Q_SIGNAL void updateBaselineDataStart();
    Q_SIGNAL void updateBaselineDataSuccess();
    Q_INVOKABLE void updateBaselineData();

    // 更新星历文件信息
    Q_SIGNAL void updateNavFileDataStart();
    Q_SIGNAL void updateNavFileDataSuccess();
    Q_INVOKABLE void updateNavFileData();

    // 更新观测文件信息
    Q_SIGNAL void updateObsFileDataStart();
    Q_SIGNAL void updateObsFileDataSuccess();
    Q_INVOKABLE void updateObsFileData();

    // 更新质检信息
    Q_SIGNAL void updateQualityDataStart();
    Q_SIGNAL void updateQualityDataSuccess();
    Q_INVOKABLE void updateQualityData();

    // 更新解算结果信息
    Q_SIGNAL void updateSolutionDataStart();
    Q_SIGNAL void updateSolutionDataSuccess();
    Q_INVOKABLE void updateSolutionData();

    // 更新坐标点
    Q_SIGNAL void updateCoordDataStart();
    Q_SIGNAL void updateCoordDataSuccess();
    Q_INVOKABLE void updateCoordData();

    // 更新基线信息
    Q_SIGNAL void updateVectorDataStart();
    Q_SIGNAL void updateVectorDataSuccess();
    Q_INVOKABLE void updateVectorData();

    // 更新闭合环信息
    Q_SIGNAL void updateCloseLoopDataStart();
    Q_SIGNAL void updateCloseLoopDataSuccess();
    Q_INVOKABLE void updateCloseLoopData();

    // 更新任务信息
    Q_SIGNAL void updateTaskDataStart();
    Q_SIGNAL void updateTaskDataSuccess();
    Q_INVOKABLE void updateTaskData();

    // 更新资源侧边栏数据
    Q_SIGNAL void updateResourceDataStart();
    Q_SIGNAL void updateResourceDataSuccess();
    Q_INVOKABLE void updateResourceData();

    // 更新图层侧边栏数据
    Q_SIGNAL void updateMapLayerDataStart();
    Q_SIGNAL void updateMapLayerDataSuccess();
    Q_INVOKABLE void updateMapLayerData();



    // 模拟数据
    Q_INVOKABLE QVariantMap generateStationData(const QString &station_name);
    Q_INVOKABLE QVariantMap generateObsFileData(const QString &station_name);
    Q_INVOKABLE QVariantMap generateBaselineData(const QString &station_start, const QString &station_end);

private:
    QList<QVariantMap> dig(const QString &path, int level);

private:
    // 站点信息
    // 站点名
    QStringList m_station = {"HBJM01", "HBJM02", "HBJM03", "HBJM04", "HBJM05", "HBJM06", "HBJM07", "HBJM08", "HBJM09", "HBSZ01", "HBSZ05", "HBTM01", "HBXY10", "HBXY11"};
    // 经纬度、高程，随机生成（ECEF坐标、当地坐标，通过转换获得）

    // 观测文件信息
    // 文件名：[测站]+202410300000.240
    // 文件类型：静态
    // 测站：随机站点名
    // 开始日期：随机日期（2024）
    // 结束日期：[开始日期]+[时间段]
    // 时间段：随机时长（0-86400s）
    // 测量方式：天线座底部
    // 测量天线高：0.0000
    // 天线相位中心：0.0000
    // 天线座底部高：0.0000
    // 天线厂商：Unknown
    // 天线类型：Unknown
    // 接收机：
    // 接收机类型：
    // 文件路径：可执行程序当前路径+[文件名]

    // 基线信息
    // 基线ID:B+index+([文件名]+[文件名])
    // 基线类型：静态
    // 起点：[文件测站]
    // 终点：[文件测站]
    // 解算类型：未解算/固定解
    // 利用率：未解算：0/固定解（93-100）
    // 同步时间：随机秒数（7200-86400）
    // Ratio:(0.0-99.0)
    // RMS:0.000-0.0300
    // 合格：合格/检查
    // Dx、Dy、Dz
    // StdX、StdY、StdZ
    // 距离：sqrt(x2+y2+z2)
    // 使用：是
    // 平距
    // 斜距
    // 高差
    // NS前进方位角
    // 椭球距离
    // Δ大地高
    // RDOP
    // PDOP
    // HDOP
    // VDOP
};
