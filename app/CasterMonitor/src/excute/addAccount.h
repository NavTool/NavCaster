#pragma once
#include <event2/util.h>
#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "EventWorker.h"
#include "EventOperationBase.h"
#include "stdafx.h"
#include "CasterMonitor.h"


/*
 *      创建一个对象
 *      设置属性
 *      绑定回调
 *      执行操作
 */

class EventAddAccount : public RedisOperationBase
{
public:
    explicit EventAddAccount(): RedisOperationBase() {};

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};


