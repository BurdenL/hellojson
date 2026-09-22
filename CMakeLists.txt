cmake_minimum_required(VERSION 3.16)

project(hellojson VERSION 0.1 LANGUAGES CXX)

if(MSVC)
    # Match source encoding and the raw dependency-prefix probe.
    add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

set(TS_FILES i18n/hellojson_zh_CN.ts i18n/hellojson_zh_TW.ts i18n/hellojson_en.ts)

# Shared implementation lists keep test and application builds in sync.
set(EDITOR_SOURCES
    src/jsontab.cpp src/jsontab.h src/jsontab_search.cpp src/jsontab_contextmenu.cpp
    src/boundededitor.cpp src/boundededitor.h src/editorlimits.h src/uistrings.h
    src/jsonindex.cpp src/jsonindex.h src/jsontreemodel.cpp src/jsontreemodel.h
    src/jsondatasource.cpp src/jsondatasource.h
    src/largefileview.cpp src/largefileview.h src/largefileactions.cpp
    src/largetreeindex.cpp src/largetreeindex.h src/largetreemodel.cpp src/largetreemodel.h
    src/largefiletasks.cpp src/largefiletasks.h src/jsonhighlighter.cpp src/jsonhighlighter.h)
set(WINDOW_SOURCES
    src/mainwindow.cpp src/mainwindow.h src/mainwindow.ui
    src/mainwindow_actions.cpp src/mainwindow_dialogs.cpp src/mainwindow_documents.cpp
    src/mainwindow_localization.cpp src/mainwindow_search.cpp)
set(PROJECT_SOURCES src/main.cpp assets/icons.qrc ${EDITOR_SOURCES} ${WINDOW_SOURCES})

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(hellojson
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
    # QM_FILES_OUTPUT_VARIABLE disables automatic embedding. Add the generated
    # catalogs explicitly so every executable works without adjacent .qm files.
    qt_add_translations(hellojson
        TS_FILES ${TS_FILES}
        QM_FILES_OUTPUT_VARIABLE QM_FILES
    )
    qt_add_resources(hellojson app_translations
        PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${QM_FILES})
    get_target_property(TRANSLATION_QMAKE Qt6::qmake IMPORTED_LOCATION)
    execute_process(COMMAND "${TRANSLATION_QMAKE}" -query QT_INSTALL_TRANSLATIONS
        OUTPUT_VARIABLE QT_TRANSLATIONS_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(QT_CHINESE_CATALOGS)
    foreach(locale zh_CN zh_TW)
        set(catalog "${QT_TRANSLATIONS_DIR}/qtbase_${locale}.qm")
        if(EXISTS "${catalog}")
            list(APPEND QT_CHINESE_CATALOGS "${catalog}")
            configure_file("${catalog}" "${CMAKE_CURRENT_BINARY_DIR}/qtbase_${locale}.qm" COPYONLY)
        endif()
    endforeach()
    if(QT_CHINESE_CATALOGS)
        qt_add_resources(hellojson qt_chinese_translations
            PREFIX "/i18n" BASE "${QT_TRANSLATIONS_DIR}" FILES ${QT_CHINESE_CATALOGS})
    endif()

else()
    if(ANDROID)
        add_library(hellojson SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(hellojson
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(hellojson_tests tests/tst_json.cpp tests/tst_json.h
        ${EDITOR_SOURCES} ${WINDOW_SOURCES})
    if(QT_VERSION_MAJOR EQUAL 6)
        qt_add_resources(hellojson_tests test_app_translations
            PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${QM_FILES})
        if(QT_CHINESE_CATALOGS)
            qt_add_resources(hellojson_tests test_qt_translations
                PREFIX "/i18n" BASE "${QT_TRANSLATIONS_DIR}" FILES ${QT_CHINESE_CATALOGS})
        endif()
    endif()
    target_include_directories(hellojson_tests PRIVATE src)
    add_executable(hellojson_benchmark tests/benchmark_largefile.cpp tests/stress_largefile.cpp
        ${EDITOR_SOURCES})
    target_include_directories(hellojson_benchmark PRIVATE src)
    target_link_libraries(hellojson_benchmark PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
    if(WIN32)
        target_link_libraries(hellojson_benchmark PRIVATE Psapi)
    endif()
    target_link_libraries(hellojson_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME hellojson_tests COMMAND ${CMAKE_COMMAND}
        "-DTEST_EXECUTABLE=$<TARGET_FILE:hellojson_tests>"
        "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/test-results.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.cmake")
    set_tests_properties(hellojson_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60)
    if(WIN32)
        add_custom_command(TARGET hellojson_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Test>"
                "$<TARGET_FILE_DIR:hellojson_tests>")
        get_target_property(QT_QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
        execute_process(COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
            OUTPUT_VARIABLE QT_PLUGIN_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
        set_tests_properties(hellojson_tests PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${QT_PLUGIN_DIR}/platforms")
    endif()
endif()

target_link_libraries(hellojson PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(hellojson PRIVATE HELLOJSON_VERSION="${PROJECT_VERSION}")
include(cmake/PlatformResources.cmake)

include(GNUInstallDirs)
install(TARGETS hellojson
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(hellojson)

    # ── Deploy Qt DLLs to output dir for running without PATH setup ─────
    if(WIN32)
        find_program(WINDEPLOYQT windeployqt
            HINTS "$ENV{QTDIR}/bin" "${CMAKE_PREFIX_PATH}/bin"
        )
        if(WINDEPLOYQT)
            add_custom_command(TARGET hellojson POST_BUILD
                COMMAND "${WINDEPLOYQT}"
                    --no-translations
                    --no-compiler-runtime
                    --no-opengl-sw
                    "$<TARGET_FILE:hellojson>"
                COMMENT "Deploying Qt DLLs to output directory..."
            )
        else()
            message(WARNING
                "windeployqt not found — Qt DLLs will not be deployed. "
                "Add Qt bin directory to PATH before running.")
        endif()
    endif()
endif()

include(cmake/MsvcNinjaDependencies.cmake)
