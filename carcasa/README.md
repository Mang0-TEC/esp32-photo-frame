# Carcasa del marco — PETG negro, prototipo v5

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

## Cambios v4 → v5 (12-oct-2026, tras el test fit de la v4 impresa)

Test fit de la v4 con el display, el ESP32 montado en la tapa y el cable USB-C
conectado.

**Lo que quedó bien y no se toca:**
- La ventana 0.5 mm más abajo: el bisel coincide con el borde del display y no asoma
  nada del borde del vidrio.
- La posición de las rejillas.
- Las marcas de versión grabadas: sí sirven para distinguir impresiones.
- Los M3×25: entran sin rajar el PETG, y el cajeado deja la cabeza al ras.
- El LED, mirando al frente por su agujero.
- Los dos tornillos inferiores sujetan bien la tapa por ahora.

**Cambios:**
- **Calza del USB-C con holgura.** Con el ESP32 en la tapa y el cable conectado, el
  cuerpo del conector chocaba con la calza: los 0.5 mm de aire no alcanzaban (o el
  conector es algo más largo que los 18 mm medidos, o el ESP32 queda un poco más
  abajo). Además, la ranura apretaba el cable 0.2 mm, y así no hay forma de cerrar
  sin forzarlo.
  - Aire entre el cuerpo del conector y la calza: **2.5 mm** (`usb_calza_aire`). La
    calza baja de 14.5 a 12.5 mm.
  - Ranuras con holgura en vez de apriete: **0.5 mm por lado** (`usb_calza_holgura`,
    nuevo). Ranura del cable de Ø5.0 (antes 3.8) y de la bota de 6.3 (antes 5.8). La
    ranura del fondo sigue la misma regla y queda en 5.0 mm (`muesca_cable_w`).
  - Se borró `usb_calza_apriete`.
  - Sigue siendo un tope: un jalón mueve el conector a lo más 2.5 mm antes de que la
    carcasa lo detenga. Lo que retiene el cable en el día a día es el doblez por la
    muesca y el canal de la base.
  - **Para otro cable:** un conector hasta 2.5 mm más largo entra sin cambiar nada.
    Si es más largo, o la bota es más gruesa, cambia `usb_tope`, `usb_bota_d` y
    `usb_bota_l` (medidos con el cable conectado), o `cable_d`, y la calza se recalcula
    sola.
- **Touch en la pared derecha, abajo** (lado del lector SD, vista frontal). El canto
  superior ya tiene el TSL2561, la tira de pines del display con su UTP y el gancho de
  la tapa; con el pad ahí no había manera de acomodar todo.
  - Bolsa de 15 × 15 × 0.5 mm por dentro de la pared derecha (`touch_y`, `touch_z`
    nuevo). La pared queda en 1.5 mm (`pared_touch`), igual que en la v4.
  - Ocupa Z = 14–29 mm sobre la base exterior, y en Y de 15.3 a 30.3 mm, detrás de
    la capa del display.
  - El pad queda a **10.2 mm del PCB del display** (7.8 en la v4), a 10.6 del PCB del
    lector SD, a 12.7 del electrolítico de 100 µF y a 3 mm de la tapa (plástico).
    Libra el poste M3 derecho y queda por encima del labio de la base.
  - Se borró `touch_x`; la bolsa del canto superior ya no existe.
  - Los cupones de la probeta siguen sirviendo: la pared es la misma.
- **TSL2561 girado 180°** (pedido del usuario). Ahora los barrenos quedan a la
  derecha y el header a la izquierda, con los pines hacia la pared izquierda y no hacia
  el centro.
  - Libera el espacio sobre la tira de pines del display: el header del TSL2561 queda
    a **5.4 mm** del volumen del UTP del display (antes 0.08). Además, sus pines quedan
    junto a la pared, donde es más fácil soldar y acomodar los cables.
  - La placa ocupa el mismo lugar (`tsl_x0` no cambia). El chip queda ahora a 6.5 mm
    del extremo derecho, así que **el agujero del sensor se mueve 5 mm a la derecha**,
    de X = 18 a X = 23 (`sensor_x`). Los pernos del techo pasan al extremo derecho, con
    sus barrenos.
  - El header queda a 1.1 mm del poste superior izquierdo de la tapa y a 6 mm del
    ESP32.
  - La repisa de la tapa que sostiene la placa pasa al extremo de los barrenos, para no
    chocar con el header.
  - `alinear_cad.py` coloca el CAD del TSL2561 con la nueva orientación.
- **Marca «v5»** grabada en la tapa y en la barbilla de la carcasa frontal.
- `fusion/helpers.py` ganó `box_x`, para cajas de eje X (cortes en las paredes
  laterales).

**Verificado:**
- Intersecciones booleanas de las tres piezas entre sí y contra los dummies y los
  CAD: los mismos choques intencionales que en la v4, menos el apriete de la calza
  sobre el cable, que ya no existe.
- Distancias mínimas: 2.5 mm del cuerpo del conector a la calza y 0.5 mm de la bota y
  del cable a las paredes de sus ranuras.
- Pruebas de punto en los bordes de la bolsa del touch, en las dos ranuras de la calza
  y en el canto superior, que quedó macizo.
- TSL2561: el túnel y el collar están centrados en X = 23, y el agujero viejo quedó
  cerrado. Los pernos caen dentro de los barrenos del CAD real, a 0.25 mm de su borde y
  sin tocarlo. La repisa nueva sostiene la placa sin tocar el header.

**Reimprimir: carcasa frontal y tapa.** La base y la probeta de la v4 siguen
sirviendo.

## Cambios v3 → v4 (11-oct-2026, tras el test fit de la v3 impresa)

Test fit en seco de las tres piezas en PETG, sin cablear. **El display entra bien en la
carcasa frontal con todo y PCB, y el bisel tapa su borde perfectamente.** El USB-C del
ESP32 no choca y el cable dobla bien hacia la muesca. Lo demás no se midió todavía.

- **Ventana 0.5 mm más abajo** (`ventana_ext_inf`). Medido con calibrador: el bisel
  inferior cubría menos de 1 mm de más. Ahora abajo se ve 1.0 mm de borde negro bajo
  el área activa, contra 0.5 mm en los otros lados. La carcasa mide lo mismo y el LED
  sigue en su barbilla.
- **Los 4 postes del display llevan perno.** Los dos de abajo estaban a 5 mm del lado
  y del borde, fuera de los barrenos (5.58 mm del lado, 2.42 mm del borde, igual que
  arriba). Ahora entran en ellos, con pernos de Ø2.5 (`holgura_perno`). Los de abajo
  son 0.2 mm más cortos, para no tocar los apoyos del bisel que hay delante. Se borró
  `poste_disp_inset`.
- **Tornillos M3×25 de cabeza Allen** (medidos: Ø5.5 × 3 de cabeza, 28 mm en total).
  En la v3 el barreno era para un M3×8, y un tornillo de 25 mm no entraba.
  - La cabeza va **al ras**, en un cajeado de Ø6.1 × 3.2 dentro de un refuerzo
    interior de la tapa. Deja 1.6 mm de piso bajo la cabeza.
  - Una **guía tubular** de la tapa lleva el tornillo hasta un **poste corto** de la
    carcasa. Solo los últimos 6 mm (`rosca_prof`) roscan en Ø2.7; el resto pasa libre.
    La guía también ayuda a encontrar el barreno a ciegas.
  - Para que el cajeado no rompa el borde de la tapa, el eje del tornillo se movió
    1.1 mm hacia dentro (`poste_inset` = 4.1). El poste de la carcasa bajó a Ø5.4
    (`poste_d`, calculado) para librar el PCB del display por 0.3 mm, y se apoya en un
    refuerzo de esquina.
  - La repisa inferior se recortó alrededor de los refuerzos.
  - Si cambias de tornillo, cambia `tornillo_l`: los postes y la guía se recalculan
    solos.
- **TSL2561 1 mm más abajo** (`sensor_separacion`). Las colas soldadas del header
  necesitan más aire contra el techo. El usuario va a pasar el electrolítico de 10 µF a
  la altura de los pines, con UTP, para que no quede entre la placa y el techo.
  - Un **collar** baja del techo hasta 0.2 mm del chip. Así la óptica junto al chip
    queda igual que en la v3: Ø2 con 0.4 mm de tramo recto y abocinado de 45°.
  - El túnel mide ahora 2 mm (`sensor_tunel`), y el agujero en el canto superior pasa
    de Ø3.2 a **Ø5.2**.
  - Con el abocinado a 45°, el campo de visión sigue limitado a ≈ ±43°. Por eso la
    pared de la probeta, que no cambió, sigue sirviendo para medir la atenuación.
- **Rejillas detrás del ESP32-WROOM.** Las dos filas quedan sobre el módulo
  (`vent_x0`, `vent_z0`, `vent_fila_sep`), que es lo que más calienta después del
  backlight y queda a 1.9 mm de la tapa. Las que daban al lector SD y al fondo se
  cerraron: la muesca del USB-C ya funciona como entrada de aire por abajo.
- **Bolsa del touch movida y achicada a 15 × 15 mm.** Queda a la derecha y atrás
  (`touch_x`, `touch_y`), según la regla del BOM de no poner masa detrás del pad.
  - El pad (dummy de 12 × 12 mm) queda a 7.8 mm del PCB del display, a 4.7 mm de sus
    pines y del volumen de su UTP, y a más de 8.9 mm del lector SD y del ESP32. En la
    v3 estaba a ~2 mm del PCB.
- **Cupones de touch en la probeta** (`Probeta_touch`): tres placas de pie, de
  1.5 / 2.0 / 2.5 mm de izquierda a derecha, cada una con la bolsa de 15 × 15 × 0.5 mm
  del pad.
  - Se imprimen de pie para que, como el techo de la carcasa impresa boca abajo, sean
    perímetros y no capas.
  - Sirven para la tecla `t` del banco. Esa medida fija `TOQUE_DELTA` y `pared_touch`,
    que por ahora sigue en 1.5 mm.
- **El TSL2561 se ubica con pernos.** Dos pernos de Ø2.5 (`tsl_perno_d`) bajan del
  fondo de la bolsa y entran 1.2 mm en sus barrenos (posición sacada del CAD:
  `tsl_hole_x`, `tsl_hole_inset`). Sin ellos, nada centraba el chip bajo el túnel, y un
  milímetro de desvío al pegarlo cambiaba lo que lee el sensor. Al imprimir boca abajo
  quedan horizontales (3.75 mm): pueden colgarse un poco, sin consecuencia.
- **Alivio de tensión del USB-C: calza bajo el overmold.**
  - Un bloque de 14.5 mm de alto en el fondo de la carcasa queda a 0.5 mm bajo el
    cuerpo del conector (`usb_calza_h`, `usb_calza_aire`). Si alguien jala el cable, el
    cuerpo topa con la calza antes de que el conector se salga, y la fuerza la toma la
    carcasa, no las soldaduras del receptáculo.
  - **Medido con el cable conectado** (`usb_tope`, `usb_bota_d`, `usb_bota_l`): el
    cuerpo del conector termina 18 mm bajo el borde del PCB del ESP32 y le sigue una
    bota de refuerzo de Ø5.3 × 5 mm.
  - Tiene una ranura abierta hacia atrás. Arriba mide 5.8 mm para la bota; abajo mide
    3.8 mm y abraza el cable de 4 mm (`usb_calza_apriete` = 0.2 mm). El cable sigue
    doblando hacia atrás por la muesca de la tapa.
  - `usbp_l` se queda en 22 mm aunque la parte rígida mide ~23 (18 + 5): fija la altura
    del ESP32, que ya se validó en el test fit de la v3.
  - Al frente lleva una rampa de 45° para imprimir sin soporte.
  - La ranura del fondo se angostó a 4.5 mm (`muesca_cable_w`). El ancho del overmold
    (`muesca_usb_w`) sigue mandando en la muesca de la tapa y en la base, que no
    cambia.
  - **Cambia el montaje:** el cable se conecta al ESP32 con la tapa abierta, antes de
    cerrar (ver Montaje, paso 10). Ya no se mete desde abajo con la carcasa cerrada.
- **Marca «v4» grabada** 0.4 mm por dentro de la tapa y de la barbilla de la carcasa,
  para no confundir impresiones.
- `alinear_cad.py` ahora reconoce los X-ref aunque Fusion les agregue «.f3d» al nombre,
  y baja el TSL2561 con `sensor_separacion`.

**Verificado:**
- Intersecciones booleanas de las tres piezas entre sí y contra todos los dummies y
  modelos CAD. Solo aparecen choques intencionales:
  - El apriete de 0.2 mm de los 4 postes sobre el PCB del display: contra el CAD real,
    que tiene barrenos, dan justo 10.0 mm³.
  - Los pernos dentro de dummies sin barrenos: display, ESP32, SD y TSL2561.
  - El apriete de la calza sobre el dummy del cable.
- Los pernos del TSL2561 caen dentro de los barrenos del CAD real, sin tocarlo.
- Las patas rectas del LED CAD atraviesan tapa y base, igual que en la v3: el modelo
  trae las patas sin cortar.
- Más de 50 pruebas de punto sobre la geometría nueva.
- **Barrenos de los postes M3, revisados por el usuario.** El refuerzo de esquina
  estaba después del barreno en la línea de tiempo y rellenaba un cuarto del agujero
  (se veía en forma de «C»). Ahora va antes del barreno. Para comprobarlo se probaron
  144 puntos en anillo: huecos a r = 1.2 mm y macizos a r = 1.6 mm, en 12 ángulos y 3
  profundidades por poste.
  - Lección: probar el barreno en un anillo dentro del radio, no sobre el eje. El eje
    caía justo en la arista del refuerzo y daba «hueco».
- Huecos alrededor del poste: 0.8 mm hasta el apoyo del display y 0.3 mm hasta su PCB.
- Las dos marcas «v4» se revisaron en pantalla y se leen bien desde dentro.

**Reimprimir: carcasa frontal y tapa.** La probeta solo si vas a medir el touch: en el
slicer puedes separar el objeto y dejar únicamente las tres placas. **La base no
cambia.**

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
| `marco_carcasa_frontal.stl` | Bisel + cuatro paredes + calza del USB-C, una pieza (v5) | 63.8 × 33.8 × 106.2 | 29.0 cm³ |
| `marco_tapa_trasera.stl` | Tapa + postes del display + cunas ESP32/SD + guías de tornillo | 59.3 × 28.4 × 102.8 | 15.3 cm³ |
| `marco_base_cuna.stl` | Cuna de sobremesa a 15° | 63.8 × 50.8 × 21.6 | 17.7 cm³ |
| `marco_probeta_ajustes.stl` | Probeta de rosca, LED y ventana del sensor + cupones de touch | 63 × 33 × 21 | 8.9 cm³ |

Los STL están exportados **en las coordenadas del ensamble**, no en la orientación de
impresión: hay que girarlos en el slicer según la tabla de abajo.

## Tabla de impresión (Ender 3 V3 KE, boquilla 0.4, capa 0.16, PETG negro)

| Pieza | Orientación en la cama | Soporte | Perímetros / sup-inf / relleno | Filamento* | Tiempo* |
|---|---|---|---|---|---|
| Carcasa frontal | **Cara frontal abajo** (girar −90° en X). Sobre PEI texturizado el frente sale mate, con aspecto de producto | **Ninguno** | 4 perímetros, 5 capas sup/inf, gyroid 15 % (casi todo es pared maciza de 2 mm) | ~32 g | ~3–3.5 h |
| Tapa trasera | **Cara exterior abajo** (girar +90° en X); postes, guías y ganchos hacia arriba | **Ninguno** | 4 / 5 / 15 % | ~19 g | ~2 h |
| Base cuna | **Cara de apoyo abajo**: en el slicer, «colocar sobre cara» en la cara inferior grande (está inclinada 15° en el STL) | **Ninguno** | 3 / 5 / gyroid 15 % | ~12 g | ~1.5 h |
| Probeta | Tal cual (Z arriba): el bloque plano, la pared del sensor y las placas del touch de pie sobre su base. **Los cupones del touch, con los mismos perímetros y relleno que la carcasa (4 / 15 %)** | **Ninguno** | 4 / 5 / 20 % | ~11 g | ~50 min |

\* Estimados gruesos a partir del volumen sólido (PETG ≈ 1.27 g/cm³); los números
reales los da el slicer. Total del juego: **unos 75 g**.

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
- Agujero horizontal Ø2 del sensor, con abocinado de 45°, y el collar del túnel:
  saliente de 1.0 mm.
- Piso bajo la cabeza de los tornillos: anillo de 1.35 mm sobre el cajeado, que se
  imprime contra la cama. Es un puente corto; si se cuelga un poco, la cabeza lo
  aplana al apretar.

No hay ningún sitio donde la geometría haya obligado a usar soporte.

## Decisiones (y por qué)

1. **Concha frontal + tapa trasera** (variante A). La única junta queda en el canto
   trasero; frente y costados son una sola pieza.
2. **Cierre sin insertos termofijos: gancho arriba + 2 tornillos M3 abajo.**
   - Los M3×25 roscan directo en postes de PETG (Ø `rosca_directa_d`), solo en los
     últimos 6 mm (`rosca_prof`). La cabeza queda al ras, en un cajeado de la tapa, y
     una guía tubular lleva el tornillo hasta el poste (v4).
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
   - Desde la v5 la placa va con el header hacia la pared izquierda, lejos de la tira
     de pines del display, y el agujero queda en X = 23 mm.
   - Lleva un agujero de Ø2.0 con 0.4 mm de tramo recto y abocinado de 45° hacia fuera.
     Desde la v4 el agujero arranca en un collar que baja 1 mm del techo, así que el
     túnel mide 2 mm y el agujero en la cara mide Ø5.2.
   - El chip queda a 0.2 mm del collar. El PCB queda 2.55 mm bajo el fondo de la
     bolsa, para que quepan las colas soldadas del header.
   - Campo de visión de ≈ ±43°, que da una **atenuación estimada de ≈ 2×**.
   - Un agujero recto en pared de 2 mm habría dado ≈ ±23° y ≈ 6.5×: demasiado.
   - Membrana descartada: el PETG negro es opaco y algunos negros dejan pasar
     infrarrojo, lo que falsearía el canal 1.
8. **Touch en la pared derecha, abajo** (desde la v5): bolsa de 15 × 15 × 0.5 mm que
   deja la pared en 1.5 mm (`pared_touch`). Sirve para un pad de cinta de cobre o
   lámina de 12–15 mm.
   - Hasta la v4 iba en el canto superior. Ahí ya están el TSL2561, la tira de pines
     del display con su UTP y el gancho de la tapa, y no cabía todo.
   - Va del lado del lector SD y no del ESP32, y abajo, donde hay menos componentes.
     Queda detrás de la capa del display, a más de 10 mm de todo PCB.
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
    tapa lleva dos filas de 5 ranuras **detrás del ESP32-WROOM** (v4), ocultas a la
    vista. El aire entra por la muesca del USB-C. `vent_n` controla cuántas.

## Piezas no impresas

| Cant. | Pieza | Nota |
|---|---|---|
| 2 | Tornillo M3×25 Allen (cabeza Ø5.5 × 3) | Rosca directa en PETG; **sin insertos**. Otro largo: cambiar `tornillo_l` |
| 1 | Pad de touch: cinta de cobre de 12–15 mm por lado | Dentro de la bolsa de la pared derecha, abajo (15 × 15) |
| — | Silicón caliente | Alivio de tensión del UTP; fija los pernos del ESP32, el TSL2561 y el capacitor |
| — | Termofit | Uniones soldadas |
| 1 | Cable USB-C **recto** + cargador 5 V | Ya en inventario |

## Montaje (en orden)

Cada módulo entra ya soldado al UTP, así que mientras se arma la tapa (con el ESP32 y
el lector SD) queda colgando de la carcasa por los cables, como una bisagra. Con
tantos componentes el orden ya importa. La regla es **primero la tapa, después la
carcasa frontal de adelante hacia atrás, y al final lo que cruza entre las dos (el
cable USB y el cierre)**. Si agregas un componente, anótalo en la fase donde va y
revisa que no tape a uno que se coloca después.

**Fase 0 — Piezas sueltas, sin electrónica**

1. **Rosca de los postes:** pasa cada M3×25 por su poste con la carcasa frontal
   suelta, para hacer la rosca. Con los cables puestos ya no hay espacio para forzar.
2. **Prueba en seco:** mete el display en la carcasa frontal (pantalla hacia el
   bisel). Presenta la tapa con el ESP32 en sus pernos y el cable USB-C conectado: el
   cuerpo del conector no debe tocar la calza y el cable debe entrar suelto en su
   ranura. Así se encontró el choque de la v4.

**Fase 1 — Soldar, fuera de la carcasa**

3. **Soldar el UTP** a display, ESP32, lector SD, TSL2561, LED y pad de touch, con el
   capacitor de 100 µF y los cerámicos de 100 nF según el BOM.
   - Deja el largo justo para que cada módulo llegue a su sitio con la tapa abierta a
     un lado.
   - En el TSL2561, pasa el electrolítico de 10 µF a la altura de los pines (con UTP),
     para que nada quede entre la placa y el techo.
   - El pad de touch lleva **un solo hilo**, a GPIO32. No lo trences con tierra.

**Fase 2 — Tapa (capa trasera)**

4. **ESP32:** presiónalo por sus barrenos sobre los 4 pernos de la tapa, con
   componentes hacia la tapa y pines hacia el frente. Fija la punta de los pernos con
   una gota de silicón. El USB-C queda hacia abajo, alineado con la muesca.
5. **Lector SD:** con la tarjeta ya puesta, mete sus 2 barrenos en los pernos de la tapa
   (ranura hacia arriba, porta-tarjeta hacia el frente) y fíjalos con silicón o
   fundiendo la punta. Revisa que la tarjeta no toque su tope.

**Fase 3 — Carcasa frontal, de adelante hacia atrás** (carcasa boca abajo sobre la
mesa)

6. **LED:** empújalo por detrás en su funda de la barbilla y fíjalo con silicón. Va
   antes que el display para no apoyar la mano cerca del vidrio.
7. **Display:** déjalo en la carcasa frontal, sobre topes y nervios, con la tira de
   pines arriba. Al cerrar, los pernos de los cuatro postes de la tapa entran en sus
   cuatro barrenos.
8. **TSL2561:** va después del display, porque su placa queda 0.4 mm por encima del
   volumen del UTP del display. Acomoda primero ese UTP.
   - Colócalo bajo la bolsa del canto superior, con el chip hacia arriba y centrado bajo
     el collar del túnel, **barrenos a la derecha y header a la izquierda**, vistos de
     frente (desde la v5). Los pines quedan hacia la pared izquierda.
   - Mete sus dos barrenos en los pernos del techo: eso lo centra bajo el túnel. Fíjalo
     con un punto de silicón. La repisa de la tapa lo sostiene por debajo al cerrar.
9. **Pad de touch:** pégalo en la bolsa de la **pared derecha, abajo** (lado del lector
   SD, vista frontal). No estorba a nada, así que puede ir en cualquier momento de esta
   fase.
   - La soldadura cabe en cualquier punto del pad: lo más cercano es la tapa, a 3 mm.
   - Lleva el hilo por debajo del lector SD hacia el ESP32, lejos del cable del
     backlight (GPIO19).
10. **Alivio de tensión:** un punto de silicón junto a cada soldadura y sobre el UTP
    donde cruza hacia la tapa. Ningún cable debe tirar del header del SD ni del
    receptáculo USB.

**Fase 4 — Cierre**

11. **Cable USB-C:** con la tapa todavía abierta, conéctalo al ESP32 y déjalo colgando
    hacia abajo.
12. **Tapa:** mete su labio en la ranura superior con la tapa inclinada, y bájala.
    - El cable entra por detrás en la ranura de la calza, que le queda holgada, y el
      cuerpo del conector queda 2.5 mm encima de ella. Si hay que empujar el cable, algo
      está desalineado: no fuerces.
    - Revisa que ningún cable quede atrapado entre la tapa y las repisas.
13. **Tornillos:** mete los 2 M3×25 por los cajeados: la guía los lleva al poste.
    Aprieta suave hasta que la cabeza quede al ras: es rosca en plástico.
14. **Base:** asienta el marco en la cuna con el cable en su canal.

Para desconectar el cable hay que abrir la tapa: la calza no deja salir el conector, y
para eso está.

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
  - Ancho y grueso del cuerpo del conector (12 × 6.5 mm supuestos): sin medir, pero en
    el test fit de la v3 el cable entró y dobló sin problema. Ya están medidos el cable
    (Ø4.0), la bota (Ø5.3 × 5) y el fin del cuerpo (18 mm bajo el PCB).
  - **Calza del USB-C (v5):** confirmar que el conector ya no la toca y cuánto aire
    queda. Si sobra mucho, se puede bajar `usb_calza_aire`; si sigue tocando, medir
    con el calibrador desde el borde del PCB del ESP32, montado en la tapa, hasta la
    calza.
- **Los modelos CAD vienen de internet:** las medidas del display, el ESP32 y el SD se
  tomaron de ellos. Si alguna pieza impresa no ajusta, compara esa cota con el
  calibrador antes de culpar a la impresora.
- **Firmware, sensor:** con la carcasa cerrada, repetir la lectura de noche
  (126–137 lux sin carcasa). Si la atenuación queda < 5.8×, los umbrales 10/300 siguen
  separando todas las condiciones medidas; si no, se escalan en `updateBrightness()`.
- **Touch:**
  - Desde la v5 el pad va en la pared derecha, abajo, y su hilo cruza el ancho del
    marco hasta GPIO32, que en el DevKit queda en la mitad superior del header: unos
    7–10 cm, en el límite de los 5–10 cm del BOM. Medir el delta con el pad ya en su
    bolsa y el marco cerrado: lo que importa es la masa alrededor, que cambió con la
    posición.
  - Imprimir los cupones de la probeta (v4) y fijar `TOQUE_DELTA` (`firmware/src/toque.h`, provisional en 15) con la tecla `t`
    del banco, a través de cupones de PETG impresos con los **mismos perímetros y
    relleno** que la carcasa. Eso decide `pared_touch`: si a 1.5 mm la señal no
    alcanza, la pared es demasiado gruesa.
- **Firmware, LED:** ahora mira al frente por un agujero de Ø3. Revisar si hay que
  bajar el brillo con `escala[3]`; el apagado a los 30 s se mantiene.

## Para próximas iteraciones

- **Postes de tornillo en las esquinas superiores.** Con los 2 tornillos de abajo y el
  gancho arriba la tapa sujeta bien por ahora. Si hace falta más (tapa que se abre
  arriba, más peso dentro), arriba no hay sitio libre: el PCB del display llena el
  ancho con 0.3 mm por lado, y en las esquinas ya están los pernos de sus barrenos
  superiores, el TSL2561 a la izquierda y el labio del gancho a la derecha. Opciones a
  evaluar: postes detrás de la capa del display (capa trasera, junto al TSL2561 y al
  extremo de la ranura del SD), o ensanchar la carcasa unos milímetros arriba.
- **Orden de montaje.** Cada componente nuevo tiene que decir en qué fase entra (ver
  Montaje). Si dos componentes se tapan entre sí, hay que resolverlo en el diseño, no
  en el armado.

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

Tras cambiar parámetros en Fusion, recolocar los CAD con `alinear_cad.py` y re-exportar cada componente imprimible
(`Carcasa_frontal`, `Tapa_trasera`, `Base_cuna`, `Probeta_ajustes`) a esta carpeta con
los mismos nombres de archivo. Usa refinamiento de malla **alto**
(`MeshRefinementHigh`): con el medio, los pernos más delgados salen muy facetados.

**Ojo: Fusion exporta vacío un componente oculto** y aun así responde que tuvo
éxito. Antes de exportar, prende la visibilidad de los cuatro componentes y revisa
que la hora y el tamaño de los archivos hayan cambiado.
