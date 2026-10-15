import plistlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import make


class PackageSigningTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.bundle = Path(temporary.name) / "Workpane.app"
        contents = self.bundle / "Contents"
        binaries = contents / "MacOS"
        binaries.mkdir(parents=True)

        for name in ("Workpane", "WorkpaneTerminal", "WorkpaneAskpass"):
            (binaries / name).touch()

        frameworks = contents / "Frameworks"
        frameworks.mkdir()
        (frameworks / "libvarn.dylib").touch()
        translations = contents / "Resources" / "pt.lproj"
        translations.mkdir(parents=True)
        usages = ("NSCameraUsageDescription", "NSMicrophoneUsageDescription")
        (translations / "InfoPlist.strings").write_text("\n".join(f'"{key}" = "Usage";' for key in usages), encoding="utf-8")
        information = {"CFBundleShortVersionString": "1.2.3", "CFBundleVersion": "7", "LSMinimumSystemVersion": "13.3", **dict.fromkeys(usages, "Usage")}
        (contents / "Info.plist").write_bytes(plistlib.dumps(information))
        self.signatures = {}

        for target, name, options in ((make, "run", {}), (make, "validate_resources", {}), (make, "validate_version", {}), (make, "current_version", {"return_value": "1.2.3"}), (make, "current_build_number", {"return_value": "7"}), (make, "executable", {"side_effect": lambda value: value}), (make.subprocess, "run", {"side_effect": self.capture})):
            patch = mock.patch.object(target, name, **options)
            patch.start()
            self.addCleanup(patch.stop)

    def capture(self, command, **options):
        if command[0] == "otool":
            output = "Workpane:\n\t@rpath/libvarn.dylib\n" if command[1] == "-L" else "    minos 13.3\n"
            return subprocess.CompletedProcess(command, 0, stdout=output)

        if command[0] == "codesign" and "--entitlements" in command:
            granted = {"com.apple.security.device.camera": True, "com.apple.security.device.audio-input": True}
            return subprocess.CompletedProcess(command, 0, stdout=plistlib.dumps(granted))

        if command[0] == "codesign" and "--display" in command:
            signature = self.signatures.get(Path(command[-1]).name, "flags=0x10000(runtime)\nTimestamp=Valid secure timestamp\n")
            return subprocess.CompletedProcess(command, 0, stderr=signature)

        raise AssertionError(command)

    def test_accepts_complete_distribution_signatures(self):
        make.validate_macos_bundle(self.bundle)

    def test_refuses_a_helper_without_a_secure_timestamp(self):
        self.signatures["WorkpaneAskpass"] = "flags=0x10000(runtime)\nSigned Time=Unsecured time\n"

        with self.assertRaisesRegex(RuntimeError, "WorkpaneAskpass"):
            make.validate_macos_bundle(self.bundle)

    def test_refuses_an_ad_hoc_helper_in_a_distribution_bundle(self):
        self.signatures["WorkpaneTerminal"] = "Signature=adhoc\n"

        with self.assertRaisesRegex(RuntimeError, "WorkpaneTerminal"):
            make.validate_macos_bundle(self.bundle)

    def test_refuses_a_runtime_library_without_the_hardened_runtime(self):
        self.signatures["libvarn.dylib"] = "flags=0x0\nTimestamp=Valid secure timestamp\n"

        with self.assertRaisesRegex(RuntimeError, "libvarn.dylib"):
            make.validate_macos_bundle(self.bundle)


if __name__ == "__main__":
    unittest.main()
