#include "ServerDataController.h"
#include "MonitorCore.h"
#include "Tool.h"


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

            MonitorCore::getInstance()->forEachServer(
                [this](const std::string &key, const std::shared_ptr<server_info> &st)
                {
                    auto info = st->info();
                    QVariantMap data= JsonToQVariantMap(info);

                    data["update_flag"]=1;

                    //查询是否是已有项目
                    bool _is_exist=false;
                    for(auto &task : m_data)
                    {
                        if (task["UID"] == key.c_str()) {
                            task=data;  //更新指定元素
                            _is_exist=true;
                        }
                    }
                    if(!_is_exist)
                    {
                        m_data.append(data);
                    }
                });

            //删除所有本次没有更新的元素
            auto it = m_data.begin();
            while (it != m_data.end()) {
                if (it->value("update_flag").toInt() == 0) {
                    it = m_data.erase(it);  // 删除元素，并更新迭代器
                } else {
                    ++it;  // 仅在未删除时前进迭代器
                }
            }

            //所有元素置为0
            for(auto &task : m_data)
            {
                task["update_flag"] =0;
            }

            Q_EMIT loadDataSuccess();
        });
}

