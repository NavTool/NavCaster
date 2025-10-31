#include "ServerDataController.h"


#include <QThreadPool>

ServerDataController::ServerDataController(QObject *parent) : QObject{parent}
{
}

void ServerDataController::loadData(const QString UID)
{
    //创建一个线程执行数据读取操作
    QThreadPool::globalInstance()->start(
        [UID,this]()
        {
            Q_EMIT loadDataStart();

            m_data.clear();  //清除数据
          

                for(int i=100;i<120;i++)
                {


                    QVariantMap data;//= JsonToQVariantMap(info);
                   
                    data["mpt"]=i;

                    m_data.append(data);
                }

            
            Q_EMIT loadDataSuccess();
        });
}

