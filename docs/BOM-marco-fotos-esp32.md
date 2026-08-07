# BOM — Marco de Fotos Digital ESP32

**Proyecto:** Marco de fotos digital con servidor local WiFi
**Presupuesto objetivo:** $700 MXN
**Costo real a comprar:** $261 MXN

---

## A. Comprar

| # | Componente | SKU | Proveedor | Cant. | Precio |
|---|---|---|---|---|---|
| 1 | Módulo TFT ST7796S 3.5" IPS 320×480 SPI | AR3952 | Unit Electronics | 1 | $196 |
| 2 | Lector de Memorias SD H95 | AR0532 | Unit Electronics | 1 | $23 |
| 3 | GY-302 Sensor de intensidad luminosa BH1750 | — | Unit Electronics | 1 | $42 |
| | **Subtotal** | | | | **$261** |

**El ESP32 de reposición ya se compró** y pasó a la tabla B (fila 16). Estaba
aquí porque la unidad USB-C que había resultó defectuosa — no transmitía ni
sincronizaba con esptool, ni en macOS ni en Windows, pese a 3.3 V correctos, `EN`
liberado y loopback TX0↔RX0 exitoso. Ese diagnóstico sigue cerrado: la unidad
nueva es **otra placa física**, con puente y revisión de silicio distintos, no la
misma que volvió a probarse. Con la compra hecha, el pendiente vuelve a los $261
de pantalla, lector y sensor.

**Debe ser WROOM, no WROVER.** La WROVER ocupa GPIO16 y GPIO17 con la PSRAM, y
ahí van `RST` y `DC` del display. **Confirmado WROOM en la unidad nueva**, medido
—no leído de la etiqueta— con `ESP.getPsramSize()` desde el banco del LED:
`PSRAM 0 B`. GPIO16 y GPIO17 quedan libres y el mapa de pines cierra. esptool no
reporta PSRAM, de ahí que la comprobación viva en el banco; sale gratis porque ese
flasheo se hace igual, y descubrirlo después de soldar cuesta la pantalla entera.

---

## B. Ya en inventario

| # | Componente | Nota |
|---|---|---|
| 5 | ESP32 DevKit WROOM, **micro-USB** | **Placa de desarrollo.** Puente CP2102, `/dev/cu.usbserial-0001`. Chip verificado `ESP32-D0WD-V3 rev v3.0`, cristal 40 MHz, flash 4 MB |
| 6 | Tarjeta SD 8 GB | FAT32, fotos en subdirectorio `/fotos/` |
| 7 | Cargador USB 5 V ≥1 A + cable USB-C | Alimentación permanente |
| 8 | Capacitor electrolítico 100 µF / 16 V | ✔ Confirmado |
| 9 | Capacitores cerámicos 100 nF (código "104") | ✔ Confirmado — 3 piezas |
| 10 | Transistor PN2222A (NPN, TO-92) | **Sin uso.** Era para el backlight en Caso A; `BL` resultó entrada lógica y va directo a GPIO19. Se queda en inventario |
| 11 | LED RGB 5 mm **cátodo común** | ✔ Sustituye a los dos LEDs separados |
| 12 | Resistencias **220 Ω** (LED rojo), 470 Ω ×2 (LED verde y azul), 10 kΩ, más surtido | El rojo llevaba 1 kΩ y **se cambió a 220 Ω** tras verse más apagado que los otros dos; con eso el blanco sale blanco. El 1 kΩ queda libre. Las dos 470 Ω están puestas en el LED, y **no hace falta una tercera**: el backlight resultó Caso B y va directo a GPIO19, sin transistor y sin resistencia de base. Ver la nota de corriente por canal, abajo |
| 13 | Header macho 2.54 mm (9 pines) | ✔ Confirmado |
| 14 | Cable UTP Cat5e (núcleo sólido 24 AWG) | ✔ Sustituye jumpers Dupont |
| 15 | Filamento PLA | Carcasa y bisel |
| 16 | ESP32 DevKit **USB-C** — reposición, $126 ya pagados | **Candidata a unidad final.** Puente **CH340** (`0x1a86:0x7523`), `/dev/cu.wchusbserial1110` — va literal en los dos `.ini` del banco, un glob no funciona. Verificado con esptool: `ESP32-D0WD-V3 rev v3.1`, cristal 40 MHz, flash 4 MB, MAC `70:4b:ca:48:d3:ec`. **WROOM confirmado** — `PSRAM 0 B` medido desde el banco del LED |

La placa micro-USB **no es la unidad final**: micro-USB es mal conector para un
objeto fijo 24/7 durante años; se afloja y su pad termina desprendiéndose del
PCB. Sirve para todo el desarrollo del firmware — el chip es el mismo
`ESP32-D0WD-V3` — y el cambio a la USB-C final no toca una sola línea de código.

---

## C. Verificar / conseguir

| # | Componente | Cant. | Para qué |
|---|---|---|---|
| 16 | Pad conductor para touch capacitivo | 1 | Ver opciones abajo |
| 17 | Termofit surtido | — | Aislar uniones soldadas |
| 18 | Soldadura + flux | — | Header y cableado |
| 19 | Silicón caliente | — | Alivio de tensión del UTP |
| 20 | Insertos roscados M3 o tornillos autorroscantes | 4-6 | Cierre de la carcasa |

---

## D. Descartado (y por qué)

| Componente | Razón |
|---|---|
| ESP32-S3 DevKit | TJpg_Decoder trabaja por bloques MCU 16×16; no necesita framebuffer completo. El clásico basta |
| ESP32 **WROVER** | Ocupa GPIO16 y GPIO17 con la PSRAM, y ahí van `RST` y `DC` del display |
| Módulo USB standalone | El pin `5V`/`VIN` del devkit ya entrega VBUS |
| Step-down LM2596S | Solo haría falta alimentando desde 12 V |
| Batería LiPo | ~250 mA continuos → ~8 h. LCD necesita alimentación constante. Riesgo de hinchamiento en carcasa sellada |
| PIR HC-SR501 | Domo de 23 mm imposible de ocultar |
| Sensor ultrasónico HC-SR04 | Peor estética; cono de 15° detecta paredes, no personas |
| Sensor IR proximidad | Alcance de centímetros, no de habitación |
| Botón físico | Reemplazado por touch capacitivo del ESP32 (0 componentes) |
| Dos LEDs separados (verde + ámbar) | Reemplazados por el RGB: un solo barreno, más estados distinguibles |
| Jumpers Dupont | UTP soldado ocupa menos espacio interno |
| Google Drive como backend | OAuth2 se rompe solo con el tiempo; ata el regalo a una cuenta ajena |
| Switch de orientación / acelerómetro | Se decidió orientación nativa con barras. Reevaluar tras pruebas |

---

## Mapa de GPIOs

| Función | Pin del módulo | GPIO |
|---|---|---|
| **Display — VSPI** | | |
| Reloj | `SCL` | 18 |
| Datos | `SDA` | 23 |
| Chip select | `CS` | 5 |
| Data/Command | `DC` | 17 |
| Reset | `RST` | 16 |
| Backlight (PWM) | `BL` | 19 |
| Salida de datos | `SDA-0` | **NC** |
| Alimentación | `VIN` / `GND` | **5 V** / GND |
| **Lector SD — HSPI** | | |
| Reloj | `SCK` | 14 |
| Datos hacia SD | `MOSI` | 13 |
| Datos desde SD | `MISO` | 27 |
| Chip select | `CS` | 26 |
| Alimentación | `+5` / `GND` | **5 V** / GND |
| **BH1750 — I2C** | | |
| Datos | `SDA` | 21 |
| Reloj | `SCL` | 22 |
| Alimentación | `VCC` / `GND` | **3.3 V** / GND |
| **LED RGB (cátodo común)** | | |
| Azul (470 Ω) | pin 1 | 4 |
| Verde (470 Ω) | pin 2 | 33 |
| Común | pin 3 | GND |
| Rojo (220 Ω) | pin 4 | 25 |
| **Touch capacitivo** | | |
| Pad (T9) | — | 32 |

### Justificación de la asignación

- **GPIO12 evitado deliberadamente**, aunque es el MISO nativo del HSPI. Es pin de strapping: alto al arrancar configura la flash a 1.8 V y la placa no bootea. Como el H95 no trae buffer, MISO no queda en alta impedancia confiable → lotería en cada encendido. MISO va a GPIO27.
- **GPIO19 para backlight** aprovecha el pin liberado al no conectar `SDA-0`. Soporta PWM por LEDC.
- **GPIO0, 2 y 15 evitados** (strapping). **GPIO34-39 evitados** (solo entrada).
- **GPIO5 sí es strapping** pero necesita estar alto al arrancar — justo el estado de reposo de un CS.
- **Nota:** GPIOs 4, 13, 14, 25, 26, 27 son ADC2, que no funciona con WiFi activo. Aquí todos son digitales, sin problema. Tenerlo presente si se agrega un sensor analógico.

---

## Notas críticas de armado

### Alimentación

- **Display (`VIN`) y lector SD (`+5`) van al riel de 5 V**, no a 3.3 V. Ambos traen regulación propia. Así el AMS1117 del devkit no disipa el consumo del backlight (~0.7 W) dentro de una carcasa sellada sin ventilación.
- **En el lector SD, deja el pin `3.3V` sin conectar.** Solo se usa `+5`.
- **BH1750 va a 3.3 V.** Consume microamperes; así las líneas I2C quedan al nivel lógico del ESP32.
- **El capacitor de 100 µF no es opcional.** Entre 5 V y GND, lo más cerca posible del lector SD. Sin él, los picos de escritura hunden el riel y resetean el ESP32 — falla intermitente y difícil de diagnosticar con la carcasa cerrada.
- Un capacitor de 100 nF en VCC-GND de cada módulo (display, SD, BH1750).

### Backlight — determinar el tipo de pin `BL` antes de soldar

El pin `BL` equivale al `LED`/`BLK`/`LED-A` de otros módulos.

> **Corrección al procedimiento anterior, que era ambiguo.** La versión previa de
> esta tabla decía «solo uno funciona → respuesta inequívoca». **No es cierto:**
> dos desenlaces distintos mandan `BL` a alto y se ven idénticos en una prueba de
> sí/no, pero son circuitos muy diferentes —una entrada lógica pasa µA y un ánodo
> crudo pasa decenas de mA—. El discriminante es la **corriente**, no el
> encendido, y por eso el paso 3 lleva ahora una medición. Es la misma técnica que
> quedó escrita para el LED RGB: voltímetro sobre la resistencia e `I = V/R`.

#### Paso 0 — la lectura al aire, que es la que más dice y no cuesta nada

Solo `VIN` a 5 V y `GND`. `BL` **al aire**. Multímetro en DC volts de `BL` a GND: sus 10 MΩ de entrada pasan ~0.5 µA, así que no enciende ni daña nada.

| Lectura en `BL` al aire | Qué significa | Caso |
|---|---|---|
| ≈5 V (o ≈3.3 V) **y el backlight encendido** | entrada lógica con pull-up | **B** |
| ≈0 V, backlight apagado | entrada lógica con pull-down, o base flotante | **B** — confirmar en el paso 3 |
| **intermedia, ~1.5–3 V**, backlight apagado | hay un LED en serie hacia el riel: el nodo se queda un Vf por debajo | **A — cátodo crudo** |

La lectura intermedia es la firma de un diodo en el camino, y va antes de conectar ningún cable.

#### El paso decisivo: `BL` a un riel a través de 100 Ω, y medir la caída **sobre la resistencia**

Los 100 Ω hacen doble trabajo: limitan la corriente y sirven de shunt. `I = V/R`, la misma técnica que quedó escrita para el LED RGB. El entregable es la corriente, no el «prende sí o no».

### Resultado — pendiente #1 CERRADO, Caso B: `BL` es entrada lógica

**Medido en placa sobre el AR3952, con 100 Ω en serie:**

| Riel | V sobre los 100 Ω | Corriente | Tensión en `BL` |
|---|---|---|---|
| 5.0 V | 0.140 V | **1.40 mA** | 4.86 V |
| 3.3 V | 0.088 V | **0.88 mA** | 3.21 V |

Prende en los dos, y con un potenciómetro en serie la atenuación se distingue a simple vista.

**Por qué es inequívoco.** 1.40 mA no encienden un panel de 3.5". Si `BL` fuera el ánodo del string, la corriente del backlight —decenas de mA— pasaría por ese pin y el nodo quedaría clavado cerca del Vf, no a 0.14 V del riel. Que el panel encienda brillante mientras el pin pasa 1.4 mA significa que **la corriente del backlight sale de `VIN` por un driver del propio módulo**, y eso es la definición de una entrada de control.

**Y el Caso A queda descartado por la misma observación:** un cátodo crudo no puede encender con 5 V aplicados al pin — los LEDs quedarían sin tensión o en inversa.

**Las dos lecturas ajustan una recta y dan la impedancia de entrada:** ΔV/ΔI = (4.86 − 3.21) V / (1.40 − 0.88) mA = **3.17 kΩ**. Es un valor de catálogo para una resistencia de base, y explica la atenuación con el potenciómetro: control por corriente de base en zona activa. El ajuste deja un offset de ~0.42 V, más bajo que el Vbe de un BJT de silicio; con dos puntos de un multímetro de mano eso está en el ruido y **no se afirma si hay o no una unión ahí dentro**. Da igual para la decisión.

> **La tabla de umbrales que traía este documento se quedó corta y conviene decirlo.** Proponía «< 1 mA → entrada lógica» y «> 10 mA → ánodo», y el valor real, 1.4 mA, cayó en el hueco. El hueco no genera ambigüedad porque la pregunta de fondo es de orden de magnitud —¿es ésta la corriente del backlight o no?— y 1.4 mA está unas 70 veces por debajo de lo que consume el panel. El criterio correcto es ése, no un umbral numérico.

**Consecuencias:**

- **`BL` va directo a GPIO19**, PWM por LEDC en sentido intuitivo (`ledcWrite(pin, 0-255)`, alto = brillante). **Sin transistor, sin resistencia de base, sin el 10 kΩ.**
- **El pendiente #2 desaparece**, y con él el PN2222A del circuito del backlight. Ver abajo.
- El backlight se alimenta de `VIN` (5 V) a través del driver del módulo, que es lo que este BOM ya especificaba para el riel de 5 V.

**Lo que el voltímetro no puede contestar y queda para el banco:** si el brillo a 3.3 V iguala al de 5 V. Con 0.88 mA de excitación y una beta de 100 el driver satura ~88 mA, y con 200 llega a 176 — o sea que *debería* estar saturado, pero eso es cálculo con una beta supuesta y el criterio real es el ojo. Se resuelve al primer arranque de `[env:display]` con las teclas `9` y `0`, en la configuración de producción y no en el banco de cables.

**Tampoco se ha comprobado el arranque en frío con `BL` al aire.** Sin corriente de base el backlight debería quedar apagado, que es lo que `display.cpp` asume al poner el brillo a 0 antes de `tft.begin()`. Si resultara que enciende, hay destello a pantalla llena en cada encendido del marco mientras GPIO19 flota, y eso es regla 3.

**Notas del procedimiento**, por si hay que repetirlo con otro módulo: en cuarto oscuro y mirando el **canto** del módulo, no el frente — con el ST7796S sin inicializar el panel puede quedar opaco y el backlight solo se ve como fuga de luz en los bordes. El riel del paso 3 sale del pin del propio devkit, con el devkit alimentado, para compartir referencia de tierra. Ninguno de los pasos daña nada.

### PN2222A — fuera del backlight, pendiente #2 cerrado por irrelevancia

**El transistor no va.** `BL` resultó entrada lógica (arriba), así que no hay nada que conmutar: GPIO19 maneja el driver del módulo directamente. Con eso caen también sus 470 Ω de base y el 10 kΩ de base a emisor, y **la tercera resistencia de 470 Ω que este documento reservaba para el Caso A ya no hace falta**.

El PN2222A se queda en inventario sin uso asignado. Su pendiente de pinout —pin central = base confirmado, pero 0.704 V contra 0.703 V hacia los extremos, dentro del ruido del instrumento— **deja de importar**: se resolvería con zócalo hFE (orientación correcta → beta 100-300, invertida → beta 2-5) el día que se le encuentre otro trabajo. No es una deuda de este proyecto.

### LED RGB — cátodo común (confirmado con multímetro)

Pinout medido de izquierda a derecha: **azul, verde, GND, rojo**. El común en tercera posición no es el orden más habitual — por eso valió medirlo.

Resistencias **en serie con cada color, nunca en la pata común**. Una sola resistencia común haría que el brillo cambie según cuántos colores estén activos (el blanco saldría más tenue que el rojo). Además los voltajes directos difieren: rojo ~2.0 V, verde y azul ~3.0 V.

**Valores finales: 220 Ω en rojo, 470 Ω en verde y azul.** El rojo empezó con 1 kΩ y hubo que bajarlo; el porqué está tres párrafos más abajo y no es el que dice el cálculo.

Con cátodo común el PWM funciona en sentido intuitivo: `ledcWrite(canal, 0-255)` directo, sin invertir. El ESP32 tiene 16 canales LEDC; se asignan tres.

#### El rojo lleva 220 Ω, no 1 kΩ — y la razón contradice el cálculo

**Resuelto en placa, y el resultado fue el contrario del previsto.** El cálculo
sobre el papel decía esto, con los Vf de ficha —**asumidos, nunca medidos**— y un
GPIO en 3.3 V:

| Canal | R original | Vf asumido | Corriente calculada |
|---|---|---|---|
| Rojo | 1 kΩ | ~2.0 V | 1.3 mA |
| Verde | 470 Ω | ~3.0 V | 0.64 mA |
| Azul | 470 Ω | ~3.0 V | 0.64 mA |

De ahí se predijo que el rojo dominaría y **que el blanco saldría rosa**. Es
falso: encendiendo un canal a la vez desde el banco, **el rojo se ve claramente
más apagado** que el verde y el azul, que salen intensos.

**Por qué el cálculo no bastaba: mide corriente, y lo que importa es brillo
percibido.** Dos factores que no aparecen en la ley de Ohm y que van en la misma
dirección:

- La curva fotópica del ojo vale ~1.0 a 555 nm (verde) y cae a ~0.27 a 630 nm
  (rojo). A igual potencia radiada el verde se ve unas cuatro veces más.
- Verde y azul son InGaN, bastante más eficientes por mA que el rojo AlGaInP, y
  lo siguen siendo a corrientes bajas.

Entre los dos, el doble de corriente en el rojo no compensa ni de lejos.

**La corrección va en el rojo y es de hardware: 1 kΩ → 220 Ω**, que con el Vf
asumido lo sube de 1.3 mA a ~5.9 mA. Con eso el blanco `(255,255,255)` **se ve
blanco**, verificado a ojo. Se prefirió el hierro al software porque bajar verde y
azul con `LedRGB::escala` habría igualado los canales tirando brillo, y el
conjunto ya es tenue a propósito. `escala` se queda en identidad y sigue ahí como
perilla para el ajuste fino que haga falta tras montar la carcasa.

**El riesgo caro queda descartado:** verde y azul encienden bien e intensos, así
que la hipótesis de que 0.3 V de margen sobre el Vf los dejara inservibles era
infundada. Sus 470 Ω se quedan.

**El ámbar hubo que recalibrarlo, y su valor depende de estas resistencias.** El
`(255, 80, 0)` de la tabla de estados se eligió en el escritorio suponiendo
canales equilibrados, y sobre el hardware real tiraba a verde — el peor fallo
posible en esa fila, porque ámbar es «subiendo» y rojo es «falló» y tienen que
distinguirse de un vistazo. Barrido con la tecla `c` del banco, el valor bueno es
**`(255, 70, 0)`**. Vale para 220 Ω en el rojo y 470 Ω en el verde: **si esas
resistencias cambian, hay que volver a barrerlo.**

> **Lo que sigue sin medirse son las corrientes.** Todo lo de arriba se decidió
> por observación visual del compuesto, que es el criterio correcto para el color
> pero no da números. Las tres caídas sobre las resistencias, y el Vf real por
> diferencia contra el GPIO, siguen pendientes de multímetro.

### Conexiones del display (8 cables)

`GND`, `VIN`, `SCL`, `SDA`, `RST`, `DC`, `CS`, `BL`

**`SDA-0` (SDO/MISO) se deja SIN CONECTAR.** El flujo es unidireccional: se lee el JPEG de la SD y se empujan píxeles al display, nunca se le pregunta nada. Además la lectura por SPI en estos módulos es poco confiable (devuelve 18 bits aunque escribas 16) y muchas placas genéricas no liberan bien MISO en alta impedancia.

### Dos buses SPI separados — obligatorio

- **VSPI** → Display
- **HSPI** → Lector SD

El H95 **no trae buffer 74LVC125** (sus R1-R4 marcados "101" son solo 100 Ω de protección en serie, no level shifting). Sin buffer, MISO no se libera limpiamente cuando CS está en alto. Compartir un solo bus daría corrupción de imagen intermitente.

**Confirmación externa:** [TFT_eSPI issue #3601](https://github.com/Bodmer/TFT_eSPI/issues/3601). El ejemplo oficial `ESP32_SDcard_jpeg` falla cuando TFT_eSPI y la librería `SD` terminan en buses distintos sin declararlo explícitamente. La solución del reportante es exactamente la arquitectura de aquí: instanciar `SPIClass hspi(HSPI)` con pines dedicados y pasarlo a `SD.begin(CS, hspi)`. Cita textual del hilo: le tomó dos días encontrarlo. Vale tenerlo referenciado para no reabrir la decisión más adelante.

```cpp
SPIClass hspi(HSPI);
hspi.begin(14 /*SCK*/, 27 /*MISO*/, 13 /*MOSI*/, 26 /*CS*/);
if (!SD.begin(26, hspi)) { /* LED rojo */ }
tft.begin();   // VSPI, por defecto
```

### Touch capacitivo

El ESP32 tiene 10 pines de touch nativo (T0-T9). Un pad conductor pegado **por dentro** de la pared de la carcasa funciona a través de 1.5-2 mm de PLA. Sin sensor visible, sin botón, $0 de costo.

Opciones de pad, en orden de preferencia:

1. **Placa fenólica / perfboard** — se suelda el cable directo al cobre. Ideal.
2. **Cinta de cobre** — se suelda bien.
3. **Alambre sólido enrollado en espiral plana** — funciona y ya está en inventario.
4. **Papel aluminio de cocina** — funciona eléctricamente pero **no se puede soldar** (el óxido de aluminio lo impide). Requiere sujeción mecánica: prensado bajo cinta o con tornillo. Punto de falla dentro de carcasa cerrada.
5. **Moneda de $1** con cable sujeto con cinta.

### Cableado con UTP

- Cat5e núcleo sólido 24 AWG: sobrado para ~300 mA, suelda limpio, ocupa menos que Dupont.
- Ocho colores para codificación.
- Su debilidad (se fractura al flexionarse) es irrelevante en montaje fijo.
- **Alivio de tensión con un punto de silicón caliente cerca de cada soldadura.**
- Bonus gratis: son pares trenzados. Trenzar cada línea SPI con un retorno de tierra reduce diafonía. Probablemente imperceptible a estas frecuencias y distancias, pero no cuesta nada.

---

## Toolchain: PlatformIO

**Decisión cerrada:** PlatformIO con `framework = arduino`, **no** ESP-IDF puro.

Razón: las siete librerías del stack (`TFT_eSPI`, `TJpg_Decoder`, `WiFiManager`, `BH1750`, `QRCode`, `ESPAsyncWebServer`, `AsyncTCP`) son del ecosistema Arduino. ESP-IDF puro obligaría a reemplazarlas por `esp_lcd` + `esp_jpeg` + provisioning propio — más limpio a largo plazo pero implica reescribir el equivalente de WiFiManager, que es justo la pieza que sostiene la regla 1.

PlatformIO además fija versiones exactas en un archivo versionado, que es lo que hace el build reproducible dentro de tres años.

### Plataforma: pioarduino, no la oficial

La plataforma oficial `platformio/platform-espressif32` quedó estancada en arduino-esp32 2.x — su issue #1225 ("Support Arduino ESP32 v3.0") sigue abierto desde noviembre de 2023. El fork comunitario **pioarduino** es el que da arduino-esp32 3.x, y está activo (commits de julio 2026, actualmente Arduino 3.3.11 / IDF 5.5.5).

Ambas rutas son válidas para el ESP32 clásico. Se elige pioarduino por estar mantenida; la oficial 6.x era el plan B por si WiFiManager resultaba incompatible, y **ese plan B ya no hace falta**: el pendiente #5 se cerró el 6-ago-2026 con el provisioning verificado en placa sobre pioarduino `55.03.311`.

### `platformio.ini`

Los flags de TFT_eSPI **no viven aquí dentro**: viven en `firmware/tft_comun.ini`, sección `[tft]`, y entran por `extra_configs`. La razón está tres subsecciones más abajo, con el banco del display; el resumen es que un pin divergente haría que el banco probara otro circuito en silencio.

```ini
[platformio]
extra_configs = tft_comun.ini   ; los flags de TFT_eSPI, compartidos con el banco

[env:marco]
; Pinear a un tag de release concreto, no a "stable" — la etiqueta se mueve
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board     = esp32dev
framework = arduino

monitor_speed = 115200
board_build.partitions = huge_app.csv   ; sin OTA, todo el flash para la app

lib_deps =
    bodmer/TFT_eSPI@2.5.43
    bodmer/TJpg_Decoder@1.1.0
    claws/BH1750@1.3.0
    tzapu/WiFiManager@2.0.17
    ricmoo/QRCode@0.0.1
    ESP32Async/ESPAsyncWebServer@3.12.0
    ESP32Async/AsyncTCP@3.5.0

build_flags =
    ; --- TFT_eSPI: config por flags, NUNCA editando User_Setup.h ---
    ; Driver, geometría, pines y fuentes vienen de tft_comun.ini. Si ese archivo
    ; falta, esta línea no resuelve y el build se cae con «No section: 'tft'» —
    ; verificado. No hay forma de compilar en silencio con otros pines.
    ${tft.build_flags}
    ; Las frecuencias NO se comparten: SPI_FREQUENCY es la variable del pendiente
    ; #3 y el barrido de tres binarios del banco no debe tocar la del marco.
    -D SPI_FREQUENCY=27000000       ; arrancar aquí, subir después
    -D SPI_READ_FREQUENCY=20000000  ; sin esta línea TFT_eSPI cae a 10 MHz, no
                                    ; a SPI_FREQUENCY (TFT_eSPI.h:126)
```

Y `firmware/tft_comun.ini`, que es lo que resuelve ese `${tft.build_flags}`:

```ini
[tft]
build_flags =
    -D USER_SETUP_LOADED=1
    -D ST7796_DRIVER=1
    -D TFT_WIDTH=320
    -D TFT_HEIGHT=480
    -D TFT_MISO=-1          ; SDA-0 sin conectar
    -D TFT_MOSI=23
    -D TFT_SCLK=18
    -D TFT_CS=5
    -D TFT_DC=17
    -D TFT_RST=16
    ; BL se controla aparte con LEDC — no declararlo aquí
    -D LOAD_GLCD=1
    -D LOAD_FONT2=1
    -D LOAD_FONT4=1
    -D LOAD_GFXFF=1
    -D SMOOTH_FONT=1
    ; USE_HSPI_PORT deliberadamente ausente
```

**La extracción se verificó contra el binario, no solo compilando:** RAM 55,988 B y Flash 1,232,897 B, idénticos al byte antes y después. Si algún flag se hubiera perdido en el camino, el tamaño habría cambiado.

**`TFT_WIDTH=320` y `TFT_HEIGHT=480` son las dimensiones físicas del panel y no dependen de cómo se monte el marco.** No se tocan al cambiar la orientación. Lo que fija la orientación es `tft.setRotation(0)` en el firmware, y ese es justamente el valor nativo del ST7796S: montado vertical, el driver no aplica ninguna transformación de coordenadas. Ver §6 de la especificación funcional.

**`USER_SETUP_LOADED=1` es la clave.** Le dice a TFT_eSPI que no lea `User_Setup.h`. Sin eso, la configuración vive dentro de la carpeta de la librería y se pierde en cada actualización o al clonar el repo en otra máquina — el dolor clásico de esta librería.

**`USE_HSPI_PORT` no debe definirse.** TFT_eSPI usa VSPI por defecto, que es lo que se quiere; el HSPI se asigna a la SD manualmente.

### Notas sobre las dependencias

| Librería | Nota |
|---|---|
| `ESP32Async/ESPAsyncWebServer` | **No usar `me-no-dev/ESPAsyncWebServer`: archivado el 20-ene-2025, solo lectura.** El fork ESP32Async incluye los fixes de concurrencia de `yubox-node-org`, que es precisamente donde vivían los bugs de upload multipart. Va en 3.x; anuncian una 4.x que dropea ESP8266 e IDF 4.x → **fijar la versión** |
| `ESP32Async/AsyncTCP` | Dependencia obligatoria del anterior. Debe venir del mismo fork, no mezclar con la de me-no-dev. **Su hilo `async_tcp` corre a prioridad 10 con 16384 B de stack** (`CONFIG_ASYNC_TCP_STACK_SIZE`, `8192*2`) — ver la trampa de abajo |
| `tzapu/WiFiManager` | **Riesgo cerrado 6-ago-2026**: el panic del issue #1797 no ocurre, provisioning verificado de extremo a extremo. Dos cosas que sí salieron de ahí: `getWiFiIsSaved()` miente si se llama antes de `WiFi.mode(WIFI_STA)`, y el portal se tradujo al español con `WM_STRINGS_FILE` porque el `wm_strings_es.h` de la librería está sin traducir — pendiente #5 |
| `bodmer/TJpg_Decoder` | **No confundir con `JPEGDecoder`**, el anterior del mismo autor. El ejemplo oficial `ESP32_SDcard_jpeg` usa el viejo; la API es distinta |

Si el resolvedor de PlatformIO no encuentra los paquetes de `ESP32Async`, usar la forma con URL de git y tag:

```ini
    https://github.com/ESP32Async/ESPAsyncWebServer.git#v3.6.0
    https://github.com/ESP32Async/AsyncTCP.git
```

**Las siete versiones ya están pineadas** — las de arriba son las que resolvieron en el primer build limpio y las que están en el `platformio.ini` real. No usar `latest` en ninguna: cambia el comportamiento bajo los pies. Al actualizar cualquiera, actualizar también este bloque.

### El banco de red duplica tres de estas versiones a mano

`firmware/banco/` es un proyecto PlatformIO **aparte**, con su propio `platformio.ini`, su `src/` y su `.pio/`. Implementa el contrato HTTP de §4 sin SD y sin display, para desarrollar y medir la capa de red de la página contra el ESP32 real. No es la base del firmware final y no toca `[env:marco]`.

Repite copiadas a mano la plataforma pioarduino `55.03.311`, `ESPAsyncWebServer@3.12.0` y `AsyncTCP@3.5.0`. **Al actualizar cualquiera de las tres aquí, sincronizar allá.** Si divergen, el banco deja de probar lo que el firmware va a correr — que es el único motivo de que ese mock esté en el ESP32 y no en Python. El archivo del banco lleva el aviso en su encabezado, pero eso se lee estando ya dentro; el desfase se produce editando **este** lado.

#### `firmware/banco/` tiene cuatro envs, uno por cosa que se mide

| env | Qué prueba | GPIOs | `lib_deps` |
|---|---|---|---|
| `mock` | El contrato HTTP de §4, sin SD y sin display | ninguno | las dos async, duplicadas a mano |
| `led` | `LedRGB` contra el LED real | 25, 33, 4 | ninguna |
| `wifi` | El provisioning de WiFiManager — pendiente #5, **etapa 1** | 25, 33, 4 (el LED) | `WiFiManager`, duplicada a mano |
| `display` | El display y el BH1750 — pendientes **#1, #3 y #4** | 18, 23, 5, 17, 16, 19 · 21, 22 | `TFT_eSPI`, duplicada a mano |

`default_envs = mock`, así que `pio run` a secas sigue compilando solo el banco de
red; los otros tres se piden con `-e led`, `-e wifi` y `-e display`. Cada uno lleva
su `build_src_filter` — sin eso los cuatro `setup()` acaban en el mismo binario.

#### Los flags de TFT_eSPI son la única excepción a la copia manual

`env:display` **no copia** los flags de TFT_eSPI: los toma de `../tft_comun.ini`
con `extra_configs`, el mismo archivo que usa `[env:marco]`. Es deliberado y es la
única duplicación de este proyecto que se resolvió compartiendo. La asimetría
tiene una razón concreta:

- Una **versión de librería** divergente es **detectable**: produce otro binario y
  el encabezado de cada `.ini` lo avisa. Se acepta copiarla.
- Un **pin de TFT** divergente no se nota por ningún lado: haría que el banco
  probara **otro circuito**, en silencio. Y todo el sentido del pendiente #3 es que
  su respuesta se transfiera al marco — con otros pines no se transfiere nada.

**Tampoco hay caída silenciosa si el archivo falta**, que es la propiedad que este
proyecto ya le exige a `WM_STRINGS_FILE`. Verificado renombrándolo a mano:

```
InvalidProjectConfError: Invalid '…/banco/platformio.ini': 'No section: 'tft''
```

**Y `../tft_comun.ini` resuelve bien desde cualquier directorio**, también con
`pio run -d firmware/banco` desde la raíz del repo — comprobado en la fuente de
PlatformIO instalada, no supuesto: `run/cli.py:108` hace `with fs.cd(project_dir)`
**antes** de construir el `ProjectConfig`, y `project/config.py:119-123` resuelve
el patrón con `glob.glob` ya dentro de ese directorio. Una sección `[tft]` tampoco
emite warning: la validación salta todo scope que no sea `platformio` ni `env`
(`project/config.py:131-133`).

**`SPI_FREQUENCY` y `SPI_READ_FREQUENCY` se quedan fuera del archivo compartido, a
propósito.** La primera es justo la variable del pendiente #3, y dejarla local es
lo que hace que el barrido de tres binarios del banco no toque nunca la
configuración del marco.

**`env:led` y `env:wifi` compilan `firmware/src/LedRGB.cpp` en su sitio, sin
copiarlo** (`+<../../src/LedRGB.cpp>` en los dos). El banco tiene que probar el
mismo archivo que va a correr en el marco. La advertencia de sincronización manual
de arriba **no le aplica a `env:led`** —no usa ninguna librería— pero **sí a
`env:wifi`**, que duplica `WiFiManager@2.0.17`.

> **Los dos objetos de `LedRGB.cpp` comparten ruta, y eso ya se midió.** El `..`
> del filtro deja el objeto en `.pio/build/src/LedRGB.cpp.o`, un nivel ARRIBA del
> directorio de cada env. Como SCons firma por contenido **y flags**, y los de los
> dos envs difieren, alternar entre `-e led` y `-e wifi` **recompila** ese archivo
> — verificado, sale `Compiling .pio/build/src/LedRGB.cpp.o` al cambiar. Es un
> recompilado de un segundo, no un objeto viejo colándose.

**`env:wifi` también trae el portal en español** (`-D WM_STRINGS_FILE`), porque
tiene que probar el portal que corre en el marco y no otro. Ver la sección del
pendiente #5.

Lo que responde, y que no se puede decidir desde el escritorio: el pinout medido
del LED, la corriente real por canal (ver la nota de las resistencias, arriba), si
a igual duty los tres canales dan el mismo brillo, y si un fade lineal en duty se
**ve** lineal o hace falta gamma. Lleva además una autocomprobación de la
interpolación que corre sin mirar el LED.

#### Cómo se apunta la página al banco

**La ruta preferente es LittleFS del propio ESP32** — `pio run -t uploadfs` desde `firmware/banco/`, que sirve `web/index.html` sin copiarlo. Ahí la página y el endpoint son el **mismo origen**, que es la configuración de producción: sin CORS, sin fragmento, y `CFG.BASE` con su valor por defecto.

Para iterar rápido con la página abierta desde la Mac hay un override, y **solo funciona bajo `CFG.DIAG`**:

```
file:///…/web/index.html#marco=http://192.168.100.175
```

Va en el **fragmento** a propósito: no viaja al servidor, y por lo tanto no puede colarse como una ruta que el `onNotFound` tendría que atender. Ese camino exige compilar el banco con `-D BANCO_CORS`, que es una cabecera de banco y **no puede sobrevivir al firmware final**.

**Afinación de `SPI_FREQUENCY`:** arrancar en 27 MHz para las primeras pruebas. Con imagen estable, subir a 40 y luego a 80. Si aparecen líneas o píxeles corridos, bajar un escalón. Es empírico y depende del largo de los cables.

### Nunca escribir a la SD desde el callback de upload

El callback de upload de `ESPAsyncWebServer` **no corre en `loop()`**: corre en el hilo `async_tcp`, a prioridad 10 y con 16384 B de stack. Bloquearlo con I/O de tarjeta SD —una escritura puede tardar decenas de milisegundos cuando el bloque necesita borrado previo— provoca watchdog y conexiones caídas. Y está en la única ruta por la que entran fotos al marco.

El patrón correcto, aprovechando que los archivos llegan a 32 KB en el peor caso:

- Acumular los chunks en un buffer de heap.
- Escribir a la SD **una sola vez**, cuando `final == true`.
- **Tope duro de 64 KB.** Si se excede, responder `413` y liberar el buffer de inmediato. Sin ese tope, un cliente roto o malicioso tumba el heap del ESP32.

Si aun con una sola escritura truena el watchdog, la salida es cola de FreeRTOS + tarea dedicada de escritura y responder `202` en vez de `200`. Es más complejo y solo se paga si hace falta — no empezar ahí.

Detalle de los endpoints y sus códigos de respuesta en §4 de la especificación funcional.

---

## Datos de referencia

### Display AR3952

| Parámetro | Valor |
|---|---|
| Controlador | ST7796S |
| Panel | TFT **IPS** |
| Resolución | 320×480 nativo (2:3) — **se monta vertical**, que es la orientación nativa del panel |
| Voltaje | 3.3 / 5 VDC |
| Área de pantalla | 74.5 × 50 mm |
| PCB | 94.8 × 59.19 mm |
| Peso | 41 g |
| Pines | 9, **sin soldar** |

> **Para el diseño de la carcasa:** montado vertical, el área activa mide **50 mm de ancho × 74.5 mm de alto**. El PCB sobresale ~20 mm respecto al área visible, y de forma asimétrica, sobre el eje vertical. Ahí es donde el bisel tiene que trabajar.

> **Relación de aspecto:** montado vertical, 320×480 es exactamente **2:3**. La lógica se invierte respecto del montaje horizontal que se contemplaba antes: ahora las **verticales de celular (3:4) casi llenan la pantalla** —salen en 320×427, con 11 % del alto en negro—, y son las **horizontales las que quedan a la mitad**: una 4:3 sale en 320×240, con 50 % del alto en negro. Una vertical 2:3 de réflex encaja con recorte cero; una horizontal 3:2 de réflex es ahora el peor caso, con 56 %. Justificación completa en §6 de la especificación funcional.

### Capacidad de la SD de 8 GB

~7.4 GiB utilizables tras formatear FAT32, o sea **7,945,689,498 B** — esa es la base de la tabla, que antes no estaba escrita y no cuadraba con los números.

| Peso por foto | Fotos que caben |
|---|---|
| 25 KB | ~310,000 |
| **32 KB** (el peor caso real, ver abajo) | **~242,000** |
| 40 KB | ~194,000 |
| 60 KB | ~129,000 |

> Esta tabla es **estimación de capacidad**, no un ajuste ofrecido a quien sube fotos. El peso lo decide el firmware de la página (presupuesto de 0.213 B/píxel, ver §4 de la especificación funcional); quien usa el marco nunca elige una calidad. Pedirle esa decisión violaría la regla 1.

### El cluster de 32 KB, que es donde está lo interesante

Una SDHC de 8 GB formateada según la especificación SD usa **clusters de 32 KB**, y el presupuesto de 32,768 B de §4 coincide **exactamente** con uno. No es una coincidencia aprovechada por accidente: una foto dentro de presupuesto ocupa un cluster y ni un byte más.

El borde importa. Una foto que **tope la cota de calidad** y salga en 33 KB consume **dos clusters, 64 KB — el doble**. Con 242,000 fotos de capacidad no es un problema de espacio, pero es la razón por la que el panel de diagnóstico de la página marca en ámbar las fotos por encima del presupuesto: no es decoración, marca el cruce de esa frontera.

Consecuencia para la tabla de arriba, que mide presupuesto y no ocupación real:

| Peso por foto | En disco | Fotos que caben de verdad |
|---|---|---|
| 25 KB | 32 KB (1 cluster) | ~242,000 |
| 32 KB | 32 KB (1 cluster) | ~242,000 |
| 40 KB | 64 KB (2 clusters) | ~121,000 |
| 60 KB | 64 KB (2 clusters) | ~121,000 |

Bajar del presupuesto no compra capacidad; pasarse la corta a la mitad.

**Y el caso medido cae del lado de los dos clusters.** El peso medio real de una tanda de 30 fotos de iPhone son **34.0 KB**, por encima de los 32,768 B del presupuesto. O sea que la fila que describe la realidad de este proyecto no es la de 32 KB sino la de 40: **~121,000 fotos**, no 242,000. Sigue sin ser una restricción —son más fotos de las que nadie va a tomar— pero el número que hay que citar es ése. El piso de calidad `q = 0.620` es el régimen normal, no la excepción.

**El peor caso medido no ha dejado de subir, y por eso no se usa como cota.** Van 40.3 KB (tirol blanco), 43.0 (malla metálica perforada) y **50.7 KB** (un perro sobre césped, o sea textura fina en todo el encuadre). Las tres son el mismo fenómeno: la búsqueda binaria toca el suelo de calidad y se acepta por encima del presupuesto, que es el comportamiento correcto de §4. Cualquier constante dimensionada contra el récord del día envejece en la siguiente tanda.

> Se intentó lo obvio —igualar el búfer de retención del banco al tope duro, para que no hubiera fotos sin verificar— y **se midió que no sale a cuenta**: `mayorBloque` cae de 34804 a 16372 B tras levantar el WiFi, la mitad del bloque contiguo mayor, con AsyncTCP pidiendo memoria por conexión. Ese búfer no lo limita el tamaño de las fotos sino el heap, y es un lujo del banco que el firmware real no va a tener. Se queda en 48 KB, y una foto mayor se reporta como «no retenida», que no es corrupción.

**Y deja un margen que conviene mirar:** 50.7 KB contra los 64 KB del tope duro son **1.26×**. Por encima de ese tope el servidor responde `413` y la página no tiene salida más que descartar la foto — hoy es un riesgo teórico, pero era más teórico con el récord en 40.

**Verificar al preparar la tarjeta.** 32 KB es lo que trae de fábrica y lo que produce el SD Card Formatter oficial, pero macOS puede elegir otro tamaño al reformatear. Se confirma con `diskutil info /dev/diskNs1`, campo de tamaño de asignación.

La capacidad dejó de ser una restricción del proyecto.

### Verificado en hardware real, 2026-08-05

La tarjeta física ya se preparó y se midió, no solo se asumió de la ficha:

- **Cluster de 32,768 B confirmado.** Método usado, en Windows:

  ```powershell
  Get-CimInstance Win32_Volume -Filter "DriveLetter='F:'" |
    Select-Object DriveLetter, Label, FileSystem, BlockSize
  ```

  → `BlockSize 32768`. Coincide con lo asumido arriba. `diskutil info /dev/diskNs1` (macOS, campo Allocation Block Size) sigue siendo la alternativa válida si se reformatea desde Mac.
- **Label `MARCOFOTOS`.** Sin uso funcional en el firmware — es solo identificación de la tarjeta física frente a otras SD del inventario.
- **`/fotos/` y `.metadata_never_index` creados desde Windows**, antes de que la tarjeta tocara macOS. El segundo evita que Spotlight indexe el volumen y deje metadatos (`.Spotlight-V100`, `.fseventsd`) que el firmware tendría que filtrar en cada recorrido — ver la nota de la raíz, abajo.
- **Alineación de partición — ASUMIDA, no verificada.** La partición se reformateó con Disk Management. Si en vez de reformatear se hubiera recreado la partición, quedaría alineada a 1 MB y no al erase block de la tarjeta. Irrelevante para esta carga de trabajo (fotos de hasta 64 KB, sin escritura aleatoria de alto volumen), pero no se afirma en ningún sentido porque no se midió.

**Dos límites reales que sí importan:**

1. **No poner las fotos en la raíz.** No es por un límite de entradas: el famoso tope de 512 entradas en el directorio raíz es de **FAT16**, no de FAT32 — en FAT32 la raíz es una cadena de clusters ordinaria y crece igual que cualquier subdirectorio. Las razones reales son otras: macOS deja metadatos en la raíz del volumen (`.Spotlight-V100`, `.fseventsd`, `.Trashes`) que el firmware tendría que filtrar en cada recorrido, y conviene separar las fotos de `/manifest.txt` y de cualquier otro archivo de servicio. Las fotos van en `/fotos/`.
2. **Archivo manifiesto.** Recorrer el directorio en cada cambio de foto se vuelve lento con miles de archivos. Mantener un índice que el ESP32 lee al arrancar y actualiza solo al subir o borrar. Formato y fallback en §3 de la especificación funcional.

### BH1750

| Parámetro | Valor |
|---|---|
| Interfaz | I2C |
| Dirección | `0x23` (ADDR al aire o a GND) / `0x5C` (ADDR a VCC) |
| Rango | 1 – 65,535 lux |
| Alimentación | 3.3 V |
| Librería | `BH1750` de claws |

El rango cubre desde cuarto casi a oscuras hasta luz de día directa — exactamente la curva necesaria para control de brillo.

---

## Pendientes de confirmar (requieren hardware en mano)

| # | Pendiente | Cómo se resuelve |
|---|---|---|
| 1 | ~~Tipo de pin `BL` (Caso A o B)~~ | **CERRADO 6-ago-2026. Caso B**, entrada lógica: 1.40 mA a 5 V y 0.88 mA a 3.3 V sobre 100 Ω. GPIO19 directo, sin transistor. Arriba |
| 2 | ~~Pinout emisor/colector del PN2222A~~ | **CERRADO por irrelevancia**, mismo día: el Caso B saca al transistor del diseño. Arriba |
| 3 | Velocidad SPI máxima estable | Empírico: 27 → 40 → 80 MHz, tres binarios de `firmware/banco/` `[env:display]` |
| 4 | Dirección I2C del BH1750 | Escaneo I2C del mismo env; casi seguro `0x23` |
| 5 | ~~WiFiManager con arduino-esp32 ≥3.1.0~~ | **CERRADO 6-ago-2026.** No panica. Ver abajo |

Los dos que quedan no bloquean el desarrollo del firmware — son constantes que se ajustan al final, y los dos se miden con el mismo binario. Ya no hay ningún pendiente capaz de forzar un cambio de plataforma ni de circuito.

**Quedan dos comprobaciones visuales pegadas al #1**, que un multímetro no puede dar y que salen gratis al arrancar `[env:display]` por primera vez: que el brillo a 3.3 V iguale al de 5 V (teclas `9` y `0`), y que con `BL` al aire el backlight arranque **apagado**, sin destello mientras GPIO19 flota. Detalle arriba.

### Pendiente #5 — CERRADO, 2026-08-06. El panic no ocurre

[Issue #1797 de tzapu/WiFiManager](https://github.com/tzapu/WiFiManager/issues/1797) reportaba que con arduino-esp32 ≥3.1.0 el portal provoca `Guru Meditation Error: Core 1 panic'ed`. **Verificado en placa que no ocurre en esta combinación**, con el provisioning completo de extremo a extremo:

borrar credenciales → arrancar virgen → AP `Marco-Fotos` → portal cautivo → elegir red y guardar → conecta → **reiniciar y reconectar solo, sin levantar el AP**.

Versiones exactas probadas: `WiFiManager@2.0.17`, plataforma pioarduino `55.03.311` (arduino-esp32 3.3.11 / IDF 5.5.5), `ESP32-D0WD-V3 rev v3.1`, 4 MB de flash. Los tres planes B (`#master`, bajar a `espressif32` 6.x, portal a mano sobre `ESPAsyncWebServer` + `DNSServer`) quedan sin usar.

#### La prueba se partió en dos etapas, y por qué

Para no confundir «es la librería» con «es la presión de heap» —el confusor #1— cada etapa mide con el mismo `printf`:

| Etapa | Qué corre | Dónde |
|---|---|---|
| 1 | WiFiManager + `LedRGB`, nada más | `firmware/banco/`, `[env:wifi]` |
| 2 | La vecindad real de `setup()`: los cinco globales, con `tft.begin()`, `SD.begin()` y `lux.begin()` ya corridos | `firmware/`, `[env:marco]` — el firmware de verdad, no una copia |

Heap libre en bytes, placa con credenciales guardadas en las dos:

| Punto de medida | Etapa 1 | Etapa 2 | Δ |
|---|---|---|---|
| Arranque (solo el LED) | 282,184 | 268,540 | −13,644 |
| Justo antes de `autoConnect()` | 230,988 | 214,760 | −16,228 |
| Justo después | 229,848 | 213,608 | −16,240 |

**Confusor #1 descartado con números.** Los cuatro globales de más cuestan 13,644 B de `.bss`, y sus `begin()` solo 2,584 B encima. Con el portal realmente abierto (etapa 1, placa virgen, 9 redes escaneadas) el mínimo histórico tocó **189,108 B libres** — o sea que el portal cuesta unos 42 KB de suelo transitorio sobre un margen de más de 200 KB.

**Y `mayorBloque` se quedó en 110,580 B en las seis mediciones de las dos etapas**, sin moverse un byte. No hay fragmentación. Contrasta con el banco de red, donde el mismo indicador cae de 34,804 a 16,372 B tras levantar WiFi porque AsyncTCP pide memoria por conexión — aquí no hay nada de eso.

> **Un matiz que no se midió:** la etapa 2 corrió con credenciales ya guardadas, así que **no ejerció el portal**, solo la reconexión. Que el portal aguante bajo la presión de heap de la etapa 2 se infiere de restar los 42 KB medidos en la etapa 1 (quedarían ~172 KB), no está medido. Se cierra igualmente porque el margen es de un orden de magnitud; para medirlo habría que borrar credenciales con `env:wifi` (tecla `z`), flashear `env:marco` sin borrar la NVS y volver a provisionar desde el celular.
>
> Los otros dos confusores siguen como estaban: el **orden de inicialización** no aplica (`server.begin()` corre después de que `autoConnect()` retorna), y `asentarLed()` es landmark de bisección, no sospechoso (`delay()` es `vTaskDelay`).

#### Lo que sí salió roto: `getWiFiIsSaved()` miente antes de arrancar el driver

No es el panic, y para este proyecto es peor, porque golpea la regla 1 y **solo en el arranque que importa**.

Sobre una placa sin credenciales, `wm.getWiFiIsSaved()` devuelve **«sí»**. Medido con el código de retorno, no deducido:

```
ANTES de WiFi.mode(STA): esp_wifi_get_config=ESP_ERR_WIFI_NOT_INIT           → guardadas=sí
TRAS  WiFi.mode(STA):    esp_wifi_get_config=ESP_OK  ssid=""                 → guardadas=no
```

El mecanismo: `getWiFiIsSaved()` → `WiFi_hasAutoConnect()` → `WiFi_SSID(true)` → `esp_wifi_get_config(WIFI_IF_STA,&conf)`. Esa última devuelve `ESP_ERR_WIFI_NOT_INIT` y **no toca `conf`** si el driver no ha arrancado; WiFiManager ignora el código de retorno y construye el `String` con pila sin inicializar.

**La corrección es una línea: `WiFi.mode(WIFI_STA)` antes de preguntar**, ya puesta en `main.cpp` y en el banco.

- **No sirve fiarse de que `autoConnect()` acierta.** Acierta por otro camino: tiene su propia llamada a `getWiFiIsSaved()` **comentada** y un `wifiIsSaved = true` a pelo (`WiFiManager.cpp` 2.0.17, línea 283). El síntoma en el log era esa contradicción — nuestro «sí» seguido de su `No wifi saved, skipping`.
- **Por qué habría pasado todas las pruebas de banco:** con credenciales guardadas las dos lecturas coinciden en «sí». El fallo **solo existe en el primer arranque de una placa virgen**, que es exactamente el único arranque en el que alguien provisiona el regalo. Sin la corrección se pinta la pantalla de «conectando» en vez del QR de setup, y quien recibe el marco se queda sin nada que escanear.

#### El portal se tradujo al español, y el archivo `es` de la librería NO sirvió

`wm_strings_es.h` **viene en la librería pero no está traducido**: contenido idéntico al inglés salvo mayúsculas (`"Credentials Saved"` contra `"Credentials saved"`), encabezado «SAMPLE SAMPLE SAMPLE», versión 0.0.0. Comprobado cadena por cadena, no supuesto. **Activarlo no cambia nada — no volver a intentarlo.**

Las cadenas propias viven en **`firmware/src/wm_strings_marco.h`** y se enchufan con un build flag, que es el patrón que este proyecto ya exige para TFT_eSPI — **nunca editar la librería**, porque se pierde al actualizar y al clonar:

```ini
build_flags = -D WM_STRINGS_FILE='"wm_strings_marco.h"' -I src
```

`WiFiManager.h` hace `#include WM_STRINGS_FILE` (línea 141) con `wm_strings_en.h` por defecto. Va en los dos `.ini` —el del marco y el del banco, allá con `-I../src`— y el archivo propio incluye `wm_consts_en.h`, que trae rutas y tokens y **se queda en inglés a propósito**: son URLs, no texto visible.

**Como el `#include` es obligatorio, no existe la caída silenciosa al inglés:** si el archivo no se encuentra, el build falla. Y al actualizar WiFiManager, un símbolo nuevo rompe la compilación con «undeclared identifier» en vez de dejar un texto en inglés sin que nadie se entere.

**Verificado en el binario, no solo compilado:** `strings firmware.elf` encuentra `Conectar a tu WiFi`, `Guardar` y `Buscar otra vez`, y **no** encuentra `Configure WiFi`, `Show Password`, `Saving Credentials` ni `Available pages`. El inglés del portal no se enlaza.

##### El portal es de una sola página, y las que se quitan no son pérdida

`wm.setMenu({"wifi"})`. Eso baja la traducción de 101 símbolos a unos 15 —la mayoría del archivo es HTML, CSS y JS, que no se toca— y de paso responde a si convenía conservar la funcionalidad extra de la librería:

| Página | Por qué se quita |
|---|---|
| `update` | **No puede funcionar.** `huge_app.csv` tiene una sola partición de app (`app0`, sin `ota_1`), así que `Update.begin()` falla. Botón muerto por construcción |
| `erase` | Redundante y destructiva. Si cambian el módem, `autoConnect()` falla al arrancar y el portal vuelve a salir solo, más el toque largo de §8. Un botón que borra la configuración dentro de un regalo solo puede restar |
| `info` | ~40 filas de diagnóstico (chip id, heap, IDF, MAC): justo lo que la regla 3 prohíbe. Lo único útil de ahí —la IP— ya lo da el QR de uso diario con la IP en texto debajo |
| `restart` / `exit` / `close` | Tras guardar, WiFiManager cierra el portal por su cuenta |

Consecuencia: `/info` y `/update` **se quedan en inglés a propósito**, porque no hay enlace que lleve ahí. Y hay cadenas que **nunca** se traducen porque son protocolo y no texto: `S_debugPrefix` (el prefijo `*wm:` con el que se buscan los mensajes en el monitor serie), `S_GET`/`S_POST`, `S_options` y `S_parampre`.

Coste en flash: **−444 B**. Traducir no costó nada y vaciar `HTTP_HELP` —una tabla con la lista de rutas del portal, en inglés— devolvió más de lo que ocuparon los acentos.
