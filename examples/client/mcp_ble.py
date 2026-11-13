#!/usr/bin/env python3
"""Dependency-free MCP client for the ESP32 MCP server (Streamable HTTP, /mcp),
with BLE helpers (issue #14).

Library use:

    from mcp_ble import McpHttpClient
    c = McpHttpClient("http://192.168.1.50", token="...")
    c.initialize()
    devices = c.ble_scan_and_collect(duration_ms=5000)["devices"]

Command line:

    export MCP_URL=http://192.168.1.50 MCP_TOKEN=<token>
    python3 mcp_ble.py scan [--seconds 5]
    python3 mcp_ble.py enumerate <mac> [--addr-type random]
    python3 mcp_ble.py write <mac> <handle> <hex> [--no-response]
    python3 mcp_ble.py watch <mac> <handle> [--seconds 10]
    python3 mcp_ble.py tools

Over HTTP the server cannot push, so subscriptions are read with
ble/notifications (polling).
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request


class McpError(Exception):
    """A JSON-RPC error with the server's structured code and data."""

    def __init__(self, code, message, data=None):
        super().__init__(f"{code}: {message}" + (f" {json.dumps(data)}" if data else ""))
        self.code = code
        self.message = message
        self.data = data


class McpHttpClient:
    def __init__(self, base_url, token, timeout=20.0):
        self.url = base_url.rstrip("/") + "/mcp"
        self.token = token
        self.timeout = timeout
        self.session_id = None
        self._next_id = 1

    # --- transport ---------------------------------------------------------

    def _post(self, message, timeout=None):
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "Authorization": f"Bearer {self.token}",
            "MCP-Protocol-Version": "2025-06-18",
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        req = urllib.request.Request(self.url, data=json.dumps(message).encode(),
                                     headers=headers, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=timeout or self.timeout) as resp:
                sid = resp.headers.get("Mcp-Session-Id")
                if sid:
                    self.session_id = sid
                body = resp.read()
                return json.loads(body) if body else None
        except urllib.error.HTTPError as e:
            detail = e.read().decode(errors="replace")
            if e.code == 401:
                raise SystemExit("401 Unauthorized: check MCP_TOKEN (serial console: token show)")
            if e.code == 404 and self.session_id:
                raise SystemExit("404: session expired; run again to re-initialize")
            raise SystemExit(f"HTTP {e.code}: {detail}")

    def request(self, method, params=None, timeout=None):
        msg_id = self._next_id
        self._next_id += 1
        reply = self._post({"jsonrpc": "2.0", "id": msg_id, "method": method,
                            "params": params or {}}, timeout)
        if reply is None:
            raise RuntimeError(f"{method}: empty response")
        if "error" in reply:
            err = reply["error"]
            raise McpError(err.get("code"), err.get("message"), err.get("data"))
        return reply["result"]

    def notify(self, method, params=None):
        self._post({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def initialize(self):
        result = self.request("initialize", {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "mcp_ble.py", "version": "1.0.0"},
        })
        self.notify("notifications/initialized")
        return result

    def close(self):
        if not self.session_id:
            return
        req = urllib.request.Request(self.url, method="DELETE", headers={
            "Authorization": f"Bearer {self.token}", "Mcp-Session-Id": self.session_id})
        try:
            urllib.request.urlopen(req, timeout=5).close()
        except urllib.error.URLError:
            pass
        self.session_id = None

    # --- tools ----------------------------------------------------------------

    def list_tools(self):
        return self.request("tools/list")["tools"]

    def call_tool(self, name, arguments=None):
        return self.request("tools/call", {"name": name, "arguments": arguments or {}})

    # --- BLE ------------------------------------------------------------------

    def ble_scan(self, duration_ms=5000, **filters):
        return self.request("ble/scan", {"durationMs": duration_ms, **filters})

    def ble_scan_and_collect(self, duration_ms=5000, **filters):
        self.ble_scan(duration_ms, **filters)
        time.sleep(duration_ms / 1000 + 0.2)
        return self.request("ble/scan/results")

    def ble_payloads(self):
        return self.request("ble/scan/payloads")

    def ble_connect(self, mac, addr_type=None, timeout_ms=5000):
        params = {"mac": mac, "timeoutMs": timeout_ms}
        if addr_type:
            params["addrType"] = addr_type
        self.request("ble/connect", params)
        deadline = time.time() + timeout_ms / 1000 + 1
        while True:
            st = self.request("ble/connect/results")
            if st.get("connected"):
                return st
            if not st.get("connecting") or time.time() > deadline:
                raise RuntimeError(f"connect to {mac} failed "
                                   "(phones and most sensors need --addr-type random)")
            time.sleep(0.2)

    def ble_enumerate(self, notify_capture_ms=2000):
        self.request("ble/services/start")
        res = self.request("ble/services/results", {"notifyCaptureMs": notify_capture_ms},
                           timeout=notify_capture_ms / 1000 + 15)
        return res["services"]

    def ble_write(self, handle, value_hex, with_response=True):
        return self.request("ble/write", {"handle": handle, "value": value_hex,
                                          "withResponse": with_response})

    def ble_subscribe(self, handle, indicate=None):
        params = {"handle": handle}
        if indicate is not None:
            params["indicate"] = indicate
        return self.request("ble/subscribe", params)

    def ble_unsubscribe(self, handle):
        return self.request("ble/unsubscribe", {"handle": handle})

    def ble_notifications(self):
        return self.request("ble/notifications")

    def ble_disconnect(self):
        return self.request("ble/disconnect")


# --- command line -----------------------------------------------------------------

def _print_services(services):
    for s in services:
        print(f"service {s['uuid']}")
        for c in s["characteristics"]:
            value = c.get("valueText") or c.get("valueHex") or ""
            print(f"  [{c['handle']:>5}] {c['uuid']}  ({c['properties']})  {value}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=os.environ.get("MCP_URL", "http://esp32.local"))
    ap.add_argument("--token", default=os.environ.get("MCP_TOKEN", ""))
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("scan"); p.add_argument("--seconds", type=float, default=5)
    sub.add_parser("tools")
    for name in ("enumerate", "write", "watch"):
        p = sub.add_parser(name)
        p.add_argument("mac")
        p.add_argument("--addr-type", choices=["public", "random", "rpa-public", "rpa-random"])
        if name in ("write", "watch"):
            p.add_argument("handle", type=int)
        if name == "write":
            p.add_argument("value", help="hex bytes, e.g. 01ff")
            p.add_argument("--no-response", action="store_true")
        if name == "watch":
            p.add_argument("--seconds", type=float, default=10)
    args = ap.parse_args()
    if not args.token:
        ap.error("set MCP_TOKEN or --token (serial console: token show)")

    c = McpHttpClient(args.url, args.token)
    c.initialize()
    try:
        if args.cmd == "tools":
            for t in c.list_tools():
                hint = "ro" if t.get("annotations", {}).get("readOnlyHint") else "rw"
                print(f"{t['name']:<24} {hint}  {t.get('description', '')}")
            return
        if args.cmd == "scan":
            res = c.ble_scan_and_collect(int(args.seconds * 1000))
            for d in sorted(res["devices"], key=lambda d: -d["rssi"]):
                print(f"{d['mac']}  {d['addrType']:<10} {d['rssi']:>4} dBm  "
                      f"{'conn' if d['connectable'] else '    '}  {d['name']}")
            if res.get("evicted"):
                print(f"({res['evicted']} devices evicted to stay within limits)")
            return

        c.ble_connect(args.mac, args.addr_type)
        try:
            services = c.ble_enumerate()
            if args.cmd == "enumerate":
                _print_services(services)
            elif args.cmd == "write":
                r = c.ble_write(args.handle, args.value, with_response=not args.no_response)
                print(f"wrote {r['bytes']} bytes to handle {r['handle']} ({r['uuid']})")
            elif args.cmd == "watch":
                r = c.ble_subscribe(args.handle)
                print(f"subscribed ({r['mode']}) to {r['uuid']}; polling for {args.seconds:g} s")
                end = time.time() + args.seconds
                while time.time() < end:
                    batch = c.ble_notifications()
                    for v in batch["values"]:
                        print(f"  {v['ts']:>10} [{v['handle']}] {v['valueHex']}  {v['valueText']}")
                    if batch.get("dropped"):
                        print(f"  ({batch['dropped']} values dropped: poll faster)")
                    time.sleep(0.5)
                c.ble_unsubscribe(args.handle)
        finally:
            c.ble_disconnect()
    except McpError as e:
        sys.exit(f"MCP error {e}")
    finally:
        c.close()


if __name__ == "__main__":
    main()
