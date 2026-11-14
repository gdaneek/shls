"""Проверяет восстановление контейнеров и политики перезапуска при ошибках и прерывании теста.
Вызовы Docker заменены моками, поэтому реальные сервисы не останавливаются.
"""

import signal
import unittest
from unittest import mock

import support


class Cleanup(unittest.TestCase):
    def test_stopped_containers_restart_on_error_or_interrupt(self):
        for error in (RuntimeError("fixture failed"), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__), \
                 mock.patch.object(support, "container_id", side_effect=lambda name: name + "-id"), \
                 mock.patch.object(support, "compose"), mock.patch.object(support, "run") as run:
                with self.assertRaises(type(error)):
                    with support.stopped("redis", "event-worker"):
                        raise error
                run.assert_called_once_with("docker", "start", "redis-id", "event-worker-id", timeout=90)

    def test_partial_stop_failure_still_restarts_containers(self):
        with mock.patch.object(support, "container_id", return_value="worker-id"), \
             mock.patch.object(support, "compose", side_effect=RuntimeError("stop failed")), \
             mock.patch.object(support, "run") as run:
            with self.assertRaises(RuntimeError):
                with support.stopped("event-worker"):
                    self.fail("stop should fail before the fixture")
            run.assert_called_once_with("docker", "start", "worker-id", timeout=90)

    def test_crash_restores_policy_even_if_start_fails(self):
        before = {"Id": "broker-id", "HostConfig": {"RestartPolicy": {"Name": "unless-stopped"}}}
        down = {"State": {"Running": False, "ExitCode": 137}}

        def command(*args, **kwargs):
            if args[:2] == ("docker", "start"):
                raise RuntimeError("start failed")

        with mock.patch.object(support, "inspect", side_effect=[before, down]), \
             mock.patch.object(support, "compose"), \
             mock.patch.object(support, "run", side_effect=command) as run:
            with self.assertRaisesRegex(RuntimeError, "start failed"):
                with support.crashed("rabbitmq"):
                    raise AssertionError("delivery failed")
            self.assertEqual(run.call_args, mock.call("docker", "update", "--restart=unless-stopped", "broker-id"))

    def test_sigterm_unwinds_cleanup_and_restores_handler(self):
        previous = signal.getsignal(signal.SIGTERM)
        with mock.patch.object(support, "container_id", return_value="redis-id"), \
             mock.patch.object(support, "compose"), mock.patch.object(support, "run") as run:
            with self.assertRaises(KeyboardInterrupt):
                with support.stopped("redis"):
                    signal.raise_signal(signal.SIGTERM)
            run.assert_called_once_with("docker", "start", "redis-id", timeout=90)
        self.assertEqual(signal.getsignal(signal.SIGTERM), previous)


if __name__ == "__main__":
    unittest.main(verbosity=2)
