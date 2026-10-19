"""Package the existing PlatformIO build and the matching changelog section."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def release_notes(changelog, version):
    sections = list(re.finditer(r"^##\s+\[([^\]]+)\][^\n]*$", changelog, re.MULTILINE))
    matches = [i for i, section in enumerate(sections) if section[1] == version]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one changelog section for {version}")
    index = matches[0]
    end = sections[index + 1].start() if index + 1 < len(sections) else len(changelog)
    body = changelog[sections[index].end():end]
    # Reference links belong to the document, not to the last release's notes.
    body = re.sub(r"^\[[^\]]+\]:[^\n]*$", "", body, flags=re.MULTILINE).strip()
    if not any(line.strip() and not line.lstrip().startswith("#") for line in body.splitlines()):
        raise ValueError(f"Empty changelog section for {version}")
    return body + "\n"


def prepare_release(project, metadata_path, tag, output):
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", tag):
        raise ValueError(f"Invalid release tag: {tag}")
    config = (project / "src/config.h").read_text(encoding="utf-8")
    versions = re.findall(r'^\s*#define\s+VERSION\s+"([^"]+)"', config, re.MULTILINE)
    if versions != [tag[1:]]:
        raise ValueError(f"Tag {tag} does not match VERSION in src/config.h")
    notes = release_notes((project / "CHANGELOG.md").read_text(encoding="utf-8"), tag[1:])
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    firmware = Path(metadata["firmware"])
    images = metadata["flash_images"] + [
        {"offset": metadata["application_offset"], "path": str(firmware)}
    ]
    images.sort(key=lambda image: int(image["offset"], 0))
    if not metadata["flash_images"]:
        raise ValueError("Missing bootloader/partition images in PlatformIO metadata")
    for image in images:
        path = Path(image["path"])
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing or empty flash image: {path}")

    output.mkdir(parents=True, exist_ok=True)
    app_image = output / f"entime-{tag}-firmware.bin"
    full_image = output / f"entime-{tag}-full.bin"
    # Keep the flash headers produced by the build; only combine the images.
    command = [metadata["esptool"], "--chip", metadata["chip"], "merge-bin", "-o", str(full_image)]
    for image in images:
        command.extend([image["offset"], image["path"]])
    subprocess.run(command, check=True)
    shutil.copyfile(firmware, app_image)
    checksums = "".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
        for path in (app_image, full_image)
    )
    (output / "SHA256SUMS").write_text(checksums, encoding="utf-8")
    (output / "release-notes.md").write_text(notes, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare_release(args.project, args.metadata, args.tag, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release preparation failed: {error}\n")


if __name__ == "__main__":
    main()
