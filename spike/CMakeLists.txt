# M13 集成 spike：ZzRenderView 契约的 Qt 前端可行性验证（独立工程，不进主构建）。
# 注：ZzTermCore 根构建要求 cmake >= 3.26，本工程同步抬到 3.26。
cmake_minimum_required(VERSION 3.26)
project(zzcore_spike CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# spike/ 位于 ZzClawTerm/spike/，../../ 即 github/ 同级目录，ZzTermCore 与其平级。
set(ZZTERMCORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ZzTermCore"
    CACHE PATH "ZzTermCore 源码目录（add_subdirectory 消费）")

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
qt_standard_project_setup()

# ZzTermCore：静态消费显式 contour OFF（M13 前置批实证的静态链接约束）；
# 只用 native 后端，关闭其测试以加速。
set(ZZTERM_WITH_CONTOUR OFF CACHE BOOL "" FORCE)
set(ZZTERM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ZZTERMCORE_SOURCE_DIR}" zztermcore EXCLUDE_FROM_ALL)

# 注：链接文档名 ZzTerm::ZzTermCore（build-tree 与 install-tree 统一，M13 T1 审查升格修复）。
qt_add_executable(zzcore_spike main.cpp ZzCoreViewWidget.h ZzCoreViewWidget.cpp)
target_link_libraries(zzcore_spike PRIVATE Qt6::Widgets ZzTerm::ZzTermCore ZzTerm::Pty)

enable_testing()
add_subdirectory(tests)
