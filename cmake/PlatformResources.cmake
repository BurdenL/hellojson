# Keep native desktop resources separate from the shared Qt implementation.
include(GNUInstallDirs)
if(WIN32)
    enable_language(RC)
    configure_file("${PROJECT_SOURCE_DIR}/assets/windows-icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/windows-icon.rc" @ONLY)
    target_sources(hellojson PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows-icon.rc")
    set_target_properties(hellojson PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set(app_icon "${PROJECT_SOURCE_DIR}/assets/icons/hellojson.icns")
    target_sources(hellojson PRIVATE "${app_icon}")
    set_source_files_properties("${app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(hellojson PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.burdenl.hellojson"
        MACOSX_BUNDLE_BUNDLE_NAME "HelloJson"
        MACOSX_BUNDLE_ICON_FILE "hellojson.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES "${PROJECT_SOURCE_DIR}/assets/linux/io.github.burdenl.hellojson.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    foreach(size 16 24 32 48 64 128 256 512 1024)
        install(FILES "${PROJECT_SOURCE_DIR}/assets/icons/hellojson-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
            RENAME hellojson.png)
    endforeach()
    install(FILES "${PROJECT_SOURCE_DIR}/assets/icons/hellojson.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()
