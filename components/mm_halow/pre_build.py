"""PlatformIO extra script for mm_halow component.

Generates firmware binary objects via ninja and adds them to the linker.
"""
import os
import subprocess
import shutil

Import("env")  # noqa: F821


def _read_sdkconfig_value(build_dir, key, default):
    # Search upward from build_dir for sdkconfig files
    search_dir = build_dir
    for _ in range(5):
        search_dir = os.path.dirname(search_dir)
        if not search_dir or search_dir == "/":
            break
        try:
            for fname in os.listdir(search_dir):
                if fname.startswith("sdkconfig."):
                    path = os.path.join(search_dir, fname)
                    with open(path) as f:
                        for line in f:
                            if line.startswith(f"{key}="):
                                return line.split("=", 1)[1].strip().strip('"')
        except OSError:
            pass
    return default


build_dir = env.subst("$BUILD_DIR")
mm_shims_dir = os.path.join(build_dir, "esp-idf", "mm_shims")

# Determine BCF filename from sdkconfig (handles both upstream and Seeed SDK)
bcf_file = _read_sdkconfig_value(build_dir, "CONFIG_MM_BCF_FILE", None)
if bcf_file:
    bcf_base = os.path.splitext(bcf_file)[0]
else:
    # Seeed SDK uses choice-based Kconfig; detect which BCF was selected
    bcf_base = "bcf_mf16858_us"  # default
    for name in ["MF16858_US", "MF08651_US", "MF08551", "MF08251", "MF03120"]:
        if _read_sdkconfig_value(build_dir, f"CONFIG_MM_BCF_{name}", None) == "y":
            bcf_base = f"bcf_{name.lower()}"
            break

fw_obj = os.path.join(mm_shims_dir, "mm6108.mbin.o")
bcf_obj = os.path.join(mm_shims_dir, f"{bcf_base}.mbin.o")

# Try to generate/find objects if they don't exist
ninja = shutil.which("ninja")

for obj, target in [
    (fw_obj, "esp-idf/mm_shims/mm6108.mbin.o"),
    (bcf_obj, f"esp-idf/mm_shims/{bcf_base}.mbin.o"),
]:
    if os.path.exists(obj):
        continue

    # Try ninja first (upstream SDK uses objcopy custom commands)
    if ninja and os.path.exists(os.path.join(build_dir, "build.ninja")):
        print(f"mm_halow: Generating {os.path.basename(obj)} via ninja...")
        subprocess.run([ninja, target], cwd=build_dir, capture_output=True)

    # If still missing, look for pre-built .o in the SDK's mm_shims dir (Seeed SDK)
    if not os.path.exists(obj):
        obj_name = os.path.basename(obj)
        # Search for pre-built .o in mm_shims and morsefirmware dirs
        for search_root in [mm_shims_dir.replace(build_dir + "/esp-idf/mm_shims", "")]:
            pass
        # Search common SDK locations via environment
        for env_key in ["MMIOT_ROOT", "MM_IOT_SDK_PATH"]:
            pass
        # Direct search: find the .o in the SDK tree
        sdk_mm_shims = None
        for candidate in [
            os.path.expanduser("~/esp/mm-iot-esp32/framework/mm_shims"),
            os.path.expanduser("~/esp/mm-iot-esp32-upstream/framework/mm_shims"),
        ]:
            candidate_obj = os.path.join(candidate, obj_name)
            if os.path.exists(candidate_obj):
                os.makedirs(os.path.dirname(obj), exist_ok=True)
                shutil.copy2(candidate_obj, obj)
                print(f"mm_halow: Copied {obj_name} from SDK")
                break

# Always add to LINKFLAGS (even if files don't exist yet — they will on next build)
env.Append(LINKFLAGS=[fw_obj, bcf_obj])
print(f"mm_halow: Firmware objects: {os.path.exists(fw_obj)}, BCF ({bcf_file}): {os.path.exists(bcf_obj)}")
