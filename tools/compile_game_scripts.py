from pathlib import Path
import shlex

from SCons.Script import Import

Import("env")

PROJECT_DIR = Path(env["PROJECT_DIR"])
ANIMATIONS_DIR = PROJECT_DIR / "animations"
OUT_DIR = PROJECT_DIR / "src" / "generated"
OUT_FILE = OUT_DIR / "GameScripts.generated.h"

MODE_MAP = {
    "text": "DISPLAY_MODE_TEXT",
    "scroll_up": "DISPLAY_MODE_SCROLL_UP",
    "scrollup": "DISPLAY_MODE_SCROLL_UP",
    "scroll_down": "DISPLAY_MODE_SCROLL_DOWN",
    "scrolldown": "DISPLAY_MODE_SCROLL_DOWN",
}

RENDER_STYLE_MAP = {
    "point": "ParticleModeConfig::RENDER_POINT",
    "square": "ParticleModeConfig::RENDER_SQUARE",
    "circle": "ParticleModeConfig::RENDER_CIRCLE",
    "text": "ParticleModeConfig::RENDER_TEXT",
    "glow": "ParticleModeConfig::RENDER_GLOW",
}

PARTICLE_FIELD_TYPES = {
    "count": "int",
    "renderMs": "int",
    "substepMs": "int",
    "gravityScale": "float",
    "elasticity": "float",
    "wallElasticity": "float",
    "damping": "float",
    "radius": "float",
    "temperature": "float",
    "attractStrength": "float",
    "attractRange": "float",
    "gravityEnabled": "bool",
    "collisionEnabled": "bool",
    "springStrength": "float",
    "springRange": "float",
    "springEnabled": "bool",
    "coulombStrength": "float",
    "coulombRange": "float",
    "coulombEnabled": "bool",
    "scaffoldStrength": "float",
    "scaffoldRange": "float",
    "scaffoldEnabled": "bool",
    "glowSigma": "float",
    "glowWavelength": "float",
    "speedColor": "bool",
    "physicsPaused": "bool",
    "renderStyle": "renderStyle",
}


def _parse_bool(value: str) -> bool:
    v = value.strip().lower()
    if v in ("1", "true", "on", "yes", "enabled", "running_off"):
        return True
    if v in ("0", "false", "off", "no", "disabled"):
        return False
    raise ValueError(f"invalid bool: {value}")


def _parse_text_enabled(value: str) -> bool:
    try:
        return _parse_bool(value)
    except ValueError:
        # Legacy shorthand such as `text hello` is treated as `text on`.
        # The build-time format still does not carry a text payload per step.
        return True


def _cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def _cpp_string(value: str) -> str:
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def _parse_number(value: str):
    if any(ch in value for ch in ".eE"):
        return float(value)
    return int(value)


def _step_defaults():
    return {
        "waitMs": 0,
        "gotoStep": -1,
        "gotoRepeat": 0,
        "setMode": False,
        "mode": "DISPLAY_MODE_TEXT",
        "setTextEnabled": False,
        "textEnabled": True,
        "setParticlesEnabled": False,
        "particlesEnabled": False,
        "doTextToParticles": False,
        "doScreenToParticles": False,
        "setPhysicsPaused": False,
        "physicsPaused": False,
        "setParticleConfig": False,
        "replaceParticleConfig": False,
        "particleAssignments": {},
        "gotoLabel": None,
    }


def _parse_game_file(path: Path):
    script = {
        "id": None,
        "name": path.stem,
        "steps": [],
    }
    labels = {}
    current = None

    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue

        try:
            parts = shlex.split(stripped, comments=True, posix=True)
        except ValueError as exc:
            raise RuntimeError(f"{path.name}:{line_no}: {exc}")
        if not parts:
            continue

        cmd = parts[0].lower()

        if cmd == "id":
            script["id"] = int(parts[1])
            continue
        if cmd == "name":
            script["name"] = " ".join(parts[1:])
            continue

        if cmd == "step":
            if current is not None:
                script["steps"].append(current)
            current = _step_defaults()
            if len(parts) >= 2:
                label = parts[1]
                if label in labels:
                    raise RuntimeError(f"{path.name}:{line_no}: duplicate label '{label}'")
                labels[label] = len(script["steps"])
            continue

        if cmd == "end":
            if current is None:
                raise RuntimeError(f"{path.name}:{line_no}: 'end' outside step")
            script["steps"].append(current)
            current = None
            continue

        if current is None:
            raise RuntimeError(f"{path.name}:{line_no}: command '{cmd}' outside step")

        if cmd == "wait":
            current["waitMs"] = int(parts[1])
        elif cmd == "mode":
            mode_name = parts[1].lower()
            if mode_name not in MODE_MAP:
                raise RuntimeError(f"{path.name}:{line_no}: unknown mode '{parts[1]}'")
            current["setMode"] = True
            current["mode"] = MODE_MAP[mode_name]
        elif cmd == "text":
            current["setTextEnabled"] = True
            current["textEnabled"] = _parse_text_enabled(parts[1])
        elif cmd == "particles":
            current["setParticlesEnabled"] = True
            current["particlesEnabled"] = _parse_bool(parts[1])
        elif cmd == "text_to_particles":
            current["doTextToParticles"] = True
        elif cmd == "screen_to_particles":
            current["doScreenToParticles"] = True
        elif cmd == "physics":
            state = parts[1].lower()
            current["setPhysicsPaused"] = True
            if state in ("paused", "pause"):
                current["physicsPaused"] = True
            elif state in ("running", "run"):
                current["physicsPaused"] = False
            else:
                current["physicsPaused"] = _parse_bool(parts[1])
        elif cmd == "replace_particle_config":
            current["replaceParticleConfig"] = _parse_bool(parts[1])
        elif cmd == "particle":
            field = parts[1]
            if field not in PARTICLE_FIELD_TYPES:
                raise RuntimeError(f"{path.name}:{line_no}: unknown particle field '{field}'")
            current["setParticleConfig"] = True
            ftype = PARTICLE_FIELD_TYPES[field]
            value_token = parts[2]
            if ftype == "bool":
                value = _parse_bool(value_token)
            elif ftype == "float":
                value = float(value_token)
            elif ftype == "int":
                value = int(value_token)
            elif ftype == "renderStyle":
                style_name = value_token.lower()
                if style_name not in RENDER_STYLE_MAP:
                    raise RuntimeError(f"{path.name}:{line_no}: unknown render style '{value_token}'")
                value = RENDER_STYLE_MAP[style_name]
            else:
                raise RuntimeError(f"{path.name}:{line_no}: unsupported particle field type '{ftype}'")
            current["particleAssignments"][field] = value
        elif cmd == "goto":
            if len(parts) < 2:
                raise RuntimeError(f"{path.name}:{line_no}: goto needs target label")
            current["gotoLabel"] = parts[1]
            current["gotoRepeat"] = -1
            if len(parts) >= 3:
                if parts[2].lower() == "forever":
                    current["gotoRepeat"] = -1
                elif parts[2].lower() == "repeat":
                    if len(parts) < 4:
                        raise RuntimeError(f"{path.name}:{line_no}: goto repeat needs a count")
                    current["gotoRepeat"] = int(parts[3])
                else:
                    raise RuntimeError(f"{path.name}:{line_no}: expected 'repeat N' or 'forever' after goto")
        else:
            raise RuntimeError(f"{path.name}:{line_no}: unknown command '{cmd}'")

    if current is not None:
        script["steps"].append(current)

    if script["id"] is None:
        raise RuntimeError(f"{path.name}: missing 'id' declaration")
    if not script["steps"]:
        raise RuntimeError(f"{path.name}: no steps declared")

    for step in script["steps"]:
        if step["gotoLabel"] is not None:
            if step["gotoLabel"] not in labels:
                raise RuntimeError(f"{path.name}: unknown goto label '{step['gotoLabel']}'")
            step["gotoStep"] = labels[step["gotoLabel"]]

    return script


def _emit_value(value):
    if isinstance(value, bool):
        return _cpp_bool(value)
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return f"{value:.6f}f"
    if isinstance(value, str) and value.startswith("ParticleModeConfig::"):
        return value
    if isinstance(value, str) and value.startswith("DISPLAY_MODE_"):
        return value
    return _cpp_string(str(value))


def _generate_header(scripts):
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "Animations.h"')
    lines.append("")
    lines.append("namespace GeneratedGameScripts {")
    lines.append("")
    lines.append("inline const AnimationScript* findGeneratedAnimationScript(uint8_t id) {")
    lines.append("    static bool init = false;")
    lines.append(f"    static AnimationScript scripts[{len(scripts)}];")
    for idx, script in enumerate(scripts):
        lines.append(f"    static AnimationStep steps_{idx}[{len(script['steps'])}];")
    lines.append("")
    lines.append("    if (!init) {")
    for idx, script in enumerate(scripts):
        for step_index, step in enumerate(script["steps"]):
            prefix = f"        steps_{idx}[{step_index}]"
            lines.append(f"{prefix}.waitMs = {step['waitMs']}ul;")
            lines.append(f"{prefix}.gotoStep = {step['gotoStep']};")
            lines.append(f"{prefix}.gotoRepeat = {step['gotoRepeat']};")
            if step["setMode"]:
                lines.append(f"{prefix}.setMode = true;")
                lines.append(f"{prefix}.mode = {step['mode']};")
            if step["setTextEnabled"]:
                lines.append(f"{prefix}.setTextEnabled = true;")
                lines.append(f"{prefix}.textEnabled = {_cpp_bool(step['textEnabled'])};")
            if step["setParticlesEnabled"]:
                lines.append(f"{prefix}.setParticlesEnabled = true;")
                lines.append(f"{prefix}.particlesEnabled = {_cpp_bool(step['particlesEnabled'])};")
            if step["doTextToParticles"]:
                lines.append(f"{prefix}.doTextToParticles = true;")
            if step["doScreenToParticles"]:
                lines.append(f"{prefix}.doScreenToParticles = true;")
            if step["setPhysicsPaused"]:
                lines.append(f"{prefix}.setPhysicsPaused = true;")
                lines.append(f"{prefix}.physicsPaused = {_cpp_bool(step['physicsPaused'])};")
            if step["setParticleConfig"]:
                lines.append(f"{prefix}.setParticleConfig = true;")
                if step["replaceParticleConfig"]:
                    lines.append(f"{prefix}.replaceParticleConfig = true;")
                for field, value in step["particleAssignments"].items():
                    lines.append(f"{prefix}.particleConfig.{field} = {_emit_value(value)};")
        lines.append(f"        scripts[{idx}].id = {script['id']};")
        lines.append(f"        scripts[{idx}].name = {_cpp_string(script['name'])};")
        lines.append(f"        scripts[{idx}].steps = steps_{idx};")
        lines.append(f"        scripts[{idx}].stepCount = {len(script['steps'])};")
        lines.append("")
    lines.append("        init = true;")
    lines.append("    }")
    lines.append("")
    lines.append(f"    for (uint8_t i = 0; i < {len(scripts)}; ++i) {{")
    lines.append("        if (scripts[i].id == id) return &scripts[i];")
    lines.append("    }")
    lines.append("    return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace GeneratedGameScripts")
    lines.append("")
    return "\n".join(lines)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    scripts = []
    seen_ids = set()
    script_paths = sorted(list(ANIMATIONS_DIR.glob("*.anim")) + list(ANIMATIONS_DIR.glob("*.game")))
    for path in script_paths:
        script = _parse_game_file(path)
        if script["id"] in seen_ids:
            raise RuntimeError(f"duplicate script id {script['id']} in {path.name}")
        seen_ids.add(script["id"])
        scripts.append(script)

    if not scripts:
        raise RuntimeError("no .anim or .game scripts found in animations/")

    OUT_FILE.write_text(_generate_header(scripts) + "\n")
    print(f"[anim-scripts] Compiled {len(scripts)} scripts -> {OUT_FILE.relative_to(PROJECT_DIR)}")


main()
