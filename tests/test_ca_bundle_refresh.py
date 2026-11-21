## @file
#  @brief Check trust-store refresh with local certificates and failed downloads.
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1] / "tools"
REQUIRES_REFRESH_TOOLS = os.name == "posix" and all(
    shutil.which(name) for name in ("bash", "openssl", "curl", "xxd")
)


@unittest.skipUnless(REQUIRES_REFRESH_TOOLS, "requires the Linux trust-store refresh tools")
class TrustStoreRefreshTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cupid-ca-refresh-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.pem = self.root / "root.pem"
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
             "-subj", "/CN=Cupid OS refresh fixture", "-keyout", str(self.root / "key.pem"),
             "-out", str(self.pem)],
            check=True, capture_output=True,
        )
        self.der = subprocess.check_output(
            ["openssl", "x509", "-in", str(self.pem), "-outform", "DER"]
        )
        self.output = self.root / "kernel/tls/tls_ca_bundle_data.cc"
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous trust store\n")
        self.previous_stamp = self.output.stat().st_mtime_ns

    def refresh(self, script, *args, env=None):
        return subprocess.run(
            ["bash", str(TOOLS / script), *args], cwd=self.root,
            text=True, capture_output=True, env=env, timeout=20,
        )

    def mozilla(self, name, pem):
        bundle = self.root / "ca.pem"
        bundle.write_bytes(name.encode("ascii") + b"\n===============\n" + pem)
        return self.refresh("fetch_ca_bundle_mozilla.sh", bundle.as_uri())

    def four_roots(self, *, fail=False):
        commands = self.root / "commands"
        commands.mkdir()
        curl = commands / "curl"
        curl.write_text(
            '#!/usr/bin/env bash\n'
            'while [[ "$1" != -o ]]; do shift; done\n'
            'if [[ "$CUPID_CA_FAIL" == 1 && "$2" == */GTS_Root_R4.pem ]]; then exit 18; fi\n'
            'cp -- "$CUPID_CA_FIXTURE" "$2"\n',
            encoding="utf-8",
        )
        curl.chmod(0o755)
        return self.refresh("fetch_ca_bundle.sh", env={
            **os.environ, "PATH": f"{commands}:{os.environ['PATH']}",
            "CUPID_CA_FIXTURE": str(self.pem), "CUPID_CA_FAIL": str(int(fail)),
        })

    def assert_published(self, names):
        generated = self.output.read_text(encoding="utf-8")
        arrays = re.findall(r"unsigned char (\w+)_DER\[\] = \{([^}]*)\}", generated)
        self.assertEqual([name for name, _ in arrays], names)
        for name, body in arrays:
            encoded = re.findall(r"0x[0-9a-f]{2}", body)
            self.assertEqual(bytes(int(value, 16) for value in encoded), self.der)
            self.assertIn(f"sizeof({name}_DER)", generated)
        self.assertEqual(list(self.output.parent.glob(".ca-bundle.*")), [])

    def assert_previous_store(self):
        self.assertEqual(self.output.read_bytes(), b"previous trust store\n")
        self.assertEqual(self.output.stat().st_mtime_ns, self.previous_stamp)
        self.assertEqual(list(self.output.parent.glob(".ca-bundle.*")), [])

    def test_mozilla_refresh_publishes_selected_der(self):
        result = self.mozilla("DigiCert test root", self.pem.read_bytes())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_published(["MOZ_ROOT_001"])

    def test_mozilla_refresh_rejects_empty_or_undecodable_selection(self):
        malformed = b"-----BEGIN CERTIFICATE-----\ninvalid\n-----END CERTIFICATE-----\n"
        for name, pem, error in (
            ("Unselected test root", self.pem.read_bytes(), "No certs matched"),
            ("DigiCert test root", malformed, "No matching roots could be decoded"),
        ):
            with self.subTest(name=name):
                result = self.mozilla(name, pem)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(error, result.stderr)
                self.assert_previous_store()

    def test_four_root_refresh_publishes_every_der(self):
        result = self.four_roots()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_published([
            "ISRG_Root_X1", "DigiCert_Global_Root_G2", "GTS_Root_R1", "GTS_Root_R4",
        ])

    def test_four_root_refresh_retains_store_after_late_download_failure(self):
        result = self.four_roots(fail=True)
        self.assertEqual(result.returncode, 18, result.stderr)
        self.assert_previous_store()


if __name__ == "__main__":
    unittest.main()
