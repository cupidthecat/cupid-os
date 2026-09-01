import hashlib
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FREEDOOM = ROOT / "third_party" / "freedoom" / "0.13.0"


class FreedoomFixtureTests(unittest.TestCase):
    def test_official_phase_one_iwad_is_pinned(self) -> None:
        wad = FREEDOOM / "freedoom1.wad"
        self.assertEqual(wad.stat().st_size, 28_795_076)
        self.assertEqual(
            hashlib.sha256(wad.read_bytes()).hexdigest(),
            "7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d",
        )
        self.assertEqual(wad.read_bytes()[:4], b"IWAD")

    def test_redistribution_files_are_pinned(self) -> None:
        expected = {
            "COPYING.txt": "7c62f2c520769c798774f416637eec4921e5f0aafdac1245ae7c8a8cf65fe102",
            "CREDITS.txt": "72364333d295c598d75ef9553d3b3661ad0b60e27271c87b7637c74b2f46918a",
            "CREDITS-MUSIC.txt": "2bb77677bbc4e587bba44ccb6514ee0f5397c7bb8ae9580b9188d3d6a57c0a31",
            "README.html": "e4ec61d6449c4116e818c79e15e178dacbf78cee43a637b7c468f04d0c4390b0",
        }
        for name, digest in expected.items():
            with self.subTest(name=name):
                self.assertEqual(
                    hashlib.sha256((FREEDOOM / name).read_bytes()).hexdigest(),
                    digest,
                )

        license_text = (FREEDOOM / "COPYING.txt").read_text(encoding="utf-8")
        self.assertIn("Redistribution and use in source and binary forms", license_text)
        self.assertIn("Neither the name of the Freedoom project", license_text)

    def test_upstream_release_authentication_files_are_present(self) -> None:
        checksum_path = FREEDOOM / "freedoom-0.13.0-CHECKSUM"
        signature_path = FREEDOOM / "freedoom-0.13.0.zip.sig"
        checksum = checksum_path.read_text(encoding="utf-8")
        self.assertIn(
            "SHA256 (freedoom-0.13.0.zip) = "
            "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59",
            checksum,
        )
        self.assertEqual(
            hashlib.sha256(checksum_path.read_bytes()).hexdigest(),
            "01a627fa0c80b7446f4f435df6fffef8f354a9987b23870bc5ea5464ca82e977",
        )
        self.assertEqual(signature_path.stat().st_size, 438)
        self.assertEqual(
            hashlib.sha256(signature_path.read_bytes()).hexdigest(),
            "77f243f179a42317e22e816e3e7b012a505d36e8416fe5cb914220e58412c789",
        )

    def test_extracted_iwad_matches_the_pinned_release_archive(self) -> None:
        archive = FREEDOOM / "freedoom-0.13.0.zip"
        self.assertEqual(archive.stat().st_size, 24_143_781)
        self.assertEqual(
            hashlib.sha256(archive.read_bytes()).hexdigest(),
            "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59",
        )
        with zipfile.ZipFile(archive) as release:
            member = release.read("freedoom-0.13.0/freedoom1.wad")
            self.assertEqual(
                hashlib.sha256(member).hexdigest(),
                "7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d",
            )
            self.assertEqual(member, (FREEDOOM / "freedoom1.wad").read_bytes())


if __name__ == "__main__":
    unittest.main()
