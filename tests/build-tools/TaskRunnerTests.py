import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import make


class CoverageTaskTest(unittest.TestCase):
    def check_reader(self, compiler, platform, expected):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build" / "coverage"
            generated = build / "generated"
            generated.mkdir(parents=True)
            (generated / "CoverageCompiler.txt").write_text(compiler + "\n", encoding="utf-8")
            (build / "CMakeCache.txt").write_text("CMAKE_CXX_COMPILER:FILEPATH=compiler\n", encoding="utf-8")
            stale = build / "previous.gcda"
            stale.write_bytes(b"previous counters")
            context = make.Context("Release", root / "build" / "release", 2, False)

            with mock.patch.object(make, "ROOT", root), mock.patch.object(make.sys, "platform", platform), mock.patch.object(make, "executable", return_value="gcovr"), mock.patch.object(make, "cmake_configure") as configure, mock.patch.object(make, "cmake_build"), mock.patch.object(make, "ctest") as ctest, mock.patch.object(make, "run") as run:
                make.task_coverage(context)

            self.assertFalse(stale.exists())
            self.assertEqual(configure.call_args.args[0].configuration, "Debug")
            self.assertIn("-DWORKPANE_ENABLE_COVERAGE=ON", configure.call_args.args)
            self.assertEqual(ctest.call_args.args[1], 2)
            command = run.call_args.args[0]

            if expected is None:
                self.assertNotIn("--gcov-executable", command)
            else:
                index = command.index("--gcov-executable")
                self.assertEqual(command[index + 1], expected)

            self.assertIn(str(build / "coverage" / "index.html"), command)
            self.assertIn(str(build / "coverage" / "cobertura.xml"), command)
            self.assertIn("--fail-under-line", command)

    def test_reads_configured_gnu_compiler(self):
        self.check_reader("GNU", "linux", None)

    def test_reads_configured_clang_compiler(self):
        self.check_reader("Clang", "linux", "llvm-cov gcov")

    def test_reads_configured_apple_clang_compiler(self):
        self.check_reader("AppleClang", "darwin", "xcrun llvm-cov gcov")


if __name__ == "__main__":
    unittest.main()
