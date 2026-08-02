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
sí. La foto se reduce a 480×320 y ~25 KB con la API de Canvas, y *eso* es lo
único que se sube.

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
├── docs/          Decisiones de hardware y de diseño funcional
├── web/           Página de subida: redimensionado y recorte por Canvas
├── firmware/      Proyecto PlatformIO
└── cad/           Carcasa y bisel
```

---

## Estado

| | |
|---|---|
| Documentos de diseño | Cerrados |
| Toolchain | Verificado, build limpio y reproducible |
| Página de subida | Sin empezar — es la siguiente pieza |
| Firmware | Sin empezar |
| Carcasa | Sin empezar |

Los dos documentos de `docs/` son la fuente de verdad del proyecto. Registran no
solo qué se eligió, sino **qué se descartó y por qué** — que suele ser la parte
que se olvida.