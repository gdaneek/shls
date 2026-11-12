"""Opt-in fault demonstrations, retaining their existing guarded shell logic."""

import os
import unittest

from support import ROOT, run

DISRUPTIVE = os.environ.get("CHECKERS_DISRUPTIVE") == "1"


@unittest.skipUnless(DISRUPTIVE, "fault demos affect services and data; set CHECKERS_DISRUPTIVE=1 on a demo stack")
class FaultScenarios(unittest.TestCase):
    def scenario(self, name):
        # Do not SIGKILL the shell on a timeout: its EXIT/INT/TERM traps restore
        # the temporary firewall rules and stopped services.
        result = run(str(ROOT / "tests/integration" / name), timeout=None, check=False)
        print(result.stdout, end="")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_firewall_failure(self):
        self.scenario("test-firewall-failure.sh")

    def test_postgres_failover(self):
        self.scenario("test-postgres-failover.sh")

    def test_rabbitmq_failover(self):
        self.scenario("test-rabbitmq-failover.sh")

    def test_component_failures(self):
        self.scenario("test-failure.sh")

    def test_backup_restore(self):
        self.scenario("test-backup.sh")

    def test_service_level(self):
        self.scenario("test-slo.sh")


if __name__ == "__main__":
    unittest.main(verbosity=2)
