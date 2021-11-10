cmake_minimum_required(VERSION 3.6)

GET_FILENAME_COMPONENT(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})
#message(${PROJECT_NAME})

# 程序员何苦为难程序员，这么简单的东西搞定，蛋疼。哎
# https://gitlab.kitware.com/cmake/cmake/-/issues/21242
# https://stackoverflow.com/questions/39481958/setting-cmake-install-prefix-from-cmakelists-txt-file
# Use this snippet *after* PROJECT(xxx):
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  SET(CMAKE_INSTALL_PREFIX "C:/MyExe")
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

AUX_SOURCE_DIRECTORY(. ALL_SRC)
INCLUDE_DIRECTORIES(.)

# remove the lua.c/luac.c from teh all file lists
SET(LIB_SRC ${ALL_SRC})
LIST(REMOVE_ITEM LIB_SRC ./lua.c ./luac.c ./onelua.c)

ADD_LIBRARY(lua ${LIB_SRC})

ADD_EXECUTABLE(luaexec lua.c)
target_link_libraries(luaexec lua)
set_target_properties(luaexec PROPERTIES OUTPUT_NAME lua)

add_executable(luac luac.c)
target_link_libraries(luac lua)

# 参考
# https://blog.csdn.net/qq_25439881/article/details/104457021
install(TARGETS luaexec DESTINATION bin)
install(TARGETS luac DESTINATION bin)

## 吐槽，这个文件修改了，再次运行cmake 时，不会生效，要删掉build/CMakeCache.txt