#!/usr/bin/env python3
"""Isolated HTTP/HTTPS and boundary tests for web/serve.mjs."""

from __future__ import annotations

import http.client
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "web" / "serve.mjs"
NODE = shutil.which("node")
OPENSSL = shutil.which("openssl")


def unused_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


class RunningServer:
    def __init__(self, arguments: list[str], *, tls: bool = False) -> None:
        self.port = int(arguments[arguments.index("--port") + 1]) if "--port" in arguments else int(arguments[0])
        self.tls = tls
        self.output = ""
        self.process = subprocess.Popen(
            [NODE, str(SERVER), *arguments],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                output = self.process.stdout.read() if self.process.stdout else ""
                raise AssertionError(f"server exited before readiness ({self.process.returncode}):\n{output}")
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.1):
                    return
            except OSError:
                time.sleep(0.05)
        self.close()
        raise AssertionError("server did not begin listening within 8 seconds")

    def request(self, method: str, path: str) -> tuple[int, dict[str, str], bytes]:
        if self.tls:
            connection = http.client.HTTPSConnection(
                "127.0.0.1",
                self.port,
                timeout=4,
                context=ssl._create_unverified_context(),  # Test certificate only.
            )
        else:
            connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=4)
        try:
            connection.request(method, path)
            response = connection.getresponse()
            return response.status, {key.lower(): value for key, value in response.getheaders()}, response.read()
        finally:
            connection.close()

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self.process.stdout:
            self.output += self.process.stdout.read()
            self.process.stdout.close()

    def __enter__(self) -> "RunningServer":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


@unittest.skipUnless(NODE, "node is required")
class ServerHttpTests(unittest.TestCase):
    def assert_isolation_headers(self, headers: dict[str, str]) -> None:
        self.assertEqual(headers.get("cross-origin-opener-policy"), "same-origin")
        self.assertEqual(headers.get("cross-origin-embedder-policy"), "require-corp")
        self.assertEqual(headers.get("cross-origin-resource-policy"), "same-origin")

    def test_legacy_http_invocation_headers_mime_cache_and_health(self) -> None:
        port = unused_port()
        with RunningServer([str(port)]) as server:
            status, headers, body = server.request("GET", "/")
            self.assertEqual(status, 200)
            self.assertEqual(body, (ROOT / "web" / "index.html").read_bytes())
            self.assertEqual(headers.get("content-type"), "text/html; charset=utf-8")
            self.assertEqual(headers.get("cache-control"), "no-store")
            self.assert_isolation_headers(headers)

            status, headers, _ = server.request("GET", "/web/manifest.webmanifest")
            self.assertEqual(status, 200)
            self.assertEqual(headers.get("content-type"), "application/manifest+json; charset=utf-8")
            self.assert_isolation_headers(headers)

            status, headers, _ = server.request("GET", "/web/service-worker.js")
            self.assertEqual(status, 200)
            self.assertEqual(headers.get("content-type"), "text/javascript; charset=utf-8")
            self.assertEqual(headers.get("cache-control"), "no-cache")

            status, headers, body = server.request("HEAD", "/web/index.html")
            self.assertEqual(status, 200)
            self.assertEqual(body, b"")
            self.assertEqual(headers.get("content-type"), "text/html; charset=utf-8")

            status, headers, body = server.request("GET", "/_health")
            self.assertEqual(status, 200)
            self.assertEqual(body, b'{"status":"ok","transport":"http"}')
            self.assertEqual(headers.get("content-type"), "application/json")
            self.assertNotIn(str(ROOT).encode(), body)
            self.assert_isolation_headers(headers)

    def test_traversal_bad_encoding_and_methods_are_refused(self) -> None:
        port = unused_port()
        with RunningServer(["--port", str(port), "--host", "127.0.0.1"]) as server:
            for path in (
                "/../package.json",
                "/%2e%2e/package.json",
                "/web/%2e%2e/package.json",
                "/%2e%2e%2fpackage.json",
                "/web%2f..%2fpackage.json",
                "/web%5c..%5cpackage.json",
            ):
                with self.subTest(path=path):
                    status, _, body = server.request("GET", path)
                    self.assertEqual(status, 403)
                    self.assertNotIn(str(ROOT).encode(), body)

            status, _, _ = server.request("GET", "/bad%ZZpath")
            self.assertEqual(status, 400)

            status, headers, body = server.request("POST", "/web/index.html")
            self.assertEqual(status, 405)
            self.assertEqual(headers.get("allow"), "GET, HEAD")
            self.assertEqual(body, b"method not allowed")
            self.assert_isolation_headers(headers)

    def test_preserved_mime_map(self) -> None:
        expected = {
            ".html": "text/html; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
            ".mjs": "text/javascript; charset=utf-8",
            ".wasm": "application/wasm",
            ".data": "application/octet-stream",
            ".json": "application/json",
            ".webmanifest": "application/manifest+json; charset=utf-8",
            ".png": "image/png",
            ".svg": "image/svg+xml; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".unknown": "application/octet-stream",
        }
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            fixture_dir = Path(temporary)
            for suffix in expected:
                (fixture_dir / f"fixture{suffix}").write_bytes(b"fixture")
            route_dir = fixture_dir.relative_to(ROOT).as_posix()
            port = unused_port()
            with RunningServer(["--port", str(port)]) as server:
                for suffix, content_type in expected.items():
                    with self.subTest(suffix=suffix):
                        status, headers, body = server.request("GET", f"/{route_dir}/fixture{suffix}")
                        self.assertEqual(status, 200)
                        self.assertEqual(body, b"fixture")
                        self.assertEqual(headers.get("content-type"), content_type)
                        self.assertEqual(headers.get("cache-control"), "no-store")
                        self.assert_isolation_headers(headers)

    def test_lan_binding_emits_trust_warning(self) -> None:
        server = RunningServer(["--port", str(unused_port()), "--host", "0.0.0.0"])
        server.close()
        self.assertIn("LAN serving is enabled", server.output)
        self.assertIn("self-signed or untrusted certificate does NOT", server.output)

    def test_symlink_escape_is_refused_when_supported(self) -> None:
        with tempfile.TemporaryDirectory() as outside, tempfile.TemporaryDirectory(dir=ROOT) as inside:
            outside_file = Path(outside) / "secret.txt"
            outside_file.write_text("must-not-be-served", encoding="utf-8")
            link = Path(inside) / "escape.txt"
            try:
                link.symlink_to(outside_file)
            except (OSError, NotImplementedError) as error:
                self.skipTest(f"symlink creation unavailable: {error}")

            port = unused_port()
            relative_link = link.relative_to(ROOT).as_posix()
            with RunningServer(["--port", str(port)]) as server:
                status, _, body = server.request("GET", f"/{relative_link}")
                self.assertEqual(status, 403)
                self.assertNotIn(b"must-not-be-served", body)

    def test_strict_cli_rejects_invalid_or_partial_configuration(self) -> None:
        cases = (
            (["--port", "0"], "port must be"),
            (["--port", "65536"], "port must be"),
            (["--port", "8.1"], "port must be"),
            (["--port", "8091", "--port", "8092"], "only once"),
            (["--host", "bad host"], "host must be"),
            (["--wat"], "unknown option"),
            (["--cert", "certificate.pem"], "supplied together"),
            (["8091", "--port", "8092"], "only one port"),
        )
        for arguments, expected in cases:
            with self.subTest(arguments=arguments):
                result = subprocess.run(
                    [NODE, str(SERVER), *arguments],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    timeout=4,
                )
                self.assertEqual(result.returncode, 2)
                self.assertIn(expected, result.stderr)

        with tempfile.TemporaryDirectory() as outside:
            cert = Path(outside) / "cert.pem"
            key = Path(outside) / "key.pem"
            cert.write_text("not a certificate", encoding="utf-8")
            key.write_text("not a key", encoding="utf-8")
            result = subprocess.run(
                [NODE, str(SERVER), "--cert", str(cert), "--key", str(key)],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=4,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("inside the repository", result.stderr)

    @unittest.skipUnless(OPENSSL, "openssl is required for the ephemeral HTTPS test")
    def test_https_with_ephemeral_certificate(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            cert = Path(temporary) / "cert.pem"
            key = Path(temporary) / "key.pem"
            generated = subprocess.run(
                [
                    OPENSSL,
                    "req",
                    "-x509",
                    "-newkey",
                    "rsa:2048",
                    "-nodes",
                    "-days",
                    "1",
                    "-subj",
                    "/CN=localhost",
                    "-addext",
                    "subjectAltName=DNS:localhost,IP:127.0.0.1",
                    "-keyout",
                    str(key),
                    "-out",
                    str(cert),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=20,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)

            port = unused_port()
            with RunningServer(
                ["--port", str(port), "--cert", str(cert), "--key", str(key)],
                tls=True,
            ) as server:
                status, headers, body = server.request("GET", "/_health")
                self.assertEqual(status, 200)
                self.assertEqual(body, b'{"status":"ok","transport":"https"}')
                self.assertEqual(headers.get("cache-control"), "no-store")
                self.assert_isolation_headers(headers)

                status, headers, body = server.request("GET", "/web/index_webxr.html")
                self.assertEqual(status, 200)
                self.assertEqual(body, (ROOT / "web" / "index_webxr.html").read_bytes())
                self.assertEqual(headers.get("content-type"), "text/html; charset=utf-8")
                self.assert_isolation_headers(headers)

                status, _, _ = server.request("GET", "/%2e%2e%2foutside")
                self.assertEqual(status, 403)
                status, headers, _ = server.request("DELETE", "/web/index.html")
                self.assertEqual(status, 405)
                self.assertEqual(headers.get("allow"), "GET, HEAD")


if __name__ == "__main__":
    unittest.main(verbosity=2)
