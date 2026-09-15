set(CPACK_PACKAGE_NAME "Workpane")
set(CPACK_PACKAGE_VENDOR "Workpane")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Workpane")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminals, editors, a browser and AI tasks in one native workspace")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/workpane/workpane")
set(CPACK_PACKAGE_CONTACT "Paulo Coutinho <paulocoutinhox@gmail.com>")

# Every platform produces exactly one file the reader opens, which is a disk image, an installer or a Debian package.
if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
elseif(WIN32)
    set(CPACK_GENERATOR NSIS)
else()
    set(CPACK_GENERATOR DEB)
endif()

if(WIN32)
    set(CPACK_NSIS_PACKAGE_NAME "Workpane")
    set(CPACK_NSIS_DISPLAY_NAME "Workpane")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/extras/images/logo.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/extras/images/logo.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\Workpane.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    # The shortcut names the executable inside the bin directory the installer lays down.
    set(CPACK_PACKAGE_EXECUTABLES "Workpane" "Workpane")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$DESKTOP\\\\Workpane.lnk' '$INSTDIR\\\\bin\\\\Workpane.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\Workpane.lnk'")
endif()

if(UNIX AND NOT APPLE)
    # The staging directory is reached through the install destination rather than through the prefix, which is what leaves an absolute destination inside the package and lets Qt deploy against the prefix the application really installs to.
    set(CPACK_SET_DESTDIR ON)
    # The package carries its own Qt and shares no library with the distribution, so the whole install tree is rooted under one directory of its own.
    # That root is the install prefix rather than the packaging one, because the packaging prefix is what a generator applies when it stages without a destination directory and is ignored once there is one.
    set(CPACK_INSTALL_PREFIX "/opt/workpane")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_NAME "workpane")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # The dependencies are read from the libraries the package really carries rather than written by hand, and the generator finds the bundled ones through the run paths those libraries already declare.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    # A launcher and an icon belong to the distribution rather than to the directory the application lives in, so both are installed where every desktop looks for them.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/extras/linux/workpane.desktop" DESTINATION "/usr/share/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/extras/images/logo.png" DESTINATION "/usr/share/icons/hicolor/512x512/apps" RENAME "workpane.png")

    # The command that starts the application lives where a shell already looks, and it points at the executable the package installs.
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/usr/bin\")
        file(CREATE_LINK \"/opt/workpane/bin/Workpane\" \"\$ENV{DESTDIR}/usr/bin/workpane\" SYMBOLIC)
    ")
endif()

include(CPack)
