# Every platform produces exactly one file the reader opens: a disk image on macOS, an installer on Windows and a Debian package on Linux.
set(CPACK_PACKAGE_NAME "${WORKPANE_PRODUCT_NAME}")
set(CPACK_PACKAGE_VENDOR "${WORKPANE_ORGANIZATION_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${WORKPANE_PRODUCT_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The native workspace for developers: terminals, code, browser, local servers and AI agents in one window")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/workpane/workpane")
set(CPACK_PACKAGE_CONTACT "Paulo Coutinho <paulocoutinhox@gmail.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE.md")

# Dependencies fetched with the sources declare install rules of their own, so the packages carry only the component of the product.
set(WORKPANE_INSTALL_COMPONENT "Workpane")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};Workpane;${WORKPANE_INSTALL_COMPONENT};/")

if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_DMG_VOLUME_NAME "${WORKPANE_PRODUCT_NAME}")
    workpane_install_application(Workpane COMPONENT ${WORKPANE_INSTALL_COMPONENT})
elseif(WIN32)
    set(CPACK_GENERATOR NSIS)
    set(CPACK_NSIS_PACKAGE_NAME "${WORKPANE_PRODUCT_NAME}")
    set(CPACK_NSIS_DISPLAY_NAME "${WORKPANE_PRODUCT_NAME}")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/extras/images/logo.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/extras/images/logo.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\Workpane.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_PACKAGE_EXECUTABLES "Workpane" "${WORKPANE_PRODUCT_NAME}")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$DESKTOP\\\\Workpane.lnk' '$INSTDIR\\\\bin\\\\Workpane.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\Workpane.lnk'")

    workpane_install_application(Workpane COMPONENT ${WORKPANE_INSTALL_COMPONENT})
else()
    set(CPACK_GENERATOR DEB)
    # The staging directory is reached through the install destination, which keeps the absolute destinations below inside the package.
    set(CPACK_SET_DESTDIR ON)
    set(CPACK_INSTALL_PREFIX "/opt/workpane")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_NAME "workpane")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # The dependencies are read from the libraries the package really links, such as GTK and WebKitGTK, instead of being written by hand.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

    # The executable finds the runtime library through its run path, which names the library directory beside the one it lives in.
    workpane_install_application(Workpane COMPONENT ${WORKPANE_INSTALL_COMPONENT})

    # A launcher and an icon belong to the distribution rather than to the directory the product lives in, so both go where every desktop looks.
    install(FILES "${PROJECT_SOURCE_DIR}/extras/linux/workpane.desktop" DESTINATION "/usr/share/applications" COMPONENT ${WORKPANE_INSTALL_COMPONENT})
    install(FILES "${PROJECT_SOURCE_DIR}/extras/images/logo.png" DESTINATION "/usr/share/icons/hicolor/512x512/apps" RENAME "workpane.png" COMPONENT ${WORKPANE_INSTALL_COMPONENT})
    install(
        CODE "
            file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/usr/bin\")
            file(CREATE_LINK \"/opt/workpane/bin/Workpane\" \"\$ENV{DESTDIR}/usr/bin/workpane\" SYMBOLIC)
        "
        COMPONENT ${WORKPANE_INSTALL_COMPONENT}
    )
endif()

include(CPack)
