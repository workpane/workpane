include(FetchContent)

# Every dependency is pinned by an immutable archive and its digest, so a build reads exactly the sources it was reviewed against.
set(FETCHCONTENT_QUIET ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "Policies a pinned dependency that declares an older version is configured with, the oldest CMake keeps without a deprecation warning")

# Answers the arguments that apply the patches of a dependency to its sources and name its archive after them, so the archive is extracted again whenever a patch changes and every patch meets the sources it was written for.
function(workpane_patches output name extension source)
    set(arguments "")
    set(digests "")
    set(keyword PATCH_COMMAND)

    # A patch is a dependency of the configure step, so an edited patch names a new archive and applies to pristine sources.
    foreach(patch IN LISTS ARGN)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/${patch}")
        file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/${patch}" digest)
        string(APPEND digests "${digest}")
        list(APPEND arguments ${keyword} ${CMAKE_COMMAND} -D SOURCE=${source} -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/${patch})
        set(keyword COMMAND)
    endforeach()

    string(SHA256 signature "${digests}")
    string(SUBSTRING "${signature}" 0 16 signature)
    list(APPEND arguments DOWNLOAD_NAME ${name}-${signature}.${extension})
    set(${output} ${arguments} PARENT_SCOPE)
endfunction()

# The JSON library is built first, and Varn takes its target instead of fetching its own, so the runtime and the product share one copy.
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    EXCLUDE_FROM_ALL
    SYSTEM
)
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# Varn is consumed through its embeddable library, which exports only the C API and links every dependency privately, and it fetches CPM and each library it builds from an archive pinned by its digest, so the digest of its own archive pins every source it builds.
FetchContent_Declare(
    varn
    URL https://github.com/varn-org/varn/archive/19d8d869a61a0684583f5c951f956a7e52374619.tar.gz
    URL_HASH SHA256=1fdde6a16c4e228157e9a947877931164940007871544388e642173da9e232c7
    EXCLUDE_FROM_ALL
    SYSTEM
)
set(VARN_TARGET lib CACHE STRING "Varn artifact the product embeds" FORCE)
set(VARN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VARN_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

# Varn links its static dependencies into a shared library, so every one of them is built as position independent code, which the thread local storage of Poco needs on x86_64.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_MakeAvailable(varn)
unset(CMAKE_POSITION_INDEPENDENT_CODE)

workpane_patches(glfwPatches glfw tar.gz <SOURCE_DIR> InputMethodText.cmake)
FetchContent_Declare(
    glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.5.1.tar.gz
    URL_HASH SHA256=5234f4f29473e9a06bc7847d8371858dd135d38466eeeaa652fdc9f8f9ff0c20
    ${glfwPatches}
    EXCLUDE_FROM_ALL
    SYSTEM
)
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_INSTALL OFF CACHE INTERNAL "")
# Native web views embed into an X11 window on Linux, so the window system is X11 and Wayland sessions reach it through XWayland.
set(GLFW_BUILD_WAYLAND OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(glfw)
workpane_enable_sanitizers(glfw)

# The runtime builds zlib and libpng and serves both to every library of the build, so FreeType inflates with that zlib and reads the pictures of color emoji with that libpng.
if(NOT TARGET ZLIB::ZLIB OR NOT TARGET PNG::PNG)
    message(FATAL_ERROR "The runtime no longer builds the zlib and the libpng FreeType reads color emoji with")
endif()

# The libpng of the runtime is a target already, so finding it answers that target instead of searching the system.
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/png-config.cmake" "")

FetchContent_Declare(
    freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-3.tar.gz
    URL_HASH SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261
    EXCLUDE_FROM_ALL
    SYSTEM
)
set(FT_REQUIRE_ZLIB ON CACHE INTERNAL "")
set(FT_REQUIRE_PNG ON CACHE INTERNAL "")
set(FT_DISABLE_BZIP2 ON CACHE INTERNAL "")
set(FT_DISABLE_HARFBUZZ ON CACHE INTERNAL "")
set(FT_DISABLE_BROTLI ON CACHE INTERNAL "")
set(SKIP_INSTALL_ALL ON CACHE INTERNAL "")
FetchContent_MakeAvailable(freetype)

workpane_patches(imguiPatches imgui tar.gz <SOURCE_DIR> ColorGlyphStrikes.cmake IgnorableCharacters.cmake)
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    ${imguiPatches}
    SYSTEM
)
FetchContent_MakeAvailable(imgui)

workpane_patches(editorPatches imgui_text_editor tar.gz <SOURCE_DIR>/TextEditor.cpp CenterEditorRows.cmake EditorLeavesFind.cmake EditorRenumbersOverlays.cmake EditorGuidesIndentedLines.cmake EditorCommandArrows.cmake EditorReadsOnlyItsLines.cmake EditorBoundsThePointer.cmake)
FetchContent_Declare(
    imgui_text_editor
    URL https://github.com/goossens/ImGuiColorTextEdit/archive/6919c91c6fb1e9ee94438df894b604646a747afb.tar.gz
    URL_HASH SHA256=cb4d08de1e80e2842933d30085991e2776642e2685fc899bc702a3c94280a20a
    ${editorPatches}
    SYSTEM
)
FetchContent_MakeAvailable(imgui_text_editor)

FetchContent_Declare(
    sqlite
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
    SYSTEM
)
FetchContent_MakeAvailable(sqlite)

FetchContent_Declare(
    miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.22.tar.gz
    URL_HASH SHA256=bcb07bfb27e6fa94d34da73ba2d5642d4940b208ec2a660dbf4e52e6b7cd492f
    SOURCE_SUBDIR none
    SYSTEM
)
FetchContent_MakeAvailable(miniaudio)

workpane_patches(libvtermPatches libvterm tar.gz <SOURCE_DIR> TerminalAttributes.cmake TerminalWidths.cmake)
FetchContent_Declare(
    libvterm
    URL https://launchpad.net/libvterm/trunk/v0.3/+download/libvterm-0.3.3.tar.gz
    URL_HASH SHA256=09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0
    ${libvtermPatches}
    SYSTEM
)
FetchContent_MakeAvailable(libvterm)

# Windows builds its web views on WebView2 directly, through the headers and the static loader of its SDK, while macOS and Linux build theirs through the web view library.
if(WIN32)
    FetchContent_Declare(
        microsoft_web_webview2
        URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1150.38
        URL_HASH SHA256=921c004bd1764b585496b2eb3eec0a59a9a98e698246f1d9a3f1c08d1d84ebd5
        DOWNLOAD_NAME microsoft.web.webview2.1.0.1150.38.zip
    )
    FetchContent_MakeAvailable(microsoft_web_webview2)
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" webview2Architecture)
    add_library(workpane_webview2 INTERFACE)
    target_include_directories(workpane_webview2 SYSTEM INTERFACE "${microsoft_web_webview2_SOURCE_DIR}/build/native/include")
    target_link_libraries(workpane_webview2 INTERFACE "${microsoft_web_webview2_SOURCE_DIR}/build/native/${webview2Architecture}/WebView2LoaderStatic.lib")
else()
    workpane_patches(webviewPatches webview tar.gz <SOURCE_DIR>/core/include/webview/webview.h WebViewDataDirectory.cmake)
    FetchContent_Declare(
        webview
        URL https://github.com/webview/webview/archive/refs/tags/0.12.0.tar.gz
        URL_HASH SHA256=e2c8d0bed3fcd13e624074448b7d3b1d0b0d03546a15839f17ee0013f2cba1db
        ${webviewPatches}
        EXCLUDE_FROM_ALL
        SYSTEM
    )
    set(WEBVIEW_BUILD_TESTS OFF CACHE INTERNAL "")
    set(WEBVIEW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
    set(WEBVIEW_BUILD_DOCS OFF CACHE INTERNAL "")
    set(WEBVIEW_BUILD_AMALGAMATION OFF CACHE INTERNAL "")
    set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE INTERNAL "")
    set(WEBVIEW_BUILD_STATIC_LIBRARY OFF CACHE INTERNAL "")
    set(WEBVIEW_INSTALL_DOCS OFF CACHE INTERNAL "")
    set(WEBVIEW_INSTALL_TARGETS OFF CACHE INTERNAL "")
    set(WEBVIEW_ENABLE_CHECKS OFF CACHE INTERNAL "")
    set(WEBVIEW_ENABLE_PACKAGING OFF CACHE INTERNAL "")
    set(WEBVIEW_USE_COMPAT_MINGW OFF CACHE INTERNAL "")
    # GTK 3 is the toolkit whose windows can be plugged into a foreign X11 window, which is how a web view joins the product window on Linux.
    set(WEBVIEW_WEBKITGTK_API 4.1 CACHE INTERNAL "")
    FetchContent_MakeAvailable(webview)
endif()

# The native dialogs of Linux and macOS start their helper programs through the system in one step, with the signals of a shell and no descriptor of the product.
workpane_patches(dialogPatches portable_file_dialogs tar.gz <SOURCE_DIR>/portable-file-dialogs.h DialogsSpawnPrograms.cmake)
FetchContent_Declare(
    portable_file_dialogs
    URL https://github.com/samhocevar/portable-file-dialogs/archive/c12ea8c9a727f5320a2b4570aee863bbede2a204.tar.gz
    URL_HASH SHA256=8f06b35c6017e7e6796cf7393815f7ccde2268a9259d553573da78e2a405b1b2
    ${dialogPatches}
    SYSTEM
)
FetchContent_MakeAvailable(portable_file_dialogs)

FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.58.0.tar.gz
    URL_HASH SHA256=31932ce8b33f2905472a987dc1984b9d8a7d338083a066021349587cc5f1cfea
    EXCLUDE_FROM_ALL
    SYSTEM
)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE INTERNAL "")
set(HTTPLIB_INSTALL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
    stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    SYSTEM
)
FetchContent_MakeAvailable(stb)

if(WORKPANE_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
        EXCLUDE_FROM_ALL
        SYSTEM
    )
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK OFF CACHE INTERNAL "")
    set(gtest_force_shared_crt ON CACHE INTERNAL "")
    FetchContent_MakeAvailable(googletest)
endif()

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Dear ImGui ships sources without a build of its own, so the product compiles the core, the two backends it renders through and the FreeType rasterizer once.
add_library(workpane_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_null.cpp"
    "${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
)
target_include_directories(workpane_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imgui_SOURCE_DIR}/misc/freetype" "${imgui_SOURCE_DIR}/misc/cpp")
target_compile_definitions(workpane_imgui PUBLIC IMGUI_ENABLE_FREETYPE IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DEFINE_MATH_OPERATORS IMGUI_USE_WCHAR32)
target_link_libraries(workpane_imgui PUBLIC glfw freetype OpenGL::GL)
set_target_properties(workpane_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(workpane_text_editor STATIC "${imgui_text_editor_SOURCE_DIR}/TextEditor.cpp")
target_include_directories(workpane_text_editor SYSTEM PUBLIC "${imgui_text_editor_SOURCE_DIR}")
target_link_libraries(workpane_text_editor PUBLIC workpane_imgui)

add_library(workpane_sqlite STATIC "${sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(workpane_sqlite SYSTEM PUBLIC "${sqlite_SOURCE_DIR}")
target_compile_definitions(workpane_sqlite PUBLIC SQLITE_THREADSAFE=2 SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED)
target_link_libraries(workpane_sqlite PUBLIC Threads::Threads)
set_target_properties(workpane_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Only playback and the built in decoders of WAV, FLAC and MP3 are compiled, and Apple links its audio frameworks directly instead of loading them at runtime.
add_library(workpane_miniaudio STATIC "${miniaudio_SOURCE_DIR}/miniaudio.c")
target_include_directories(workpane_miniaudio SYSTEM PUBLIC "${miniaudio_SOURCE_DIR}")
target_compile_definitions(workpane_miniaudio PUBLIC MA_NO_ENCODING MA_NO_GENERATION)
target_link_libraries(workpane_miniaudio PUBLIC Threads::Threads)
set_target_properties(workpane_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    target_compile_definitions(workpane_miniaudio PUBLIC MA_NO_RUNTIME_LINKING)
    target_link_libraries(workpane_miniaudio PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(workpane_miniaudio PUBLIC ${CMAKE_DL_LIBS} m)
endif()

# The terminal emulator is C99 compiled once with its own warnings, and its release already carries the tables its build would generate.
add_library(workpane_vterm STATIC "${libvterm_SOURCE_DIR}/src/encoding.c" "${libvterm_SOURCE_DIR}/src/keyboard.c" "${libvterm_SOURCE_DIR}/src/mouse.c" "${libvterm_SOURCE_DIR}/src/parser.c" "${libvterm_SOURCE_DIR}/src/pen.c" "${libvterm_SOURCE_DIR}/src/screen.c" "${libvterm_SOURCE_DIR}/src/state.c" "${libvterm_SOURCE_DIR}/src/unicode.c" "${libvterm_SOURCE_DIR}/src/vterm.c")
target_include_directories(workpane_vterm SYSTEM PUBLIC "${libvterm_SOURCE_DIR}/include")
target_include_directories(workpane_vterm PRIVATE "${libvterm_SOURCE_DIR}/src")
set_target_properties(workpane_vterm PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)

# The terminal measures characters with the C library of the system, whose locales and widths C99 leaves undeclared without the POSIX level that adds them.
if(NOT WIN32)
    target_compile_definitions(workpane_vterm PRIVATE _XOPEN_SOURCE=700)
endif()

# The libraries the product patches run under the sanitizers as well, since a patch is code of the product.
workpane_enable_sanitizers(workpane_text_editor)
workpane_enable_sanitizers(workpane_vterm)

add_library(workpane_file_dialogs INTERFACE)
target_include_directories(workpane_file_dialogs SYSTEM INTERFACE "${portable_file_dialogs_SOURCE_DIR}")

add_library(workpane_http INTERFACE)
target_link_libraries(workpane_http INTERFACE httplib::httplib)
target_compile_definitions(workpane_http INTERFACE CPPHTTPLIB_HEADER_MAX_LENGTH=32768)

# The image decoder is third party code compiled once with its own warnings, and it reads only the PNG files the product bundles.
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/stb_image.c" CONTENT "#define STB_IMAGE_IMPLEMENTATION\n#define STBI_ONLY_PNG\n#include <stb_image.h>\n")
add_library(workpane_stb STATIC "${CMAKE_BINARY_DIR}/generated/stb_image.c")
target_include_directories(workpane_stb SYSTEM PUBLIC "${stb_SOURCE_DIR}")
set_target_properties(workpane_stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
