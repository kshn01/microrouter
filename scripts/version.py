Import("env")
import subprocess
import os

def get_firmware_version():
    # 1. Try git tag / commit
    try:
        try:
            subprocess.run(["git", "fetch", "--tags", "-q"], timeout=2, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        except Exception:
            pass
        tag = subprocess.check_output(["git", "describe", "--tags", "--always"], stderr=subprocess.DEVNULL).decode().strip()
        if tag:
            return tag.lstrip("v")
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

    return "1.0.5"

version = get_firmware_version()
print(f"--> [Build] Auto-detected dynamic FIRMWARE_VERSION: {version}")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", f'\\"{version}\\"')])
