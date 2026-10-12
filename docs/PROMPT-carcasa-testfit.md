# Continuar la carcasa: resultados del test fit de la v3

Eres ingeniero de diseño mecánico para impresión 3D FDM y sigues un trabajo ya avanzado
en Autodesk Fusion, usando el MCP de Fusion (`fusion_mcp_read`, `fusion_mcp_execute`,
`fusion_mcp_update`). Imprimí la carcasa v3 completa (carcasa frontal, tapa trasera y
base cuna) y te traigo los resultados del test fit. Tu trabajo es convertir mis
observaciones en cambios de parámetros, verificar y reexportar.

## Lee primero (no reinventes)

- `carcasa/README.md`: estado v3, cambios v1→v2→v3, decisiones con su porqué, tabla de
  impresión, montaje, plan de pruebas y pendientes. Es la fuente de verdad de la carcasa.
- `carcasa/fusion/helpers.py`: generador de cajas y cilindros paramétricos (`box`,
  `cyl`, `cyl_y`, `_dim`, `report`) con el que se construyó todo el modelo. Úsalo
  cargándolo con `exec(open(ruta).read(), g)` dentro de los scripts.
- `carcasa/fusion/alinear_cad.py`: recoloca los modelos CAD de los componentes según los
  parámetros actuales.
- `carcasa/fusion/stlcheck.py`: verifica STL exportados (cerrados, medidas, volumen).
  Córrelo con `python -I`.
- `docs/BOM-marco-fotos-esp32.md` y `docs/ESPECIFICACION-funcional.md` solo si hace falta
  contexto de la electrónica (§6 orientación, §7 sensor, §8 touch, §9 LED).

## Estado del modelo (lo aprendido)

- **Documento único:** «Marco Ensamble» en el proyecto *Marco Fotos*. Es a la vez el
  ensamble: un componente por pieza (`Carcasa_frontal`, `Tapa_trasera`, `Base_cuna`,
  `Probeta_ajustes`) más `_Referencia_electronica`. El documento «Carcasa» (v2 vieja) no
  se toca.
- **Ejes:** origen en la esquina exterior inferior-izquierda-frontal; X a lo ancho,
  **Y hacia atrás (cara frontal en Y = 0)**, Z hacia arriba. El marco se modela
  vertical; sobre la mesa todo el conjunto va girado 15° con la base plana.
- **Parámetros:** ~150 *User Parameters*, cada uno con su origen en el comentario:
  MEDIDO / CONFIRMADO / CAD / PENDIENTE / PROPUESTO / CALC. Toda cota depende de ellos:
  **cambia valores, no geometría.** Las expresiones admiten `max(a; b)`, con punto y
  coma.
- **Dummies paramétricos** en `_Referencia_electronica` (`Ref_Display`, `Ref_ESP32`,
  `Ref_Cable_USB`, `Ref_Lector_SD`, `Ref_TSL2561`, `Ref_Touch_pad`, `Ref_LED`). Siguen
  los parámetros.
- **Modelos CAD reales** de los 5 componentes (documentos .f3d del proyecto), insertados
  como X-ref en `_Referencia_electronica > _Modelos_CAD`. El ESP32 va dentro del
  contenedor `Pos_ESP32`, porque su X-ref directo vuelve a la identidad al capturar
  posición. **No siguen los parámetros**: recolócalos con `alinear_cad.py` después de
  cada cambio.

## Trampas de Fusion ya conocidas

- **`design.analyzeInterference` no es confiable con cuerpos de X-ref**: una vez dijo
  «sin choques» cuando sí había. Verifica los CAD con
  `TemporaryBRepManager.booleanOperation(..., IntersectionBooleanType)` cuerpo por
  cuerpo, con un filtro previo de cajas envolventes, y con
  `measureManager.measureMinimumDistance`.
- **Exportar una ocurrencia oculta escribe un archivo vacío y aun así devuelve True.**
  Antes de exportar prende `isLightBulbOn`, regresa la visibilidad como estaba y
  comprueba la hora y el tamaño de cada archivo.
- **Bocetos con líneas exactamente alineadas:** Fusion rechaza una segunda cota
  («over constrained») sin mostrar la restricción implícita. Atrápalo y amarra el
  segmento con `addHorizontal`/`addVertical`; luego confirma `isFullyConstrained`.
- **Un script que falla se revierte completo**: no deja basura, pero tampoco aplica
  nada.
- **Capturas de posición:** después de mover ocurrencias llama
  `design.snapshots.add()`. Una captura que se queda sin componentes deja un aviso en la
  línea de tiempo; bórrala.
- **Cotas en cero:** una cota de distancia de 0 falla. `helpers._dim` ya la convierte en
  restricción de alineación.

## Preferencias y decisiones cerradas (no se reabren)

- **Impresión:** PETG negro, Ender 3 V3 KE (cama 220×220), boquilla 0.4, capa 0.16,
  **cero soporte**.
- **Pernos impresos:** 0.5 mm más chicos de diámetro que su barreno
  (`holgura_perno`).
- **Sin insertos termofijos:** 2 M3×8 roscan directo en postes de Ø2.7 (confirmado),
  más un gancho arriba.
- **Uso y cable:** solo sobremesa (base cuna a 15° con llave asimétrica; el marco no
  debe poder girarse); cable USB-C recto, que sale por una muesca en el canto inferior
  trasero.
- **LED** mirando al frente, en una barbilla bajo la pantalla, por un agujero de Ø3.
  **Sensor TSL2561** en el canto superior, por un agujero de Ø2 abocinado en pared de
  1 mm. **Touch** en el canto superior; su posición y su grosor de pared se corrigen en
  esta v4 (ver abajo).
- **Headers:** soldados en todos los módulos; el UTP se suelda a la punta de los pines.
- **Display:** se sujeta por el PCB, nunca por el vidrio. Los postes superiores de la
  tapa llevan pernos en los barrenos del display.

## El touch ya está en el firmware, y manda sobre la carcasa

Desde el merge `34715d2` el touch vive en `firmware/src/toque.h`: la base persigue el
reposo y el umbral es un delta, `TOQUE_DELTA`, hoy provisional en 15. Lee la sección
«Touch capacitivo» del BOM y §8 de la especificación. Dos reglas de ahí que la v3
incumple:

- **Nada de masa detrás del pad:** ni el PCB del display, ni el devkit, ni planos de
  tierra. En la v3 la bolsa (`bolsa_touch` en `Carcasa_frontal`, parámetros `touch_*`)
  quedó a ~2 mm del borde superior del PCB del display y casi encima de su header de
  9 pines (`Display_header`, `Display_pines`). **Muévela:**
  - A la derecha, lejos del header del display (X > ~45 mm con la envolvente actual).
  - Hacia atrás, lejos del PCB del display (Y 5.45–6.65).
  - Sin invadir la ranura del gancho (`ranura_gancho`, Y ≳ 27.6) ni el poste superior
    derecho del display.
  - Achícala a ~15 × 15 mm: el BOM recomienda un pad de 12–15 mm. Comprueba la
    distancia del pad a todo lo que sea masa: PCB del display, header, ESP32 y lector
    SD.
- **El grosor de pared del pad (`pared_touch`) no es libre:** lo fija la tecla `t` de
  `[env:display]` del banco, a través de cupones de PETG impresos con **los mismos
  perímetros y relleno** que la carcasa (la tabla «El umbral y el grosor de pared» del
  BOM). Propón agregar a `Probeta_ajustes` tres cupones de 1.5 / 2.0 / 2.5 mm con la
  misma bolsa del pad, y pregúntame si ya hay números en esa tabla antes de fijar
  `pared_touch`.

## Qué hacer

1. **Pide mis observaciones del test fit** en una sola ronda. Usa una lista de revisión
   que ligue cada síntoma con el parámetro que lo corrige. Por ejemplo:

   | Síntoma | Parámetro |
   |---|---|
   | Ventana corrida respecto al área iluminada | `disp_vidrio_borde_sup`, `disp_panel_*`, `ventana_margen` |
   | Display flojo o apretado | `disp_apriete`, `disp_pcb_t`, `disp_panel_t`, `holgura_vidrio` |
   | Pernos que no entran o bailan | `holgura_perno` |
   | Tapa que no cierra o queda floja | `holgura_ajuste`, `gancho_*`, `repisa_*` |
   | Tornillos | `rosca_directa_d` |
   | LED | `comp_agujero`, `led_*` |
   | Cable USB-C | `usb_sobresale`, `usbp_*`, `muesca_usb_*` |
   | Base | `pata_angulo`, `base_*` |
   | Lector SD | `sd_*`, `holgura_perno` |
   | Sensor y touch | `sensor_*`, `touch_*` |

   Pregunta también por los pendientes del README:
   - ¿El USB-C del ESP32 sobresale, queda al ras o queda **hundido**? El CAD lo dibuja
     hundido 0.4 mm.
   - Medidas reales del header del display (plástico 2.5 / pin 6 supuestos) y del
     conector del cable (12 × 6.5 × 22 supuestos).
   - Si ya mediste la atenuación del sensor con la probeta.
   - Si ya corriste la tecla `t` (delta del touch a través de PETG).
2. **Aplica los cambios** como parámetros, en pasos pequeños. Incluye mover y achicar
   la bolsa del touch, y los cupones del touch en la probeta. Después de cada etapa,
   lee el estado y verifica (medidas, línea de tiempo sana).
3. **Recoloca los CAD** con `alinear_cad.py`.
4. **Verifica:** choques CAD contra piezas con booleanos, dummies contra piezas,
   holguras entre módulos (≥ 0.5 mm) y carcasa contra tapa. Reporta cualquier choque,
   diciendo si es intencional (el apriete del display y los pernos dentro de barrenos lo
   son).
5. **Guarda el documento** (autorizado). **Exporta** STL (solo STL, sin 3MF) de las piezas que
   cambiaron, con la visibilidad prendida, y verifica los STL con `stlcheck.py`.
6. **Actualiza `carcasa/README.md`** con una sección «Cambios v3 → v4»: qué se midió,
   qué cambió y qué piezas hay que reimprimir.
7. **Si el ajuste ya es bueno**, calcula del modelo los **largos del arnés de UTP**:
   distancia de cada pin del ESP32 a cada pin del display, el lector SD, el TSL2561, el
   LED y el pad, más margen para doblar. Entrégalo como tabla.
8. **No hagas commit** sin preguntarme.
