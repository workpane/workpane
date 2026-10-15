# The suites link the same core library as the product and read the resources the build staged, so what they test is what ships.
include(GoogleTest)
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)

file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/generated/ArchiveHeaders.txt" CONTENT "$<TARGET_PROPERTY:zip,SOURCE_DIR>/zip.h\n$<TARGET_PROPERTY:zip,BINARY_DIR>/../zipconf.h\n")
add_test(NAME DependencyHeaders.PinnedArchive COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/DependencyHeaderTests.py" "${CMAKE_BINARY_DIR}" "${PROJECT_BINARY_DIR}/generated/ArchiveHeaders.txt" "${CMAKE_MAKE_PROGRAM}")
set_tests_properties(DependencyHeaders.PinnedArchive PROPERTIES TIMEOUT 30)

foreach(compiler IN ITEMS gnu clang apple_clang)
    add_test(NAME CoverageTask.${compiler} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/TaskRunnerTests.py" "CoverageTaskTest.test_reads_configured_${compiler}_compiler")
    set_tests_properties(CoverageTask.${compiler} PROPERTIES TIMEOUT 30)
endforeach()

foreach(case IN ITEMS attaches_and_validates_ticket retries_only_unavailable_delivery rejects_non_delivery_failures stops_at_delivery_bound requires_native_disk_image)
    add_test(NAME StapleTask.${case} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/StapleTaskTests.py" "StapleTaskTest.test_${case}")
    set_tests_properties(StapleTask.${case} PROPERTIES TIMEOUT 30)
endforeach()

foreach(case IN ITEMS accepts_complete_distribution_signatures refuses_a_helper_without_a_secure_timestamp refuses_an_ad_hoc_helper_in_a_distribution_bundle refuses_a_runtime_library_without_the_hardened_runtime)
    add_test(NAME PackageSigning.${case} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/PackageSigningTests.py" "PackageSigningTest.test_${case}")
    set_tests_properties(PackageSigning.${case} PROPERTIES TIMEOUT 30)
endforeach()

include("${PROJECT_SOURCE_DIR}/examples/plugin-app/Applications.cmake")

add_test(NAME FoundationApplication.ExampleStudio COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/FoundationApplicationTests.py" "$<TARGET_FILE:ExampleStudio>" "$<TARGET_PROPERTY:ExampleStudio,WORKPANE_RESOURCES_DIRECTORY>" ExampleStudio "Example studio" 1.2.0 7 org.example.studio studio)
add_test(NAME FoundationApplication.PlainStudio COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/FoundationApplicationTests.py" "$<TARGET_FILE:PlainStudio>" "$<TARGET_PROPERTY:PlainStudio,WORKPANE_RESOURCES_DIRECTORY>" PlainStudio "Plain studio" 3.1.0 1 org.example.plain)
set_tests_properties(FoundationApplication.ExampleStudio FoundationApplication.PlainStudio PROPERTIES TIMEOUT 60)

foreach(case IN ITEMS rejects_case_collisions keeps_distinct_application_resources)
    add_test(NAME ApplicationFactory.${case} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build-tools/ApplicationFactoryTests.py" "ApplicationFactoryTest.test_${case}")
    set_tests_properties(ApplicationFactory.${case} PROPERTIES TIMEOUT 90)
endforeach()

add_executable(WorkpaneLibraryTests core/LibraryHostTests.cpp support/Resources.cpp support/TemporaryDirectory.cpp)
target_include_directories(WorkpaneLibraryTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(WorkpaneLibraryTests PRIVATE Workpane::Core GTest::gtest_main)
target_compile_definitions(WorkpaneLibraryTests PRIVATE WORKPANE_TEST_RESOURCES="${WORKPANE_STAGED_RESOURCES}")
target_compile_definitions(WorkpaneLibraryTests PRIVATE WORKPANE_TEST_OPENSSL_VERSION="${CPM_PACKAGE_varn_openssl_source_VERSION}")
add_dependencies(WorkpaneLibraryTests WorkpaneResources)
workpane_set_warnings(WorkpaneLibraryTests)
workpane_enable_coverage(WorkpaneLibraryTests)
workpane_enable_sanitizers(WorkpaneLibraryTests)
gtest_discover_tests(WorkpaneLibraryTests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 30)

add_executable(WorkpaneTests
    core/CoreTests.cpp
    core/FileDigestTests.cpp
    http/HttpTests.cpp
    localization/LocalizationTests.cpp
    persistence/PersistenceTests.cpp
    platform/PlatformTests.cpp
    platform/PrivateDirectoryTests.cpp
    platform/AskpassTests.cpp
    platform/CredentialStoreTests.cpp
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
    product/FoundationTests.cpp
    product/RemoteSshModuleTests.cpp
    product/RemoteSshPluginTests.cpp
    product/TerminalPluginTests.cpp
    product/WebServerTests.cpp
    scripting/ScriptingTests.cpp
    scripting/HttpExchangeTests.cpp
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
    support/FakeClipboard.cpp
    support/FakeClipboard.h
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
    support/SftpProbeHelper.cpp
    support/SftpProbeHelper.h
    support/SshWorkspaceProbeHelper.cpp
    support/SshWorkspaceProbeHelper.h
    support/SshTerminalProbeHelper.cpp
    support/SshTerminalProbeHelper.h
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
    target_sources(WorkpaneTests PRIVATE platform/ApplicationMenuTests.mm platform/InputMethodTests.mm platform/NativeBrowserInputTests.mm)
    target_compile_options(WorkpaneTests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
else()
    target_sources(WorkpaneTests PRIVATE platform/InputMethodTests.cpp platform/NativeBrowserInputTests.cpp support/InputMethodProbe.cpp support/InputMethodProbe.h)
endif()

# The suites run under the code page the product declares, so paths behave on Windows as they do in the product.
if(WIN32)
    target_sources(WorkpaneTests PRIVATE "${PROJECT_SOURCE_DIR}/extras/windows/Workpane.manifest")
    target_sources(WorkpaneTests PRIVATE platform/WindowsSshIntegrationTests.cpp support/WindowsAccountHelper.cpp support/WindowsAccountHelper.h support/WindowsFileAccessHelper.cpp support/WindowsFileAccessHelper.h)
    add_executable(WorkpaneDesktopTests desktop-main.cpp support/WindowsDesktop.cpp support/WindowsDesktop.h "${PROJECT_SOURCE_DIR}/extras/windows/Workpane.manifest")
    target_include_directories(WorkpaneDesktopTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(WorkpaneDesktopTests PRIVATE WorkpaneSystem GTest::gtest)
    workpane_set_warnings(WorkpaneDesktopTests)
    workpane_enable_coverage(WorkpaneDesktopTests)
    workpane_enable_sanitizers(WorkpaneDesktopTests)
    add_dependencies(WorkpaneTests WorkpaneDesktopTests)
endif()

target_include_directories(WorkpaneTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(WorkpaneTests PRIVATE WorkpaneCore GTest::gtest_main)
target_compile_definitions(WorkpaneTests PRIVATE WORKPANE_TEST_DATA_DIRECTORY="${WORKPANE_DATA_DIRECTORY}")
target_compile_definitions(WorkpaneTests PRIVATE WORKPANE_TEST_EXAMPLE_PLUGINS="${PROJECT_SOURCE_DIR}/examples/plugin-app/plugins")
target_compile_definitions(WorkpaneTests PRIVATE WORKPANE_TEST_RESOURCES="${WORKPANE_STAGED_RESOURCES}")
target_compile_definitions(WorkpaneTests PRIVATE WORKPANE_TEST_EXECUTABLE="$<TARGET_FILE:WorkpaneTests>")
target_compile_definitions(WorkpaneTests PRIVATE WORKPANE_TEST_ASKPASS="$<TARGET_FILE:WorkpaneAskpass>")
add_dependencies(WorkpaneTests WorkpaneResources)
add_dependencies(WorkpaneTests WorkpaneAskpass)

workpane_set_warnings(WorkpaneTests)
workpane_enable_coverage(WorkpaneTests)
workpane_enable_sanitizers(WorkpaneTests)

# Every suite runs in its own process, so one ImGui context or one Lua runtime never leaks from a test into the next.
set(WORKPANE_NATIVE_TEST_FILTER "PlatformWindow.*:NativeWebViewTest.*:NativeBrowserInputTest.*:InputMethodTest.*:ApplicationMenu.*:LinuxNativeViews.*:WindowsSshIntegrationTest.*")
gtest_discover_tests(WorkpaneTests TEST_FILTER "-${WORKPANE_NATIVE_TEST_FILTER}" DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 120)
if(WIN32)
    set(WORKPANE_SSH_WORKSPACE_TEST "WindowsSshIntegrationTest.EditsFilesAndRunsOwnedProgramsThroughTheProductionSshWorkspace")
    gtest_discover_tests(WorkpaneTests TEST_FILTER "${WORKPANE_NATIVE_TEST_FILTER}-NativeBrowserInputTest.*:${WORKPANE_SSH_WORKSPACE_TEST}" DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 120 RESOURCE_LOCK native_desktop)
    gtest_discover_tests(WorkpaneTests TEST_FILTER "${WORKPANE_SSH_WORKSPACE_TEST}" DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 240 RESOURCE_LOCK native_desktop)
    set_property(TARGET WorkpaneTests PROPERTY TEST_LAUNCHER "$<TARGET_FILE:WorkpaneDesktopTests>")
    gtest_discover_tests(WorkpaneTests TEST_FILTER "NativeBrowserInputTest.*" DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 120 RESOURCE_LOCK native_desktop)
else()
    gtest_discover_tests(WorkpaneTests TEST_FILTER "${WORKPANE_NATIVE_TEST_FILTER}" DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 120 RESOURCE_LOCK native_desktop)
endif()

if(UNIX)
    target_sources(WorkpaneTests PRIVATE platform/RemoteSshIntegrationTests.cpp support/SshScopeProbeHelper.cpp support/SshScopeProbeHelper.h)
    add_dependencies(WorkpaneTests WorkpaneTerminal)
endif()
