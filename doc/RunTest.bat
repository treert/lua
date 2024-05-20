@echo off
set batdir=%~dp0
pushd "%batdir%"

pushd ..\testes

..\runtime\bin\lua -W all.lua >nul

popd
popd