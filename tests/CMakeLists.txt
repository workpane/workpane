# The suites link the same core library as the product and read the resources the build staged, so what they test is what ships.
include(GoogleTest)

add_executable(WorkpaneTests
    core/CoreTests.cpp
    http/HttpTests.cpp
    localization/LocalizationTests.cpp
    persistence/PersistenceTests.cpp
    platform/PlatformTests.cpp
    platform/WebViewTests.cpp
    product/AiModuleTests.cpp
    product/GameModuleTests.cpp
    product/GamePluginTests.cpp
    product/PluginLifecycleTests.cpp
    product/AiPluginTests.cpp
    product/BrowserTests.cpp
    product/CodeEditorModuleTests.cpp
    product/CodeEditorPluginTests.cpp
    product/ProductTests.cpp
    product/TerminalPluginTests.cpp
    product/WebServerTests.cpp
    scripting/ScriptingTests.cpp
    support/ComponentSpecs.cpp
    support/ComponentSpecs.h
    support/DeclaredSurface.h
    support/DeclaredSurfaces.cpp
    support/DeclaredSurfaces.h
    support/DialogRecord.h
    support/AudioRecord.h
    support/FakeAudioOutput.cpp
    support/FakeAudioOutput.h
    support/FakeDialogService.cpp
    support/FakeDialogService.h
    support/FakeNativeViewHost.cpp
    support/FakeNativeViewHost.h
    support/FakeProcess.cpp
    support/FakeProcess.h
    support/FakeProcessLauncher.cpp
    support/FakeProcessLauncher.h
    support/FakeProvider.cpp
    support/FakeProvider.h
    support/FakePseudoTerminal.cpp
    support/FakePseudoTerminal.h
    support/FakePseudoTerminalHost.cpp
    support/FakePseudoTerminalHost.h
    support/FakeSystemInspector.cpp
    support/FakeSystemInspector.h
    support/FakeSystemServices.cpp
    support/FakeSystemServices.h
    support/FakeWebView.cpp
    support/FakeWebView.h
    support/FileAddress.cpp
    support/FileAddress.h
    support/HeadlessProduct.cpp
    support/HeadlessProduct.h
    support/InterfaceHarness.cpp
    support/InterfaceHarness.h
    support/LocalPort.cpp
    support/LocalPort.h
    support/MainThreadPump.cpp
    support/MainThreadPump.h
    support/Probe.cpp
    support/Probe.h
    support/ProcessRecord.h
    support/ProductDatabase.cpp
    support/ProductDatabase.h
    support/Resources.cpp
    support/Resources.h
    support/RootedPath.cpp
    support/RootedPath.h
    support/SampleManifest.cpp
    support/SampleManifest.h
    support/ScriptHarness.cpp
    support/ScriptHarness.h
    support/SurfaceReader.cpp
    support/SurfaceReader.h
    support/SystemRecord.h
    support/TerminalProcessRecord.h
    support/TerminalRecord.h
    support/TemporaryDirectory.cpp
    support/TemporaryDirectory.h
    support/WaveFile.cpp
    support/WaveFile.h
    support/WebViewProbe.cpp
    support/WebViewProbe.h
    support/WebViewRecord.h
    ui/CodeEditorTests.cpp
    ui/ComponentTests.cpp
    ui/FoundationTests.cpp
    ui/InteractionTests.cpp
    ui/LayoutTests.cpp
    ui/MarkdownTests.cpp
    ui/TerminalTests.cpp
)

# The application menu exists only on macOS, and the input method is driven through the protocols of each system, so those tests are built per platform.
if(APPLE)
    target_sources(WorkpaneTests PRIVATE platform/ApplicationMenuTests.mm platform/InputMethodTests.mm)
else()
    target_sources(WorkpaneTests PRIVATE platform/InputMethodTests.cpp support/InputMethodProbe.cpp support/InputMethodProbe.h)
endif()

# The suites run under the code page the product declares, so paths behave on Windows as they do in the product.
if(WIN32)
    target_sources(WorkpaneTests PRIVATE "${PROJECT_SOURCE_DIR}/extras/windows/Workpane.manifest")
endif()

target_include_directories(WorkpaneTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(WorkpaneTests PRIVATE WorkpaneCore GTest::gtest_main)
target_compile_definitions(WorkpaneTests PRIVATE WORKPANE_TEST_RESOURCES="${WORKPANE_STAGED_RESOURCES}")
add_dependencies(WorkpaneTests WorkpaneResources)

workpane_set_warnings(WorkpaneTests)
workpane_enable_coverage(WorkpaneTests)
workpane_enable_sanitizers(WorkpaneTests)

# Every suite runs in its own process, so one ImGui context or one Lua runtime never leaks from a test into the next.
gtest_discover_tests(WorkpaneTests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 120)
