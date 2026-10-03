"""Export a complete flash image into the version-controlled release folder."""

from pathlib import Path
import subprocess

Import("env")


def package_release(source, target, env):
    output = Path(env.subst("$PROJECT_DIR")) / "release" / "AirPuter.bin"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        env.subst("$PYTHONEXE"), env.subst("$OBJCOPY"),
        "--chip", env.BoardConfig().get("build.mcu"),
        "merge_bin", "-o", str(output),
        "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size", env.BoardConfig().get("upload.flash_size"),
    ]
    for offset, filename in env.get("FLASH_EXTRA_IMAGES", []):
        command.extend([str(offset), env.subst(filename)])
    command.extend([env.subst("$ESP32_APP_OFFSET"), env.subst("$BUILD_DIR/${PROGNAME}.bin")])
    subprocess.run(command, check=True)


# Also export when the firmware is already up to date.
env.AlwaysBuild(env.Alias("package_release", "$BUILD_DIR/${PROGNAME}.bin", package_release))
env.Default("package_release")
