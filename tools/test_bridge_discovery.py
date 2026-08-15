"""Focused stdlib tests for UnrealBridge UDP discovery."""

from __future__ import annotations

import importlib.util
import json
import socket
import sys
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "bridge_discovery.py"
)
SPEC = importlib.util.spec_from_file_location("bridge_discovery", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Cannot load discovery module from {MODULE_PATH}")
bridge_discovery = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bridge_discovery
SPEC.loader.exec_module(bridge_discovery)


class FakeDiscoverySocket:
    def __init__(self, *, fail_hosts=(), response_count=1):
        self.fail_hosts = set(fail_hosts)
        self.response_count = response_count
        self.request_id = ""
        self.targets = []
        self.closed = False

    def setsockopt(self, *_args):
        pass

    def bind(self, _address):
        pass

    def sendto(self, payload, target):
        self.targets.append(target)
        self.request_id = json.loads(payload.decode("utf-8"))["request_id"]
        if target[0] in self.fail_hosts:
            raise OSError(f"simulated send failure for {target[0]}")

    def settimeout(self, _timeout):
        pass

    def recvfrom(self, _max_size):
        if self.response_count <= 0:
            raise socket.timeout()
        self.response_count -= 1
        payload = json.dumps({
            "v": 1,
            "type": "response",
            "request_id": self.request_id,
            "pid": 4242,
            "project": "TestProject",
            "project_path": "C:/Projects/TestProject/TestProject.uproject",
            "engine_version": "5.7.0",
            "tcp_bind": "127.0.0.1",
            "tcp_port": 32123,
            "token_fingerprint": "",
        }).encode("utf-8")
        return payload, ("127.0.0.1", 9876)

    def close(self):
        self.closed = True


class DiscoveryTests(unittest.TestCase):
    def run_discovery(self, fake_socket):
        with mock.patch.object(
            bridge_discovery.socket, "socket", return_value=fake_socket
        ):
            return bridge_discovery.discover(timeout_ms=10)

    def test_multicast_and_loopback_responses_are_deduplicated_by_pid(self):
        fake_socket = FakeDiscoverySocket(response_count=2)

        endpoints = self.run_discovery(fake_socket)

        self.assertEqual(
            fake_socket.targets,
            [
                (bridge_discovery.DEFAULT_DISCOVERY_GROUP, 9876),
                (bridge_discovery.LOCAL_DISCOVERY_HOST, 9876),
            ],
        )
        self.assertEqual([endpoint.pid for endpoint in endpoints], [4242])
        self.assertTrue(fake_socket.closed)

    def test_loopback_still_discovers_when_multicast_send_fails(self):
        fake_socket = FakeDiscoverySocket(
            fail_hosts={bridge_discovery.DEFAULT_DISCOVERY_GROUP}
        )

        endpoints = self.run_discovery(fake_socket)

        self.assertEqual([endpoint.tcp_port for endpoint in endpoints], [32123])

    def test_discovery_raises_only_when_every_send_path_fails(self):
        fake_socket = FakeDiscoverySocket(
            fail_hosts={
                bridge_discovery.DEFAULT_DISCOVERY_GROUP,
                bridge_discovery.LOCAL_DISCOVERY_HOST,
            },
            response_count=0,
        )

        with self.assertRaises(OSError):
            self.run_discovery(fake_socket)
        self.assertTrue(fake_socket.closed)


if __name__ == "__main__":
    unittest.main()
