# Especificación Funcional — Marco de Fotos Digital ESP32

Documento de decisiones de diseño y comportamiento del sistema.
Complementa a `BOM-marco-fotos-esp32.md` (hardware y conexiones).

---

## 1. Contexto y objetivo

Marco de fotos digital construido a la medida, destinado a ser **un regalo** que vivirá permanentemente en **casa de otra persona**.

### Por qué construirlo en vez de comprarlo

Un marco comercial de 10" cuesta $1,000–1,500 MXN y siempre dará más pulgadas por peso que un proyecto DIY. La electrónica hecha en casa no compite en precio contra producción masiva. Este proyecto se justifica por lo que el producto comercial **no** puede hacer:

- Carcasa a medida, diseñada para el mueble donde va a vivir.
- Comportamiento personalizado (brillo automático, control por toque invisible).
- **Sin app de nube, sin cuenta, sin suscripción.** Muchos marcos comerciales exigen registrarse en un servicio propietario.
- Está hecho por quien lo regala.

Costo final: **$261 MXN** en componentes nuevos, contra $1,000–1,500 del comercial.

### Restricciones de diseño derivadas del contexto

Como es un regalo que vivirá lejos de quien lo construyó, todas las decisiones se filtran por tres reglas:

1. **Debe funcionar sin que nadie entienda nada.** Ninguna configuración obligatoria, ningún manual.
2. **Debe degradar con elegancia.** Si falla el WiFi, el sensor de luz o la red cambia, el marco sigue mostrando fotos.
3. **Debe verse a producto, no a proyecto.** Nada de PCBs visibles, LEDs parpadeando ni cables expuestos.

---

## 2. Arquitectura general

```
   Celular  ──WiFi──►  ESP32 (servidor web local)  ──SPI──►  Display ST7796S
      │                       │
      │                       └──SPI──►  Tarjeta SD (almacén de fotos)
      │                       │
      │                       └──I2C──►  BH1750 (sensor de luz)
      │
      └── Redimensionado y recorte ocurren AQUÍ, en el navegador
```

**Decisión central:** el ESP32 nunca procesa imágenes grandes. Recibe archivos ya listos para mostrar.

---

## 3. Almacenamiento

### Decisión: tarjeta SD local, no nube

Se evaluó Google Drive como backend y **se descartó** por tres razones:

1. La API requiere OAuth2 con refresh de tokens. Ese token expira o Google cambia algo, y el regalo se rompe solo cuando quien lo construyó no está ahí para arreglarlo.
2. Drive entrega la foto en resolución original — regresa el problema del reescalado, ahora sin navegador que lo resuelva.
3. Ataría el regalo a la cuenta de Google de quien lo regala, permanentemente.

**El camino que siempre funciona es el local.** Sincronización con nube queda como posible mejora futura, encima de la base local, nunca sustituyéndola.

### Distinción importante

"Dónde viven los archivos" y "cómo se cargan" son cosas distintas. La SD va **fija dentro de la carcasa** y nadie la saca nunca; las fotos se cargan por WiFi desde el celular. No hay que elegir entre las dos cosas.

### Organización en la tarjeta

| Aspecto | Decisión |
|---|---|
| Formato | FAT32 |
| Ubicación de fotos | `/fotos/` — **nunca en la raíz** |
| Índice | Archivo manifiesto con la lista de fotos |
| Capacidad práctica | ~190,000 fotos a 40 KB (deja de ser restricción) |

**Por qué no la raíz:** FAT32 limita el directorio raíz a 512 entradas, y los nombres largos consumen varias cada uno. Truena alrededor de las 150-200 fotos, de forma silenciosa.

**Por qué el manifiesto:** recorrer el directorio en cada cambio de foto se vuelve lento con miles de archivos. El ESP32 lee el índice al arrancar y solo lo actualiza al subir o borrar.

---

## 4. Carga de fotos

### Servidor web local en el ESP32

La persona entra desde el navegador de su celular, ve las fotos que ya están, sube nuevas y borra las que no quiere. **Sin apps, sin cuentas, sin internet.**

### Pieza clave: redimensionado del lado del navegador

El ESP32 no puede reescalar una foto de 12 megapixeles — no le alcanza la memoria ni de cerca. El navegador del celular sí puede, con la API de Canvas.

**Flujo:**

1. La persona selecciona una foto de su galería.
2. El navegador la reduce para caber en 480×320 respetando proporción.
3. La reencodea como JPEG de ~25 KB.
4. **Eso** es lo único que se sube al ESP32.

Desde la perspectiva de quien lo usa, simplemente eligió una foto y funcionó. Esta pieza es la que hace que toda la arquitectura se sostenga, y es la más delicada del proyecto.

### Dos trampas del escalado por Canvas

**1. Reducir en pasos, no de un jalón.** Un `drawImage` directo de 4032×3024 a 480×320 produce aliasing visible: el navegador usa un filtro bilineal con pocas muestras y se come el detalle fino. El patrón correcto es reducir a la mitad iterativamente hasta acercarse al destino, y ahí sí hacer el paso final.

```js
// Halving progresivo hasta que el siguiente paso pasaría del objetivo
while (w / 2 >= targetW && h / 2 >= targetH) {
  ctx.drawImage(src, 0, 0, w, h, 0, 0, w / 2, h / 2);
  w /= 2; h /= 2;
  src = canvas;   // el canvas se vuelve la fuente del siguiente paso
}
```

En una pantalla de 3.5" con fotos de gente, la diferencia se nota en pelo y texturas finas. Es lo que resuelve la librería `pica` internamente; aquí son ~15 líneas propias y se evita la dependencia.

**2. Orientación EXIF.** Las fotos de iPhone traen tag de rotación. Un `<img>` en navegador moderno lo aplica solo; `createImageBitmap()` **no**, salvo que reciba `imageOrientation: 'from-image'`. Mezclar ambos caminos hace que unas fotos salgan de lado y otras no.

**No verificado** — hay que probarlo empíricamente en Safari de iOS, que es donde va a subir fotos la persona destinataria. Primer caso de prueba de `web/test/`: una foto vertical tomada con iPhone, sin editar, sin pasar por WhatsApp.

### Sobre librerías de recorte

`Cropper.js` pesa ~12 KB minificado+gzip y cubre el caso. Dos trampas: la v2.x es una reescritura con API incompatible respecto de la 1.6.x (que es de lo que habla casi toda la documentación en línea), y `Croppr.js` — la alternativa de <6 KB — está abandonada en su repo original.

**Decisión: escribir el recuadro de recorte a mano.** El tamaño no es la restricción real (12 KB en 4 MB de flash no mueven la aguja); el argumento es no meter una dependencia externa en la pieza más delicada del proyecto, para un objeto que debe seguir funcionando dentro de cinco años sin mantenimiento.

### Recorte manual con vista previa

Las fotos de celular vienen en 4:3 o 16:9 contra una pantalla de 3:2. El recorte automático al centro funciona la mayoría de las veces pero decapita gente en las verticales.

Como el redimensionado ya ocurre en el navegador, se agrega un **recuadro de recorte arrastrable con vista previa**. Poco código extra y la persona ve exactamente cómo va a quedar antes de subir.

---

## 5. Conectividad

### Servidor web: `ESP32Async/ESPAsyncWebServer`

**El repositorio original de `me-no-dev` está archivado desde el 20 de enero de 2025 y es de solo lectura.** El sucesor mantenido por la comunidad es `ESP32Async/ESPAsyncWebServer`, que incorpora los fixes de concurrencia del fork de `yubox-node-org`.

Esto importa aquí en concreto: **el upload multipart es exactamente donde vivían esos bugs**, y el upload multipart es el único camino por el que entran fotos al marco. Arrancar con el repo archivado sería empezar con código congelado hace año y medio en la ruta crítica.

Versiones y sintaxis exacta de `lib_deps` en el BOM.

### Portal cautivo obligatorio (WiFiManager)

Como el marco vivirá en casa ajena, **no se pueden dejar credenciales en el firmware**.

> **Riesgo abierto:** el issue #1797 de WiFiManager reporta un `Guru Meditation Error` al levantar el portal con arduino-esp32 ≥3.1.0. Probablemente ya corregido, pero **no verificado**. Es la única dependencia capaz de forzar un cambio de plataforma, así que el provisioning se prueba de extremo a extremo antes de integrar nada más. Detalle y planes B en el pendiente #5 del BOM.

**Flujo de primer arranque:**

1. El marco no encuentra red conocida.
2. Levanta su propio AP: `Marco-Fotos`.
3. Muestra QR de setup en pantalla.
4. La persona lo escanea → se conecta → se abre el portal cautivo.
5. Elige su WiFi y escribe su contraseña.
6. Se guarda en NVS. No vuelve a preguntar.

### Regla de oro: el marco debe funcionar sin WiFi

Si la red se cae, cambian el módem, o nunca lo llegan a configurar, **las fotos de la SD siguen pasando**.

> El WiFi es solo para subir fotos, nunca para mostrarlas. Un regalo que se queda en negro porque cambió el internet es un regalo roto.

### Códigos QR en pantalla

Dos QRs distintos para dos momentos distintos:

| QR | Cuándo aparece | Qué codifica |
|---|---|---|
| **Setup** | Automático, sin credenciales guardadas | `WIFI:S:Marco-Fotos;T:WPA;P:fotos1234;;` |
| **Uso diario** | Toque largo en el pad | `http://192.168.1.47` (IP actual) |

El formato `WIFI:` es reconocido nativamente por Android e iOS: al escanearlo con la cámara se conectan solos. **La persona nunca ve una contraseña.**

**El QR de uso diario debe regenerarse al vuelo**, leyendo la IP actual. Los routers domésticos reasignan por DHCP, así que la IP de hoy puede no ser la de la próxima semana.

**Requisitos de implementación:**

- Librería `QRCode` de ricmoo (C puro, ~2 KB de RAM al generar).
- QR versión 3 con corrección baja cubre 53 caracteres — suficiente para ambos casos.
- 29×29 módulos a escala 8 = 232 px. Centrado en 480×320 se escanea al instante desde medio metro.
- **Margen blanco de al menos 4 módulos alrededor** (zona tranquila del estándar). Sin él, muchos escáneres no lo detectan — es la causa número uno de QRs que no funcionan.
- **Mostrar la IP en texto legible debajo del QR.** Cuesta una línea y cubre a quien prefiera teclearla.

---

## 6. Visualización

### Orientación: horizontal, con imagen en su proporción nativa

El marco se monta **horizontal (480×320)**. El soporte de la carcasa permitirá rotarlo físicamente si se quiere.

**Decisión sobre fotos verticales:** se muestran completas y centradas, con negro a los costados (pillarbox). Se ve intencional si el bisel es negro.

Se evaluaron y **pospusieron** dos alternativas, a reevaluar tras las primeras pruebas visuales:

- Playlists separadas por orientación, con detección por switch SW-520D o acelerómetro.
- Cambio manual de modo por toque largo.

### Decisión de implementación que preserva la reversibilidad

**Las barras negras NO se hornean dentro del JPEG.**

- El navegador escala la foto para caber en 480×320 respetando proporción. Una vertical de celular queda en 240×320.
- El ESP32 pinta la pantalla de negro y dibuja la imagen centrada, calculando el offset.

Si las barras se metieran al archivo, cambiar de estrategia después obligaría a resubir todas las fotos. Así el archivo guarda su recorte nativo y el comportamiento vive en el firmware, cambiable con un flash.

### Decodificación

`TFT_eSPI` (Bodmer) + `TJpg_Decoder`. Trabaja en bloques MCU de 16×16 empujados directo por SPI.

Consumo real ≈ buffer del JPEG (25-40 KB) + ~4 KB de workspace. **Por esto no hace falta un ESP32-S3**: nunca se arma el framebuffer completo de 300 KB.

### Sobre el ejemplo oficial `ESP32_SDcard_jpeg`

Sirve como referencia del patrón — el callback `tft_output(x, y, w, h, bitmap)` que empuja bloques MCU directo al display es la estructura correcta. **Pero no copiarlo tal cual**, tiene dos cosas que este proyecto ya descartó:

- Usa `JPEGDecoder`, el decodificador viejo de Bodmer, no `TJpg_Decoder`. API distinta.
- Exige que las imágenes estén en la **raíz** de la SD. Justo el fallo silencioso de las 150-200 fotos documentado arriba.

**No existe hoy un repositorio público con esta arquitectura** (redimensionado en navegador + servidor local + SD fija). Lo más cercano encontrado, `TylerSaccomanno/ESP32-WebPhotoFrame`, usa el mismo ST7796 pero jala imágenes de un servidor Flask en una Raspberry Pi — arquitectura opuesta, y requeriría dejar una Raspberry prendida en casa ajena. Viola las tres reglas de golpe.

---

## 7. Control de brillo

### Automático por BH1750

La lógica es la de un celular:

| Condición | Brillo |
|---|---|
| Cuarto iluminado | 100 % |
| Luz tenue de noche | ~20 % |
| Oscuridad sostenida (varios minutos) | Backlight apagado |

**Esto resuelve el objetivo original sin necesitar detección de presencia:** si el cuarto está oscuro, nadie está viendo el marco.

### Degradación elegante

Si el BH1750 falla, la pantalla se queda en **brillo fijo**, no apagada. Nunca debe quedarse en negro por un sensor descompuesto.

### Por qué no un sensor de presencia

| Opción evaluada | Por qué se descartó |
|---|---|
| PIR HC-SR501 | Domo blanco de 23 mm, imposible de ocultar |
| Ultrasónico HC-SR04 | Dos cilindros expuestos; cono de 15° detecta la pared de enfrente, no personas |
| IR de proximidad | Alcance de centímetros — detecta una mano, no una persona en el cuarto |
| Receptor IR + control remoto | Obliga a la persona a cuidar un controlito. Carga inaceptable en un regalo |
| **LD2410 mmWave** | **Descartado por ahora, no por malo.** Atraviesa plástico (montaje invisible), detecta micro-movimiento. Mejora futura si el marco resulta quedarse prendido de más |

---

## 8. Control por toque

### Touch capacitivo nativo del ESP32

Pad conductor pegado **por dentro** de la pared de la carcasa. Funciona a través de 1.5-2 mm de PLA.

**Sin sensor visible. Sin botón. $0 de costo. Un GPIO.**

| Gesto | Acción |
|---|---|
| Toque corto | Siguiente foto |
| Toque largo | Mostrar QR con la IP para subir fotos |

Para un objeto que debe sentirse a producto terminado, esto supera a cualquier botón físico.

---

## 9. Indicador de estado (LED RGB)

Un solo LED RGB de cátodo común sustituye a los dos LEDs fijos originalmente planeados: un solo barreno en la carcasa y más estados distinguibles.

| Color | Estado |
|---|---|
| Azul pulsante | Arrancando / conectando a WiFi |
| Verde | Conectado y operando |
| Ámbar | Escribiendo en SD / subida en progreso |
| Rojo | Error (SD no montada, WiFi caído) |
| **Apagado** | **Operación normal, tras 30 s** |

**El rojo es la ganancia real:** con dos LEDs fijos no había forma de distinguir "error de SD" de "error de WiFi" ni de mostrar arranque en progreso.

### Debe apagarse tras el arranque

Un punto de color respirando en la sala de noche es exactamente lo que no se quiere en un regalo. **Los LEDs sirven durante el armado y la depuración**, no en operación normal. El estado de WiFi se comunica mejor con un icono discreto en la propia pantalla.

Resistencias de valor alto (470 Ω / 1 kΩ) para que queden tenues, y montaje en cara trasera o inferior.

---

## 10. Alimentación

### Conectado a la pared, sin batería

**Consumo:** backlight ~100-150 mA continuos + ESP32 con WiFi ~80-160 mA ≈ **250 mA constantes**. Una LiPo de 2000 mAh daría ~8 horas.

**No se puede rescatar con deep sleep:** un LCD necesita alimentación continua para mostrar cualquier cosa. (Es la razón por la que los proyectos con batería que se ven en internet son casi siempre de tinta electrónica, que retiene la imagen con cero consumo.)

**Riesgo adicional:** una LiPo dentro de una carcasa impresa y sellada se hincha con los años, en un objeto que estará prendido 24/7 sobre un mueble.

**Conclusión:** un marco de fotos no es portátil. Vive en un solo lugar, junto a un contacto. Un cable USB-C a la pared, y el diseño de la carcasa se simplifica bastante.

---

## 11. Resumen de decisiones

| Decisión | Elegido | Descartado |
|---|---|---|
| Procesador | ESP32 clásico (USB-C, ya en inventario) | ESP32-S3 (innecesario con decodificación por bloques) |
| Pantalla | ST7796S 3.5" IPS SPI | Nextion (25 fotos precompiladas), all-in-one 5" (presupuesto) |
| Almacenamiento | Tarjeta SD local | Google Drive / nube |
| Carga de fotos | Servidor web local + Canvas | App, SD extraíble, USB |
| Redimensionado | En el navegador del celular | En el ESP32 (imposible) |
| Provisioning WiFi | Portal cautivo + QR | Credenciales en firmware |
| Orientación | Horizontal, proporción nativa con barras | Rotación automática, playlists |
| Barras negras | Calculadas en firmware | Horneadas en el JPEG |
| Brillo | Automático por BH1750 | Sensor de presencia (PIR/ultrasónico) |
| Control manual | Touch capacitivo invisible | Botón físico, control remoto IR |
| Estado | LED RGB, apagado tras 30 s | Dos LEDs fijos permanentes |
| Alimentación | USB-C a la pared | Batería LiPo |
| Toolchain | PlatformIO + framework Arduino | ESP-IDF puro (obligaría a reescribir WiFiManager) |
| Plataforma PIO | pioarduino (arduino-esp32 3.x, activa) | `platformio/platform-espressif32` oficial, estancada en 2.x |
| Servidor web | `ESP32Async/ESPAsyncWebServer` | `me-no-dev/…`, archivado ene-2025 |
| Recorte en navegador | Código propio | Cropper.js (API rota entre v1/v2), Croppr.js (abandonada) |
| Config de TFT_eSPI | `build_flags` con `USER_SETUP_LOADED` | Editar `User_Setup.h` en la carpeta de la librería |

---

## 12. Orden de trabajo sugerido

### Se puede hacer HOY, sin hardware

1. **Página web con redimensionado por Canvas.** Es HTML y JavaScript puros — se abre en el navegador de escritorio, se sube una foto, se verifica que salga un JPEG de 480×320 y ~25 KB. Es la pieza más delicada del proyecto y puede quedar terminada y probada antes de que llegue el paquete. Casos de prueba mínimos: foto 4:3, 16:9, vertical de iPhone sin editar (EXIF), y una de 12 MP para medir el tiempo de escalado en celular.
2. **Repositorio en GitHub** con los dos documentos, `web/`, y GitHub Pages activado — sirve la página por HTTPS y permite probarla desde el celular real, no solo desde escritorio.
3. **Instalar PlatformIO** y armar el `platformio.ini` del BOM. Compila sin placa conectada; valida que todas las dependencias resuelvan.
4. Formatear la SD en FAT32 y crear `/fotos/`.

### Requiere los componentes en mano, en este orden

5. **Provisioning de extremo a extremo (pendiente #5).** Va primero porque es lo único que puede forzar un cambio de plataforma. Todo lo demás se construye encima.
6. Prueba de tres pasos del pin `BL`.
7. Confirmar pinout del PN2222A (zócalo hFE).
8. Afinar velocidad SPI (27 → 40 → 80 MHz).
9. Confirmar dirección I2C del BH1750.
10. Integración y modelado de la carcasa.
