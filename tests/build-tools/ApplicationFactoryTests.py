import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ApplicationFactoryTest(unittest.TestCase):
    def configure(self, applications):
        with tempfile.TemporaryDirectory(prefix="workpane-factory-") as temporary:
            folder = Path(temporary)
            source = f"""cmake_minimum_required(VERSION 3.29)
project(FactoryTest LANGUAGES C CXX)
add_library(WorkpaneCore INTERFACE)
add_library(Workpane::Core ALIAS WorkpaneCore)
set_property(TARGET WorkpaneCore PROPERTY WORKPANE_SOURCE_DIRECTORY "{ROOT.as_posix()}")
add_executable(WorkpaneAskpass IMPORTED GLOBAL)
set_property(TARGET WorkpaneAskpass PROPERTY IMPORTED_LOCATION "${{CMAKE_COMMAND}}")
if(UNIX)
    add_executable(WorkpaneTerminal IMPORTED GLOBAL)
    set_property(TARGET WorkpaneTerminal PROPERTY IMPORTED_LOCATION "${{CMAKE_COMMAND}}")
endif()
add_library(varn::varn SHARED IMPORTED GLOBAL)
set_target_properties(varn::varn PROPERTIES IMPORTED_LOCATION "${{CMAKE_COMMAND}}" IMPORTED_SONAME "libvarn")
include("{ROOT.as_posix()}/cmake/CompilerWarnings.cmake")
include("{ROOT.as_posix()}/cmake/Coverage.cmake")
include("{ROOT.as_posix()}/cmake/Sanitizers.cmake")
include("{ROOT.as_posix()}/cmake/WorkpaneApplication.cmake")
{applications}
"""
            (folder / "CMakeLists.txt").write_text(source, encoding="utf-8")
            result = subprocess.run(["cmake", "-S", str(folder), "-B", str(folder / "build"), "-G", "Ninja"], capture_output=True, text=True, timeout=60)
            return result

    def test_rejects_case_collisions(self):
        result = self.configure("""workpane_add_application(Studio NAME "Studio" VERSION 1.0.0 IDENTIFIER org.example.studio)
workpane_add_application(studio NAME "Other studio" VERSION 2.0.0 IDENTIFIER org.example.other)
""")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unique when case is ignored", result.stderr)

    def test_keeps_distinct_application_resources(self):
        result = self.configure("""workpane_add_application(Studio NAME "Studio" VERSION 1.0.0 IDENTIFIER org.example.studio)
workpane_add_application(OtherStudio NAME "Other studio" VERSION 2.0.0 IDENTIFIER org.example.other)
get_target_property(first Studio WORKPANE_RESOURCES_DIRECTORY)
get_target_property(second OtherStudio WORKPANE_RESOURCES_DIRECTORY)
if(first STREQUAL second)
    message(FATAL_ERROR "Distinct applications share their resources")
endif()
""")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
