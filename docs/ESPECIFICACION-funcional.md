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

Costo final: **$387 MXN** en componentes nuevos, contra $1,000–1,500 del comercial. Son $261 de pantalla, lector SD y sensor de luz, más $126 del ESP32 de reposición — la unidad USB-C que había en inventario resultó defectuosa.

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
| Nombres | `NNNNNNNN.JPG` — ocho dígitos, contador monótono en NVS |
| Índice | `/manifest.txt` en la raíz, un nombre por línea |
| Capacidad práctica | ~190,000 fotos a 40 KB (deja de ser restricción) |

**Por qué no la raíz.** No es por un límite de entradas: el famoso tope de 512 entradas en el directorio raíz es de **FAT16**, no de FAT32 — en FAT32 la raíz es una cadena de clusters ordinaria y crece dinámicamente igual que cualquier subdirectorio. Las razones reales son dos:

- macOS deja metadatos en la raíz del volumen (`.Spotlight-V100`, `.fseventsd`, `.Trashes`) que el firmware tendría que filtrar en cada recorrido.
- Conviene separar las fotos de `/manifest.txt` y de cualquier otro archivo de servicio.

### Nombres de archivo: `NNNNNNNN.JPG`

Ocho dígitos con ceros a la izquierda, de un contador monótono guardado en NVS. `00000042.JPG`.

- Es **8.3 estricto**, así que consume una sola entrada de directorio en vez de las varias que gasta un nombre largo.
- El orden de reproducción es el de subida, sin necesidad de leer fechas.
- El techo son 99,999,999 fotos.
- **El contador no retrocede al borrar.** Reutilizar números daría colisiones con fotos que aún estén en el manifiesto.

### Manifiesto: `/manifest.txt`

Texto plano, un nombre de archivo por línea. Va en la raíz precisamente para que no se liste a sí mismo al recorrer `/fotos/`.

**Por qué no JSON:** el ESP32 no necesita parser, agregar una foto es un append puro, y el endpoint que lo sirve puede hacer streaming del archivo tal cual, sin construir nada en heap.

**Por qué el manifiesto:** recorrer el directorio en cada cambio de foto se vuelve lento con miles de archivos. El ESP32 lee el índice al arrancar y solo lo actualiza al subir o borrar.

**Fallback obligatorio — lo exige la regla 2.** Si el manifiesto falta, viene vacío, o apunta a archivos que ya no existen, el firmware recorre `/fotos/` y lo reconstruye. **Un índice corrupto nunca puede dejar el marco en negro.**

Al reconstruir hay que **reanclar el contador**: tomar el nombre numérico más alto que se encontró y dejar el contador de NVS en ese valor + 1. Sin ese paso, un `erase_flash` o un reflasheo deja el contador en cero y las siguientes subidas van sobrescribiendo `00000001.JPG` en adelante, en silencio.

---

## 4. Carga de fotos

### Servidor web local en el ESP32

La persona entra desde el navegador de su celular, ve las fotos que ya están, sube nuevas y borra las que no quiere. **Sin apps, sin cuentas, sin internet.**

### Pieza clave: redimensionado del lado del navegador

El ESP32 no puede reescalar una foto de 12 megapixeles — no le alcanza la memoria ni de cerca. El navegador del celular sí puede, con la API de Canvas.

**Flujo:**

1. La persona selecciona **una o varias** fotos de su galería.
2. Cada una se recorta automáticamente al centro y se muestra en una rejilla de miniaturas.
3. Si alguna quedó mal recortada, toca la miniatura y ajusta el recuadro a mano.
4. El navegador reduce cada foto para caber en 480×320 respetando proporción.
5. Las reencodea como JPEG dentro de su presupuesto de bytes.
6. **Eso** es lo único que se sube al ESP32.

Desde la perspectiva de quien lo usa, simplemente eligió fotos y funcionaron. Esta pieza es la que hace que toda la arquitectura se sostenga, y es la más delicada del proyecto.

### Multi-selección con recorte al centro y edición opcional

Se eligen N fotos de golpe y **todas se recortan automáticamente al centro**; tocar una miniatura abre el editor de recuadro para esa foto en particular.

El recorte automático al centro funciona la mayoría de las veces pero decapita gente en las verticales, así que el editor manual tiene que existir. Lo que no puede existir es la obligación de usarlo: pedir un recorte manual por cada una de 30 fotos de un viaje convertiría la subida en una tarea.

Dos consecuencias que no son negociables:

- **Procesamiento secuencial, cediendo al event loop entre fotos.** Procesar N imágenes de 12 MP en cadena congela el hilo principal del celular y el navegador ofrece matar la pestaña. Worker con `OffscreenCanvas` solo si se mide lento — es optimización, no punto de partida.
- **Concurrencia de subida = 1.** Las fotos se suben de una en una. Dos multipart simultáneos escribiendo a la misma SD es pedir corrupción. Si falla la subida de algunas, se reintentan **solo esas**, no la tanda completa.

### Presupuesto de bytes: proporcional a los píxeles de salida

`canvas.toBlob()` no acepta un tamaño en bytes, acepta una calidad de 0 a 1. Llegar a un peso objetivo exige iterar.

**El objetivo no es un tamaño fijo, es una densidad: 0.167 bytes por píxel** de la imagen de salida (≈1.33 bpp), que sale de 25,600 B ÷ (480×320).

| Salida | Píxeles | Objetivo |
|---|---|---|
| 480×320 (3:2, recortada) | 153,600 | 25.6 KB |
| 480×270 (16:9 sin recortar) | 129,600 | 21.6 KB |
| 240×320 (vertical, con barras) | 76,800 | 12.8 KB |

Con un objetivo plano de 25 KB, una vertical de 240×320 saldría al doble de calidad efectiva que una horizontal — bytes gastados en una diferencia que nadie va a ver.

**Búsqueda binaria acotada a calidad [0.62, 0.92]**, máximo 6 iteraciones. Si la búsqueda topa una cota, se acepta ese resultado: un paisaje con follaje que a calidad 0.62 sigue pesando 34 KB se sube en 34 KB en vez de destruirlo, y un retrato liso que a 0.92 pesa 14 KB se queda en 14 KB en vez de inflarlo. Así el peso depende del tamaño de salida **y** del contenido.

La capacidad de la SD dejó de ser una restricción (§3), de modo que unos KB de más nunca son razón para degradar una foto.

### Dos trampas del escalado por Canvas

**1. Reducir en pasos, no de un jalón.** Un `drawImage` directo de 4032×3024 a 480×320 produce aliasing visible: el navegador usa un filtro bilineal con pocas muestras y se come el detalle fino. El patrón correcto es reducir a la mitad iterativamente hasta acercarse al destino, y ahí sí hacer el paso final.

```js
// Halving progresivo hasta que el siguiente paso pasaría del objetivo.
// Dos canvas alternándose: el destino de un paso es la fuente del siguiente.
while (w / 2 >= targetW && h / 2 >= targetH) {
  const nw = Math.max(1, Math.round(w / 2));
  const nh = Math.max(1, Math.round(h / 2));
  dst.width = nw; dst.height = nh;
  dst.getContext('2d').drawImage(src, 0, 0, w, h, 0, 0, nw, nh);
  [src, dst] = [dst, src];   // ping-pong
  w = nw; h = nh;
}
```

Dos detalles que el código tiene que respetar:

- **Dos canvas alternándose, no uno dibujándose sobre sí mismo.** Un `drawImage` de un canvas sobre sí mismo con regiones que se traslapan está definido en la especificación y funciona, pero depende de que el navegador tome un snapshot interno. No hay razón para apostar la pieza más delicada del proyecto a eso.
- **Redondeo explícito.** Las dimensiones impares producen fraccionales — 3025 ÷ 2 = 1512.5 — y a partir de ahí el conteo de píxeles deja de ser entero.

En una pantalla de 3.5" con fotos de gente, la diferencia se nota en pelo y texturas finas. Es lo que resuelve la librería `pica` internamente; aquí son ~15 líneas propias y se evita la dependencia.

**2. Orientación EXIF.** Las fotos de iPhone traen tag de rotación. Un `<img>` en navegador moderno lo aplica solo; `createImageBitmap()` **no**, salvo que reciba `imageOrientation: 'from-image'`. Mezclar ambos caminos hace que unas fotos salgan de lado y otras no.

La rotación se aplica **una sola vez**, en el primer paso del halving, y solo si se comprobó que el navegador no la aplicó ya.

**Cómo se comprueba: por orientación, nunca por igualdad de píxeles.** La pregunta es si el bitmap salió vertical u horizontal, comparado con lo que dicen el marcador SOF del JPEG y el tag `0x0112`. La comparación tentadora —¿las dimensiones del bitmap coinciden exactamente con las del SOF?— se rompe en Safari de iOS: con imágenes grandes aplica subsampling al decodificar, lo que afecta el resultado de `drawImage` por factores de 2, 4 u 8. La señal de orientación sobrevive a cualquier factor de subsampling; la de igualdad exacta no.

> `naturalWidth` no sirve para detectar el subsampling: reporta las dimensiones lógicas decodificadas, no las del bitmap real.

**No verificado** — hay que probarlo empíricamente en Safari de iOS, que es donde va a subir fotos la persona destinataria. Casos de `web/test/`: una foto vertical de iPhone sin editar y sin pasar por WhatsApp, y una de 48 MP (8064×6048) para provocar el subsampling a propósito.

### Sobre librerías de recorte

`Cropper.js` pesa ~12 KB minificado+gzip y cubre el caso. Dos trampas: la v2.x es una reescritura con API incompatible respecto de la 1.6.x (que es de lo que habla casi toda la documentación en línea), y `Croppr.js` — la alternativa de <6 KB — está abandonada en su repo original.

**Decisión: escribir el recuadro de recorte a mano.** El tamaño no es la restricción real (12 KB en 4 MB de flash no mueven la aguja); el argumento es no meter una dependencia externa en la pieza más delicada del proyecto, para un objeto que debe seguir funcionando dentro de cinco años sin mantenimiento.

### El editor de recuadro

Las fotos de celular vienen en 4:3 o 16:9 contra una pantalla de 3:2, así que siempre hay algo que decidir. El recuadro es arrastrable, con vista previa de cómo va a quedar, y relación fija — 3:2 o 2:3 según la orientación de la foto.

Tres restricciones de implementación:

- **Opera sobre la imagen ya reducida a ~2× el destino**, nunca sobre los 12 MP originales. Arrastrar un recuadro sobre una imagen de 12 MP en un celular va a tirones.
- **Pointer Events** (`pointerdown` / `pointermove` / `pointerup`), que unifica mouse y touch en un solo camino de código. No `touchstart` y `mousedown` por separado.
- **Estado en coordenadas normalizadas de 0 a 1**, para que sobreviva a los cambios de escala entre la vista previa y el render final.

Al confirmar, se re-ejecutan el escalado y el encoder con el nuevo recuadro.

### Contrato HTTP

```
GET  /                  → página, gzip desde PROGMEM, Content-Encoding: gzip
GET  /list              → text/plain, streaming de /manifest.txt desde la SD
GET  /photo?n=NOMBRE    → JPEG desde /fotos/NOMBRE
POST /upload            → multipart, campo "foto"
                          200 {"ok":true,"n":"00000042.JPG"}
                          413 archivo > 64 KB
                          507 SD llena
POST /delete            → {"n":"00000042.JPG"}
```

**Sin miniaturas.** Las fotos ya pesan ~25 KB; la galería sirve los archivos reales y se ahorra generarlas, guardarlas y mantenerlas sincronizadas.

**`/list` devuelve el manifiesto crudo**, con `request->send(SD, "/manifest.txt", "text/plain")`. Es streaming directo desde la tarjeta: cero heap, cero parser en ambos extremos, y el JavaScript hace `split('\n')`. Devolver JSON obligaría al ESP32 a construir la cadena completa en memoria, que es justo lo que el formato del manifiesto (§3) busca evitar.

**Validación obligatoria en `/photo` y `/delete`:** el nombre recibido debe cumplir `^[0-9]{8}\.JPG$` exacto, no basta con concatenarlo a `/fotos/`. Sin esa comprobación, `?n=../manifest.txt` sale del directorio. El esquema rígido de nombres (§3) hace que la validación cueste tres líneas y elimine la clase entera de problema.

**El upload nunca escribe a la SD desde su callback.** Corre en el hilo `async_tcp` a prioridad 10, y bloquearlo con I/O de tarjeta provoca watchdog. Se acumula en heap y se escribe una sola vez al terminar, con tope duro de 64 KB. Detalle completo en el BOM.

### Dónde vive la página

**Embebida en el binario, gzipeada, en PROGMEM.** No en SPIFFS, no en la SD.

- **No en la SD** porque una tarjeta muerta dejaría al marco sin página, ni siquiera para diagnosticar por qué no funciona.
- **No en SPIFFS** —aunque `huge_app.csv` reserva 896 KB— porque obligaría a un `pio run -t uploadfs` aparte del flasheo. Un segundo paso que hay que recordar tres años después es exactamente el tipo de cosa que rompe un objeto sin mantenimiento.

Con PROGMEM hay una sola imagen que flashear y es imposible que el firmware y su página queden desincronizados. Consecuencia para `web/`: el script de build no solo concatena a un HTML único, también lo gzipea y emite el array de C.

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

### Si el WiFi se cae para siempre: ruta de recuperación por toque largo

Mostrar fotos sin red ya está cubierto, pero faltaba lo otro: el día que cambien el módem, **tiene que seguir habiendo forma de subir fotos**. Si no, la regla 2 se cumple a medias.

**El toque largo cambia de significado según el contexto:**

| Contexto | Qué hace el toque largo |
|---|---|
| WiFi arriba | Muestra el QR con la IP actual |
| Sin WiFi | Levanta el AP `Marco-Fotos` y muestra el QR de setup |

Mismo gesto, sin nada que aprender, y **sin un AP encendido permanentemente** — eso sería un punto de exposición 24/7 y no encaja con la regla 3.

**El AP se baja solo. Dos relojes de 5 minutos:**

- Si nadie se conecta en 5 minutos, baja y vuelve a mostrar fotos.
- Si hay alguien conectado, se mantiene, y el reloj se reinicia con cada petición. Así una cola de 30 fotos no se corta a la mitad.

**Decisión consciente, no olvido:** nadie le va a explicar a la persona destinataria que ese gesto existe, y no hace falta. El marco sigue mostrando fotos, que es el 99 % del uso. La ruta de recuperación existe para quien la busque — típicamente quien lo regaló, guiando por teléfono.

### Códigos QR en pantalla

Dos QRs distintos para dos momentos distintos:

| QR | Cuándo aparece | Qué codifica |
|---|---|---|
| **Setup** | Sin credenciales guardadas, o toque largo sin WiFi | `WIFI:S:Marco-Fotos;T:WPA;P:fotos1234;;` |
| **Uso diario** | Toque largo con WiFi arriba | `http://192.168.1.47` (IP actual) |

El formato `WIFI:` es reconocido nativamente por Android e iOS: al escanearlo con la cámara se conectan solos. **La persona nunca ve una contraseña.**

**El QR de uso diario debe regenerarse al vuelo**, leyendo la IP actual. Los routers domésticos reasignan por DHCP, así que la IP de hoy puede no ser la de la próxima semana.

**El QR de setup no necesita nada de eso: codifica una constante.** Por lo tanto se pinta **antes** de llamar a `autoConnect()`, y no hace falta el modo no bloqueante de WiFiManager (`setConfigPortalBlocking(false)` + `process()`). Solo el QR de uso diario depende de un dato de runtime, y para cuando se necesita, el WiFi ya está arriba.

**Pero no se pinta incondicionalmente.** `autoConnect()` primero intenta conectar con las credenciales guardadas y solo levanta el AP si eso falla; pintar el QR antes de saberlo lo mostraría unos segundos en cada arranque normal, que se ve a proyecto y no a producto. Se decide antes, con `getWiFiIsSaved()`:

| Estado | Qué se pinta antes de `autoConnect()` |
|---|---|
| Hay credenciales guardadas | Pantalla de "conectando" |
| No hay credenciales | QR de setup |

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
- Exige que las imágenes estén en la **raíz** de la SD, que es justo lo que este proyecto prohíbe (§3): ahí es donde macOS deja sus metadatos y donde vive el manifiesto.

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
| Toque largo, con WiFi | Mostrar QR con la IP para subir fotos |
| Toque largo, sin WiFi | Levantar el AP `Marco-Fotos` y mostrar el QR de setup |

**El toque largo es contextual a propósito.** Un solo gesto cubre los dos casos, y el segundo es la ruta de recuperación para el día que cambien el módem (§5). Nada que aprender, nada que recordar, y ningún AP encendido de forma permanente.

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
| Procesador | ESP32 clásico WROOM, USB-C (a comprar) | ESP32-S3 (innecesario con decodificación por bloques), WROVER (PSRAM ocupa GPIO16/17) |
| Pantalla | ST7796S 3.5" IPS SPI | Nextion (25 fotos precompiladas), all-in-one 5" (presupuesto) |
| Almacenamiento | Tarjeta SD local | Google Drive / nube |
| Carga de fotos | Servidor web local + Canvas | App, SD extraíble, USB |
| Redimensionado | En el navegador del celular | En el ESP32 (imposible) |
| Selección de fotos | Multi-selección, recorte al centro + edición opcional | Una a la vez (tedioso con 30 fotos), sin edición (decapita verticales) |
| Peso del JPEG | 0.167 B/píxel de salida, calidad acotada [0.62, 0.92] | 25 KB plano, calidad fija, selector de calidad para el usuario |
| Nombres de archivo | `NNNNNNNN.JPG`, contador en NVS | Nombres largos (gastan varias entradas de directorio) |
| Índice | `/manifest.txt` plano, con fallback a recorrer `/fotos/` | JSON (obliga a construir la cadena en heap) |
| Ubicación de la página | PROGMEM gzipeado en el binario | SPIFFS (paso `uploadfs` olvidable), SD (muere con la tarjeta) |
| Provisioning WiFi | Portal cautivo + QR | Credenciales en firmware |
| Sin WiFi | Toque largo levanta el AP, con timeout de 5 min | AP permanente (exposición 24/7), no tener ruta de recuperación |
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

1. **Arnés de diagnóstico en `web/test/`, antes que nada.** Un HTML que carga una foto y reporta tres cosas: las dimensiones que devolvió el decodificador, el tag `0x0112` leído a mano de los bytes del JPEG, y la imagen sin transformar. Se corre en Chrome de escritorio, Safari de macOS y **Safari de iOS**. Responde de forma medida —no supuesta— si el navegador ya aplicó la rotación y si hay subsampling con fotos grandes. **Hasta que este paso no dé resultados, el resto del pipeline es especulación.**
2. **Página web con redimensionado por Canvas**, en este orden: lector EXIF → escalador → encoder → rejilla y cola → editor de recuadro → capa de red → empaquetado. La capa de red incluye un **modo mock que descarga los JPEG a disco** en vez de subirlos, para poder cerrar y probar `web/` completa antes de que exista el firmware.
3. **Repositorio en GitHub** con los dos documentos, `web/`, y GitHub Pages activado — sirve la página por HTTPS y permite probarla desde el celular real, no solo desde escritorio.
4. **Instalar PlatformIO** y armar el `platformio.ini` del BOM. Compila sin placa conectada; valida que todas las dependencias resuelvan.
5. Formatear la SD en FAT32 y crear `/fotos/`.

Casos de prueba mínimos para los pasos 1 y 2: foto 4:3, 16:9, vertical de iPhone sin editar y sin pasar por WhatsApp (EXIF), una de 12 MP para cronometrar el escalado en celular, y una de 48 MP (8064×6048) para provocar el subsampling de Safari a propósito.

### Requiere los componentes en mano, en este orden

6. **Provisioning de extremo a extremo (pendiente #5).** Va primero porque es lo único que puede forzar un cambio de plataforma. Todo lo demás se construye encima.
7. Prueba de tres pasos del pin `BL`.
8. Confirmar pinout del PN2222A (zócalo hFE).
9. Afinar velocidad SPI (27 → 40 → 80 MHz).
10. Confirmar dirección I2C del BH1750.
11. Integración y modelado de la carcasa.
