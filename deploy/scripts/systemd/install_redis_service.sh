#!/bin/bash

cd $(dirname "$(readlink -f "$0")")
cd ../../redis
# 获取当前服务的根目录
#EXECUTABLE_DIR=$(dirname "$(readlink -f "$0")")
EXECUTABLE_DIR=$(pwd)

# 固定的可执行程序名称
EXECUTABLE_NAME="redis-server"

# Supervisor注册的名称
SUPERVISOR_NAME="REDIS_SERVICE"


# 守护进程的完整路径
COMMAND="$EXECUTABLE_DIR/$EXECUTABLE_NAME $EXECUTABLE_DIR/redis.config"

# 文件路径
SYSTEMD_CONF_PATH="/usr/lib/systemd/system/$SUPERVISOR_NAME.service "
echo "Caster服务配置路径: $SYSTEMD_CONF_PATH"


# # 生成Caster的systemd的配置文件
cat <<EOL > $SYSTEMD_CONF_PATH
[Unit]
Description=Caster Service Running in the Background
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=$COMMAND
Restart=on-failure
RestartSec=5
User=$USER 
WorkingDirectory=$EXECUTABLE_DIR
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=$SERVICE_NAME
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
EOL


#重新加载systemctl
sudo systemctl daemon-reload

# 提示如何启动和关闭服务
echo "服务管理提示："
echo "启动服务,请运行:sudo systemctl start $SUPERVISOR_NAME"
echo "停止服务,请运行:sudo systemctl stop $SUPERVISOR_NAME"
echo "重启服务,请运行:sudo systemctl restart $SUPERVISOR_NAME"
echo "服务状态,请运行:sudo systemctl status $SUPERVISOR_NAME"



