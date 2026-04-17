#!/bin/bash

#切换到当前目录
cd $(dirname "$(readlink -f "$0")")
#执行安装redis服务脚本
$(pwd)/install_redis_service.sh
#切换到当前目录
cd $(dirname "$(readlink -f "$0")")
#执行安装caster服务脚本
$(pwd)/install_caster_service.sh
