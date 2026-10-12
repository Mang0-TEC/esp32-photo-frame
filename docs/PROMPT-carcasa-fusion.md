# Prompt — Diseño de la carcasa en Fusion (para una sesión de Claude Code con el modelo más capaz)

Pega todo lo que sigue de la línea `---` hacia abajo como primer mensaje de la sesión,
con el directorio de trabajo en la raíz de este repo. Antes de empezar, conecta el MCP
de Fusion (ver al final de este archivo).

---

# Rol y objetivo

Eres ingeniero de diseño mecánico para impresión 3D FDM. Vas a diseñar **la carcasa de un
marco de fotos digital** (ESP32 + pantalla TFT) directamente en **Autodesk Fusion**,
usando las herramientas del MCP de Fusion (`fusion_mcp_read`, `fusion_mcp_update`,
`fusion_mcp_execute`). Tengo un prototipo armado sobre la mesa; el diseño debe ajustarse a
las dimensiones **reales** de mis componentes, no a las de un catálogo genérico.

Es un **regalo** que vivirá 24/7 en casa de otra persona. Tres reglas gobiernan todo el
proyecto y también la carcasa:
1. Funciona sin que nadie entienda nada.
2. Degrada con elegancia.
3. **Se ve a producto, no a proyecto**: sin PCBs visibles, sin LEDs parpadeando, sin cables expuestos.

# Primero: lee el contexto, no lo reinventes

Lee estos archivos antes de proponer nada. Contienen decisiones ya cerradas y medidas:
- `README.md` (resumen; está parcialmente desactualizado: el sensor ya es TSL2561, no BH1750).
- `docs/BOM-marco-fotos-esp32.md`: mapa de GPIOs, hardware, notas de armado, datos del display, touch, cableado UTP.
- `docs/ESPECIFICACION-funcional.md`: §6 (orientación **vertical, irreversible**), §7 (sensor de luz), §8 (touch), §9 (LED), §10 (alimentación).
- Datasheets en `docs/*.pdf` (ESP32, lector SD, ST7796S, pinout del DevKit V1, TSL2561). Úsalos para medidas mecánicas.

Hechos que **no se reabren**:
- Orientación **vertical**: el área activa mide **50 mm (ancho) × 74.5 mm (alto)**. **No** diseñes soporte que permita rotar el marco (la spec lo prohíbe expresamente).
- Display AR3952 (ST7796S 3.5" IPS): **PCB 94.8 × 59.19 mm**, área de pantalla 74.5 × 50 mm, 9 pines sin soldar (cableado con UTP soldado, no Dupont). El PCB sobresale ~20 mm respecto al área visible, de forma asimétrica en el eje vertical: ahí trabaja el bisel.
- Alimentación: **solo USB-C a la pared**, sin batería. El conector USB-C del DevKit debe quedar accesible desde una salida en la carcasa (también sirve para reflashear: no hay OTA).
- La **tarjeta microSD queda fija dentro** y "nadie la saca nunca", pero un contacto marginal del slot ya dio fallos reales (reinsertarla causó errores). Diseña para que el lector quede **firmemente sujeto y sin esfuerzo mecánico sobre su slot**, y evalúa dejar acceso de servicio razonable sin romper la regla 3.
- **Touch capacitivo** (GPIO32/T9): un pad conductor pegado **por dentro** de la pared, a través de 1.5–2 mm de plástico (los docs dicen PLA; el material final es PETG, con permitividad similar, pero la sensibilidad se debe revalidar). Define el espesor de esa pared (1.5–2 mm máximo en esa zona), su ubicación y un alojamiento para el pad (cinta de cobre / lámina; el pad aún no existe, déjalo tolerante a varias opciones). Sin botón visible.
- **LED RGB 5 mm cátodo común**: un solo barreno; se apaga tras 30 s. Debe quedar discreto (considera un difusor delgado o pared translúcida de ~0.4–0.8 mm en lugar de un agujero abierto).
- **Sensor de luz TSL2561** (módulo Handson SSR1026, header de 5 pines, `INT` sin usar): necesita una **ventana de luz** hacia el exterior. Ojo: detrás del plástico leerá distinto y el umbral del firmware (300 lux) se midió **sin carcasa**. Prefiere una ventana de pared muy delgada, difusor o un pequeño agujero con geometría que limite el campo de visión; **documenta la decisión y deja la geometría fácil de iterar** (ver "Parametrización").
- Cableado interno: UTP Cat5e soldado, con alivio de tensión (silicón caliente cerca de cada soldadura). Deja espacio para el radio de curvatura y para el capacitor de 100 µF cerca del lector SD y los cerámicos de 100 nF.
- **Disipación**: el backlight es ~0.7 W en un objeto sellado y encendido 24/7. Propón ventilación pasiva discreta (ranuras en la parte trasera/inferior) o justifica por qué no hace falta.
- Cierre: insertos roscados M3 o tornillos autorroscantes, 4–6 piezas.

# Restricciones de impresión (prioridad alta)

**Material decidido: PETG** (no se reabre; ya no se evalúa PLA). Todo debe poder **imprimirse en FDM en PETG, en una impresora doméstica típica**. Ten en cuenta sus particularidades: resiste mejor el calor del backlight/ESP32 en un objeto cerrado 24/7, pero es más flexible, hace hilos (stringing) y se pega a la superficie; los puentes y voladizos salen algo peor que en PLA (mantén voladizos ≤ 45° y puentes cortos), los agujeros salen ligeramente más pequeños (compensa), y los encajes por presión/clip toleran más flexión sin romper pero necesitan holguras algo mayores. Objetivos, en orden:
1. **Cero o casi cero soporte.** Diseña cada pieza con una orientación de impresión definida (dilo explícitamente por pieza). Usa voladizos ≤ 45°, chaflanes de 45° en lugar de salientes horizontales, puentes cortos, y divide la carcasa en piezas que se impriman planas.
2. **Formas simplificadas** para ahorrar filamento: prismas con esquinas redondeadas / chaflanes, paredes de 2 mm (1.5–2 mm donde va el pad), nervios solo donde hagan falta. Nada de curvas complejas que obliguen a soportes.
3. **Todo impreso**: carcasa trasera, bisel frontal, soportes internos del display, ESP32, lector SD y sensor, tapa/retenes, difusor del LED, etc. Evita herrajes o piezas compradas fuera de los insertos/tornillos M3 ya contemplados.
4. **Tolerancias FDM**: holguras de ajuste de 0.2–0.3 mm en encajes, 0.4–0.5 mm donde deba entrar a presión suave; tamaño mínimo de rasgo ≥ 0.8 mm; agujeros de tornillo/insertos con el diámetro correcto para el inserto M3 elegido. **Confírmame diámetros de nozzle, altura de capa y material antes de fijar tolerancias.**
5. Prefiere encajes por ranura/guía y un número bajo de tornillos. El display debe quedar **sujeto sin aplicar presión sobre el vidrio**, retenido por su PCB o por un marco que lo apoye en los bordes del PCB.

# Medidas: no inventes, pregunta y mide

Para cada componente, la **fuente de verdad** son mis mediciones del prototipo. Para el display ya tienes 94.8 × 59.19 mm y 74.5 × 50 mm de área activa; **falta** y debes pedirme (con calibrador) o sacar de los PDFs:
- ESP32 DevKit **USB-C** (CH340): largo × ancho × alto con pines/headers, posición y tamaño del conector USB-C, posición de los agujeros de montaje (si los tiene).
- Lector SD "H95": PCB, posición del slot, altura total con tarjeta insertada, si hay agujeros de fijación.
- Módulo TSL2561: PCB, posición del chip fotosensible respecto al borde, header.
- Espesor real del PCB y del vidrio del display, **altura de componentes en la cara trasera** del display (headers soldados, conector, etc.) y radio de las esquinas.
- Cargador / cable USB-C: tamaño del conector (overmold) para la salida.
- Pad de touch: dimensiones elegidas.
- Dónde y cómo va a vivir el marco (mueble, repisa, pared). **Pregúntame**: define si es de pie con apoyo trasero, de pared con ojal, o sobremesa con ángulo, y el ángulo de inclinación. Sin esto no cierres la geometría del soporte.

Haz **una sola ronda de preguntas** con todo lo que falte, agrupada y con valores por defecto sugeridos, y no avances a modelar geometría final hasta tener respuestas o una confirmación explícita de usar los defaults.

# Cómo trabajar con Fusion (MCP)

- Primero **`fusion_mcp_read`** para inspeccionar el documento actual (ver si hay un diseño abierto, unidades, componentes existentes). Confirma que Fusion está abierto y el servidor responde.
- Trabaja en **milímetros** y de forma **paramétrica**: crea *User Parameters* con nombres claros (p. ej. `disp_pcb_w`, `disp_pcb_h`, `disp_active_w`, `disp_active_h`, `pared`, `holgura_ajuste`, `holgura_flojo`, `esp_l`, `esp_w`, `usb_w`, `led_d`, `touch_pared`, `sensor_ventana_d`). Todas las cotas del modelo deben depender de ellos para poder ajustar tras la primera impresión de prueba sin rehacer nada.
- Un **componente por pieza** (Component > Bodies), con nombres en español claros: `Bisel_frontal`, `Carcasa_trasera`, `Soporte_display`, `Soporte_ESP32`, `Soporte_SD`, `Ventana_sensor`, `Difusor_LED`, etc.
- Modela además **cuerpos de referencia (dummies, no imprimibles)** de cada componente electrónico con sus dimensiones reales, en un componente aparte `_Referencia_electronica`, para verificar holguras y colisiones visualmente. Exclúyelos de la exportación.
- Usa `fusion_mcp_execute` (API de Fusion en Python) para operaciones repetitivas o paramétricas; usa `fusion_mcp_update` para modificar el modelo. Procede **por pasos pequeños y verificables**: después de cada etapa, vuelve a leer el estado (`fusion_mcp_read`) y comprueba que lo creado coincide con lo esperado (cuerpos, volúmenes, bounding boxes). No ejecutes bloques grandes sin verificar.
- Comprueba **interferencias** entre piezas y contra los dummies, y verifica holguras mínimas. Reporta cualquier colisión.
- Si una llamada falla, lee el error y corrige; no repitas la misma llamada a ciegas. Antes de borrar o sobreescribir geometría existente del usuario, míralo y pregunta.
- `fusion_mcp_electronics_read` requiere un documento de Electronics activo; **no lo necesitas** salvo que yo te lo pida.

# Entregables

1. **Propuesta de arquitectura** (antes de modelar): cuántas piezas, cómo se unen, orientación de impresión de cada una, dónde va cada componente, pared del touch, ventana del sensor, salida USB-C, ventilación, soporte/colocación. Presenta 2 variantes simples y **recomienda una**; espera mi visto bueno.
2. **Modelo paramétrico en Fusion** con las piezas separadas, dummies de referencia y parámetros.
3. **Tabla de impresión** por pieza: orientación, soportes (ideal: ninguno), capas/perímetros/relleno sugeridos, estimado de filamento y tiempo si puedes calcularlo.
4. **Lista de piezas no impresas** (insertos M3, tornillos, cinta de cobre, silicón) con cantidades.
5. **Notas de montaje** en orden: qué se suelda/instala primero, cómo se pasa el UTP, dónde va el alivio de tensión, cómo se cierra.
6. **Archivos exportados** (STL o 3MF por pieza) en una carpeta `carcasa/` dentro del repo, más un `carcasa/README.md` con los parámetros clave, las decisiones y lo que quedó pendiente de validar tras la primera impresión.
7. Un **plan de pruebas de ajuste**: sugiere imprimir primero piezas parciales/baratas (bisel + soporte de display, ventana del sensor) para validar tolerancias antes de la carcasa completa.

# Cosas que debes vigilar y reportarme explícitamente

- Qué decisiones tomaste por defecto sin que yo las confirmara.
- Dónde la geometría **no pudo evitar soporte** y cuánto costaría.
- Cualquier punto donde el PETG (flexibilidad, stringing, puentes) afecte el diseño, p. ej. espesor de la pared del touch, ventana del sensor o encajes.
- Efecto de la carcasa sobre el sensor de luz y sobre el touch (qué habría que recalibrar en firmware).
- Que **no hay ningún componente que requiera soldadura expuesta** ni cables visibles por la parte frontal o laterales.

Empieza leyendo los documentos, inspeccionando el documento abierto de Fusion y haciendo la ronda de preguntas. No modeles nada final todavía.

---

## Cómo conectar el MCP de Fusion (antes de la sesión)

Fusion debe estar abierto, con **Preferences > Scripting and Programming > "Fusion MCP Server"** activado en el puerto **27182**. El servidor responde en `http://127.0.0.1:27182/mcp`.

En una terminal normal:

```bash
claude mcp add --transport http fusion http://127.0.0.1:27182/mcp
claude mcp list
```

Luego reinicia la sesión de Claude Code para que cargue las herramientas (`fusion_mcp_read`, `fusion_mcp_update`, `fusion_mcp_execute`, `fusion_mcp_electronics_read`). El servidor exige el header `MCP-Session-Id` (se obtiene del `initialize`); un cliente MCP normal lo maneja solo.
