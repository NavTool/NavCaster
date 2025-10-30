#!/bin/bash

#切换到当前目录
cd $(dirname "$(readlink -f "$0")")
#执行安装redis服务删除脚本
$(pwd)/uninstall_redis_service.sh
#切换到当前目录
cd $(dirname "$(readlink -f "$0")")
#执行安装caster服务删除脚本
$(pwd)/uninstall_caster_service.sh
