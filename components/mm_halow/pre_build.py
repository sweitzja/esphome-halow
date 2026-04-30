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

bcf_file = _read_sdkconfig_value(build_dir, "CONFIG_MM_BCF_FILE", "bcf_mf16858.mbin")
bcf_base = os.path.splitext(bcf_file)[0]

fw_obj = os.path.join(mm_shims_dir, "mm6108.mbin.o")
bcf_obj = os.path.join(mm_shims_dir, f"{bcf_base}.mbin.o")

# Try to generate objects via ninja if they don't exist
ninja = shutil.which("ninja")
if ninja and os.path.exists(os.path.join(build_dir, "build.ninja")):
    for obj, target in [
        (fw_obj, "esp-idf/mm_shims/mm6108.mbin.o"),
        (bcf_obj, f"esp-idf/mm_shims/{bcf_base}.mbin.o"),
    ]:
        if not os.path.exists(obj):
            print(f"mm_halow: Generating {os.path.basename(obj)}...")
            subprocess.run([ninja, target], cwd=build_dir, capture_output=True)

# Always add to LINKFLAGS (even if files don't exist yet — they will on next build)
env.Append(LINKFLAGS=[fw_obj, bcf_obj])
print(f"mm_halow: Firmware objects: {os.path.exists(fw_obj)}, BCF ({bcf_file}): {os.path.exists(bcf_obj)}")
