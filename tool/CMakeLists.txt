# cmake最低版本号要求
cmake_minimum_required(VERSION 3.5)
# 项目名称
project(tool LANGUAGES C)
# 选择要使用的编译器，请确保系统环境变量包含编译器路径
set(CMAKE_C_COMPILER "gcc")
# 设置编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -g -Og -Wall -Wsign-compare -Wsign-conversion")
# 查找目录下的所有源文件，并将名称保存到 *_SRCS 变量
aux_source_directory(. MAIN_SRCS)
# 指定生成目标，链接库
add_executable(tool ${MAIN_SRCS})
target_link_libraries(
tool
-Wl,--start-group
libm.a libpthread.a
-Wl,--end-group
)
