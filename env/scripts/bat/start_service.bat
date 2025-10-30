@echo off
chcp 65001 >nul

echo 运行状态检测脚本启动

:CheckLoop


echo 检查 Redis 服务器是否在运行
tasklist /fi "imagename eq redis-server.exe" | findstr /i "redis-server.exe" >nul
if %errorlevel% neq 0 (
    echo Redis 服务不存在/已停止！停止正在运行的服务...
    taskkill /f /im "Caster_Service.exe"
    
    echo 尝试重新启动 Redis 服务...
    start "" "env/redis-server.exe"
    timeout /t 5 /nobreak
    
    echo 重新启动服务...
    start "" "Caster_Service.exe"
)

echo 检查可执行程序是否在运行

tasklist /fi "imagename eq Caster_Service.exe" | findstr /i "Caster_Service.exe" >nul
if %errorlevel% neq 0 (
    echo 服务已停止！尝试重新启动...
    start "" "Caster_Service.exe"
)

echo 运行状态检测脚本检测完成，等待下一次检测...
timeout /t 10 /nobreak

goto CheckLoop
