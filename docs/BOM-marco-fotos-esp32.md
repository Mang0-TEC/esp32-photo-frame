# BOM — Marco de Fotos Digital ESP32

**Proyecto:** Marco de fotos digital con servidor local WiFi
**Presupuesto objetivo:** $700 MXN
**Costo real a comprar:** $387 MXN

---

## A. Comprar

| # | Componente | SKU | Proveedor | Cant. | Precio |
|---|---|---|---|---|---|
| 1 | Módulo TFT ST7796S 3.5" IPS 320×480 SPI | AR3952 | Unit Electronics | 1 | $196 |
| 2 | Lector de Memorias SD H95 | AR0532 | Unit Electronics | 1 | $23 |
| 3 | GY-302 Sensor de intensidad luminosa BH1750 | — | Unit Electronics | 1 | $42 |
| 4 | ESP32 DevKit V1 **WROOM**, USB-C | — | Por definir | 1 | $126 |
| | **Subtotal** | | | | **$387** |

**Por qué el ESP32 está aquí y no en inventario:** la unidad USB-C que había
resultó defectuosa — no transmite ni sincroniza con esptool, ni en macOS ni en
Windows, pese a 3.3 V correctos, `EN` liberado y loopback TX0↔RX0 exitoso.
Diagnóstico cerrado, no se reabre. Es reposición de una unidad fallada, no un
componente que faltara en el diseño original: de ahí que el presupuesto pasara
de $261 a $387.

**Debe ser WROOM, no WROVER.** La WROVER ocupa GPIO16 y GPIO17 con la PSRAM, y
ahí van `RST` y `DC` del display.

---

## B. Ya en inventario

| # | Componente | Nota |
|---|---|---|
| 5 | ESP32 DevKit WROOM, **micro-USB** | **Placa de desarrollo.** Puente CP2102, `/dev/cu.usbserial-0001`. Chip verificado `ESP32-D0WD-V3 rev v3.0`, cristal 40 MHz, flash 4 MB |
| 6 | Tarjeta SD 8 GB | FAT32, fotos en subdirectorio `/fotos/` |
| 7 | Cargador USB 5 V ≥1 A + cable USB-C | Alimentación permanente |
| 8 | Capacitor electrolítico 100 µF / 16 V | ✔ Confirmado |
| 9 | Capacitores cerámicos 100 nF (código "104") | ✔ Confirmado — 3 piezas |
| 10 | Transistor PN2222A (NPN, TO-92) | ✔ Sustituye al 2N7000 |
| 11 | LED RGB 5 mm **cátodo común** | ✔ Sustituye a los dos LEDs separados |
| 12 | Resistencias 470 Ω ×2, 1 kΩ, 10 kΩ | ✔ Confirmado |
| 13 | Header macho 2.54 mm (9 pines) | ✔ Confirmado |
| 14 | Cable UTP Cat5e (núcleo sólido 24 AWG) | ✔ Sustituye jumpers Dupont |
| 15 | Filamento PLA | Carcasa y bisel |

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
| Rojo (1 kΩ) | pin 4 | 25 |
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

El pin `BL` equivale al `LED`/`BLK`/`LED-A` de otros módulos. Con solo `VIN` a 5 V y `GND` conectados:

| Prueba | Resultado | Conclusión |
|---|---|---|
| `BL` al aire | Prende | Tiene pull-up, activo por defecto |
| `BL` a 3.3 V con 100 Ω en serie | Prende | **Caso B:** entrada lógica → GPIO directo, **sin transistor** |
| `BL` a GND directo | Prende | **Caso A:** cátodo crudo → va el PN2222A |

Solo uno funciona → respuesta inequívoca. Ninguna prueba daña nada (la placa trae resistencia limitadora en serie).

**Si resulta Caso A** — circuito con PN2222A:
- Emisor → GND
- Colector → pin `BL` del display
- Base → GPIO19 a través de **470 Ω**
- **10 kΩ de base a emisor** (evita que flote durante el arranque y el backlight parpadee)

> Cálculo: (3.3 V − 0.7 V) ÷ 5 mA ≈ 520 Ω → 470 Ω comercial. Da ~5.5 mA de base; con beta forzado ~20 satura los 100 mA de colector.

### PN2222A — pinout PENDIENTE de confirmar

Medición realizada: pin central = base (NPN confirmado). Pero las lecturas hacia los extremos dieron **0.704 V y 0.703 V** — 1 mV de diferencia está dentro del ruido del instrumento y **no permite distinguir emisor de colector**. La regla del "emisor lee más alto" requiere 20-50 mV de diferencia.

Dos formas de resolverlo:

1. **Zócalo hFE del multímetro** (agujeros marcados EBCE), si lo tiene. Inserta en una orientación, anota, invierte emisor y colector, compara. Orientación correcta → beta 100-300. Invertida → beta 2-5. Inequívoco.
2. **Prueba empírica.** No se daña nada al conectarlo al revés. Arma con E-B-C (estándar en PN2222A con cara plana de frente) y prueba el backlight. Si sale a media luz, invierte las patas de los extremos.

### LED RGB — cátodo común (confirmado con multímetro)

Pinout medido de izquierda a derecha: **azul, verde, GND, rojo**. El común en tercera posición no es el orden más habitual — por eso valió medirlo.

Resistencias **en serie con cada color, nunca en la pata común**. Una sola resistencia común haría que el brillo cambie según cuántos colores estén activos (el blanco saldría más tenue que el rojo). Además los voltajes directos difieren: rojo ~2.0 V, verde y azul ~3.0 V.

Con cátodo común el PWM funciona en sentido intuitivo: `ledcWrite(canal, 0-255)` directo, sin invertir. El ESP32 tiene 16 canales LEDC; se asignan tres.

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

Ambas rutas son válidas para el ESP32 clásico. Se elige pioarduino por estar mantenida; la oficial 6.x queda como plan B si aparece incompatibilidad (ver pendiente #5).

### `platformio.ini`

```ini
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
    -D SPI_FREQUENCY=27000000       ; arrancar aquí, subir después
    -D SPI_READ_FREQUENCY=20000000
    ; USE_HSPI_PORT deliberadamente ausente
```

**`TFT_WIDTH=320` y `TFT_HEIGHT=480` son las dimensiones físicas del panel y no dependen de cómo se monte el marco.** No se tocan al cambiar la orientación. Lo que fija la orientación es `tft.setRotation(0)` en el firmware, y ese es justamente el valor nativo del ST7796S: montado vertical, el driver no aplica ninguna transformación de coordenadas. Ver §6 de la especificación funcional.

**`USER_SETUP_LOADED=1` es la clave.** Le dice a TFT_eSPI que no lea `User_Setup.h`. Sin eso, la configuración vive dentro de la carpeta de la librería y se pierde en cada actualización o al clonar el repo en otra máquina — el dolor clásico de esta librería.

**`USE_HSPI_PORT` no debe definirse.** TFT_eSPI usa VSPI por defecto, que es lo que se quiere; el HSPI se asigna a la SD manualmente.

### Notas sobre las dependencias

| Librería | Nota |
|---|---|
| `ESP32Async/ESPAsyncWebServer` | **No usar `me-no-dev/ESPAsyncWebServer`: archivado el 20-ene-2025, solo lectura.** El fork ESP32Async incluye los fixes de concurrencia de `yubox-node-org`, que es precisamente donde vivían los bugs de upload multipart. Va en 3.x; anuncian una 4.x que dropea ESP8266 e IDF 4.x → **fijar la versión** |
| `ESP32Async/AsyncTCP` | Dependencia obligatoria del anterior. Debe venir del mismo fork, no mezclar con la de me-no-dev. **Su hilo `async_tcp` corre a prioridad 10 con 16384 B de stack** (`CONFIG_ASYNC_TCP_STACK_SIZE`, `8192*2`) — ver la trampa de abajo |
| `tzapu/WiFiManager` | Ver pendiente #5. Riesgo abierto con arduino-esp32 ≥3.1.0 |
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
| 1 | Tipo de pin `BL` (Caso A o B) | Prueba de tres pasos, arriba |
| 2 | Pinout emisor/colector del PN2222A | Zócalo hFE, o prueba empírica del backlight |
| 3 | Velocidad SPI máxima estable | Empírico: 27 → 40 → 80 MHz |
| 4 | Dirección I2C del BH1750 | Escaneo I2C; casi seguro `0x23` |
| 5 | **WiFiManager con arduino-esp32 ≥3.1.0** | Ver abajo. Prueba de provisioning de extremo a extremo |

Los primeros cuatro no bloquean el desarrollo del firmware — son constantes que se ajustan al final. El quinto sí puede obligar a bajar de plataforma.

### Pendiente #5 — riesgo abierto de WiFiManager

[Issue #1797 de tzapu/WiFiManager](https://github.com/tzapu/WiFiManager/issues/1797): con arduino-esp32 3.1.0 o posterior, `wm.startConfigPortal()` provoca `Guru Meditation Error: Core 1 panic'ed` y reinicia el equipo. Con 3.0.7 o anterior funciona.

**No verificado.** El issue es de febrero de 2025 y la librería tuvo actualización en febrero de 2026, así que probablemente ya está corregido. Dos matices que reducen el riesgo:

- El flujo de este proyecto usa `autoConnect()` (arranque sin credenciales), no `startConfigPortal()` explícito. Puede que ni aplique.
- pioarduino stable trae arduino-esp32 3.3.x, muy por encima de la versión del reporte.

**Cómo se resuelve:** probar el provisioning completo (borrar NVS → arrancar → AP `Marco-Fotos` → portal → guardar credenciales → reconectar) **antes** de integrar nada más. Si truena, hay dos salidas: bajar a la plataforma oficial `espressif32` 6.x (arduino-esp32 2.0.17), o escribir el portal cautivo a mano sobre `ESPAsyncWebServer` + `DNSServer`, que ya están en el proyecto.

Es la única dependencia que puede forzar un cambio de plataforma. Por eso se prueba primero.
