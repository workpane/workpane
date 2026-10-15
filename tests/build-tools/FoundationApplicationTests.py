import plistlib
import subprocess
import sys
import unittest
from pathlib import Path


class FoundationApplicationTest(unittest.TestCase):
    def test_identity_resources_and_runtime(self):
        executable = Path(sys.argv[1])
        resources = Path(sys.argv[2])
        target = sys.argv[3]
        name = sys.argv[4]
        version = sys.argv[5]
        build = sys.argv[6]
        identifier = sys.argv[7]
        plugins = sys.argv[8:]
        output = subprocess.check_output([str(executable), "--version"], text=True, timeout=20).strip()
        self.assertEqual(output, name + " " + version)
        help_text = subprocess.check_output([str(executable), "--help"], text=True, timeout=20)
        self.assertIn(target, help_text)
        self.assertIn("--data-dir", help_text)
        self.assertEqual(sorted(path.name for path in (resources / "plugins").iterdir()), sorted(plugins))
        self.assertTrue((resources / "lua/workpane/bootstrap.lua").is_file())
        self.assertTrue((resources / "fonts/Lucide.ttf").is_file())
        helper = "WorkpaneAskpass.exe" if sys.platform == "win32" else "WorkpaneAskpass"
        self.assertTrue((executable.parent / helper).is_file())

        if sys.platform != "win32":
            self.assertTrue((executable.parent / "WorkpaneTerminal").is_file())

        if sys.platform == "darwin":
            metadata = plistlib.loads((executable.parent.parent / "Info.plist").read_bytes())
            self.assertEqual(metadata["CFBundleName"], name)
            self.assertEqual(metadata["CFBundleIdentifier"], identifier)
            self.assertEqual(metadata["CFBundleShortVersionString"], version)
            self.assertEqual(metadata["CFBundleVersion"], build)
            self.assertTrue(any((executable.parent.parent / "Frameworks").glob("libvarn*.dylib")))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], exit=True)
