"""Exercise fixed helper operations with mocked system commands; no network changes."""
import io
from pathlib import Path
import types
import unittest
from unittest.mock import patch

helper = types.ModuleType("hachimi_network_helper_test")
source = Path(__file__).resolve().parents[1] / "platform/linux/hachimi-network"
exec(compile(source.read_text(encoding="utf-8"), str(source), "exec"), helper.__dict__)


class EthernetControls(unittest.TestCase):
    def operation(self, operation, paths):
        with patch.object(helper.sys, "stdin", io.StringIO(operation + "\n")), \
                patch.object(helper.glob, "glob", return_value=paths), \
                patch.object(helper, "run", return_value="") as run, \
                patch.object(helper, "emit"):
            helper.main()
            return run.call_args.args[0]

    def test_eth2_is_preferred_and_only_link_state_changes(self):
        paths = ["/sys/class/net/eth0", "/sys/class/net/eth2"]
        self.assertEqual(self.operation("ethernet-off", paths), ["ip", "link", "set", "dev", "eth2", "down"])
        self.assertEqual(self.operation("ethernet-on", paths), ["ip", "link", "set", "dev", "eth2", "up"])

    def test_first_available_interface_is_used(self):
        self.assertEqual(self.operation("ethernet-on", ["/sys/class/net/eth1"])[4], "eth1")

    def test_absent_device_and_untrusted_interface_are_rejected(self):
        for paths in ([], ["/sys/class/net/eth2;anything"]):
            with self.assertRaises(RuntimeError):
                self.operation("ethernet-on", paths)

    def test_extra_arguments_are_rejected(self):
        with self.assertRaises(ValueError):
            self.operation("ethernet-off\teth0", ["/sys/class/net/eth2"])


if __name__ == "__main__":
    unittest.main()
