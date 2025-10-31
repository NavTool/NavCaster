#include "GnssResourceController.h"
#include "Tool.h"
#include <QThreadPool>

GnssResourceController::GnssResourceController(QObject *parent) : QObject{parent}
{
}

QList<QVariantMap> GnssResourceController::dig(const QString &path, int level)
{
    QList<QVariantMap> list;
    // for (int i = 0; i < 5; ++i) {
    //     QString key = path + "-" + QString::number(i);
    //     // auto rowData = generateRowData();
    //     rowData.insert("key", key);
    //     if (level > 0) {
    //         rowData.insert("children", QVariant::fromValue(dig(key, level - 1)));
    //     }
    //     list.append(rowData);
    // }
    return list;
}

void GnssResourceController::loadData()
{
    QThreadPool::globalInstance()->start(
        [this]()
        {
            Q_EMIT loadDataStart();
            updateBaselineData();
            updateNavFileData();
            updateQualityData();
            updateSolutionData();
            updateStationData();
            updateVectorData();
            updateCloseLoopData();
            updateCoordData();
            updateTaskData();

            updateResourceData();
            updateMapLayerData();

            Q_EMIT loadDataSuccess();
        });
}

void GnssResourceController::updateStationData()
{
    Q_EMIT updateStationDataStart();

    // m_station_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachStation(
        [this](const std::string &key, const std::shared_ptr<station> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);

            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_station_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_station_data.append(data);
            }
        });

    //删除所有本次没有更新的元素
    auto it = m_station_data.begin();
    while (it != m_station_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_station_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_station_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateStationDataSuccess();
}

void GnssResourceController::updateControlPointData()
{
    Q_EMIT updateControlPointDataStart();

    // m_station_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachStation(
        [this](const std::string &key, const std::shared_ptr<station> &st)
        {
            if(!st->is_control_point())
            {
                return; //不是控制点
            }

            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);

            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_controlpoint_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_controlpoint_data.append(data);
            }
        });

    //删除所有本次没有更新的元素
    auto it = m_controlpoint_data.begin();
    while (it != m_controlpoint_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_controlpoint_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_controlpoint_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateControlPointDataSuccess();
}

void GnssResourceController::updateVectorData()
{
    Q_EMIT updateVectorDataStart();

    // m_vector_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachVector(
        [this](const std::string &key, const std::shared_ptr<vector> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);

            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_vector_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_vector_data.append(data);
            }
        });


    //删除所有本次没有更新的元素
    auto it = m_vector_data.begin();
    while (it != m_vector_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_vector_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_vector_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateVectorDataSuccess();
}

void GnssResourceController::updateCloseLoopData()
{
    Q_EMIT updateCloseLoopDataStart();

    // m_vector_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachCloseloop(
        [this](const std::string &key, const std::shared_ptr<closeloop> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);

            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_closeloop_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_closeloop_data.append(data);
            }
        });


    //删除所有本次没有更新的元素
    auto it = m_closeloop_data.begin();
    while (it != m_closeloop_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_closeloop_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_closeloop_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateCloseLoopDataSuccess();
}

void GnssResourceController::updateObsFileData()
{
    Q_EMIT updateObsFileDataStart();

    // m_obsfile_data.clear();

    NavCore::getInstance()->forEachObsfile(
        [this](const std::string &key, const std::shared_ptr<obsfile> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);


            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_obsfile_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_obsfile_data.append(data);
            }
        });

    //删除所有本次没有更新的元素
    auto it = m_obsfile_data.begin();
    while (it != m_obsfile_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_obsfile_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_obsfile_data)
    {
        task["update_flag"] =0;
    }


    Q_EMIT updateObsFileDataSuccess();
}

void GnssResourceController::updateQualityData()
{
    Q_EMIT updateQualityDataStart();

    // m_quality_data.clear();

    NavCore::getInstance()->forEachQuality(
        [this](const std::string &key, const std::shared_ptr<quality> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);


            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_quality_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_quality_data.append(data);
            }
        });

    //删除所有本次没有更新的元素
    auto it = m_quality_data.begin();
    while (it != m_quality_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_quality_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_quality_data)
    {
        task["update_flag"] =0;
    }


    Q_EMIT updateQualityDataSuccess();
}

void GnssResourceController::updateSolutionData()
{
    Q_EMIT updateSolutionDataStart();

    // m_solution_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachSolution(
        [this](const std::string &key, const std::shared_ptr<solution> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);


            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_solution_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_solution_data.append(data);
            }
        });


    //删除所有本次没有更新的元素
    auto it = m_solution_data.begin();
    while (it != m_solution_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_solution_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_solution_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateSolutionDataSuccess();
}

void GnssResourceController::updateCoordData()
{
    Q_EMIT updateCoordDataStart();

    // m_solution_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachCoord(
        [this](const std::string &key, const std::shared_ptr<coord> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);

            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_coord_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_coord_data.append(data);
            }
        });


    //删除所有本次没有更新的元素
    auto it = m_coord_data.begin();
    while (it != m_coord_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_coord_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_coord_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateCoordDataSuccess();
}

void GnssResourceController::updateNavFileData()
{
    Q_EMIT updateNavFileDataStart();

    // m_navfile_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachNavfile(
        [this](const std::string &key, const std::shared_ptr<navfile> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);


            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_navfile_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_navfile_data.append(data);
            }
        });


    //删除所有本次没有更新的元素
    auto it = m_navfile_data.begin();
    while (it != m_navfile_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_navfile_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_navfile_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateNavFileDataSuccess();
}

void GnssResourceController::updateBaselineData()
{
    Q_EMIT updateBaselineDataStart();

    // m_baseline_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachBaseline(
        [this](const std::string &key, const std::shared_ptr<baseline> &st)
        {
            auto info = st->info();
            QVariantMap data= JsonToQVariantMap(info);

            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_baseline_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_baseline_data.append(data);
            }
        });


    //删除所有本次没有更新的元素
    auto it = m_baseline_data.begin();
    while (it != m_baseline_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_baseline_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_baseline_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateBaselineDataSuccess();
}

void GnssResourceController::updateTaskData()
{
    Q_EMIT updateTaskDataStart();

    // m_task_data.clear();
    // 使用回调遍历
    NavCore::getInstance()->forEachTask(
        [this](const std::string &key, const std::shared_ptr<TaskBase> &st)
        {
            auto para=st->get_para();
            QVariantMap data= JsonToQVariantMap(para);

            auto info=st->get_task_info();
            data["UID"]=key.c_str();
            data["state"]=info.state;
            data["state_info"]=info.state_info.c_str();
            data["step"]=info.step;
            data["step_total"]=info.step_total;
            data["step_of_total"]=fmt::format("{}/{}",info.step,info.step_total).c_str();
            data["step_info"]=info.step_info.c_str();
            data["step_percent"]=fmt::format("{:5.2f} %",info.step_percent*100).c_str();
            data["total_percent"]=fmt::format("{:5.2f} %",info.total_percent*100).c_str();

            data["create_time"]=static_cast<qint64>(info.create_time);
            data["start_time"]=static_cast<qint64>(info.start_time);
            data["usage_time"]=static_cast<qint64>(info.usage_time);
            data["finished_time"]=static_cast<qint64>(info.finished_time);

            data["completed"]=info.completed;

            data["update_flag"]=1;

            //查询是否是已有项目
            bool _is_exist=false;
            for(auto &task : m_task_data)
            {
                if (task["UID"] == key.c_str()) {
                    task=data;  //更新指定元素
                    _is_exist=true;
                }
            }
            if(!_is_exist)
            {
                m_task_data.append(data);
            }

        });

    //删除所有本次没有更新的元素
    auto it = m_task_data.begin();
    while (it != m_task_data.end()) {
        if (it->value("update_flag").toInt() == 0) {
            it = m_task_data.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    //所有元素置为0
    for(auto &task : m_task_data)
    {
        task["update_flag"] =0;
    }

    Q_EMIT updateTaskDataSuccess();
}

void GnssResourceController::updateResourceData()
{
    Q_EMIT updateResourceDataStart();

    //更新资源管理器

    // GNSS数据类型

    m_resource_data.clear();

    // 站点
    QVariantMap station_group;
    station_group["key"]="station_group";
    QList<QVariantMap> station_list;
    for(auto iter:m_station_data)
    {
        QVariantMap item;
        item["key"]="station_"+iter["UID"].toString();
        station_list.append(item);
    }
    station_group.insert("children", QVariant::fromValue(station_list));
    // m_resource_data.append(station_group);


    // 控制点
    QVariantMap controlpoint_group;
    controlpoint_group["key"]="controlpoint_group";
    QList<QVariantMap> controlpoint_list;
    for(auto iter:m_controlpoint_data)
    {
        QVariantMap item;
        item["key"]="controlpoint_"+iter["UID"].toString();
        controlpoint_list.append(item);
    }
    controlpoint_group.insert("children", QVariant::fromValue(controlpoint_list));
    // m_resource_data.append(controlpoint_group);


    // 静态基线
    QVariantMap vector_group;
    vector_group["key"]="vector_group";
    QList<QVariantMap> vector_list;
    for(auto iter:m_vector_data)
    {
        QVariantMap item;
        item["key"]="vector_"+iter["UID"].toString();
        vector_list.append(item);
    }
    vector_group.insert("children", QVariant::fromValue(vector_list));
    // m_resource_data.append(vector_group);

    // 动态基线
    QVariantMap baseline_group;
    baseline_group["key"]="baseline_group";
    QList<QVariantMap> baseline_list;
    for(auto iter:m_baseline_data)
    {
        QVariantMap item;
        item["key"]="baseline_"+iter["UID"].toString();
        baseline_list.append(item);
    }
    baseline_group.insert("children", QVariant::fromValue(baseline_list));
    // m_resource_data.append(baseline_group);

    // 闭合环
    QVariantMap closeloop_group;
    closeloop_group["key"]="closeloop_group";
    QList<QVariantMap> closeloop_list;
    for(auto iter:m_closeloop_data)
    {
        QVariantMap item;
        item["key"]="closeloop_"+iter["UID"].toString();
        closeloop_list.append(item);
    }
    closeloop_group.insert("children", QVariant::fromValue(closeloop_list));
    // m_resource_data.append(closeloop_group);

    // 观测文件
    QVariantMap obsfile_group;
    obsfile_group["key"]="obsfile_group";
    QList<QVariantMap> obsfile_list;
    for(auto iter:m_obsfile_data)
    {
        QVariantMap item;
        item["key"]="obsfile_"+iter["UID"].toString();
        obsfile_list.append(item);
    }
    obsfile_group.insert("children", QVariant::fromValue(obsfile_list));
    // m_resource_data.append(obsfile_group);

    // 星历文件
    QVariantMap navfile_group;
    navfile_group["key"]="navfile_group";
    QList<QVariantMap> navfile_list;
    for(auto iter:m_navfile_data)
    {
        QVariantMap item;
        item["key"]="navfile_"+iter["UID"].toString();
        navfile_list.append(item);
    }
    navfile_group.insert("children", QVariant::fromValue(navfile_list));
    // m_resource_data.append(navfile_group);


    QVariantMap gnss_group;
    gnss_group["key"]="gnss_group";
    QList<QVariantMap> gnss_list;
    gnss_list.append(station_group);
    gnss_list.append(controlpoint_group);
    gnss_list.append(vector_group);
    gnss_list.append(baseline_group);
    gnss_list.append(closeloop_group);
    gnss_list.append(obsfile_group);
    gnss_list.append(navfile_group);
    gnss_group.insert("children", QVariant::fromValue(gnss_list));
    m_resource_data.append(gnss_group);


    QVariantMap rtk_group;
    rtk_group["key"]="rtk_group";
    QList<QVariantMap> rtk_list;
    // gnss_list.append(station_group);
    // gnss_list.append(controlpoint_group);
    // gnss_list.append(vector_group);
    // gnss_list.append(baseline_group);
    // gnss_list.append(closeloop_group);
    // gnss_list.append(obsfile_group);
    // gnss_list.append(navfile_group);
    rtk_group.insert("children", QVariant::fromValue(rtk_list));
    m_resource_data.append(rtk_group);

    Q_EMIT updateResourceDataSuccess();
}

void GnssResourceController::updateMapLayerData()
{
    Q_EMIT updateMapLayerDataStart();

    //更新资源管理器

    // GNSS数据类型

    m_maplayer_data.clear();

    // 站点
    QVariantMap station_group;
    station_group["key"]="station_group";
    station_group["view"]=1;
    QList<QVariantMap> station_list;
    for(auto iter:m_station_data)
    {
        QVariantMap item;
        item["key"]="station_"+iter["UID"].toString();
        item["view"]=1;
        station_list.append(item);
    }
    station_group.insert("children", QVariant::fromValue(station_list));
    // m_resource_data.append(station_group);


    // 控制点
    QVariantMap controlpoint_group;
    controlpoint_group["key"]="controlpoint_group";
    controlpoint_group["view"]=1;
    QList<QVariantMap> controlpoint_list;
    for(auto iter:m_controlpoint_data)
    {
        QVariantMap item;
        item["key"]="controlpoint_"+iter["UID"].toString();
        item["view"]=1;
        controlpoint_list.append(item);
    }
    controlpoint_group.insert("children", QVariant::fromValue(controlpoint_list));
    // m_resource_data.append(controlpoint_group);


    // 静态基线
    QVariantMap vector_group;
    vector_group["key"]="vector_group";
    vector_group["view"]=1;
    QList<QVariantMap> vector_list;
    for(auto iter:m_vector_data)
    {
        QVariantMap item;
        item["key"]="vector_"+iter["UID"].toString();
        item["view"]=1;
        vector_list.append(item);
    }
    vector_group.insert("children", QVariant::fromValue(vector_list));
    // m_resource_data.append(vector_group);

    // 动态基线
    QVariantMap baseline_group;
    baseline_group["key"]="baseline_group";
    baseline_group["view"]=1;
    QList<QVariantMap> baseline_list;
    for(auto iter:m_baseline_data)
    {
        QVariantMap item;
        item["key"]="baseline_"+iter["UID"].toString();
        item["view"]=1;
        baseline_list.append(item);
    }
    baseline_group.insert("children", QVariant::fromValue(baseline_list));
    // m_resource_data.append(baseline_group);



    QVariantMap gnss_group;
    gnss_group["key"]="gnss_group";
    QList<QVariantMap> gnss_list;
    gnss_list.append(station_group);
    gnss_list.append(controlpoint_group);
    gnss_list.append(vector_group);
    gnss_list.append(baseline_group);

    gnss_group.insert("children", QVariant::fromValue(gnss_list));
    m_maplayer_data.append(gnss_group);


    QVariantMap rtk_group;
    rtk_group["key"]="rtk_group";
    QList<QVariantMap> rtk_list;
    // gnss_list.append(station_group);
    // gnss_list.append(controlpoint_group);
    // gnss_list.append(vector_group);
    // gnss_list.append(baseline_group);
    // gnss_list.append(closeloop_group);
    // gnss_list.append(obsfile_group);
    // gnss_list.append(navfile_group);
    rtk_group.insert("children", QVariant::fromValue(rtk_list));
    m_maplayer_data.append(rtk_group);

    Q_EMIT updateMapLayerDataSuccess();
}

QVariantMap GnssResourceController::generateStationData(const QString &station_name)
{
    return {
        {"station_name", station_name},
        {"utm_n", (QRandomGenerator::global()->bounded(340000000, 350000000) / 100.0)},
        {"utm_e", QRandomGenerator::global()->bounded(500000000, 600000000) / 1000.0},
        {"utm_u", QRandomGenerator::global()->bounded(500000, 2000000) / 10000.0},
        {"llh_lat", QRandomGenerator::global()->bounded(300000000, 320000000) / 10000000.0},
        {"llh_lon", QRandomGenerator::global()->bounded(1120000000, 1140000000) / 10000000.0},
        {"llh_height", QRandomGenerator::global()->bounded(800000, 2000000) / 10000.0},
        {"ecef_x", -(QRandomGenerator::global()->bounded(200000000, 220000000) / 100.0)},
        {"ecef_y", QRandomGenerator::global()->bounded(503000000, 507000000) / 100.0},
        {"ecef_z", QRandomGenerator::global()->bounded(320000000, 340000000) / 100.0},
        // {"height", 30},
        // {"minimumHeight", 25},
        // {"maximumHeight", 240}
    };
}

QVariantMap GnssResourceController::generateObsFileData(const QString &station_name)
{
    return {
            {"file_name", station_name + "202410300000.24O"},
            {"file_type", tr("static")},
            {"station_name", station_name},
            {"file_start_time", 1698614400},
            {"file_end_time", 1698614400},
            {"file_time_span", 86399}, // 持续时间：终止时间减去起始时间file_end_time-file_start_time
            {"measurement_method", tr("天线座底部")},
            {"measurement_ant_height", 0.000},
            {"ant_height_corrent", 0.000},
            {"ant_phase_height", 0.000},
            {"ant_pedestal_height", 0.000},
            {"ant_manufacturer", "Unknown"},
            {"ant_type", "Unknown"},
            {"ant_sn", "Unknown"},
            {"receiver_sn", "Unknown"},
            {"receiver_type", "Unknown"},
            {"receiver_version", "Unknown"},
            {"rinex_measurement_method", "Unknown"},
            {"rinex_ant_height", "Unknown"},
            {"rinex_ant_type", "Unknown"},
            {"rinex_manufacturer", "Unknown"},

            {"file_path", "F:/Test/" + station_name + "202410300000.24O"}};
}

QVariantMap GnssResourceController::generateBaselineData(const QString &station_start, const QString &station_end)
{
    return {
            {"baseline_id", "B00(" + station_start + "->" + station_end + ")"},
            {"baseline_type", tr("静态")},
            {"start_station", station_start},
            {"end_station", station_end},
            {"start_file", station_start + "202410300000.24O"},
            {"end_file", station_end + "202410300000.24O"},
            {"solution_type", tr("固定解")},
            {"utilization_percentage", QRandomGenerator::global()->bounded(0, 10000) / 100.0},
            {"sync_seconds", 86399},
            {"solution_ratio", QRandomGenerator::global()->bounded(0, 1000000) / 10000.0},
            {"solution_rms", QRandomGenerator::global()->bounded(0, 1000) / 10000.0},
            {"solution_check", tr("合格")},
            {"solution_dx", QRandomGenerator::global()->bounded(400000000, 800000000) / 10000.0},
            {"solution_dy", QRandomGenerator::global()->bounded(400000000, 800000000) / 10000.0},
            {"solution_dz", QRandomGenerator::global()->bounded(400000000, 800000000) / 10000.0},
            {"solution_stdx", QRandomGenerator::global()->bounded(0, 500) / 10000.0},
            {"solution_stdy", QRandomGenerator::global()->bounded(0, 500) / 10000.0},
            {"solution_stdz", QRandomGenerator::global()->bounded(0, 500) / 10000.0},
            {"solution_horizontal_distance", QRandomGenerator::global()->bounded(400000000, 800000000) / 10000.0},   // 平距
            {"solution_slope_distance", QRandomGenerator::global()->bounded(600000000, 1100000000) / 10000.0},       // 斜距
            {"solution_elevation_difference", QRandomGenerator::global()->bounded(0, 3000000) / 10000.0},            // 高差
            {"solution_forward_angle", QRandomGenerator::global()->bounded(0, 3600000) / 10000.0},                   // NS前进方向角
            {"solution_ellipsoidal_distance", QRandomGenerator::global()->bounded(600000000, 1200000000) / 10000.0}, // 椭球距离
            {"solution_geodetic_height", QRandomGenerator::global()->bounded(1000000, 8000000) / 10000.0},           // 大地高
            {"solution_RDOP", QRandomGenerator::global()->bounded(0, 8000) / 10000.0},                               //
            {"solution_PDOP", QRandomGenerator::global()->bounded(0, 8000) / 10000.0},                               //
            {"solution_HDOP", QRandomGenerator::global()->bounded(0, 8000) / 10000.0},                               //
            {"solution_VDOP", QRandomGenerator::global()->bounded(0, 8000) / 100000.},
            {"baseline_enuse", tr("是")}};
}

// QVariantMap GnssResourceController::generateRowData() {
//     return {
//         {"name", m_names.at(QRandomGenerator::global()->bounded(m_names.size()))},
//         {"age", QRandomGenerator::global()->bounded(20, 60)},
//         {"address", m_addresses.at(QRandomGenerator::global()->bounded(m_addresses.size()))},
//         {"avatar", m_avatars.at(QRandomGenerator::global()->bounded(m_avatars.size()))},
//         {"description",
//          m_descriptions.at(QRandomGenerator::global()->bounded(m_descriptions.size()))},
//         // {"height", 48},
//         // {"minimumHeight", 40},
//         // {"maximumHeight", 240},
//         {"expanded", false},
//     };
// }
