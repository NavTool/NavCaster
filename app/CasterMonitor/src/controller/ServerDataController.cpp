#include "ServerDataController.h"
#include "CasterMonitor.h"
#include "stdafx.h"


#include <QThreadPool>

ServerDataController::ServerDataController(QObject *parent) : QObject{parent}
{
    //连接信号和槽
    // connect(_redis_op.get(),&EventUpdateServerData::updateDataFinished,this,&ServerDataController::updateData);
}

void ServerDataController::loadData()
{
    Q_EMIT loadDataStart();

    m_data.clear();

    auto data_map=CasterMonitor::getInstance()->m_ntrip_server_map;

    for(auto iter:data_map)
    {
        auto info = iter.second->info();
        QVariantMap data= JsonToQVariantMap(info);

        if(data["update_flag"].toBool() == false)
        {
            // continue;
        }
        m_data.append(data);
    }

    Q_EMIT loadDataSuccess();

}

