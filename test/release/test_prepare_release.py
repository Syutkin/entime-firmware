import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/prepare_release.py"
SPEC = importlib.util.spec_from_file_location("prepare_release", SCRIPT)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseNotesTests(unittest.TestCase):
    def test_selects_exact_version_and_preserves_markdown(self):
        changelog = """# Changelog
## [0.2.1] - 2026-10-20
- Newer
## [0.2.0] - 2026-10-19

### Fixed

- Исправление `foo`.
  Продолжение.

## [0.1.0] - 2026-05-16
- Older
"""
        self.assertEqual(
            release.release_notes(changelog, "0.2.0"),
            "### Fixed\n\n- Исправление `foo`.\n  Продолжение.\n",
        )

    def test_last_section_does_not_include_document_links(self):
        self.assertEqual(
            release.release_notes("## [0.1.0]\n- First\n\n[0.1.0]: https://example.com\n", "0.1.0"),
            "- First\n",
        )

    def test_missing_duplicate_and_empty_sections_are_rejected(self):
        for changelog in (
            "## [0.1.0]\n- Old\n",
            "## [0.2.0]\n- One\n## [0.2.0]\n- Two\n",
            "## [0.2.0]\n\n### Fixed\n\n[0.2.0]: https://example.com\n",
        ):
            with self.subTest(changelog=changelog), self.assertRaises(ValueError):
                release.release_notes(changelog, "0.2.0")


class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="entime release ")
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        (self.project / "src").mkdir()
        (self.project / "src/config.h").write_text('#define VERSION "0.2.0"\n', encoding="utf-8")
        (self.project / "CHANGELOG.md").write_text("## [0.2.0]\n- Test release\n", encoding="utf-8")
        firmware = self.project / "firmware.bin"
        firmware.write_bytes(b"firmware data")
        bootloader = self.project / "bootloader.bin"
        bootloader.write_bytes(b"bootloader data")
        self.metadata = self.project / "metadata.json"
        self.metadata.write_text(json.dumps({
            "chip": "esp32s3",
            "esptool": "/tool path/esptool",
            "firmware": str(firmware),
            "application_offset": "0x20000",
            "flash_images": [{"offset": "0x1000", "path": str(bootloader)}],
        }), encoding="utf-8")
        self.output = self.project / "release"

    def prepare(self, tag="v0.2.0"):
        release.prepare_release(self.project, self.metadata, tag, self.output)

    @patch.object(release.subprocess, "run")
    def test_tag_mismatch_and_invalid_tag_stop_before_packaging(self, run):
        for tag in ("v0.1.0", "0.2.0", "v0.2.0/other"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                self.prepare(tag)
        run.assert_not_called()
        self.assertFalse(self.output.exists())

    @patch.object(release.subprocess, "run")
    def test_missing_notes_stop_before_packaging(self, run):
        (self.project / "CHANGELOG.md").write_text("## [0.1.0]\n- Old\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.prepare()
        run.assert_not_called()
        self.assertFalse(self.output.exists())

    @patch.object(release.subprocess, "run")
    def test_missing_bootloader_stops_before_packaging(self, run):
        (self.project / "bootloader.bin").unlink()
        with self.assertRaises(ValueError):
            self.prepare()
        run.assert_not_called()

    @patch.object(release.subprocess, "run")
    def test_merge_failure_does_not_produce_release_manifest(self, run):
        run.side_effect = subprocess.CalledProcessError(1, "esptool")
        with self.assertRaises(subprocess.CalledProcessError):
            self.prepare()
        self.assertFalse((self.output / "SHA256SUMS").exists())
        self.assertFalse((self.output / "release-notes.md").exists())

    @patch.object(release.subprocess, "run")
    def test_packages_versioned_images_with_platformio_offsets_and_checksums(self, run):
        full = self.output / "entime-v0.2.0-full.bin"
        run.side_effect = lambda *args, **kwargs: full.write_bytes(b"merged image")
        self.prepare()
        command = run.call_args.args[0]
        self.assertEqual(command, [
            "/tool path/esptool", "--chip", "esp32s3", "merge-bin", "-o", str(full),
            "0x1000", str(self.project / "bootloader.bin"),
            "0x20000", str(self.project / "firmware.bin"),
        ])
        firmware = self.output / "entime-v0.2.0-firmware.bin"
        self.assertEqual(firmware.read_bytes(), b"firmware data")
        self.assertEqual((self.output / "release-notes.md").read_text(), "- Test release\n")
        checksums = (self.output / "SHA256SUMS").read_text().splitlines()
        self.assertEqual(checksums, [
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}"
            for path in (firmware, full)
        ])


if __name__ == "__main__":
    unittest.main()
