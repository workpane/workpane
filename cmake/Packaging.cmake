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
    install(TARGETS Workpane BUNDLE DESTINATION . COMPONENT ${WORKPANE_INSTALL_COMPONENT})

    # An identity signs with the hardened runtime and a secure timestamp, which notarization requires, and with the entitlements that let a page reach the camera and the microphone under that runtime, while an ad hoc signature carries none of them.
    set(WORKPANE_CODESIGN_OPTIONS "--timestamp=none")
    if(NOT WORKPANE_CODESIGN_IDENTITY STREQUAL "-")
        set(WORKPANE_CODESIGN_OPTIONS "--options runtime --timestamp --entitlements \"${PROJECT_SOURCE_DIR}/extras/macos/Workpane.entitlements\"")
    endif()

    # The bundle is sealed after it is assembled, because the runtime library and the resources copied into it are part of what the signature covers.
    install(
        CODE "execute_process(COMMAND codesign --force --deep ${WORKPANE_CODESIGN_OPTIONS} --sign \"${WORKPANE_CODESIGN_IDENTITY}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Workpane.app\" COMMAND_ERROR_IS_FATAL ANY)"
        COMPONENT ${WORKPANE_INSTALL_COMPONENT}
    )
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

    # The runtime library sits beside the executable, which is the first place Windows looks for it.
    install(TARGETS Workpane RUNTIME DESTINATION bin COMPONENT ${WORKPANE_INSTALL_COMPONENT})
    install(FILES "$<TARGET_FILE:varn>" DESTINATION bin COMPONENT ${WORKPANE_INSTALL_COMPONENT})

    # The runtime of the compiler travels beside the executable as well, so a machine without the redistributable of Visual C++ still starts the product.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT ${WORKPANE_INSTALL_COMPONENT})
    include(InstallRequiredSystemLibraries)
    install(DIRECTORY "${WORKPANE_STAGED_RESOURCES}/" DESTINATION share/workpane COMPONENT ${WORKPANE_INSTALL_COMPONENT})
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
    install(TARGETS Workpane RUNTIME DESTINATION bin COMPONENT ${WORKPANE_INSTALL_COMPONENT})
    install(FILES "$<TARGET_FILE:varn>" "$<TARGET_SONAME_FILE:varn>" DESTINATION lib COMPONENT ${WORKPANE_INSTALL_COMPONENT})
    install(DIRECTORY "${WORKPANE_STAGED_RESOURCES}/" DESTINATION share/workpane COMPONENT ${WORKPANE_INSTALL_COMPONENT})

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
