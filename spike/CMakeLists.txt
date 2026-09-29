# M13 集成 spike：ZzRenderView 契约的 Qt 前端可行性验证（独立工程，不进主构建）。
# 注：ZzTermCore 根构建要求 cmake >= 3.26，本工程同步抬到 3.26。
cmake_minimum_required(VERSION 3.26)
project(zzcore_spike CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# spike/ 位于 ZzClawTerm/spike/，../../ 即 github/ 同级目录，ZzTermCore 与其平级。
set(ZZTERMCORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ZzTermCore"
    CACHE PATH "ZzTermCore 源码目录（add_subdirectory 消费）")

# Network 供 ZzSshCore（Qt 公共依赖扫描要求顶层声明，否则 finalize 告警）。
find_package(Qt6 REQUIRED COMPONENTS Widgets Test Network)
qt_standard_project_setup()

# ZzTermCore：静态消费显式 contour OFF（M13 前置批实证的静态链接约束）；
# 只用 native 后端，关闭其测试以加速。
set(ZZTERM_WITH_CONTOUR OFF CACHE BOOL "" FORCE)
set(ZZTERM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ZZTERMCORE_SOURCE_DIR}" zztermcore EXCLUDE_FROM_ALL)

# ZzSshCore（--ssh 链路）：OpenSSL 优先 vendored bundle（ZzClawTerm 根构建同款收编
# 方式，bundle 命中后固化 OPENSSL_ROOT_DIR 供 ZzSshCore 与其嵌套 libssh2 一致解析）；
# libssh2 由 ZzSshCore 自带的嵌套子模块静态编译（其 CMake 检测无既有 libssh2 目标时
# 自动 add_subdirectory）；ZzSshCore 自带测试关闭。BUILD_SHARED_LIBS OFF 须在
# add_subdirectory 之前强制（libssh2 默认 ON），置于此处不影响已处理的 ZzTermCore。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(NOT APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openssl/OpenSSLBundleConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openssl")
    find_package(OpenSSLBundle QUIET)
    if(OpenSSLBundle_FOUND)
        message(STATUS "spike: OpenSSL 使用 vendored bundle ${OPENSSL_VERSION}")
        set(OPENSSL_ROOT_DIR "${OPENSSL_BUNDLE_SELECTED_ROOT}" CACHE PATH
            "vendored OpenSSL bundle 选中根" FORCE)
    endif()
endif()
set(ZZSSHCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ZzSshCore" zzsshcore EXCLUDE_FROM_ALL)

# 注：链接文档名 ZzTerm::ZzTermCore（build-tree 与 install-tree 统一，M13 T1 审查升格修复）。
qt_add_executable(zzcore_spike main.cpp ZzCoreViewWidget.h ZzCoreViewWidget.cpp)
target_link_libraries(zzcore_spike PRIVATE Qt6::Widgets ZzTerm::ZzTermCore ZzTerm::Pty ZzSshCore)

enable_testing()
add_subdirectory(tests)
