#include "ClientDataController.h"
#include "CasterMonitor.h"
#include "stdafx.h"


#include <QThreadPool>

ClientDataController::ClientDataController(QObject *parent) : QObject{parent}
{
    //连接信号和槽
    connect(_redis_op.get(),&EventUpdateClientData::updateDataFinished,this,&ClientDataController::updateData);
}

void ClientDataController::loadData()
{
    CasterMonitor::getInstance()->excute_caster_redis(_redis_op);
}

void ClientDataController::updateData()
{
    //创建一个线程执行数据读取操作
    QThreadPool::globalInstance()->start(
        [this]()
        {
            Q_EMIT loadDataStart();
            for (auto it = _redis_op->m_data.begin(); it != _redis_op->m_data.end(); ++it)
            {
                QVariantMap data = *it;
                data["update_flag"]=1;

                //查询是否是已有项目
                bool _is_exist=false;
                for(auto &item : m_data)
                {
                    if (item["UID"] == data["UID"]) {
                        item=data;  //更新指定元素
                        _is_exist=true;
                    }
                }
                if(!_is_exist)
                {
                    m_data.append(data);
                }
            }

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
            for(auto &item : m_data)
            {
                item["update_flag"] =0;
            }
            Q_EMIT loadDataSuccess();
        });
}

void EventUpdateClientData::execute(redisAsyncContext *ctx) {
    // Q_UNUSED(base);

    redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL USR:STAT ");

    // 执行任务逻辑
}

void EventUpdateClientData::Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 解析数据
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<EventUpdateClientData *>(privdata);

    auto&data = svr->m_data;

    data.clear();

    if (!reply)
    {
        return;
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        return;
    }

    // 更新data
    for (int i = 0; i < reply->elements; i += 2)
    {
        std::string field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        QVariantMap item= JsonToQVariantMap(StringToJson(value));

        data.append(item);
    }

    Q_EMIT svr->updateDataFinished();
}

