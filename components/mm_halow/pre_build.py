"""PlatformIO extra script for mm_halow component.

Generates firmware binary objects via objcopy and adds them to the
linker command so PlatformIO includes them in the final firmware.
PlatformIO's SCons build doesn't automatically run CMake custom targets,
so we handle this ourselves.
"""
import os
import subprocess

Import("env")  # noqa: F821 - PlatformIO provides this


def generate_and_link_firmware(env):
    build_dir = env.subst("$BUILD_DIR")
    mm_shims_dir = os.path.join(build_dir, "esp-idf", "mm_shims")

    fw_obj = os.path.join(mm_shims_dir, "mm6108.mbin.o")
    bcf_obj = os.path.join(mm_shims_dir, "bcf_mf16858.mbin.o")

    # Check if objects already exist
    if os.path.exists(fw_obj) and os.path.exists(bcf_obj):
        env.Append(LINKFLAGS=[fw_obj, bcf_obj])
        print(f"mm_halow: Using existing firmware objects")
        return

    # Try to run ninja to generate them
    ninja = None
    import shutil
    ninja = shutil.which("ninja")

    if ninja and os.path.exists(os.path.join(build_dir, "build.ninja")):
        print("mm_halow: Generating firmware objects via ninja...")
        for target in [
            "esp-idf/mm_shims/mm6108.mbin.o",
            "esp-idf/mm_shims/bcf_mf16858.mbin.o",
        ]:
            result = subprocess.run(
                [ninja, target],
                cwd=build_dir,
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                print(f"mm_halow: WARNING: ninja {target} failed: {result.stderr}")

    if os.path.exists(fw_obj) and os.path.exists(bcf_obj):
        env.Append(LINKFLAGS=[fw_obj, bcf_obj])
        print(f"mm_halow: Firmware objects linked successfully")
    else:
        print("mm_halow: ERROR: Could not generate firmware objects!")
        print(f"mm_halow: Expected: {fw_obj}")
        print(f"mm_halow: Expected: {bcf_obj}")


generate_and_link_firmware(env)
