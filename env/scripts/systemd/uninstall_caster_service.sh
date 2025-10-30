# 从systemdctl中删除服务

#要删除的服务名
SUPERVISOR_NAME="CASTER_SERVICE"

#停止服务
sudo systemctl stop $SUPERVISOR_NAME
sudo systemctl disable $SUPERVISOR_NAME

#删除脚本
sudo rm /usr/lib/systemd/system/$SUPERVISOR_NAME.service

#重新加载systemctl
sudo systemctl daemon-reload

echo "服务管理提示：$SUPERVISOR_NAME 服务已删除"
