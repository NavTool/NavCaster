#include "ClientDataController.h"
#include "CasterMonitor.h"
#include "stdafx.h"


#include <QThreadPool>

ClientDataController::ClientDataController(QObject *parent) : QObject{parent}
{

}

void ClientDataController::loadData()
{
    QThreadPool::globalInstance()->start(
        [this]()
        {
            Q_EMIT loadDataStart();

            m_data.clear();

            auto data_map=CasterMonitor::getInstance()->m_ntrip_client_map;

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
        });

}


