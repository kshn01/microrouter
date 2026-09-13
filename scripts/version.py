Import("env")
import subprocess
import os

import re

def get_firmware_version():
    # 1. Try git tag
    try:
        try:
            subprocess.run(["git", "fetch", "--tags", "-q"], timeout=2, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        except Exception:
            pass
        # Use --abbrev=0 to get the exact tag without commit count or git hash
        tag = subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"], stderr=subprocess.DEVNULL).decode().strip()
        if tag:
            # Cleanly strip leading 'v' and any trailing non-semver chars
            clean = tag.lstrip("v").strip()
            # If tag had commit suffix, strip it (e.g. 1.0.6-1-g... -> 1.0.6)
            clean = re.sub(r"-.*$", "", clean)
            if clean:
                return clean
    except Exception:
        pass

    # 2. Fallback to src/config.h if git is not available
    try:
        config_path = os.path.join(env.get("PROJECT_DIR"), "src", "config.h")
        with open(config_path, "r") as f:
            for line in f:
                if "#define FIRMWARE_VERSION" in line:
                    parts = line.split('"')
                    if len(parts) >= 2:
                        return parts[1]
    except Exception:
        pass

    return "1.0.6"

version = get_firmware_version()
print(f"--> [Build] Auto-detected dynamic FIRMWARE_VERSION: {version}")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", f'\\"{version}\\"')])
