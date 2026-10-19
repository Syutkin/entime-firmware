"""Export the actual PlatformIO flash layout for packaging after the build."""

import json
from pathlib import Path

Import("env")


def expanded_path(value):
    return str(Path(env.subst(str(value))).resolve())


metadata = {
    "chip": env.BoardConfig().get("build.mcu"),
    "esptool": expanded_path("$OBJCOPY"),
    "firmware": expanded_path("$BUILD_DIR/${PROGNAME}.bin"),
    "application_offset": env.subst("$ESP32_APP_OFFSET"),
    "flash_images": [
        {"offset": env.subst(str(offset)), "path": expanded_path(path)}
        for offset, path in env.get("FLASH_EXTRA_IMAGES", [])
    ],
}
metadata_path = Path(env.subst("$BUILD_DIR")) / "release-metadata.json"
metadata_path.parent.mkdir(parents=True, exist_ok=True)
metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
