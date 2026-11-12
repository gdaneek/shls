"""Crash the queue leader and the actual consumer connection node."""

from contextlib import ExitStack
import json
import re
import time
import unittest

from support import (BROKERS, DISRUPTIVE, compose, crashed, create_game, inspect, interruptible,
                     outbox_empty, queue, stopped, wait_for, worker_events)


def snapshot(service):
    state = inspect(service)
    return state["Id"], state["State"]["StartedAt"], state["RestartCount"]


@DISRUPTIVE
class RabbitFailover(unittest.TestCase):
    def cluster_ready(self, node="rabbitmq"):
        def cluster():
            state = json.loads(compose("exec", "-T", node, "rabbitmqctl", "-q", "cluster_status", "--formatter", "json"))
            return len(state["running_nodes"]) == 3 and not state["partitions"]

        def replicas():
            state = queue(node)
            return state["type"] == "quorum" and len(state["members"]) == len(state["online"]) == 3

        def caught_up():
            rows = json.loads(compose("exec", "-T", node, "rabbitmq-queues", "-q", "quorum_status",
                                      "--vhost", "/", "game.events.quorum", "--formatter", "json"))
            return (len(rows) == 3 and all(row["Membership"] == "voter" and row["Raft State"] in
                                          ("leader", "follower") for row in rows)
                    and len({row["Commit Index"] for row in rows}) == 1)

        wait_for(cluster, seconds=60)
        wait_for(replicas, seconds=30)
        wait_for(caught_up, seconds=30)

    def test_leader_failure_preserves_confirmed_and_new_events(self):
        self.cluster_ready()
        leader = queue()["leader"]
        old = leader.removeprefix("rabbit@")
        self.assertIn(old, BROKERS)
        control = next(node for node in BROKERS if node != old)
        before = snapshot("game-api")
        # Stop the worker before crashing the leader, but resume it before the leader.
        with interruptible(), ExitStack() as failed_broker:
            with stopped("event-worker"):
                queued = create_game()["game_id"]
                wait_for(lambda: outbox_empty(queued))
                wait_for(lambda: queue()["messages"] >= 1)
                start = time.monotonic()
                failed_broker.enter_context(crashed(old))

                def elected():
                    state = queue(control)
                    return state["leader"] != leader and len(state["online"]) == 2

                wait_for(elected, seconds=30)
                wait_for(lambda: queue(control)["messages"] >= 1)
                new = create_game()["game_id"]
                wait_for(lambda: outbox_empty(new), seconds=30)
                wait_for(lambda: queue(control)["messages"] >= 2)
            for game in (queued, new):
                wait_for(lambda: worker_events(game), seconds=30)
            self.assertFalse(inspect(old)["State"]["Running"])
            self.assertEqual(snapshot("game-api"), before)
            elapsed = time.monotonic() - start
            self.assertLessEqual(elapsed, 60)
            print(f"Leader {leader} -> {queue(control)['leader']}; backlog/new event in {elapsed:.2f}s")
        self.cluster_ready(control)

    def test_consumer_reconnect_without_client_restarts(self):
        self.cluster_ready()
        connections = json.loads(compose("exec", "-T", "rabbitmq", "rabbitmqctl", "-q",
                                         "list_connections", "name", "--formatter", "json"))
        addresses = [re.search(r" -> ([^:]+):", row["name"])[1] for row in connections
                     if row["name"].startswith("10.10.3.4:")]
        self.assertEqual(len(addresses), 1)
        old = dict(zip(("10.10.3.3", "10.10.3.5", "10.10.3.6"), BROKERS))[addresses[0]]
        before = [snapshot(service) for service in ("game-api", "event-worker")]
        start = time.monotonic()
        with crashed(old):
            game = create_game()["game_id"]
            wait_for(lambda: worker_events(game), seconds=60)
            wait_for(lambda: outbox_empty(game))
            self.assertEqual([snapshot(service) for service in ("game-api", "event-worker")], before)
            self.assertFalse(inspect(old)["State"]["Running"])
            self.assertLessEqual(time.monotonic() - start, 60)
            print(f"Consumer reconnected after {old} crash in {time.monotonic() - start:.2f}s")
        self.cluster_ready()


if __name__ == "__main__":
    unittest.main(verbosity=2)
