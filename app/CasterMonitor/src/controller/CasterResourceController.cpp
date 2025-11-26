#include "CasterResourceController.h"
#include "stdafx.h"
#include <QThreadPool>
#include "CasterMonitor.h"


CasterResourceController::CasterResourceController(QObject *parent)
{

}

void CasterResourceController::loadData()
{

}

void CasterResourceController::updateNodeData()
{
    QThreadPool::globalInstance()->start(
        [this]()
        {
            Q_EMIT updateNodeDataStart();

            m_node_status_data.clear();

            auto data_map=CasterMonitor::getInstance()->m_caster_node_map;

            for(auto iter:data_map)
            {
                auto info = iter.second->info();
                QVariantMap data= JsonToQVariantMap(info);

                if(data["update_flag"].toBool() == false)
                {
                    // continue;
                }
                m_node_status_data.append(data);
            }

            Q_EMIT updateNodeDataSuccess();
        });

}
