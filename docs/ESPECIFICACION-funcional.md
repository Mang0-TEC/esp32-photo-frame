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
4. El navegador reduce cada foto para caber en 320×480 respetando proporción.
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

**El objetivo no es un tamaño fijo, es una densidad: 0.213 bytes por píxel** de la imagen de salida (≈1.71 bpp), que sale de 32,768 B ÷ (320×480).

Los 32,768 B del caso que llena la pantalla no son un número redondo por casualidad: es exactamente un cluster de la tarjeta (ver el BOM). Una foto dentro de presupuesto ocupa un cluster y ni un byte más.

| Salida | Proporción de entrada | Píxeles | Objetivo |
|---|---|---|---|
| 320×480 | 2:3 vertical (réflex) | 153,600 | 32.0 KB |
| 320×427 | 3:4 vertical (celular) | 136,640 | 28.5 KB |
| 270×480 | 9:16 vertical | 129,600 | 27.0 KB |
| 221×480 | 19.5:9 (captura de celular) | 106,080 | 22.1 KB |
| 320×240 | 4:3 horizontal | 76,800 | 16.0 KB |
| 320×213 | 3:2 horizontal (réflex) | 68,160 | 14.2 KB |
| 320×180 | 16:9 horizontal | 57,600 | 12.0 KB |

Con un objetivo plano de 32 KB, una horizontal de 320×240 gastaría 0.43 B/píxel contra los 0.213 de una que llena la pantalla — el doble de calidad efectiva en la foto que ocupa **menos** pantalla. Bytes gastados en una diferencia que nadie va a ver.

**Búsqueda binaria acotada a calidad [0.62, 0.92]**, máximo 6 iteraciones. Si la búsqueda topa una cota, se acepta ese resultado: un paisaje con follaje que a calidad 0.62 sigue pesando de más se sube de más en vez de destruirlo, y un retrato liso que a 0.92 pesa 14 KB se queda en 14 KB en vez de inflarlo. Así el peso depende del tamaño de salida **y** del contenido.

**Banda de aceptación del 88 %.** La búsqueda corta en cuanto el resultado supera el 88 % del objetivo, en vez de agotar siempre los pasos: llegar a 28.1 KB de un objetivo de 28.5 no justifica dos llamadas más a `toBlob`. Bajó los intentos de 8 fijos a 3-6 en fotos reales.

La capacidad de la SD dejó de ser una restricción (§3), de modo que unos KB de más nunca son razón para degradar una foto.

> **Nota de lectura para quien compare contra mediciones viejas.** La búsqueda binaria no evalúa puntos fijos: evalúa el punto medio del intervalo vivo. Cambiar la cota inferior mueve toda la trayectoria — con [0.30, 0.92] el primer sondeo cae en 0.61 y con [0.62, 0.92] cae en 0.77. Cualquier medición tomada con otro rango o con otro presupuesto **no es comparable** en `q`, número de intentos ni peso final. Sí lo son las dimensiones de salida, los pasos de halving y el veredicto de orientación.

### Dos trampas del escalado por Canvas

**1. Reducir en pasos, no de un jalón.** Un `drawImage` directo de 4032×3024 a 320×480 produce aliasing visible: el navegador usa un filtro bilineal con pocas muestras y se come el detalle fino. El patrón correcto es reducir a la mitad iterativamente hasta acercarse al destino, y ahí sí hacer el paso final.

Esta es la implementación real, la de `web/index.html`:

```js
function escalar(imagen, sx, sy, sw, sh, dstW, dstH){
  let a = document.createElement("canvas");
  let b = document.createElement("canvas");
  let src = imagen, cx = sx, cy = sy, cw = sw, ch = sh;
  let pasos = 0;

  while (Math.round(cw / 2) >= dstW && Math.round(ch / 2) >= dstH) {
    const nw = Math.max(1, Math.round(cw / 2));
    const nh = Math.max(1, Math.round(ch / 2));
    a.width = nw; a.height = nh;
    const ctx = a.getContext("2d");
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(src, cx, cy, cw, ch, 0, 0, nw, nh);
    src = a; cx = 0; cy = 0; cw = nw; ch = nh;
    [a, b] = [b, a];                 // ping-pong: nunca se lee y escribe el mismo canvas
    pasos++;
  }

  const out = document.createElement("canvas");
  out.width = dstW; out.height = dstH;
  const octx = out.getContext("2d");
  octx.imageSmoothingEnabled = true;
  octx.imageSmoothingQuality = "high";
  octx.drawImage(src, cx, cy, cw, ch, 0, 0, dstW, dstH);
  if (a !== src) { a.width = a.height = 0; }
  if (b !== src) { b.width = b.height = 0; }
  if (src !== imagen) { src.width = src.height = 0; }
  return { canvas: out, pasos };
}
```

Cuatro detalles que el código tiene que respetar, y que un pseudocódigo suelto se salta:

- **Dos canvas alternándose, no uno dibujándose sobre sí mismo.** Un `drawImage` de un canvas sobre sí mismo con regiones que se traslapan está definido en la especificación y funciona, pero depende de que el navegador tome un snapshot interno. No hay razón para apostar la pieza más delicada del proyecto a eso.
- **El puntero `src` es una tercera variable, aparte de los dos canvas.** Es obligatorio, no estilo: la fuente del primer paso es un `<img>`, no un canvas. Un ping-pong de dos variables que arranque con `src` = `<img>` y `dst` = canvas deja `dst` apuntando al `<img>` tras el primer intercambio, y en la segunda vuelta intenta `dst.width = nw` sobre un elemento que no tiene esa propiedad asignable.
- **Redondeo explícito.** Las dimensiones impares producen fraccionales — 3025 ÷ 2 = 1512.5 — y a partir de ahí el conteo de píxeles deja de ser entero.
- **La liberación de los canvas va después del `drawImage` final, y guardada.** Hasta esa línea `src` sigue siendo la fuente; ponerle `width = 0` antes lo deja en blanco y la foto sale negra, con la gracia de que solo ocurre a partir de cierto número de pasos de halving. El guard contra `imagen` es porque con una foto chica no hubo ningún paso y `src` sigue siendo el `<img>`.

**El rectángulo fuente `(sx, sy, sw, sh)` está en la firma desde el principio**, aunque hoy siempre se le pase la imagen completa. Es lo que deja entrar el recorte manual sin reescribir el pipeline: el recorte se aplica sobre las coordenadas del original y el halving va de esa región al destino. Escalar primero y recortar después perdería resolución. Solo se consume en el primer `drawImage`; a partir de ahí `cx`/`cy` valen 0.

En una pantalla de 3.5" con fotos de gente, la diferencia se nota en pelo y texturas finas. Es lo que resuelve la librería `pica` internamente; aquí son ~25 líneas propias y se evita la dependencia.

**Liberar el `<img>` antes de comprimir, no después.** Hallazgo medido, y la razón de que estas dos líneas estén donde están en `procesar()`:

```js
URL.revokeObjectURL(url);
img.src = "";
```

El bitmap decodificado de una foto de 12 MP son ~48 MB. Si siguen vivos durante las llamadas a `toBlob`, el GC trabaja entre cada una: **433 ms por llamada contra 3 ms**, y el pipeline completo pasó de **3464 ms a 15 ms** en la misma foto. Es un cambio de dos líneas que parece cosmético, así que lleva un comentario en el código advirtiendo que no se muevan al final.

**2. Orientación EXIF.** Las fotos de iPhone traen tag de rotación. Un `<img>` en navegador moderno lo aplica solo; `createImageBitmap()` **no**, salvo que reciba `imageOrientation: 'from-image'`. Mezclar ambos caminos hace que unas fotos salgan de lado y otras no.

La rotación se aplica **una sola vez**, en el primer paso del halving, y solo si se comprobó que el navegador no la aplicó ya.

**Cómo se comprueba: por orientación, nunca por igualdad de píxeles.** La pregunta es si el bitmap salió vertical u horizontal, comparado con lo que dicen el marcador SOF del JPEG y el tag `0x0112`. La comparación tentadora —¿las dimensiones del bitmap coinciden exactamente con las del SOF?— se rompe en Safari de iOS: con imágenes grandes aplica subsampling al decodificar, lo que afecta el resultado de `drawImage` por factores de 2, 4 u 8. La señal de orientación sobrevive a cualquier factor de subsampling; la de igualdad exacta no.

> `naturalWidth` no sirve para detectar el subsampling: reporta las dimensiones lógicas decodificadas, no las del bitmap real.

#### Lo verificado

Medido en el **navegador integrado de VS Code (Simple Browser) sobre macOS**, que es un webview de Electron y por lo tanto motor **Chromium**. Ahí la rotación EXIF **sí** se aplica sola por la ruta `<img>`.

| Archivo | Entrada | EXIF | SOF | Salida | Peso | q | Intentos | Total |
|---|---|---|---|---|---|---|---|---|
| IMG_1434 | 3024×4032 | 6 | 4032×3024 | 320×427 | 29.2 KB | 0.688 | 4 | 134 ms |
| IMG_1470 | 2316×3088 | 5 | 3088×2316 | 320×427 | 29.6 KB | 0.862 | 6 | 78 ms |
| IMG_1347 | 4032×3024 | 1 | 4032×3024 | 320×240 | 31.9 KB | 0.881 | 5 | 131 ms |

**Esta tabla se tomó con el tope plano de 32 KB y el rango de calidad [0.30, 0.92] anteriores.** Con el presupuesto por densidad y el rango [0.62, 0.92] de arriba, las columnas de peso, `q` e intentos cambian por construcción — IMG_1347 baja a un objetivo de 16.0 KB. Lo que sigue siendo comparable son la salida, el veredicto de orientación y los pasos de halving.

#### Lo que sigue abierto

- **Ningún motor WebKit fue probado.** Ni Safari de macOS ni Safari de iOS. Todo lo de arriba es Chromium, así que el pendiente sigue completo: el navegador que importa es el del celular de la persona destinataria, y hasta que el panel diga «SÍ rotó» ahí, esto es **no verificado**.
- **El subsampling de Safari sigue sin observarse**, precisamente porque no se corrió en Safari. La foto de 48 MP (8064×6048) existe como caso para provocarlo a propósito y todavía no se usó.
- **EXIF = 5 es transpose: espejo diagonal, no solo rotación.** El parser comprueba el intercambio de ejes pero **no** el reflejo. Un navegador que rotara sin reflejar daría veredicto verde igualmente. Es el caso de IMG_1470 en la tabla, y el panel lo marca como «reflejo SIN verificar» para que no se lea como una comprobación que no es. Limitación conocida del arnés, no del pipeline.
- **Calidad en fotos con ruido de alta frecuencia.** IMG_1434 (césped y pelaje) terminó en q = 0.688 contra 0.862 de una foto con pared lisa. Puede haber bloqueo visible. Se evalúa cuando llegue el hardware, sobre la pantalla real — no tiene sentido juzgarlo en un monitor.

Las fotos que faltan por pasar: una vertical de iPhone sin editar y sin pasar por WhatsApp, contra Safari de iOS, y una de 48 MP para el subsampling.

### Sobre librerías de recorte

`Cropper.js` pesa ~12 KB minificado+gzip y cubre el caso. Dos trampas: la v2.x es una reescritura con API incompatible respecto de la 1.6.x (que es de lo que habla casi toda la documentación en línea), y `Croppr.js` — la alternativa de <6 KB — está abandonada en su repo original.

**Decisión: escribir el recuadro de recorte a mano.** El tamaño no es la restricción real (12 KB en 4 MB de flash no mueven la aguja); el argumento es no meter una dependencia externa en la pieza más delicada del proyecto, para un objeto que debe seguir funcionando dentro de cinco años sin mantenimiento.

### El editor de recuadro

Las fotos de celular vienen en 3:4 o 9:16 contra una pantalla de 2:3, así que siempre hay algo que decidir. El recuadro es arrastrable, con vista previa de cómo va a quedar, y **relación fija 2:3** — la de la pantalla, porque el objetivo del recorte es justamente que la foto la llene.

Tras la decisión de orientación (§6) el editor dejó de ser una mejora opcional: es la compensación de que las fotos horizontales queden a media pantalla. Una 4:3 horizontal sin recortar sale en 320×240 con la mitad del alto en negro; recortada a 2:3 llena los 320×480.

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

**Sin miniaturas.** Las fotos pesan 32 KB en el peor caso; la galería sirve los archivos reales y se ahorra generarlas, guardarlas y mantenerlas sincronizadas.

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
- 29×29 módulos a escala 8 = 232 px. Centrado en 320×480 deja 44 px de margen a cada lado, por encima de los 4 módulos (32 px) de zona tranquila que exige el estándar. Se escanea al instante desde medio metro.
- **Margen blanco de al menos 4 módulos alrededor** (zona tranquila del estándar). Sin él, muchos escáneres no lo detectan — es la causa número uno de QRs que no funcionan.
- **Mostrar la IP en texto legible debajo del QR.** Cuesta una línea y cubre a quien prefiera teclearla.

---

## 6. Visualización

### Orientación: vertical (320×480), la nativa del panel

El marco se monta **vertical**, que es además la orientación nativa del ST7796S: en firmware es `tft.setRotation(0)`, sin ninguna transformación de coordenadas.

**Por qué vertical.** La mayoría de las fotos que van a acabar en el marco son verticales de celular, en 3:4. La comparación decide sola:

| Montaje | Foto 3:4 vertical | Píxeles de imagen | Barras |
|---|---|---|---|
| Horizontal 480×320 | 240×320 | 76,800 | 120 px por lado — la mitad del ancho |
| **Vertical 320×480** | **320×427** | **136,640** | **27 px arriba y abajo** |

Son **+78 % de imagen útil** en el caso que más se va a dar.

**El costo, que es real y se acepta.** Es el caso simétrico del que se acaba de resolver: las fotos horizontales quedan a media pantalla.

| Foto | Salida | Barras |
|---|---|---|
| 2:3 vertical (réflex) | 320×480 | ninguna, recorte cero |
| 3:4 vertical (celular) | 320×427 | 11 % del alto |
| 9:16 vertical | 270×480 | 16 % del ancho |
| 19.5:9 (captura de celular) | 221×480 | 31 % del ancho |
| 4:3 horizontal | 320×240 | 50 % del alto |
| 3:2 horizontal (réflex) | 320×213 | 56 % del alto |

Las 3:2 de réflex pasan de ser el caso perfecto a ser el peor. A cambio, las 2:3 verticales llenan la pantalla con recorte cero.

**Por eso el recorte manual (§4) sube de mejora opcional a compensación necesaria de esta decisión.** La página avisa en lenguaje llano cuando las barras se comen más del 20 % de un eje, y el aviso nombra el eje correcto — una captura de pantalla de celular deja barras a los lados, no arriba y abajo.

### Esta decisión es irreversible sin resubir todas las fotos

El escalado ocurre en el navegador (§2), así que **las fotos se guardan ya ajustadas a la orientación elegida**. Una foto de 320×427 no cabe en una pantalla horizontal de 480×320, y el ESP32 no puede reescalar — es justamente la restricción que sostiene toda la arquitectura.

**Girar el marco físicamente después de subir fotos obligaría a resubirlas todas.** La orientación se fija antes de la primera subida y ahí se queda.

Esto retira una promesa que hacía la versión anterior de este documento: que el soporte de la carcasa permitiría rotarlo si se quisiera. Deja de ser cierta y no se debe reintroducir en el diseño de la carcasa.

Se evaluaron y **pospusieron** dos alternativas, a reevaluar tras las primeras pruebas visuales. La irreversibilidad de arriba es exactamente lo que las encarece:

- Playlists separadas por orientación, con detección por switch SW-520D o acelerómetro.
- Cambio manual de modo por toque largo.

### Decisión de implementación que preserva la reversibilidad

**Las barras negras NO se hornean dentro del JPEG.**

- El navegador escala la foto para caber en 320×480 respetando proporción. Una vertical de celular queda en 320×427.
- El ESP32 pinta la pantalla de negro y dibuja la imagen centrada, calculando el offset.

Si las barras se metieran al archivo, cambiar de estrategia después obligaría a resubir todas las fotos. Así el archivo guarda su recorte nativo y el comportamiento vive en el firmware, cambiable con un flash.

> Esto atenúa la irreversibilidad de la sección anterior, no la elimina: el firmware puede cambiar dónde pone las barras, pero no puede inventar píxeles que el navegador ya descartó.

### Decodificación

`TFT_eSPI` (Bodmer) + `TJpg_Decoder`. Trabaja en bloques MCU de 16×16 empujados directo por SPI.

Consumo real ≈ buffer del JPEG (12-32 KB según el tamaño de salida, ver §4; 64 KB en el tope duro) + ~4 KB de workspace. **Por esto no hace falta un ESP32-S3**: nunca se arma el framebuffer completo de 300 KB.

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
| Peso del JPEG | 0.213 B/píxel de salida (32 KB = un cluster al llenar la pantalla), calidad acotada [0.62, 0.92] | Tamaño plano, calidad fija, selector de calidad para el usuario |
| Nombres de archivo | `NNNNNNNN.JPG`, contador en NVS | Nombres largos (gastan varias entradas de directorio) |
| Índice | `/manifest.txt` plano, con fallback a recorrer `/fotos/` | JSON (obliga a construir la cadena en heap) |
| Ubicación de la página | PROGMEM gzipeado en el binario | SPIFFS (paso `uploadfs` olvidable), SD (muere con la tarjeta) |
| Provisioning WiFi | Portal cautivo + QR | Credenciales en firmware |
| Sin WiFi | Toque largo levanta el AP, con timeout de 5 min | AP permanente (exposición 24/7), no tener ruta de recuperación |
| Orientación | Vertical 320×480, nativa del panel (`setRotation(0)`), proporción nativa con barras | Horizontal 480×320, que dejaba las verticales de celular en 240×320 · rotación física reversible · rotación automática · playlists |
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

1. **Arnés de diagnóstico, antes que nada.** Un HTML que carga una foto y reporta las dimensiones que devolvió el decodificador, el tag `0x0112` leído a mano de los bytes del JPEG, y la imagen sin transformar. Responde de forma medida —no supuesta— si el navegador ya aplicó la rotación y si hay subsampling con fotos grandes.

   **Estado: hecho a medias, y la mitad que falta es la que importa.** El arnés existe, ya no en `web/test/` sino integrado en `web/index.html` detrás de `CFG.DIAG`, que se apaga en producción. Corrió sobre tres fotos reales y los resultados están en §4. Pero corrió **solo en Chromium** (el navegador integrado de VS Code sobre macOS). Falta Safari de macOS y sobre todo **Safari de iOS**, que es donde va a subir fotos la persona destinataria. **Hasta que ese paso no dé resultados, la orientación EXIF y el subsampling siguen siendo especulación.**

2. **Página web con redimensionado por Canvas**, en este orden: lector EXIF → escalador → encoder → rejilla y cola → editor de recuadro → capa de red → empaquetado.

   **Hecho y medido:** lector del tag EXIF y de las dimensiones del marcador SOF por bytes, escalado por halving con ping-pong de dos canvas, encoder con búsqueda binaria acotada y banda de aceptación, presupuesto por densidad, aviso de barras en lenguaje llano, panel de diagnóstico, y descarga del JPEG a disco (el modo mock, que permite probar la página completa sin firmware).

   **Falta:** rejilla y cola con multi-selección —hoy solo se procesa la primera foto seleccionada—, editor de recuadro, capa de red contra `/upload`, y el empaquetado (gzip → array de C en PROGMEM).

3. **Repositorio en GitHub** con los dos documentos, `web/`, y GitHub Pages activado — sirve la página por HTTPS y permite probarla desde el celular real, no solo desde escritorio. **Es lo que desbloquea el punto 1**, porque Safari de iOS no va a abrir un `file://` de otra máquina.
4. **Instalar PlatformIO** y armar el `platformio.ini` del BOM. Compila sin placa conectada; valida que todas las dependencias resuelvan. **Hecho** — build limpio verificado, ver el BOM.
5. Formatear la SD en FAT32 y crear `/fotos/`. Al hacerlo, confirmar el tamaño de cluster (ver el BOM).

Casos de prueba mínimos para los pasos 1 y 2: foto 4:3, 16:9, vertical de iPhone sin editar y sin pasar por WhatsApp (EXIF), una de 12 MP para cronometrar el escalado en celular, y una de 48 MP (8064×6048) para provocar el subsampling de Safari a propósito.

De esos, **ya se probaron** la 4:3 y dos verticales de iPhone con EXIF 6 y 5, todas en Chromium. **Sin correr:** la de 48 MP, cuya única razón de ser es provocar el subsampling de Safari, y por lo tanto no tiene sentido correrla en otro motor.

### Requiere los componentes en mano, en este orden

6. **Provisioning de extremo a extremo (pendiente #5).** Va primero porque es lo único que puede forzar un cambio de plataforma. Todo lo demás se construye encima.
7. Prueba de tres pasos del pin `BL`.
8. Confirmar pinout del PN2222A (zócalo hFE).
9. Afinar velocidad SPI (27 → 40 → 80 MHz).
10. Confirmar dirección I2C del BH1750.
11. Integración y modelado de la carcasa.
