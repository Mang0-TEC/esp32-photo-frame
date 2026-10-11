# Carcasa del marco — PETG negro, prototipo v3

Modelo paramétrico en Autodesk Fusion: documento **«Marco Ensamble»** del proyecto
*Marco Fotos* (un solo documento: cada pieza imprimible es un componente y
`_Referencia_electronica` guarda los dummies de la electrónica, que **no** se exportan).
El documento previo «Carcasa» (v2) quedó intacto como referencia.

Todas las cotas salen de *User Parameters*. Cada uno lleva en el comentario su origen:
**MEDIDO** (BOM o calibrador), **CONFIRMADO** (respuesta del usuario), **PENDIENTE**
(valor por defecto a sustituir por una medida), **PROPUESTO** (decisión de diseño) o
**CALC** (fórmula). Ajustar tras la primera impresión es cambiar valores, no rehacer
geometría.

**Ejes del modelo:** origen en la esquina exterior inferior-izquierda-frontal; X a lo
ancho, **Y hacia atrás (cara frontal en Y = 0)**, Z hacia arriba. El marco se modela
vertical; sobre la mesa todo el conjunto queda girado 15° con la base plana.

## Cambios v2 → v3 (10-oct-2026, con los modelos CAD de los componentes)

Los modelos 3D a escala de los cinco componentes están insertados en el ensamble
(`_Referencia_electronica > _Modelos_CAD`, ver más abajo). De ellos, y de dos
respuestas del usuario, salen estos cambios:

- **Display:** PCB de **1.2 mm** (no 1.6) y panel de 3.15 mm.
  - Con los valores viejos, los postes de la tapa quedaban 0.16 mm cortos (display
    flojo) y el vidrio a 0.05 mm del bisel.
  - Área activa **48.96 × 73.44 mm** (320 × 480 px a 0.153 mm); la ventana se ajusta.
  - Bordes del vidrio tomados del CAD (4.9 / 4.94 / 1.95 mm); el área activa queda a
    7.57 mm del borde superior.
- **Los 2 postes superiores de la tapa entran en los barrenos del display** (Ø3.0, a
  5.58 mm de los lados y 2.42 mm de arriba) con **pernos de ubicación**. El display
  queda alineado con la ventana por sus barrenos, no por fricción. La repisa superior de
  la carcasa se recortó alrededor de esos postes.
- **Pernos impresos 0.5 mm más chicos (diámetro) que su barreno** (`holgura_perno`,
  confirmado por el usuario): display Ø2.5, ESP32 Ø2.5, lector SD Ø1.61.
- **Los headers se quedan soldados** en todos los módulos (usuario). Los pines del ESP32
  miden 10.4 mm bajo el PCB, y eso hace **el marco 1.9 mm más profundo**. El header de
  9 pines del display está modelado (plástico, pines y salida del UTP hacia abajo).
- **ESP32:** 28.0 mm de ancho; soportes de Ø3.7 para librar los botones BOOT/EN.
- **Lector SD, cuna nueva** con el PCB real (47.52 × 29.84 mm, porta-tarjeta que
  sobresale 4.09 mm, tarjeta 6 mm fuera del porta-tarjeta, medido):
  - 2 soportes con pernos en sus 2 barrenos (extremo del header).
  - 2 apoyos por detrás en el extremo de la ranura.
  - El tope de la tarjeta sigue a 0.3 mm.
  - Se descartó un retén frente a la tarjeta: sería un voladizo de 5 mm imposible de
    imprimir sin soporte. Los pernos fijados con silicón bastan, porque el PCB no puede
    girar sin doblarse.
- TSL2561 1.5 mm a la derecha y gancho de la tapa 3 mm más angosto, para librar el
  poste nuevo.
- Envolvente: **63.8 × 106.2 × 33.8 mm**.

**Hay que reimprimir las tres piezas** (carcasa frontal, tapa y base: la base cambia
con la envolvente). La probeta no cambia.

## Cambios v1 → v2 (9-oct-2026, tras la primera prueba física)

Medido con la probeta y con la carcasa frontal v1 impresa:

- **Rosca directa M3 confirmada en Ø2.7**: agarra bien en los postes de la carcasa
  impresa. `rosca_directa_d` pasa a CONFIRMADO.
- **El vidrio del display queda a 5 mm del borde del PCB arriba y abajo, y a 2 mm a
  los lados.** La tira de arriba es la de los pines; el borde negro grueso del vidrio
  (su barbilla) va abajo. El modelo v1 suponía 1.5 mm arriba y todo el sobrante abajo.
  - Por eso **la ventana del bisel baja 4.6 mm**: el área activa empieza a 7.6 mm del
    borde superior del PCB, no a 3.
  - El bisel queda más parejo: unos 9 mm arriba y 21 mm abajo, contra ~5 / 26 en v1.
- **Soldaduras y cables del display pasan arriba**, a la tira de pines. Se reserva
  un volumen para doblar el UTP.
- **El TSL2561 gira 90°**: placa a lo largo de X, barrenos a la izquierda y header a la
  derecha. Así su placa pasa por detrás de los cables del display y su header libra el
  poste de la esquina. El agujero del sensor se movió a X = 16.5 mm.
- **Los postes de la tapa que sujetan el display bajan 1 mm** (`poste_disp_inset`
  = 5): presionan en las esquinas de la tira, fuera de los pines.
- **El lector SD baja 0.5 mm** (`sd_techo` = 8.5) para conservar los 0.3 mm de aire
  entre la tarjeta y su tope.

Hay que reimprimir **carcasa frontal y tapa**. La base y la probeta no cambian.

## Piezas y archivos

| Archivo | Pieza | Medidas (mm) | Volumen |
|---|---|---|---|
| `marco_carcasa_frontal.stl/.3mf` | Bisel + cuatro paredes, una pieza | 63.8 × 33.8 × 106.2 | 28.8 cm³ |
| `marco_tapa_trasera.stl/.3mf` | Tapa + postes del display + cunas ESP32/SD | 59.3 × 28.4 × 102.8 | 14.4 cm³ |
| `marco_base_cuna.stl/.3mf` | Cuna de sobremesa a 15° | 63.8 × 50.8 × 21.6 | 17.7 cm³ |
| `marco_probeta_ajustes.stl/.3mf` | Probeta de rosca, LED y ventana del sensor | 52 × 20 × 18 | 5.5 cm³ |

Los STL están exportados **en las coordenadas del ensamble**, no en la orientación de
impresión: hay que girarlos en el slicer según la tabla de abajo.

## Tabla de impresión (Ender 3 V3 KE, boquilla 0.4, capa 0.16, PETG negro)

| Pieza | Orientación en la cama | Soporte | Perímetros / sup-inf / relleno | Filamento* | Tiempo* |
|---|---|---|---|---|---|
| Carcasa frontal | **Cara frontal abajo** (girar −90° en X). Sobre PEI texturizado el frente sale mate, con aspecto de producto | **Ninguno** | 4 perímetros, 5 capas sup/inf, gyroid 15 % (casi todo es pared maciza de 2 mm) | ~34 g | ~3–3.5 h |
| Tapa trasera | **Cara exterior abajo** (girar +90° en X); postes y ganchos hacia arriba | **Ninguno** | 4 / 5 / 15 % | ~17 g | ~1.5–2 h |
| Base cuna | **Cara de apoyo abajo**: en el slicer, «colocar sobre cara» en la cara inferior grande (está inclinada 15° en el STL) | **Ninguno** | 3 / 5 / gyroid 15 % | ~12 g | ~1.5 h |
| Probeta | Tal cual (Z arriba): el bloque plano y la pared del sensor de pie sobre su base | **Ninguno** | 4 / 5 / 20 % | ~6 g | ~30 min |

\* Estimados gruesos a partir del volumen sólido (PETG ≈ 1.27 g/cm³); los números
reales los da el slicer. Total del juego: **unos 70 g**.

**PETG:** boquilla 235–245 °C, cama 75–80 °C, ventilador 30–50 %, primera capa lenta,
retracción afinada contra los hilos (*stringing*: hay muchos postes delgados en la
tapa), costura atrás. En PEI liso usar pegamento en barra como capa de separación:
el PETG se pega demasiado.

### Voladizos que quedaron (sin soporte, todos ≤ 1.05 mm o a 45°)
- Chaflán de 45° de la ventana: es el borde que queda sobre la cama al imprimir boca
  abajo.
- Repisas superior e inferior donde asienta la tapa: estante de 1.0 mm.
- Techo de la ranura del gancho, de las bolsas del sensor y del touch, y del bolsillo de
  la llave: voladizos ≤ 1.05 mm.
- Labio del gancho de la tapa: con chaflán de 45° para que no cuelgue.
- Agujero horizontal Ø2 del sensor, con abocinado de 45°.

No hay ningún sitio donde la geometría haya obligado a usar soporte.

## Decisiones (y por qué)

1. **Concha frontal + tapa trasera** (variante A). La única junta queda en el canto
   trasero; frente y costados son una sola pieza.
2. **Cierre sin insertos termofijos: gancho arriba + 2 tornillos M3 abajo.**
   - Los M3 roscan directo en postes de PETG (Ø `rosca_directa_d`, 6 mm de rosca útil
     con M3×8).
   - Arriba no caben postes: el PCB del display ocupa casi todo el interior. Por eso
     la tapa engancha un labio de 30 mm en una ranura bajo la pared superior.
   - Los dos postes van en las esquinas de la barbilla, donde no hay display delante.
   - Son 2 tornillos en vez de 4–6, a propósito: el prompt pedía pocos tornillos y
     preferir guías.
3. **El display se sujeta por el PCB, nunca por el vidrio.**
   - Por delante apoya en dos topes inferiores y dos nervios laterales del bisel, que
     tocan solo el margen del PCB fuera del panel.
   - Por detrás lo empujan 4 postes de la tapa, con 0.2 mm de apriete
     (`disp_apriete`). Los 2 de arriba llevan pernos que entran en los barrenos del
     PCB y fijan su posición respecto a la ventana.
   - El vidrio queda a 0.3 mm del bisel (`holgura_vidrio`).
   - Ya no existe pieza `Reten_display`.
4. **ESP32 vertical, con el USB-C hacia abajo.**
   - Con cable recto, el conector (overmold de 22 mm) más el radio de curvatura obligan
     a tener el receptáculo 32 mm sobre el fondo interior (`usb_salida_z`).
   - El ESP32 se adelantó 1.4 mm (`esp_gap` calculado) para que el conector quede
     dentro sin cortar la tapa.
   - El cable baja por una **muesca en el canto inferior trasero** y sigue por el canal
     de la base.
   - Un cable en L liberaría unos 25 mm de alto.
5. **Lector SD en una cuna de la tapa.**
   - El PCB entra por sus 2 barrenos en 2 pernos impresos (extremo del header) y apoya
     por detrás en 2 topes en el extremo de la ranura. Los pernos se fijan con una gota
     de silicón o fundiendo la punta con el cautín.
   - Un tope deja 0.3 mm a la punta de la tarjeta, así que no puede salirse pero
     **nada empuja el slot**.
   - Para servicio: quitar los 2 tornillos y desenganchar la tapa. No hay ranura
     externa que alguien pueda jalar.
6. **LED mirando al frente** (pedido del usuario).
   - Entre el bisel y el PCB solo hay 3.2 mm, así que se agregó una **barbilla de
     6.8 mm** (`menton_led`) bajo el display.
   - El LED queda centrado en una funda, detrás de un agujero de Ø3 (`led_agujero_d`).
   - El PETG negro no deja pasar luz, por eso es agujero y no membrana.
7. **Sensor de luz en el canto superior**, mirando al techo, como cuando se midieron
   los umbrales sobre la mesa.
   - Lleva un agujero de Ø2.0 en una pared adelgazada a 1.0 mm, con 0.4 mm de tramo
     recto y abocinado de 45° hacia fuera (Ø3.2 en la cara).
   - El chip queda a 0.2 mm de la pared.
   - Campo de visión de ≈ ±43°, que da una **atenuación estimada de ≈ 2×**.
   - Un agujero recto en pared de 2 mm habría dado ≈ ±23° y ≈ 6.5×: demasiado.
   - Membrana descartada: el PETG negro es opaco y algunos negros dejan pasar
     infrarrojo, lo que falsearía el canal 1.
8. **Touch en el canto superior**: bolsa de 25 × 18 × 0.5 mm que deja la pared en
   1.5 mm (`pared_touch`). Sirve para cinta de cobre, lámina o perfboard.
9. **Base tipo cuna en vez de pata.**
   - Recargado hacia atrás, el marco se apoya en su canto inferior trasero. Con el
     display al frente, el centro de masa queda delante de ese canto: una pata sola
     necesitaría más de ~22°.
   - La cuna sostiene todo el fondo a 15°, y el centro de masa cae ~7 mm dentro del
     apoyo.
   - Tiene labio frontal de 2.5 mm (no tapa el LED) y respaldo solo al centro (libra
     las cabezas M3 y el cable). La cola de 15 mm resiste el toque en el canto
     superior.
   - Una **llave asimétrica** impide meter el marco de cabeza: la orientación no se
     puede girar, como pide la §6.
10. **Ventilación**: no hace falta. Cálculo: 1.2–1.5 W sobre ~0.026 m² dan unos
    6–10 K sobre el ambiente, contra ~70 °C de reblandecimiento del PETG. Aun así la
    tapa lleva dos grupos de 5 ranuras (abajo y detrás del SD, para tiro de
    chimenea), ocultos a la vista. `vent_n` controla cuántas.

## Piezas no impresas

| Cant. | Pieza | Nota |
|---|---|---|
| 2 | Tornillo M3×8 (Allen o avellanado) | Rosca directa en PETG; **sin insertos** |
| 1 | Pad de touch: cinta de cobre ~20×12 mm (o perfboard ≤ 24×17) | Dentro de la bolsa del canto superior |
| — | Silicón caliente | Alivio de tensión del UTP; fija los pernos del ESP32, el TSL2561 y el capacitor |
| — | Termofit | Uniones soldadas |
| 1 | Cable USB-C **recto** + cargador 5 V | Ya en inventario |

## Montaje (en orden)

1. **Prueba en seco antes de soldar.** Mete el display en la carcasa frontal (pantalla
   hacia el bisel) y presenta la tapa, para confirmar holguras.
2. **Soldar el UTP** a display, ESP32, lector SD, TSL2561 y LED, con el capacitor de
   100 µF y los cerámicos de 100 nF según el BOM. Deja el largo justo para que cada
   módulo llegue a su sitio con la tapa abierta a un lado.
3. **ESP32:** presiónalo por sus barrenos sobre los 4 pernos de la tapa, con
   componentes hacia la tapa y pines hacia el frente. Fija la punta de los pernos con
   una gota de silicón. El USB-C queda hacia abajo, alineado con la muesca.
4. **Lector SD:** con la tarjeta ya puesta, mete sus 2 barrenos en los pernos de la tapa
   (ranura hacia arriba, porta-tarjeta hacia el frente) y fíjalos con silicón o
   fundiendo la punta. Revisa que la tarjeta no toque su tope.
5. **TSL2561:** colócalo en la bolsa del canto superior de la carcasa frontal (chip
   hacia arriba, barrenos a la izquierda y header a la derecha, vistos de frente) y
   fíjalo con un punto de silicón. La repisa de
   la tapa lo sostiene por debajo al cerrar.
6. **LED:** empújalo por detrás en su funda de la barbilla y fija con silicón.
7. **Pad de touch:** pégalo en la bolsa del canto superior y suelda el cable a GPIO32.
8. **Alivio de tensión:** un punto de silicón junto a cada soldadura y sobre el UTP
   donde cruza hacia la tapa. Ningún cable debe tirar del header del SD ni del
   receptáculo USB.
9. **Display:** déjalo en la carcasa frontal, sobre topes y nervios. Al cerrar, los
   pernos de los postes superiores deben entrar en sus barrenos de arriba. Pasa el UTP
   del header del display por delante del header del TSL2561: sus volúmenes de cables
   quedan a 0.08 mm.
10. **Cierre:** mete el labio de la tapa en la ranura superior con la tapa inclinada,
    bájala y pon los 2 M3 en los postes de la barbilla. Aprieta suave: es rosca en
    plástico.
11. **Cable:** levanta el marco, mete el USB-C por la muesca inferior hacia arriba
    hasta el receptáculo, y asienta el marco en la cuna con el cable en su canal.

Por la parte frontal y los costados no se ve ninguna soldadura ni cable. Lo único que
sale es el USB-C, por el canto inferior trasero.

## Plan de pruebas de ajuste

1. **Probeta (~30 min, ~6 g).**
   - Rosca: atornilla un M3×8 en los cuatro agujeros (2.5 / 2.6 / 2.7 / 2.8; la muesca
     marca el de 2.5). Elige el que rosca firme sin rajar y ponlo en
     `rosca_directa_d`.
   - LED: debe entrar justo en su alojamiento; si no, ajusta `comp_agujero`.
   - Sensor: lux con el TSL2561 detrás de la pared de la probeta contra lux al aire,
     con la misma luz. El cociente es la atenuación real (objetivo ≈ 2×, límite 5.8×);
     ajusta `sensor_ventana_d` / `sensor_pared`.
2. **Bisel recortado (~1.5 h, ~20 g).** En el slicer, corta la carcasa frontal a
   **Z = 10 mm** (impresa boca abajo).
   - Comprueba que el PCB del display entre en sus guías.
   - Comprueba que la ventana coincida con el área iluminada. Esto valida C2/C3, lo
     más incierto.
   - Comprueba que el vidrio no toque el bisel y que el LED asome por su agujero.
3. **Carcasa frontal + tapa completas:** montaje en seco con toda la electrónica, cierre
   del gancho y de los tornillos.
4. **Base:** estabilidad a 15° y tocando el canto superior.

## Pendiente de validar tras la primera impresión

- **Medidas que siguen siendo supuestas (PENDIENTE en Fusion):**
  - Header del display: plástico de 2.5 mm y pin de 6 mm, valores estándar.
  - USB-C del ESP32: el CAD lo dibuja **hundido 0.4 mm** respecto al borde del PCB; el
    modelo lo supone sobresaliendo 1 mm. Revisa en tu placa si sobresale, queda al ras
    o queda hundido: si queda hundido, el conector del cable podría topar con el PCB.
  - Conector del cable (B3): 12 × 6.5 × 22 mm supuestos.
- **Los modelos CAD vienen de internet:** las medidas del display, el ESP32 y el SD se
  tomaron de ellos. Si alguna pieza impresa no ajusta, compara esa cota con el
  calibrador antes de culpar a la impresora.
- **Firmware, sensor:** con la carcasa cerrada, repetir la lectura de noche
  (126–137 lux sin carcasa). Si la atenuación queda < 5.8×, los umbrales 10/300 siguen
  separando todas las condiciones medidas; si no, se escalan en `updateBrightness()`.
- **Firmware, touch:** calibrar `TOUCH_THRESHOLD` con el pad real tras 1.5 mm de PETG
  negro (media de N lecturas × 0.6 + 2–3 lecturas consecutivas, como dice §8).
- **Firmware, LED:** ahora mira al frente por un agujero de Ø3. Revisar si hay que
  bajar el brillo con `escala[3]`; el apagado a los 30 s se mantiene.

## Modelos CAD de los componentes

Están en el proyecto de Fusion (*Marco Fotos*) como documentos propios:
«ESP32 dev Kit v1», «LCD_TFT_3.5inch_ST7796S», «tsl2561», «Led5mm_RGB» y
«SD Card Module - 1».

- **Inserción:** van como referencias externas (X-ref) en
  `_Referencia_electronica > _Modelos_CAD`. El ESP32 va dentro de un contenedor
  `Pos_ESP32`, porque Fusion no deja fijar la posición de esa referencia directamente.
- **No siguen los parámetros.** Después de cambiar parámetros hay que recolocarlos con
  [`fusion/alinear_cad.py`](fusion/alinear_cad.py).
- **Choques:** el análisis de interferencias de Fusion **no es confiable con
  referencias externas** (una vez dijo «sin choques» cuando sí los había). Se
  verifican con intersecciones booleanas cuerpo por cuerpo
  (`TemporaryBRepManager`).
- **Scripts:** [`fusion/helpers.py`](fusion/helpers.py) es el generador de cajas y
  cilindros paramétricos con el que se construyó el modelo, y
  [`fusion/stlcheck.py`](fusion/stlcheck.py) verifica los STL exportados (cerrados,
  medidas, volumen).

## Volver a exportar

Tras cambiar parámetros en Fusion, re-exportar cada componente imprimible
(`Carcasa_frontal`, `Tapa_trasera`, `Base_cuna`, `Probeta_ajustes`) a esta carpeta con
los mismos nombres de archivo.

**Ojo: Fusion exporta vacío un componente oculto** y aun así responde que tuvo
éxito. Antes de exportar, prende la visibilidad de los cuatro componentes y revisa
que la hora y el tamaño de los archivos hayan cambiado.
