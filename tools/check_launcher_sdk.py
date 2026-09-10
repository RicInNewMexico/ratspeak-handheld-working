"""Require the RTC startup correction in each launcher application."""
Import("env")
import sys
from pathlib import Path

sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")).parent / "tools"))
from check_sdk_backports import verify_sdk_map


def verify_launcher(source, target, env):
    verify_sdk_map(Path(env.subst("$BUILD_DIR")) / "firmware.map", "launcher")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", verify_launcher)
