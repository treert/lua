:: 判断文件夹是否存在
if not exist build ( md build )

cd build
cmake -DCMAKE_INSTALL_PREFIX=C:/MyExeTest ..

pause