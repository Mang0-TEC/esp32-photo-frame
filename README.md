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

Costo en componentes nuevos: **$387 MXN** — $261 de pantalla, lector SD y sensor
de luz, más $126 del ESP32 de reposición.

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
      │                       └──I2C──►  BH1750 (sensor de luz)
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
| Almacenamiento | Lector SD H95 + microSD 8 GB | Bus HSPI **separado**, obligatorio |
| Sensor de luz | BH1750 (GY-302) por I2C | Brillo automático |
| Indicador | LED RGB cátodo común | Se apaga tras 30 s |
| Control | Touch capacitivo nativo (T9) | Pad interno a la carcasa, invisible |
| Alimentación | USB-C a la pared | Sin batería |

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
├── web/             Página de subida: redimensionado, recorte y red
│   └── test/        Arnés de regresión sobre Chrome sin cabeza
├── firmware/        Proyecto PlatformIO del marco
│   └── banco/       Proyecto APARTE: mock del contrato HTTP sobre ESP32 real
└── cad/             Carcasa y bisel
```

El **banco** implementa las cinco rutas del contrato sin SD ni display, para
desarrollar y medir la capa de red contra hardware de verdad. No es la base del
firmware final, y duplica a mano tres versiones de `platformio.ini` que hay que
sincronizar.

---

## Estado

| Pieza | Estado |
|---|---|
| Documentos de diseño | Cerrados |
| Toolchain | Verificado, build limpio y reproducible |
| Página de subida | **Completa** — preparación, recorte y subida |
| Banco de red | Contrato HTTP corriendo sobre un ESP32 real |
| Componentes | **$387 por comprar** — pantalla, lector SD, sensor y el ESP32 definitivo |
| Firmware del marco | Sin empezar |
| Carcasa | Sin empezar |

### Lo que ya funciona

La página es la pieza más delicada de la arquitectura y está terminada, salvo el
empaquetado. Medido en un iPhone 11, sobre Safari de iOS:

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

### Lo que falta

**Sin hardware nuevo:**

- **Empaquetado de la página**: gzip → array de C en PROGMEM. Son 30.6 KB
  gzipeados, o sea ~150 KB de fuente de C que cambia entera al tocar una línea —
  hay que decidir antes si se versiona o se genera en el build.
- **Nada más.** La medición contra el banco ya se hizo, incluida la pestaña en
  segundo plano: con el teléfono bloqueado dos minutos, el `fetch` y el reloj del
  timeout se suspenden juntos y la tanda continúa al desbloquear.

**Con los componentes en mano**, y en este orden:

1. **Provisioning WiFi de extremo a extremo.** Va primero porque es lo único que
   puede forzar un cambio de plataforma: `WiFiManager@2.0.17` sobre
   arduino-esp32 3.3.x es la combinación del issue #1797. Hay tres planes B.
2. Prueba de tres pasos del pin `BL` del display, pinout del PN2222A, velocidad
   SPI estable (27 → 40 → 80 MHz) y dirección I2C del BH1750.
3. Firmware del marco: manifiesto con reconstrucción, decodificación por bloques,
   brillo por BH1750, toque capacitivo y QR.
4. **Galería de lo ya cargado y `POST /delete`** — la única ruta del contrato que
   la página todavía no llama.
5. Integración y modelado de la carcasa.

Los dos documentos de `docs/` son la fuente de verdad del proyecto. Registran no
solo qué se eligió, sino **qué se descartó y por qué** — que suele ser la parte
que se olvida.