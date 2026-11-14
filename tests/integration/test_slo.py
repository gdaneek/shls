"""Проверяет корректность ста чтений, задержку p95 и доставку пяти событий за заданное время.
Это короткая проверка учебных SLO на работающем стенде, а не нагрузочный тест или оценка месячной доступности.
"""

import json
import math
import time
import unittest

from support import compose, create_game, wait_for, worker_events


class ServiceLevels(unittest.TestCase):
    def test_correct_reads_and_latency(self):
        game = create_game()["game_id"]
        expected = dict(game_id=game, status="waiting", version=1, current_turn="white", winner=None,
                        board=[".b.b.b.b", "b.b.b.b.", ".b.b.b.b", "........",
                               "........", "w.w.w.w.", ".w.w.w.w", "w.w.w.w."])
        times = []
        for _ in range(100):
            try:
                # curl measures HTTP time inside API, excluding docker exec overhead.
                raw = compose("exec", "-T", "game-api", "curl", "-sS", "--max-time", "5",
                              "-w", "\n%{http_code} %{time_total}", f"http://127.0.0.1:8000/games/{game}")
                body, metrics = raw.rsplit("\n", 1)
                status, elapsed = metrics.split()
                state = json.loads(body)
                if status == "200" and isinstance(state, dict) and all(state.get(key) == value for key, value in expected.items()):
                    times.append(float(elapsed))
            except (RuntimeError, ValueError):
                pass  # Failed/invalid reads still count in the denominator of 100.
        self.assertGreaterEqual(len(times), 99, f"correct reads: {len(times)}/100")
        p95 = sorted(times)[math.ceil(len(times) * 0.95) - 1]
        self.assertLessEqual(p95, 0.5)
        print(f"Correct reads: {len(times)}/100; p95={p95:.6f}s")

    def test_five_events_within_fifteen_seconds(self):
        for sample in range(5):
            with self.subTest(sample=sample):
                start = time.monotonic()
                game = create_game()["game_id"]
                wait_for(lambda: worker_events(game, "game.created"), seconds=max(0, 15 - (time.monotonic() - start)))
                self.assertLessEqual(time.monotonic() - start, 15)


if __name__ == "__main__":
    unittest.main(verbosity=2)
