"""Check the test harness without changing the running stack."""

import importlib.util
import os
from pathlib import Path
import unittest
from unittest import mock


def load_flows_with_faults_enabled():
    path = Path(__file__).with_name("test_flows.py")
    spec = importlib.util.spec_from_file_location("checkers_flows_for_harness", path)
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(os.environ, {"CHECKERS_DISRUPTIVE": "1"}):
        spec.loader.exec_module(module)
    return module


class Cleanup(unittest.TestCase):
    def test_worker_restarted_when_fixture_creation_fails(self):
        flows = load_flows_with_faults_enabled()
        with mock.patch.object(flows, "container_id", return_value="worker-id"), \
             mock.patch.object(flows, "compose"), \
             mock.patch.object(flows, "create_game", side_effect=RuntimeError("fixture failed")), \
             mock.patch.object(flows, "run") as start:
            with self.assertRaisesRegex(RuntimeError, "fixture failed"):
                flows.Flows("test_queued_events_survive_worker_stop").test_queued_events_survive_worker_stop()
            start.assert_called_once_with("docker", "start", "worker-id", timeout=60)

    def test_redis_restarted_when_request_fails(self):
        flows = load_flows_with_faults_enabled()
        with mock.patch.object(flows, "container_id", return_value="redis-id"), \
             mock.patch.object(flows, "compose"), \
             mock.patch.object(flows, "create_game", return_value={"game_id": "fixture"}), \
             mock.patch.object(flows, "wait_for"), \
             mock.patch.object(flows, "request", side_effect=RuntimeError("request failed")), \
             mock.patch.object(flows, "run") as start:
            with self.assertRaisesRegex(RuntimeError, "request failed"):
                flows.Flows("test_redis_outage_falls_back_to_database").test_redis_outage_falls_back_to_database()
            start.assert_called_once_with("docker", "start", "redis-id", timeout=60)


if __name__ == "__main__":
    unittest.main(verbosity=2)
