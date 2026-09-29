# M13 spike 的 QTest（offscreen）：复用宿主的 ZzCoreViewWidget 编译单元。
qt_add_executable(tst_spikerender
    tst_spikerender.cpp
    ../ZzCoreViewWidget.h
    ../ZzCoreViewWidget.cpp)
target_link_libraries(tst_spikerender PRIVATE Qt6::Widgets Qt6::Test ZzTerm::Core ZzTerm::Pty)

add_test(NAME tst_spikerender COMMAND tst_spikerender)
set_tests_properties(tst_spikerender PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
