#!/bin/bash

cd $(dirname "$(readlink -f "$0")")
cd ../../..
# 获取当前服务的根目录
#EXECUTABLE_DIR=$(dirname "$(readlink -f "$0")")
EXECUTABLE_DIR=$(pwd)

# 固定的可执行程序名称
EXECUTABLE_NAME="Caster_Service"

# Supervisor注册的名称
SUPERVISOR_NAME="CASTER_SERVICE"

# 配置文件路径
CONFIG_FILE="/etc/supervisor/conf.d/$SUPERVISOR_NAME.conf"

# 守护进程的完整路径
COMMAND="$EXECUTABLE_DIR/$EXECUTABLE_NAME"

# 文件路径
SUPERVISOR_SERVICE_PATH="/usr/lib/systemd/system/supervisor.service "
echo "Supervisor服务配置路径: $SUPERVISOR_SERVICE_PATH"

# 检测系统包管理器并安装 Supervisor
install_supervisor() {
    if which apt >/dev/null 2>&1; then
        echo "正在通过 apt 安装 Supervisor..."
        sudo apt update
        sudo apt install -y supervisor
    elif which yum >/dev/null 2>&1; then
        echo "正在通过 yum 安装 Supervisor..."
        sudo yum install -y supervisor
    else
        echo "无法识别系统的包管理器，请手动安装 Supervisor。"
        exit 1
    fi
}

# 检查 Supervisor 是否已安装
if ! which supervisorctl >/dev/null 2>&1; then
    echo "Supervisor 未安装，正在安装..."
    install_supervisor
else
    echo "Supervisor 已安装。"
fi


# # 修改 Supervisor limit上限
cat <<EOL > $SUPERVISOR_SERVICE_PATH
[Service]
LimitNOFILE=65536
LimitNPROC=65536
ExecStart=/usr/bin/supervisord -n -c /etc/supervisor/supervisord.conf
ExecStop=/usr/bin/supervisorctl $OPTIONS shutdown
ExecReload=/usr/bin/supervisorctl -c /etc/supervisor/supervisord.conf $OPTIONS reload
KillMode=process
Restart=on-failure
RestartSec=50s

[Install]
WantedBy=multi-user.target
EOL


# 重新加载 systemd的Supervisor 配置
systemctl daemon-reload
systemctl restart supervisor

echo "Systemd 配置已重新加载。"

# 检查文件是否存在，存在则覆盖
if [ -f "$CONFIG_FILE" ]; then
    echo "配置文件已存在，将覆盖 $CONFIG_FILE"
else
    echo "创建新的配置文件: $CONFIG_FILE"
fi

# 创建或覆盖 Supervisor 配置文件
cat <<EOL > $CONFIG_FILE
[program:$SUPERVISOR_NAME]
command=$COMMAND         
directory=$EXECUTABLE_DIR    
startsecs=3
startretries=3        
stderr_logfile=$EXECUTABLE_DIR/$SUPERVISOR_NAME.err.log  
stdout_logfile=$EXECUTABLE_DIR/$SUPERVISOR_NAME.out.log  
stderr_logfile_maxbytes=2MB
stdout_logfile_maxbytes=2MB
user=$USER            ; 指定运行用户
priority=999
numprocs=1

EOL

# 提示操作完成
echo "Supervisor配置文件已生成或覆盖: $CONFIG_FILE"

# 重载 Supervisor 配置
echo "重新加载 Supervisor 配置..."
sudo supervisorctl reread
sudo supervisorctl update

# 提示如何启动和关闭服务
echo "服务管理提示："
echo "启动服务,请运行:sudo supervisorctl start $SUPERVISOR_NAME"
echo "停止服务,请运行:sudo supervisorctl stop $SUPERVISOR_NAME"
echo "重启服务,请运行:sudo supervisorctl restart $SUPERVISOR_NAME"
echo "服务状态,请运行:sudo supervisorctl status $SUPERVISOR_NAME"



