#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
PACKAGE_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
EXECUTABLE_DIR="$PACKAGE_ROOT"

# 固定的可执行程序名称
EXECUTABLE_NAME="CasterService"

# Supervisor注册的名称
SUPERVISOR_NAME="CASTER_SERVICE"


# 守护进程的完整路径
COMMAND="$EXECUTABLE_DIR/$EXECUTABLE_NAME"

# 文件路径
SYSTEMD_CONF_PATH="/etc/systemd/system/$SUPERVISOR_NAME.service"
echo "Caster服务配置路径: $SYSTEMD_CONF_PATH"


# # 生成Caster的systemd的配置文件
sudo tee "$SYSTEMD_CONF_PATH" >/dev/null <<EOL
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
SyslogIdentifier=$SUPERVISOR_NAME
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



