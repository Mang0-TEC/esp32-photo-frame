"""Empaqueta web/index.html gzipeado a src/pagina.h, para servirlo desde PROGMEM.

Corre como extra_script `pre:` de PlatformIO, o sea antes de compilar, en cada
`pio run`. Por eso src/pagina.h va al .gitignore: el repo lleva ESTE archivo, que
es lo que hace falta para flashear, y no los ~183 KB de fuente de C que cambiarían
enteros al tocar una línea de la página.

Toma UNICAMENTE web/index.html. web/test/ es el arnes de regresion y no puede
acabar en el binario.
"""

import gzip
import re
import sys
from pathlib import Path

Import("env")  # noqa: F821  — lo inyecta SCons

RAIZ = Path(env.subst("$PROJECT_DIR")).parent  # noqa: F821
ORIGEN = RAIZ / "web" / "index.html"
DESTINO = Path(env.subst("$PROJECT_DIR")) / "src" / "pagina.h"  # noqa: F821

if not ORIGEN.exists():
    sys.exit("[empaquetar] no existe %s" % ORIGEN)

html = ORIGEN.read_text(encoding="utf-8")

# El regalo no lleva panel de diagnostico ni override de CFG.BASE por el
# fragmento de la URL, y las dos cosas cuelgan de esta bandera. Que el build falle
# es la unica forma de que un `true` de depuracion no viaje a la flash sin que
# nadie se entere: la pagina se ve igual de bien con el panel puesto.
diag = re.search(r"^\s*DIAG:\s*(\w+)", html, re.MULTILINE)
if not diag:
    sys.exit("[empaquetar] no encuentro CFG.DIAG en web/index.html")
if diag.group(1) != "false":
    sys.exit(
        "[empaquetar] CFG.DIAG = %s. Tiene que ser false en lo que se empaqueta: "
        "el panel de diagnostico y el override por fragmento no van al regalo."
        % diag.group(1)
    )

# mtime=0 para que la salida sea determinista: con el mtime por defecto, dos
# builds del mismo HTML dan bytes distintos y pagina.h se reescribiria siempre.
crudo = html.encode("utf-8")
comprimido = gzip.compress(crudo, compresslevel=9, mtime=0)

lineas = [
    "// GENERADO por tools/empaquetar_pagina.py — NO editar, no versionar.",
    "// Fuente: web/index.html (%d B en claro, %d B gzip)."
    % (len(crudo), len(comprimido)),
    "#pragma once",
    "#include <Arduino.h>",
    "",
    "// El `const` NO es decoracion y PROGMEM sí es un no-op en el ESP32: lo que",
    "// manda este array a .rodata en flash es que sea const. Sin él, los ~31 KB",
    "// aterrizan en DRAM y se comen media RAM libre sin que nada avise.",
    "static const uint8_t PAGINA_GZ[] PROGMEM = {",
]
for i in range(0, len(comprimido), 16):
    trozo = comprimido[i : i + 16]
    lineas.append("    " + "".join("0x%02x," % b for b in trozo))
lineas.append("};")
lineas.append("static const size_t PAGINA_GZ_LEN = sizeof(PAGINA_GZ);")
lineas.append("")
salida = "\n".join(lineas)

# Reescribir solo si cambio, o cada `pio run` recompila todo lo que incluya esto.
anterior = DESTINO.read_text(encoding="utf-8") if DESTINO.exists() else None
if anterior != salida:
    DESTINO.parent.mkdir(parents=True, exist_ok=True)
    DESTINO.write_text(salida, encoding="utf-8")
    print(
        "[empaquetar] pagina.h regenerado: %d B en claro -> %d B gzip"
        % (len(crudo), len(comprimido))
    )
else:
    print("[empaquetar] pagina.h sin cambios (%d B gzip)" % len(comprimido))
