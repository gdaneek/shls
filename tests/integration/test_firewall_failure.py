"""Block an essential SQL connection, observe 503, then restore the same policy."""

import os
import re
import unittest
import uuid

from support import DISRUPTIVE, create_game, interruptible, primary, redis, request, run, sql, wait_for


@DISRUPTIVE
class FirewallFailure(unittest.TestCase):
    def test_required_connection_failure_and_rollback(self):
        node = primary()
        ip = "10.10.2.3" if node == "postgres" else "10.10.2.5"
        command = (["sudo", "-n"] if os.geteuid() else []) + ["/usr/sbin/nft"]
        table = "mini_checkers_fault_" + uuid.uuid4().hex

        def nft(*args, **kwargs):
            return run(*command, *args, **kwargs)

        before = nft("-s", "list", "table", "bridge", "mini_checkers").stdout
        self.assertNotEqual(nft("list", "table", "bridge", table, check=False).returncode, 0)
        self.assertEqual(request("GET", "/health/ready")[0], 200)
        self.assertEqual(sql("SELECT 1", node=node), "1")
        game = create_game()["game_id"]
        self.assertEqual(request("GET", f"/games/{game}")[0], 200)
        with interruptible():
            try:
                nft("-f", "-", input_text=f'''table bridge {table} {{
                  chain forward {{
                    type filter hook forward priority -10; policy accept;
                    meta ibrname "mc-data" ip saddr 10.10.2.2 ip daddr {ip} tcp dport 5432 counter drop
                  }}
                }}''')
                self.assertEqual(request("GET", "/health/live")[0], 200)
                self.assertEqual(request("GET", "/health/ready")[0], 503)
                redis("DEL", f"game:{game}")
                self.assertEqual(request("GET", f"/games/{game}")[0], 503)
                self.assertEqual(sql("SELECT 1", node=node), "1")
                rules = nft("list", "table", "bridge", table).stdout
                self.assertGreater(int(re.search(r"counter packets (\d+)", rules)[1]), 0)
            finally:
                if nft("list", "table", "bridge", table, check=False).returncode == 0:
                    nft("delete", "table", "bridge", table)
        wait_for(lambda: request("GET", "/health/ready")[0] == 200, seconds=30)
        self.assertEqual(request("GET", f"/games/{game}")[0], 200)
        self.assertEqual(nft("-s", "list", "table", "bridge", "mini_checkers").stdout, before)
        print("HTTP 200 -> 503 -> 200; DROP observed; original firewall policy unchanged")


if __name__ == "__main__":
    unittest.main(verbosity=2)
