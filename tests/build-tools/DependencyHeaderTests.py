import json
import subprocess
import sys
import unittest
from pathlib import Path


class DependencyHeaderTest(unittest.TestCase):
    def test_runtime_uses_pinned_archive_headers(self):
        build = Path(sys.argv[1]).resolve()
        expected = [Path(line).resolve() for line in Path(sys.argv[2]).read_text(encoding="utf-8").splitlines()]
        commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
        sources = [entry for entry in commands if Path(entry["file"]).as_posix().endswith("/modules/zip/src/ZipModule.cpp")]
        self.assertEqual(len(sources), 1)

        source = sources[0]
        output = (Path(source["directory"]) / source["output"]).resolve().relative_to(build).as_posix()
        dependencies = subprocess.run([sys.argv[3], "-t", "deps", output], cwd=build, capture_output=True, text=True, encoding="utf-8", timeout=20)
        self.assertEqual(dependencies.returncode, 0, dependencies.stdout + dependencies.stderr)

        names = {path.name for path in expected}
        selected = [(build / line.strip()).resolve() for line in dependencies.stdout.splitlines() if Path(line.strip()).name in names]
        self.assertCountEqual(selected, expected, dependencies.stdout)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], exit=True)
