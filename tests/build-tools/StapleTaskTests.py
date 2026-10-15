import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import make


class StapleTaskTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.package = (Path(temporary.name) / "package.dmg").resolve()
        self.package.write_bytes(b"Owned test package")
        self.context = make.Context("Release", self.package.parent / "build", 1, False, str(self.package))

        for target, name, options in ((make.sys, "platform", {"new": "darwin"}), (make, "executable", {"return_value": "xcrun"}), (make, "run", {}), (make.time, "sleep", {})):
            patch = mock.patch.object(target, name, **options)
            value = patch.start()
            self.addCleanup(patch.stop)

            if name == "run":
                self.command = value

            if name == "sleep":
                self.pause = value

    def test_attaches_and_validates_ticket(self):
        make.task_staple_package(self.context)
        self.assertEqual(self.command.call_args_list, [mock.call(["xcrun", "stapler", operation, str(self.package)]) for operation in ("staple", "validate")])
        self.pause.assert_not_called()

    def test_retries_only_unavailable_delivery(self):
        unavailable = subprocess.CalledProcessError(make.STAPLER_TICKET_UNAVAILABLE, ["xcrun"])
        self.command.side_effect = [unavailable, None, unavailable, None]
        make.task_staple_package(self.context)
        self.assertEqual([call.args[0][2] for call in self.command.call_args_list], ["staple", "staple", "validate", "validate"])
        self.assertEqual(self.pause.call_args_list, [mock.call(make.STAPLER_RETRY_SECONDS), mock.call(make.STAPLER_RETRY_SECONDS)])

    def test_rejects_non_delivery_failures(self):
        self.command.side_effect = subprocess.CalledProcessError(65, ["xcrun"])

        with self.assertRaises(subprocess.CalledProcessError):
            make.task_staple_package(self.context)

        self.assertEqual(self.command.call_count, 1)
        self.pause.assert_not_called()

    def test_stops_at_delivery_bound(self):
        self.command.side_effect = subprocess.CalledProcessError(make.STAPLER_TICKET_UNAVAILABLE, ["xcrun"])

        with self.assertRaises(subprocess.CalledProcessError):
            make.task_staple_package(self.context)

        self.assertEqual(self.command.call_count, make.STAPLER_ATTEMPTS)
        self.assertEqual(self.pause.call_count, make.STAPLER_ATTEMPTS - 1)
        self.assertTrue(all(call.args[0][2] == "staple" for call in self.command.call_args_list))

    def test_requires_native_disk_image(self):
        with mock.patch.object(make.sys, "platform", "linux"), self.assertRaises(RuntimeError):
            make.task_staple_package(self.context)

        with self.assertRaises(RuntimeError):
            make.task_staple_package(make.Context("Release", self.package.parent, 1, False))

        self.package.unlink()

        with self.assertRaises(RuntimeError):
            make.task_staple_package(self.context)

        self.command.assert_not_called()
        self.pause.assert_not_called()


if __name__ == "__main__":
    unittest.main()
