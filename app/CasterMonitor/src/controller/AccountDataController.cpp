#include "AccountDataController.h"
#include "CasterMonitor.h"

#include <QThreadPool>

AccountDataController::AccountDataController(QObject *parent) : QObject{parent}
{
}

void AccountDataController::loadData()
{
    Q_EMIT loadDataStart();

    m_data.clear();

    auto data_map=CasterMonitor::getInstance()->m_user_account_map;

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

