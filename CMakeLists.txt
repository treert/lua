cmake_minimum_required(VERSION 3.6)
# fxxk https://stackoverflow.com/questions/6241922/how-to-use-cmake-install-prefix
# 还会突然坏掉，哎。再出一次问题，就在bat里加参数吧。cmake -DCMAKE_INSTALL_PREFIX=C:/MyExe ..
# SET(CMAKE_INSTALL_PREFIX "C:/MyExe" CACHE PATH "my exe dir")

# https://stackoverflow.com/questions/15662497/difference-between-cmake-current-source-dir-and-cmake-current-list-dir
# CMAKE_SOURCE_DIR: Topmost folder(source directory) that contains a CMakeList.txt file. The value never changes in different scopes.
# PROJECT_SOURCE_DIR: The nearest folder that contains CMakeList.txt file, with its own scope, that contains project() command.
# CMAKE_CURRENT_SOURCE_DIR: The nearest folder that contains CMakeList.txt file with its own scope. (File do not need to contain project() command)
# CMAKE_CURRENT_LIST_DIR: The folder that contains currently processed CMakeList.txt or .cmake file.

set(CMAKE_C_STANDARD 17)

# https://zhuanlan.zhihu.com/p/616871895 稍微清楚点
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # C++ 11 是强制要求，不会衰退至低版本
set(CMAKE_CXX_EXTENSIONS OFF)  # 禁止使用编译器特有扩展
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

GET_FILENAME_COMPONENT(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})
#message(${PROJECT_NAME})

# https://gist.github.com/squeek502/0706332efb18edd0601a4074762b0b9a

include_directories (${PROJECT_SOURCE_DIR})

set ( SRC_CORE
    lapi.c     lbaselib.c  lcorolib.c  ldblib.c  ldo.c    lfunc.c  linit.c   llex.c      lmem.c     lobject.c   loslib.c   lstate.c   lstrlib.c  ltablib.c  ltm.c    lutf8lib.c  lzio.c
    lauxlib.c  lcode.c     lctype.c    ldebug.c  ldump.c  lgc.c    liolib.c  lmathlib.c  loadlib.c  lopcodes.c  lparser.c  lstring.c  ltable.c   ltests.c  lundump.c  lvm.c
)

set ( SRC_HEADER
    lapi.h  lauxlib.h  lcode.h  lctype.h  ldebug.h  ldo.h  lfunc.h  lgc.h  ljumptab.h  llex.h  llimits.h  lmem.h  lobject.h  lopcodes.h
    lopnames.h  lparser.h  lprefix.h  lstate.h  lstring.h  ltable.h  ltests.h  ltm.h  lua.h  luaconf.h  lualib.h  lundump.h  lvm.h  lzio.h
)

set ( SRC_LUA lua.c )
set ( SRC_LUAC  )

add_definitions(-D_CRT_SECURE_NO_WARNINGS) # removing the precompiler secure warnings that come up when using functions like scanf()
add_definitions(-DLUAI_MAXCCALLS=128) # windows 上 cstack.lua 溢出报错
# add_compile_definitions( LUAI_ASSERT )

# add_library ( liblua ${SRC_CORE} ${SRC_HEADER} )
# set_target_properties ( liblua PROPERTIES OUTPUT_NAME lua )

add_library ( libluadll SHARED ${SRC_CORE} ${SRC_HEADER} )
target_compile_definitions ( libluadll PRIVATE LUA_BUILD_AS_DLL)
set_target_properties ( libluadll PROPERTIES OUTPUT_NAME mylua ) # 名字不能和liblua一样，不然不能调试了。

add_executable ( lua ${SRC_LUA} mylua.rc)
target_link_libraries ( lua libluadll )

# luac 只能和 lualib 静态链接. 不想改代码，就这样用着吧
add_executable ( luac ${SRC_CORE} luac.c mylua.rc)

# message(FATAL_ERROR "${PROJECT_SOURCE_DIR}/../luafilesystem/src")
# set(LFS_DIR "${PROJECT_SOURCE_DIR}/../luafilesystem/src")
# add_library ( lfs SHARED ${LFS_DIR}/lfs.c ${LFS_DIR}/lfs.h)
# target_include_directories (lfs  PRIVATE ${PROJECT_SOURCE_DIR})
# target_sources(lfs lfs.c)
# target_link_libraries ( lfs libluadll )

# add_library(lfs SHARED lfs.h lfs.c)
# target_link_libraries(lfs libluadll)

# https://stackoverflow.com/questions/10487256/cmake-how-to-properly-copy-static-librarys-header-file-into-usr-include
# File: target_public_headers.cmake 应该放到公共函数中
macro(target_public_headers TARGET)
    set_target_properties(${TARGET} PROPERTIES PUBLIC_HEADER "${ARGN}")
endmacro()

target_public_headers(libluadll lua.h luaconf.h lauxlib.h lualib.h)

# https://stackoverflow.com/questions/22278381/cmake-add-library-followed-by-install-library-destination
install (TARGETS lua luac libluadll
         ARCHIVE DESTINATION lib
         LIBRARY DESTINATION lib
         RUNTIME DESTINATION bin
         PUBLIC_HEADER DESTINATION include
         )