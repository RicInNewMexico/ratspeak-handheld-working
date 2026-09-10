"""Replace the selected SDK source before PlatformIO constructs its object graph."""
from pathlib import Path
import importlib.util

Import("env")
root = Path(env.subst("$PROJECT_DIR"))
spec = importlib.util.spec_from_file_location("wifi_backport", root / "tools/patch_wifi_sockets.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32")).resolve()
sources = {name: framework / "libraries/WiFi/src" / name for name in patch.SOURCE_FILES}
outputs = {name: Path(env.subst("$BUILD_DIR")).resolve() / "sdk-backports" / name for name in sources}
for name in sources:
    patch.write_patched(sources[name], outputs[name])
selected = set()


def replace_wifi(build_env, node):
    actual = Path(node.srcnode().get_abspath()).resolve()
    name = actual.name
    if name not in sources:
        return node
    if actual != sources[name]:
        raise ValueError(f"ambiguous {name} source: {actual}; expected {sources[name]}")
    if name in selected:
        raise ValueError(f"selected SDK {name} must be compiled exactly once")
    selected.add(name)
    return build_env.Object(target=str(outputs[name]) + ".o", source=str(outputs[name]))


def require_selection(source, target, env):
    if selected != set(sources):
        raise ValueError("WiFi socket FD cleanup sources were not selected exactly once")


env.AddBuildMiddleware(replace_wifi)
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", require_selection)
