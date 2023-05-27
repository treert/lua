:: https://stackoverflow.com/questions/17063947/get-current-batchfile-directory
@REM @echo off
set batdir=%~dp0
cd /d "%batdir%"

:: 判断文件夹是否存在
if not exist build ( md build )
@echo on
pushd build
cmake -DCMAKE_INSTALL_PREFIX=%batdir%runtime ..
popd
pause