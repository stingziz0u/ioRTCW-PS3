#!/bin/bash
#
# assemble_shaders.sh -- regenerate ../ps3gl_shader_data.h without a Cg
# compiler (NVIDIA's cgc/libCg is not available for Linux x86_64 anymore).
#
#   asm/q3_vp.vpa          vertex program (NV40 assembly, with fog)
#   asm/q3_fp_*_fog.fpa    fragment programs with fog
#   q3_fp_*.fpo            fragment programs without fog (from the .fcg)
#
# The assembly is what `cgc -profile vp40/fp40` would print; `cgcomp -a`
# (PSL1GHT) turns it into .vpo/.fpo. Checked: the old q3_vp.vcg and
# q3_fp_modulate.fcg written this way assemble byte-identical to their .vpo/.fpo.
#
# Usage: cd code/gl/shaders && ./assemble_shaders.sh

set -e
CGCOMP="${PS3DEV:-/usr/local/ps3dev}/bin/cgcomp"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

"$CGCOMP" -v -a asm/q3_vp.vpa "$OUT/q3_vp.vpo"
for m in coloronly modulate replace decal add blend modulate2; do
    "$CGCOMP" -f -a asm/q3_fp_${m}_fog.fpa "$OUT/q3_fp_${m}_fog.fpo"
done

OUT="$OUT" python3 - <<'PYEOF'
import os
out = os.environ["OUT"]
files = [(os.path.join(out, "q3_vp.vpo"), "shader_vp_data")]
modes = ["coloronly", "modulate", "replace", "decal", "add", "blend", "modulate2"]
for m in modes:
    files.append((f"q3_fp_{m}.fpo", f"shader_fp_{m}_data"))
for m in modes:
    files.append((os.path.join(out, f"q3_fp_{m}_fog.fpo"), f"shader_fp_{m}_fog_data"))
lines = [
    "/* ps3gl_shader_data.h -- Auto-generated embedded shader binaries. Do not edit manually.",
    " * Regenerate with: cd shaders && ./assemble_shaders.sh */",
    "#ifndef PS3GL_SHADER_DATA_H",
    "#define PS3GL_SHADER_DATA_H",
    "",
    "#define PS3GL_SHADERS_AVAILABLE 1",
    "#define PS3GL_SHADERS_FOG 1",
    "",
]
for fname, var in files:
    data = open(fname, "rb").read()
    lines.append(f"static const unsigned char {var}[] __attribute__((aligned(16))) = {{")
    for i in range(0, len(data), 12):
        row = ", ".join(f"0x{b:02x}" for b in data[i:i+12])
        lines.append("  " + row + ("," if i + 12 < len(data) else ""))
    lines.append("};")
    lines.append(f"static const unsigned int {var}_size = {len(data)};")
    lines.append("")
lines.append("#endif /* PS3GL_SHADER_DATA_H */")
open("../ps3gl_shader_data.h", "w").write("\n".join(lines) + "\n")
print("wrote ../ps3gl_shader_data.h")
PYEOF
