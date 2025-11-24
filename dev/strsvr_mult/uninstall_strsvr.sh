#从supervisor服务中删除

# Supervisor注册的名称
SUPERVISOR_NAME="STRSVR_MULT"


#停止服务
sudo supervisorctl stop $SUPERVISOR_NAME

#删除配置文件
sudo rm /etc/supervisor/conf.d/$SUPERVISOR_NAME.conf

#更新supervisor服务列表
sudo supervisorctl reread
sudo supervisorctl update