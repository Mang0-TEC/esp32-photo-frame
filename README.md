# Marco de fotos digital ESP32

Marco de fotos construido a la medida. Las fotos se suben desde el navegador del
celular a un servidor local que corre en el propio marco.

**Sin app. Sin cuenta. Sin nube. Sin suscripción.**

---

## Por qué construirlo

Un marco comercial de 10" cuesta $1,000–1,500 MXN y siempre dará más pulgadas
por peso que un proyecto casero. La electrónica hecha en casa no compite en
precio contra producción masiva.

Este proyecto se justifica por lo que el producto comercial **no** puede hacer:

- Carcasa a medida, diseñada para el mueble donde va a vivir.
- Comportamiento personalizado: brillo automático, control por toque invisible.
- Sin registro en un servicio propietario, que es lo que exigen muchos marcos
  comerciales.
- Está hecho por quien lo regala.

Costo en componentes nuevos: **$514 MXN, todo comprado y en mano** — $261 de
pantalla, lector SD y el primer sensor de luz, $126 del ESP32 de reposición y $127
del módulo TSL2561 que sustituyó al BH1750 estropeado en el banco. Sobran $186 del
presupuesto objetivo de $700.

---

## Las tres reglas

Es un regalo que vivirá permanentemente en casa de otra persona. Toda decisión
de diseño se filtra por esto:

1. **Debe funcionar sin que nadie entienda nada.** Ninguna configuración
   obligatoria, ningún manual.
2. **Debe degradar con elegancia.** Si falla el WiFi, el sensor de luz o cambia
   la red, el marco sigue mostrando fotos.
3. **Debe verse a producto, no a proyecto.** Nada de PCBs visibles, LEDs
   parpadeando ni cables expuestos.

---

## Arquitectura

```
   Celular  ──WiFi──►  ESP32 (servidor web local)  ──SPI──►  Display ST7796S
      │                       │
      │                       └──SPI──►  Tarjeta SD (almacén de fotos)
      │                       │
      │                       └──I2C──►  TSL2561 (sensor de luz)
      │
      └── Redimensionado y recorte ocurren AQUÍ, en el navegador
```

**Decisión central:** el ESP32 nunca procesa imágenes grandes. No le alcanza la
memoria para reescalar una foto de 12 megapixeles, pero al navegador del celular
sí. La foto se reduce a la orientación nativa del panel —vertical, 320×480— con
un presupuesto de bytes por densidad de 0.2133 B/px, y *eso* es lo único que se
sube. Una foto que llena la pantalla cuesta 32 KB; una con barras negras gasta
menos, en proporción.

Desde la perspectiva de quien lo usa: eligió una foto y funcionó.

---

## Hardware

| Componente | Modelo | Nota |
|---|---|---|
| Procesador | ESP32 DevKit V1 (WROOM, USB-C) | No WROVER: usa GPIO16/17 para PSRAM |
| Pantalla | ST7796S 3.5" IPS SPI, 320×480 | Bus VSPI |
| Almacenamiento | Lector SD H95 + microSD 8 GB | Bus HSPI **separado**, obligatorio. Driver `sdspi` del IDF, no la librería `SD` |
| Sensor de luz | TSL2561 (Handson `SSR1026`) por I2C, `0x39` | Brillo automático. Driver propio en `firmware/src/luz.h`, sin librería |
| Indicador | LED RGB cátodo común | Mira al frente desde la barbilla; se apaga tras 30 s |
| Control | Touch capacitivo nativo (T9) | Pad interno al canto superior, invisible |
| Alimentación | USB-C a la pared | Sin batería |
| Carcasa | PETG negro impreso: carcasa frontal, tapa trasera y base cuna a 15° | Modelo paramétrico en Fusion; ver [`carcasa/README.md`](carcasa/README.md) |

Mapa completo de GPIOs, justificación de cada pin y notas de armado en
[`docs/BOM-marco-fotos-esp32.md`](docs/BOM-marco-fotos-esp32.md).

---

## Software

- **PlatformIO** + framework Arduino, plataforma pioarduino `55.03.311`
  (arduino-esp32 3.3.11 / IDF 5.5.5).
- `TFT_eSPI` + `TJpg_Decoder` para decodificar por bloques MCU de 16×16 — por
  eso no hace falta un ESP32-S3.
- `ESP32Async/ESPAsyncWebServer` para el servidor y el upload.
- `WiFiManager` para el portal cautivo, más QR de setup en pantalla.
- La página de subida es HTML y JavaScript puros, sin dependencias externas.
- **La tarjeta se monta con el driver `sdspi` de ESP-IDF, no con la librería `SD`
  de Arduino** — ésta manda CMD59 incondicional y después exige un OCR de un CMD58
  hecho todavía en estado idle, que estas tarjetas contestan «ocupada». Medido con
  dos tarjetas. El driver del IDF se envuelve en un `fs::FS` para no perder el
  streaming sin heap, y **el objeto es `TARJETA`, no `SD`**
  (`firmware/src/tarjeta.h`).

```bash
cd firmware
pio run                  # compilar
pio run -t upload        # flashear
pio device monitor       # 115200
```

---

## Estructura

```
├── docs/            Decisiones de hardware y de diseño funcional
├── carcasa/         STL/3MF imprimibles y notas de la carcasa (PETG)
├── web/             Página de subida: redimensionado, recorte y red
│   └── test/        Arnés de regresión sobre Chrome sin cabeza
└── firmware/        Proyecto PlatformIO del marco
    └── banco/       Proyecto APARTE: mock del contrato HTTP sobre ESP32 real
```

El **banco** creció a cuatro envs: el de red implementa las cinco rutas del
contrato sin SD ni display, y los otros tres prueban el LED, el provisioning de
WiFiManager, y el display junto con la tarjeta. Sirve para medir contra hardware
de verdad. No es la base del firmware final, y duplica a mano tres versiones de
`platformio.ini` que hay que sincronizar.

---

## Estado

| Pieza | Estado |
|---|---|
| Documentos de diseño | Cerrados |
| Toolchain | Verificado, build limpio y reproducible |
| Página de subida | **Completa** — preparación, recorte y subida |
| Banco de red | Contrato HTTP corriendo sobre un ESP32 real |
| Componentes | **$514 gastados, todo en mano** — pantalla, lector SD, TSL2561 y el ESP32 definitivo. No queda nada por comprar |
| Firmware del marco | **Muestra fotos y las recibe.** Manifiesto con reconstrucción, decodificación por bloques, `/upload`, página en PROGMEM, los dos QR y brillo automático por TSL2561. Falta probarlo desde el teléfono |
| Carcasa | **Prototipo v1 modelado y exportado** (Fusion, documento «Marco Ensamble»; archivos en `carcasa/`). Falta la primera impresión de prueba y sustituir por medidas de calibrador los valores por defecto |

### Lo que ya funciona

La página es la pieza más delicada de la arquitectura y está **terminada y
empaquetada**. Medido en un iPhone 11, sobre Safari de iOS:

- Orientación EXIF **verificada, no asumida** — comparando ejes, nunca igualdad de
  píxeles.
- Escalado por *halving* progresivo, presupuesto de bytes por densidad y encoder
  con búsqueda binaria acotada. Tanda de 30 fotos en **2711 ms**, 90 ms por foto.
- Rejilla con multi-selección, cola con estado por foto, y editor de recuadro 2:3
  propio —sin dependencias— con recorte reversible.

- Subida de una en una contra `POST /upload`, con timeout, reintento de solo las
  que fallaron y verificación de integridad byte a byte. **Medida contra el banco
  sobre un ESP32 real**: tanda de 30 en 9444 ms, 315 ms por foto, 107.9 KB/s, e
  integridad byte a byte en todas. Probados en placa el `413`, el `507`, el `500`
  con reintento parcial, el timeout, el corte por WiFi caído, y un **reinicio del
  marco a media tanda** del que la página salió sin perder una sola foto.

**84 comprobaciones automatizadas** sobre Chrome sin cabeza, sin dependencias:

```bash
node web/test/correr.js
```

### Firmware del marco — qué corre y qué no

`main.cpp` ya tiene lógica real, no es un stub vacío:

- **Corre:** provisioning con WiFiManager (`autoConnect()`, **verificado en placa
  el 6-ago-2026** de extremo a extremo, con el portal en español), máquina de
  estados del LED, gesto corto/largo del touch capacitivo, brillo automático por
  TSL2561 con histéresis (umbrales 10/300 lux medidos contra la sala), y
  `showNextPhoto()` con manifiesto y decodificación por bloques.
- **Los dos QR:** el de setup (`WIFI:`, constante) y el de uso diario con la IP
  regenerada al vuelo, que sale solo tras provisionar — **verificado en placa el
  8-ago-2026** en el recorrido de placa virgen.
- **El contrato HTTP completo:** `/`, `/list`, `/photo`, `/upload` y `/delete`. Ya
  no queda ninguna ruta respondiendo fijo.
- **Queda pendiente:** levantar el AP con timeout de 5 min en el toque largo sin
  WiFi (hoy pinta el QR de setup sin levantarlo; `TODO` en `main.cpp`), y calibrar
  el umbral del touch, que sigue en un `40` inventado hasta que exista el pad.

### Lo que falta

**Con el teléfono en la mano** — es lo único que queda de esta tanda:

- **La tanda de 30 fotos desde el iPhone contra el marco.** Contra la Mac ya se
  midieron 43 subidas sin un solo watchdog ni una conexión caída, a 0.2-1.4 s por
  foto; falta el teléfono, que es el cliente real.
- **El recorrido completo de una placa virgen**: QR de setup → portal en español →
  guardar → QR de uso diario con la IP → abrir la página. Nunca se ha recorrido
  entero, y es la regla 1 de punta a punta.
- **El heap con el portal cautivo abierto**, que es el número que decide si el
  buffer de subida de 64 KB se queda donde está.
- Si el display tartamudea mientras entra una foto.

**Con los componentes en mano**, y en este orden:

1. ~~**Provisioning WiFi de extremo a extremo.**~~ **Hecho, 6-ago-2026.** Iba
   primero por ser lo único capaz de forzar un cambio de plataforma, y no lo
   fuerza: el panic del issue #1797 no ocurre con `WiFiManager@2.0.17` sobre
   arduino-esp32 3.3.11. Virgen → AP → portal → guardar → reinicio y reconecta
   solo. Los tres planes B quedan sin usar. Lo que sí salió roto fue otra cosa:
   `getWiFiIsSaved()` devuelve «sí» sobre una placa sin credenciales si se
   pregunta antes de `WiFi.mode(WIFI_STA)` — un arranque virgen se habría quedado
   sin QR de setup, que es la regla 1. Corregido.
2. ~~Pin `BL`, pinout del PN2222A, velocidad SPI y dirección I2C del sensor.~~
   **Los cinco pendientes cerrados**, y ninguno forzó un cambio de plataforma ni
   de circuito:
   - **`BL` es entrada lógica** (Caso B): 1.40 mA a 5 V sobre 100 Ω, o sea ~70×
     menos que el backlight. Va directo a GPIO19, sin transistor.
   - **El PN2222A sale del diseño** con el punto anterior, y su pinout deja de
     importar.
   - **SPI a 40 MHz.** Las tres frecuencias dieron imagen limpia, 80 MHz
     incluido; se elige por margen, no por estabilidad.
   - **Sensor en `0x39`.** El BH1750 dio `0x23` y se estropeó después; el
     8-ago-2026 se sustituyó por un **TSL2561**, que se volvió a escanear: `0x39`,
     con el pad de dirección al aire.
   - **WiFiManager no panica** con arduino-esp32 3.3.11. Lo que sí salió roto fue
     `getWiFiIsSaved()`, arriba.
3. ~~Firmware del marco: manifiesto con reconstrucción, decodificación por bloques,
   brillo por sensor de luz, toque capacitivo y QR.~~ **Hecho, 7–9-ago-2026**,
   incluido el QR de uso diario saliendo solo al terminar el provisioning —
   verificado en placa—, porque el toque largo es un gesto que nadie va a
   explicarle a quien reciba el marco. Quedan el AP con timeout del toque largo sin
   WiFi y la calibración del touch, que espera al pad.
4. ~~**Galería de lo ya cargado y `POST /delete`**~~ — **hecha, 7-ago-2026.**
   Muestra todas las fotos de la tarjeta, paginadas de 60 en 60, con
   multi-selección y una sola confirmación. El objetivo es curación, no espacio:
   caben ~121,000 fotos. Sin endpoints ni dependencias nuevas.
5. **Carcasa — en curso.** Prototipo v1 modelado en Fusion y exportado a
   `carcasa/` (9-oct-2026): PETG negro, sin soporte, cierre con gancho + 2 M3
   roscados directo, base cuna a 15°. Sigue imprimir la probeta de ajustes y el
   bisel recortado, medir con calibrador, y después la integración: soldar el UTP
   y recalibrar touch y sensor con la carcasa cerrada. Plan en
   [`carcasa/README.md`](carcasa/README.md).

Los dos documentos de `docs/` son la fuente de verdad del proyecto. Registran no
solo qué se eligió, sino **qué se descartó y por qué** — que suele ser la parte
que se olvida.