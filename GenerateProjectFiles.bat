:: https://stackoverflow.com/questions/17063947/get-current-batchfile-directory
@echo off
:: --HAS ENDING BACKSLASH
set batdir=%~dp0
:: --MISSING ENDING BACKSLASH
:: set batdir=%CD%
cd /d "%batdir%"

:: 判断文件夹是否存在
if not exist build ( md build )
@echo on
pushd build
cmake -DCMAKE_INSTALL_PREFIX=C:/MyExe ..
popd
pause