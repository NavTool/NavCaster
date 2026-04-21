@echo off
chcp 65001 >nul
setlocal

set SCRIPT_DIR=%~dp0
for %%I in ("%SCRIPT_DIR%\..\..") do set ROOT_DIR=%%~fI

cd /d "%ROOT_DIR%"

echo 运行状态检测脚本启动，当前目录: %ROOT_DIR%

:CheckLoop

echo 检查可执行程序是否在运行

tasklist /fi "imagename eq CasterService.exe" | findstr /i "CasterService.exe" >nul
if %errorlevel% neq 0 (
    echo 服务已停止！尝试重新启动...
    start "" "%ROOT_DIR%\CasterService.exe"
)

echo 运行状态检测脚本检测完成，等待下一次检测...
timeout /t 10 /nobreak

goto CheckLoop
