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

Costo hasta hoy: **$387 MXN gastados** en componentes nuevos, contra $1,000–1,500 del comercial. Son $261 de pantalla, lector SD y sensor de luz, más $126 del ESP32 de reposición — la unidad USB-C que había en inventario resultó defectuosa. **Los cuatro están comprados y en mano.** Lo que queda por gastar son **$42** de la reposición del BH1750, que se estropeó en el banco el 7-ago-2026 después de dar su medición, así que el total del proyecto va camino de **$429**.

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
| Capacidad práctica | ~121,000 fotos al peso medio medido (deja de ser restricción) |

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

#### Cuánto cuesta ese recorrido — medido, 2026-08-07

**4,763 µs por entrada.** Cien archivos recorridos con `openNextFile()` en 476.3
ms, desde `firmware/banco/` `[env:display]`, tecla `l`. El recorrido devuelve de
paso el nombre válido más alto, que es exactamente el dato del reanclaje.

Extrapolado, y ahí está lo interesante:

| Fotos en `/fotos/` | Reconstruir el manifiesto |
|---|---|
| 100 | 0.5 s |
| 1,000 | 4.8 s |
| 5,000 | 24 s |
| 121,000 (la capacidad de arriba) | **9.6 minutos** |

**El fallback es viable, pero no a cualquier escala, y conviene tenerlo escrito
antes de implementarlo.** En los recuentos que este marco va a ver de verdad
—cientos, a lo sumo unos miles de fotos— son segundos y se pagan una sola vez,
solo cuando el manifiesto falta o está corrupto. En el techo teórico de capacidad
son diez minutos, y diez minutos de pantalla en negro al arrancar violarían la
regla 2 tan claramente como el índice roto que se intenta arreglar.

Dos salidas, ninguna necesaria hoy y las dos baratas cuando haga falta:

- **Pintar la primera foto encontrada y seguir recorriendo detrás.** El marco
  nunca está en negro, que es lo único que la regla 2 exige; el manifiesto queda
  completo unos segundos después.
- **`readdir()` sobre el VFS en vez de `openNextFile()`**, que **abre** cada
  archivo y ahí está buena parte de los 4,763 µs. Solo leer la entrada de
  directorio tiene que salir bastante más barato.

No se construye ninguna de las dos ahora: el recorrido solo ocurre cuando el
índice falta, y con los recuentos reales el número ya es aceptable. Queda escrito
para que la decisión no se retome desde cero.

#### Cómo está construido el fallback — implementado y medido, 2026-08-07

Vive en `reconstruirManifiesto()`, `firmware/src/main.cpp`, y va en **dos fases**
para que el orden reconstruido sea el numérico **sin techo de fotos**.

**Fase 1 — recorrer y volcar en crudo.** `openNextFile()` sobre `/fotos/`,
`nombreValido()` por entrada, y cada nombre válido se escribe tal cual a
`/manifest.raw`. **Cero RAM.** Es el paso caro y no cambia.

**Fase 2 — ordenar por rangos, releyendo el crudo.** El nombre *es* el número, así
que caben 4 B por foto. Se toma un bloque de `B` fotos y se recorre el crudo una
vez por rango `[lo, lo+B)`, ordenando con `qsort` y anexando:

- `B` sale **derivado del heap**, no fijado: la mitad del bloque contiguo mayor.
  Medido en placa, **`B = 13,822` fotos**. La fracción es ½ y no ¼ porque esto
  corre en el arranque y antes de `autoConnect()`, que es el momento con el heap
  más vacío de toda la vida del firmware.
- Con los recuentos reales de este documento —cientos, a lo sumo unos miles—
  **`K = 1`** y la fase 2 *es* la ordenación en RAM de toda la vida. Eso importa:
  significa que **la ruta normal se ejercita en cada prueba**, al revés que un
  merge sort cuyo código de fusión solo correría por encima del techo.
- **Verificado que `K > 1` da el mismo resultado**, con un binario desechable de
  `B = 3`: **3 pasadas**, y el manifiesto resultante idéntico al de una sola.
- Si el `malloc` falla, `rename(/manifest.raw → /manifest.txt)` y se avisa: el
  marco arranca en orden de directorio en vez de quedarse sin índice. No es una
  rama nueva — el archivo ya está escrito.

**Coste con fotos reales**, 7 fotos más los 7 sidecars de macOS:

| | |
|---|---|
| Recorrido | 63-87 ms → **4,500-6,214 µs/entrada** |
| Ordenación | 39 ms con `K = 1`, 45 ms con `K = 3` |
| Reanclaje del contador | `0 → 8`, y en el siguiente arranque `8 → 8` |

**Ese rango de µs/entrada es ancho a propósito: con 14 entradas la cifra es
ruidosa**, y las dos corridas difieren un 38 %. Ambas rodean los 4,763 µs
medidos con 100 archivos sintéticos, así que la extrapolación de la tabla de
arriba se sostiene, pero **no se debe citar un valor único** de esta medición.

**La ordenación cuesta 39 ms con 7 fotos, o sea el 45 % del recorrido, y eso NO
contradice que sea barata**: a ese recuento la fase 2 está dominada por costes
fijos —abrir el crudo, escribir el temporal, el `remove` y el `rename`, cada uno
una actualización de directorio en la tarjeta— y no por el `qsort`, que sobre
1,000 elementos son 1-2 ms. Las 2 pasadas extra del binario de `B = 3` costaron
6 ms sobre un archivo de 91 B. Con miles de fotos el recorrido crece y la
ordenación no, así que su peso relativo cae.

**El escrito va a `/manifest.tmp` y se renombra al final, con el crudo intacto
hasta entonces.** Un corte de luz a media escritura dejaría si no un índice
truncado que **parece válido** —sus entradas existen— y el fallback no volvería a
dispararse nunca. `f_rename` de FatFs falla si el destino existe, de ahí el
`remove` previo. Verificado en placa que tras reconstruir **no quedan
`/manifest.raw` ni `/manifest.tmp`** en la raíz.

#### El índice roto, ejercitado en placa

No hace falta que el manifiesto esté corrupto para que apunte a nada: basta con
borrar fotos. **Medido con 5 de 7 entradas muertas** —el manifiesto seguía
nombrando siete y solo existían dos archivos—: el firmware registra cada entrada
muerta, sigue a la siguiente y pinta las dos vivas alternándolas. **Ni un solo
frame en negro, ni un mensaje de error en pantalla.** Es la regla 2 en el caso
que la motiva.

**No reconstruye mientras haya algo pintable**, y eso es deliberado: `showNextPhoto()`
se salta hasta `SALTOS_MAX = 8` entradas antes de dar el índice por roto. El
manifiesto se queda temporalmente desfasado —cuesta un `getFsJpgSize` fallido por
entrada muerta, del orden de 16 ms— a cambio de no pagar un recorrido de
directorio entero por una foto que alguien borró a mano.

> Ese 8 no promete encontrar la foto viva: con un manifiesto largo y casi todo
> muerto, se agota y reconstruye. **Eso no es un fallo, es la otra mitad del
> mecanismo** — la reconstrucción reescribe el índice desde `/fotos/` y después
> todas las entradas existen. El umbral solo decide cuál de los dos caminos se
> paga, y los dos acaban con el marco pintando.

> **macOS deja un `._NNNNNNNN.JPG` por cada foto DENTRO de `/fotos/`**, y esto no
> estaba anotado: son sidecars AppleDouble de 4 KB que aparecen al copiar desde
> la Mac, que es como se cargan las fotos mientras `/upload` no exista.
> `nombreValido()` los rechaza por longitud —15 ≠ 12— y el recorrido los cuenta
> como descartados: **7 válidas, 7 descartadas** en la medición de arriba. O sea
> que el guard contra `?n=../manifest.txt` resultó cubrir también la basura real
> del sistema de archivos, sin haberlo buscado.
>
> Y **`.metadata_never_index` NO evita `.fseventsd`**, contra lo que afirma el
> BOM: en la tarjeta preparada hay `.fseventsd` en la raíz y no hay
> `.Spotlight-V100`. Son dos mecanismos distintos y el archivo solo apaga el
> indexado de Spotlight. No cambia ninguna decisión —las fotos no van en la raíz
> justamente por esto— pero la afirmación estaba de más.

---

## 4. Carga de fotos

### Servidor web local en el ESP32

La persona entra desde el navegador de su celular, ve las fotos que ya están, sube nuevas y borra las que no quiere. **Sin apps, sin cuentas, sin internet.**

### Pieza clave: redimensionado del lado del navegador

El ESP32 no puede reescalar una foto de 12 megapixeles — no le alcanza la memoria ni de cerca. El navegador del celular sí puede, con la API de Canvas.

**Flujo:**

1. La persona selecciona **una o varias** fotos de su galería.
2. El navegador prepara cada una en orden y las va mostrando en una rejilla de miniaturas. Ese orden es el de subida, el del manifiesto y el de reproducción en el marco.
3. Cada foto se reduce para **caber completa** en 320×480 respetando su proporción. **No se recorta nada:** lo que sobra de pantalla queda en negro, y el negro lo pinta el firmware, no el archivo (§6).
4. Si a alguna le sobra demasiado negro, o el encuadre no es el que se quería, se toca su miniatura y se ajusta el recuadro a mano. Es opcional y es foto por foto; al confirmar se rehacen los pasos 3 y 5 solo para esa.
5. Cada una se reencodea como JPEG dentro de su presupuesto de bytes.
6. **Eso** es lo único que se sube al ESP32.
7. Al tocar «Subir al marco» se suben **de una en una**, en el orden de la rejilla. Lo que suba se queda en la tarjeta aunque se cancele el resto.

Desde la perspectiva de quien lo usa, simplemente eligió fotos y funcionaron. Esta pieza es la que hace que toda la arquitectura se sostenga, y es la más delicada del proyecto.

### Multi-selección: foto completa por defecto, recorte opcional por foto

Se eligen N fotos de golpe y **todas entran completas**, con barras negras si su proporción no es la del marco. Ninguna se recorta sin que alguien lo pida: tocar una miniatura abre el editor de recuadro para esa foto en particular, y solo esa.

#### Corrección, 7-ago-2026: el auto-llenado cuando el recorte es pequeño y seguro

**Visto sobre el panel real, «entra completa» no aguantó.** Una vertical 3:4 de celular —el caso dominante— sale en 320×427 y deja 53 px negros repartidos arriba y abajo. Sobre la mesa eso es «el 11 % del alto»; en el marco **se ven**, y es lo primero que se nota. La regla pasa a ser:

| Foto | Recorte que pide | Qué hace la página |
|---|---|---|
| **3:4 vertical (celular)** | 11.1 % del **ancho** | **auto-llena** |
| **9:16 vertical** | 15.6 % del **alto** | **auto-llena** |
| 2:3 vertical | ninguno | nada, ya llena |
| 19.5:9 (captura) | 30.8 % del alto | franjas + aviso |
| 4:3 horizontal | 50 % del ancho | franjas + aviso |

**Una sola guarda, el umbral, y esto fue una corrección sobre la primera versión.** Al principio se prohibía además cualquier recorte del **alto**, por el veto de más abajo al recorte automático. **Medir la fototeca real lo tumbó**: de 29 fotos subidas desde el iPhone, **17 eran 9:16** y solo 8 llenaban. O sea que el caso dominante no es la 3:4 —que es lo que se supuso— sino la 9:16, y dejarla fuera mantenía las franjas justo donde se querían quitar.

**El veto de §4 sigue vivo donde importa.** Lo que prohíbe es el recorte automático **grande** y en silencio: media foto sigue sin tocarse, y una captura de pantalla tampoco. Lo que se acepta a cambio es que **7.8 % del alto por lado puede rozar una cabeza en un retrato muy justo**. Se tomó con los números delante, y el editor sigue a un toque para rehacer el encuadre.

El umbral es `AVISO_BARRAS` (20 %), reutilizado a propósito: es la misma pregunta —«¿esta franja es lo bastante grande como para que importe?»— y dos constantes que responden lo mismo acaban divergiendo. Si algún día se separan, ojo: las dos fracciones no son idénticas para la misma foto (una 3:4 deja 11.04 % de franja y pide 11.11 % de recorte).

**El auto-llenado NO es un `item.recorte`.** No lo decidió quien sube, así que no enciende «Quitar recorte», no cuenta como recorte en el diagnóstico y no cambia nada del editor — que además arranca justo en esa misma caja. Vive en `autoLlenar()`, `web/index.html`, aplicado como rectángulo fuente dentro de `procesar()`.

**Y tiene un costo que hay que tener presente:** recortar sube el peso, así que la 3:4 pasa de un objetivo de 28.5 KB a los 32,768 B completos. Con el auto-llenado eso deja de ser la excepción y pasa a ser el caso normal: **la tanda entera pesa más y se acerca al tope duro de 64 KB**, cuyo margen contra el récord medido ya es de solo 1.06× (ver el BOM).

**Por qué el default es entrar completa y no recortar al centro.** Un recorte automático decapita gente en las verticales y se come los extremos de las horizontales, en silencio, sin que nadie lo haya pedido y sin que se note en una miniatura de teléfono. Perder pantalla es reversible con un toque; perder la cabeza de alguien no se descubre hasta que el marco ya está en la sala. Y el aviso de barras de §6 no tendría nada que avisar: con recorte automático no habría barras nunca, y media §6 quedaría sin objeto.

**Lo que tampoco puede existir es la obligación de recortar.** Pedir un recuadro manual por cada una de 30 fotos de un viaje convertiría la subida en una tarea. El editor está a un toque de distancia y ahí se queda: quien no lo toque sube 30 fotos completas y el marco funciona.

Dos consecuencias que no son negociables:

- **Procesamiento secuencial, cediendo al event loop entre fotos.** Procesar N imágenes de 12 MP en cadena congela el hilo principal del celular y el navegador ofrece matar la pestaña. Worker con `OffscreenCanvas` solo si se mide lento — es optimización, no punto de partida.
- **Concurrencia de subida = 1.** Las fotos se suben de una en una. Dos multipart simultáneos escribiendo a la misma SD es pedir corrupción. Si falla la subida de algunas, se reintentan **solo esas**, no la tanda completa.

### La cola de preparación

Cada foto de la tanda es un elemento con **identidad propia y estado propio**, no una posición en un arreglo. Es lo que permite que se reintente únicamente lo que falló al subir, como exige el punto anterior.

| Estado | Qué significa |
|---|---|
| `pendiente` | Elegida, todavía sin preparar |
| `procesando` | Escalando o comprimiendo |
| `lista` | Preparada, esperando a subir |
| `error` | No se pudo preparar (archivo ilegible, formato que el navegador no abre) |
| `subiendo` | Su multipart está en vuelo. No se puede quitar ni recortar |
| `subida` | Está en la tarjeta del marco. **Terminal** |
| `fallo` | No se pudo subir. Reintentable, salvo el `413` |

- **Un error en una foto no tumba la tanda.** Se marca esa, se sigue con la siguiente, y al final se dice cuántas fallaron.
- **Elegir más fotos anexa, nunca reemplaza.** Volver al selector después de preparar diez y elegir tres más deja trece, no tres. Nadie va a intuir que abrir el selector destruye trabajo ya hecho —y son fotos ya listas para subir—, así que lo único que vacía la tanda es el botón de limpiar, que es un gesto explícito. Anexar al final además preserva «orden de selección = orden de subida».
- **Las que ya están en el marco sí salen al elegir más, y no contradice lo anterior.** Ese invariante protege trabajo **no entregado**; una foto en `subida` está en la tarjeta y lo único que se pierde al soltarla es su miniatura. Es además lo que promete el aviso del tope —«súbelas y vuelve por las demás»—: sin esto, quien sube 30 se queda encerrado contra el tope salvo que descubra el botón de limpiar. **El orden importa:** el descarte ocurre solo después de confirmar que hay fotos nuevas y antes de calcular el hueco. Al revés, el mero gesto de abrir el selector y cancelar borraría el rastro de lo subido a cambio de nada.
- **Se puede cancelar a media tanda.** Lo ya preparado se conserva —es trabajo válido— y las que faltaban quedan en `pendiente`, listas para continuar.
- **Se puede quitar una foto sin vaciar la tanda.** Está en su detalle, junto a «Guardar JPEG», y con confirmación porque destruye trabajo ya preparado. Es la única salida para una foto en `error` y para una que da `413`. Al quitarla se revoca su object URL y se repintan las **posteriores**, cuyo número visible sale de su posición en la cola. **No** se puede quitar una en `procesando` ni en `subiendo`: en ambos casos hay una promesa en vuelo que va a volver a escribir sobre ese elemento. Y sobre una ya `subida` el texto de la confirmación dice explícitamente que **no** la borra del marco.
- **El bucle de preparación busca la primera `pendiente`; no recorre el arreglo.** Es lo que lo hace inmune a que se anexen fotos a media tanda y a que se quite una: un `splice` bajo un iterador por índice corre el resto y deja un elemento sin visitar, en `pendiente` para siempre. A cambio, el cuerpo del bucle **tiene** que sacar al elemento de `pendiente` en su primera línea, o gira sobre él.
- **Tope de 30 fotos en la tanda.** No es memoria de los archivos preparados —30 × 32 KB son 960 KB— sino de las miniaturas: 30 bitmaps decodificados de 320×480 son ~18 MB vivos en la rejilla. El tope existe sobre todo porque el selector de iOS tiene «Seleccionar todo» a un toque, y vaciar un álbum de 800 fotos mataría la pestaña a media tanda, perdiendo el trabajo ya hecho y sin explicación. Al pasarse se avisa en lenguaje llano y se aceptan las que caben.

> **Sin verificar: el orden que entrega el selector de iOS.** No está medido si `<input type="file" multiple>` en Safari de iOS devuelve las fotos en el orden en que se tocaron o en el de la fototeca. Como ese orden acaba siendo el de reproducción en el marco, la rejilla numera cada miniatura para que sea visible antes de subir. Si la medición dice que el selector impone su propio orden, las salidas son ordenar por fecha de captura o permitir reordenar arrastrando — ninguna de las dos se construye a ciegas.

**Medido: el `File` del selector de iOS sobrevive.** Era el supuesto que sostenía toda la etapa 4, porque el recorte re-ejecuta el pipeline desde el archivo original y no desde el blob comprimido — es lo que hace que recortar y deshacer las veces que haga falta no acumule pérdida. La duda era real: en Safari de iOS esos archivos viven en un temporal y hay reportes de `NotReadableError` al releerlos pasado un rato.

Condiciones de la medición, en un iPhone 11: tanda de **30 fotos**, teléfono **bloqueado 10 minutos**, y después `file.arrayBuffer()` sobre la primera y la última — `byteLength` idéntico a `file.size` en ambas. **No se midió bajo presión de memoria de otras apps**, así que el `catch` de `NotReadableError` se queda por la regla 2: si el reproceso falla, la foto que ya estaba lista se conserva intacta y solo se avisa de que no se pudo recortar. La prueba está integrada como botón del panel de diagnóstico, para poder repetirla en la etapa 5.

### Presupuesto de bytes: proporcional a los píxeles de salida

`canvas.toBlob()` no acepta un tamaño en bytes, acepta una calidad de 0 a 1. Llegar a un peso objetivo exige iterar.

**El objetivo no es un tamaño fijo, es una densidad: 0.213 bytes por píxel** de la imagen de salida (≈1.71 bpp), que sale de 32,768 B ÷ (320×480).

Los 32,768 B del caso que llena la pantalla no son un número redondo por casualidad: es exactamente un cluster de la tarjeta (ver el BOM). Una foto dentro de presupuesto ocupa un cluster y ni un byte más.

> **Consecuencia contraintuitiva: recortar SUBE el peso del archivo.** Una 3:4 de celular entra completa en 320×427 con un objetivo de 28.5 KB; recortada a 2:3 sale en 320×480 y su objetivo pasa a los 32,768 B completos. Es correcto —la foto ocupa más pantalla y merece más bytes— pero es lo contrario de lo que la palabra «recortar» sugiere, y explica por qué una foto que topaba la cota de calidad puede dejar de topar tras recortarla.

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

**Cómo se comprueba: por orientación, nunca por igualdad de píxeles.** La pregunta es si el bitmap salió vertical u horizontal, comparado con lo que dicen el marcador SOF del JPEG y el tag `0x0112`. La comparación tentadora —¿las dimensiones del bitmap coinciden exactamente con las del SOF?— es frágil: se ha reportado que Safari de iOS subsamplea las imágenes grandes al decodificar, por factores de 2, 4 u 8. **Aquí no se ha observado a 12.2 MP** (ver la nota de alcance más abajo), pero la elección no cuesta nada y no depende de eso: la señal de orientación sobrevive a cualquier factor de subsampling y la de igualdad exacta no, así que se compara lo que es robusto.

> `naturalWidth` no sirve para detectar el subsampling: reporta las dimensiones lógicas decodificadas, no las del bitmap real.

#### Lo verificado

**En Safari de iOS, sobre un iPhone 11.** Es el motor que importa: el navegador del celular de la persona destinataria. La rotación EXIF **sí** se aplica sola por la ruta `<img>`.

| Archivo | Entrada | EXIF | SOF | Veredicto | Salida | Peso | q | Intentos | Total |
|---|---|---|---|---|---|---|---|---|---|
| image.jpg | 2316×3088 | 6 | 3088×2316 | **SÍ rotó** | 320×427 | 26.9 KB | 0.657 | 4 | 289 ms |
| IMG_1473.jpeg (de HEIC) | 3024×4032 | 6 | 4032×3024 | **SÍ rotó** | 320×427 | 40.3 KB | 0.620 | 8 | 204 ms |

`IMG_1473` es el caso de la cota: 8 intentos y q en el suelo de 0.620 significan que topó, y sus 40.3 KB están por encima de los 28.5 de presupuesto. Es el resultado documentado, no un error — el diagnóstico lo marca en ámbar. Recortada a 2:3 su presupuesto sube a 32,768 B.

Que el selector del iPhone entregue un `IMG_1473.jpeg` y no un `.heic` está medido también: transcodifica al elegir.

**En Chromium** (el navegador integrado de VS Code sobre macOS, que es un webview de Electron), con más proporciones y con el caso de transpose:

| Archivo | Entrada | EXIF | SOF | Salida | Peso | q | Intentos | Total |
|---|---|---|---|---|---|---|---|---|
| IMG_1434 | 3024×4032 | 6 | 4032×3024 | 320×427 | 29.2 KB | 0.688 | 4 | 134 ms |
| IMG_1470 | 2316×3088 | 5 | 3088×2316 | 320×427 | 29.6 KB | 0.862 | 6 | 78 ms |
| IMG_1347 | 4032×3024 | 1 | 4032×3024 | 320×240 | 31.9 KB | 0.881 | 5 | 131 ms |

**Esta tabla se tomó con el tope plano de 32 KB y el rango de calidad [0.30, 0.92] anteriores.** Con el presupuesto por densidad y el rango [0.62, 0.92] de arriba, las columnas de peso, `q` e intentos cambian por construcción — IMG_1347 baja a un objetivo de 16.0 KB. Lo que sigue siendo comparable son la salida, el veredicto de orientación y los pasos de halving.

#### Y tampoco son comparables entre motores: el encoder de WebKit pesa mucho más

`IMG_1434` está medida en los dos, y es la misma foto:

| | Chromium | Safari de iOS |
|---|---|---|
| Salida | 320×427 | 320×427 |
| `q` | 0.688 | **0.620** — el suelo |
| Intentos | 4 | 8 |
| Peso | **29.2 KB** | **50.7 KB** |

Misma entrada, misma salida, y en Safari pesa **1.74× más con menos calidad**. La única variable que queda es el encoder: el JPEG de WebKit es bastante menos agresivo que el de Chromium al mismo `q`. Los 8 intentos lo confirman — la búsqueda recorrió `[0.62, 0.92]` completa sin que nada cupiera en los 28.5 KB de presupuesto, y cayó al piso.

**Consecuencia práctica: cualquier peso medido en Chromium subestima el real**, y el peor caso solo tiene sentido medirlo en el teléfono. Es también por qué el suelo de `q` se topa tan a menudo en Safari y casi nunca en escritorio: no es que las fotos del teléfono sean peores, es que su encoder produce archivos más grandes. Si al ver la pantalla real la calidad a 0.620 resulta de sobra, el margen para bajar el piso es mayor de lo que sugerían las mediciones de escritorio.

#### Lo que sigue abierto

Dos puntos, y ninguno bloquea la construcción:

- **EXIF = 5 (transpose) está verificado solo en Chromium.** Es espejo diagonal, no solo rotación, y el arnés comprueba el intercambio de ejes pero **no** el reflejo — un navegador que rotara sin reflejar daría veredicto verde igualmente, en cualquier motor. El panel lo marca como «reflejo SIN verificar» para que no se lea como una comprobación que no es. Limitación conocida del arnés, no del pipeline. Lo medido en Safari de iOS son dos fotos con EXIF 6; ninguna con 5.
- **Calidad a q = 0.620 sobre la pantalla real.** Hay tres casos medidos detrás, y el suelo no es raro: `IMG_1473` (tirol blanco, 40.3 KB contra 28.5 de presupuesto), una malla metálica perforada (43.0 KB), un perro sobre césped (50.7 KB) y **60.4 KB**, el récord del 7-ago-2026 dentro de una tanda normal de 30 — a solo 1.06× del tope duro de 64 KB, ver el BOM. Los tres son textura fina en todo el encuadre. Puede haber bloqueo visible. **El hardware ya llegó y el pipeline ya pinta las siete fotos de prueba en placa (§6), así que esto dejó de esperar a nadie: es un juicio visual pendiente de ejecutarse** sobre el ST7796S, con una de esas tres fotos en la tarjeta. No tiene sentido evaluarlo en un monitor.

#### El subsampling: nota de alcance, no pendiente

**No es un pendiente alcanzable, y por eso sale de la lista de arriba.** No se observó a 12.2 MP, que es el techo del iPhone 11 con el que se mide y también el del teléfono de la persona destinataria. La foto de 48 MP (8064×6048) que se planteaba como caso para provocarlo no existe en la ruta de producción de nadie, y no hay dispositivo con el que generarla.

Sí importa entender **por qué el pipeline degradaría bien si ocurriera**, y por eso está escrito en el código del editor: `naturalWidth/Height` reporta las dimensiones lógicas decodificadas y `drawImage` interpreta su rectángulo fuente en ese mismo espacio lógico, de modo que es el navegador quien lo mapea al backing store real. El veredicto de orientación, por su parte, compara ejes y no igualdad de píxeles, precisamente porque la señal de orientación sobrevive a cualquier factor de subsampling.

### Sobre librerías de recorte

`Cropper.js` pesa ~12 KB minificado+gzip y cubre el caso. Dos trampas: la v2.x es una reescritura con API incompatible respecto de la 1.6.x (que es de lo que habla casi toda la documentación en línea), y `Croppr.js` — la alternativa de <6 KB — está abandonada en su repo original.

**Decisión: escribir el recuadro de recorte a mano.** El tamaño no es la restricción real (12 KB en 4 MB de flash no mueven la aguja); el argumento es no meter una dependencia externa en la pieza más delicada del proyecto, para un objeto que debe seguir funcionando dentro de cinco años sin mantenimiento.

### El editor de recuadro

Las fotos de celular vienen en 3:4 o 9:16 contra una pantalla de 2:3, así que siempre hay algo que decidir. El recuadro es arrastrable, con vista previa de cómo va a quedar, y **relación fija 2:3** — la de la pantalla, porque el objetivo del recorte es justamente que la foto la llene.

Tras la decisión de orientación (§6) el editor dejó de ser una mejora prescindible: es la compensación de que las fotos horizontales queden a media pantalla. Una 4:3 horizontal sin recortar sale en 320×240 con la mitad del alto en negro; recortada a 2:3 llena los 320×480.

Restricciones de implementación:

- **Opera sobre la imagen reducida a ~2× el destino**, nunca sobre los 12 MP originales. Arrastrar un recuadro sobre una imagen de 12 MP en un celular va a tirones.
- **Pointer Events** (`pointerdown` / `pointermove` / `pointerup`), que unifica mouse y touch en un solo camino de código. No `touchstart` y `mousedown` por separado. El contenedor lleva `touch-action:none`, sin lo cual Safari de iOS se queda con el gesto para hacer scroll de la página.
- **Estado en coordenadas normalizadas de 0 a 1**, para que sobreviva a los cambios de escala entre la vista previa y el render final. El recuadro se pinta en **porcentajes**, de modo que no hay una sola conversión a píxeles en el dibujado y girar el teléfono a media edición no lo mueve.

Al confirmar, se re-ejecutan el escalado y el encoder con el nuevo recuadro, y se revocan el blob y el object URL anteriores de esa foto — después de repintar, no antes, o Safari deja la miniatura rota.

#### De dónde sale la imagen de ~2×

No existe de antes: el pipeline libera el `<img>` antes de comprimir y el canvas de salida después. **Se reconstruye al abrir el editor, releyendo el `File`, y se libera al cerrarlo.** Retener una por foto serían 2.4 MB × 30 compitiendo con las miniaturas por la memoria de la pestaña; reconstruirla cuesta ~250 ms una vez por apertura, y ese número está medido.

**Necesita su propio contain, y esto es una trampa.** `escalar()` fuerza el canvas de salida a las dimensiones que reciba: **no preserva proporción** — el contain del pipeline vive en el cálculo del tamaño de salida, no dentro de `escalar()`. Pedirle 640×960 porque «2× el destino es 640×960» aplastaría a 2:3 toda foto que no lo sea, o sea una 3:4 y una 4:3. Y como el recuadro se pinta en porcentajes sobre esa imagen, **lo que se encuadra dejaría de ser lo que se recorta**: nada se ve roto, solo salen mal los encuadres. La previa además se ajusta a la pantalla del teléfono, no al marco, así que su contain es contra 640×960 y no contra 320×480.

**El factor 2× lleva un supuesto de densidad que conviene tener escrito.** El lienzo mide ~352 pt de ancho: a 2× —el iPhone 11, que es el teléfono de referencia— pide 704 px de dispositivo y la previa da 640, o sea un **9 % de submuestreo**. No llega a un factor completo y en una foto sin detalle fino es imperceptible; en pelo o textura, se nota. En una pantalla **3×** pediría ~1056 px y el déficit sería del **40 %**. Si al medir en el teléfono el recuadro se ve suave, la salida no es subir la constante a 2.5 sino **derivarla de `devicePixelRatio`** —cuesta el mismo código y no vuelve a envejecer—, con un tope para que un 3× sobre una foto de 12 MP no dispare el tamaño del canvas.

#### Contra qué espacio viven las coordenadas

Contra `naturalWidth × naturalHeight`, o sea el bitmap **ya orientado por el EXIF** — que es exactamente el espacio **lógico** en el que `drawImage` interpreta su rectángulo fuente. Va escrito explícito en el código porque es el tipo de ambigüedad que produce recortes girados.

Tiene una consecuencia útil: si el navegador subsamplea el bitmap real al decodificar, es él quien mapea el rectángulo al backing store, y aquí no hay nada que corregir.

#### Arrastrar y acercar

El recuadro **se arrastra y se acerca**. Solo arrastrar no bastaba: con relación fija y caja maximal, una 3:4 recorre el 11 % de su ancho y una 2:3 no se mueve en absoluto, así que el editor se abriría para no ofrecer nada justo en el caso que §6 dice que compensa.

El zoom es un `<input type="range">` nativo: táctil, accesible, sin manijas diminutas bajo el pulgar. Su tope sale de la resolución, no de un número inventado — es el que deja el recorte en 320 px de fuente, porque más allá se estaría ampliando. Cuando ese tope es 1 —una foto que ya es 2:3 y apenas mayor que el marco— el control **se esconde** en vez de quedar muerto. Si en el celular el slider se siente ajeno, la salida es pinch de dos punteros sobre el mismo estado normalizado; el estado no cambia.

#### Una foto que ya es 2:3

El editor **se abre igual**, con la caja cubriendo la foto entera, porque sigue habiendo margen para acercarse a una cara. Si al confirmar el recuadro resulta ser la imagen completa, se guarda «sin recorte» y la foto sigue contando como completa. La comparación va **con tolerancia, nunca por igualdad**: la caja maximal de una 2:3 exacta sale de una división que en flotante da 0.9999999999999999, y comparar contra 1 guardaría como recorte la foto entera — un reproceso que no cambia un píxel y un botón «Quitar recorte» sin nada que quitar.

#### El recorte es reversible

«Quitar recorte» devuelve la foto a completa y la reprocesa desde el original. Sin eso, un recuadro mal hecho solo se deshacía quitando la foto de la tanda.

### Contrato HTTP

```
GET  /                  → página, gzip desde PROGMEM, Content-Encoding: gzip
GET  /list              → text/plain, streaming de /manifest.txt desde la SD
GET  /photo?n=NOMBRE    → JPEG desde /fotos/NOMBRE
                          400 nombre que no cumple ^[0-9]{8}\.JPG$
                          404 nombre válido que no existe
POST /upload            → multipart, campo "foto"
                          200 {"ok":true,"n":"00000042.JPG"}
                          400 campo distinto de "foto", o archivo vacío
                          413 archivo > 64 KB
                          503 subida solapada
                          507 SD llena
POST /delete            → {"n":"00000042.JPG"}
                          400 nombre que no cumple el patrón
```

**El `400` y el `503` no son detalle de implementación.** El `400` es la
contrapartida de la validación obligatoria de abajo: si el nombre inválido
respondiera cualquier otra cosa, la página no tendría forma de distinguir «ese
nombre no vale» de «esa foto ya no está». Y el `503` existe porque **la
concurrencia 1 es disciplina del cliente y el servidor no puede confiar en
ella**: dos multipart simultáneos escribiendo el mismo buffer dan una foto
corrupta en silencio, así que el segundo se rechaza en vez de mezclarse. Una
subida que recibe `503` se reintenta igual que una que falló por red.

**Sin miniaturas.** Las fotos pesan 32 KB en el peor caso; la galería sirve los archivos reales y se ahorra generarlas, guardarlas y mantenerlas sincronizadas.

**`/list` devuelve el manifiesto crudo**, con `request->send(TARJETA, "/manifest.txt", "text/plain")`. Es streaming directo desde la tarjeta: cero heap, cero parser en ambos extremos, y el JavaScript hace `split('\n')`. Devolver JSON obligaría al ESP32 a construir la cadena completa en memoria, que es justo lo que el formato del manifiesto (§3) busca evitar.

**El objeto es `TARJETA`, y escribir `SD` aquí es la trampa cara de este proyecto.** Las versiones anteriores de este documento publicaban `request->send(SD, …)`, que es el `fs::FS` de la librería `SD` de Arduino — la que **no monta las tarjetas de este proyecto** (§11 y el BOM). `TJpg_Decoder.h` arrastra `<SD.h>` con ese objeto global por su `TJPGD_LOAD_SD_LIBRARY`, así que el snippet equivocado **compila sin una sola queja** y falla en tiempo de ejecución. El objeto bueno lo define `firmware/src/tarjeta.h`, sobre el driver `sdspi` del IDF.

> **Un manifiesto VACÍO hay que atajarlo a mano, y `exists()` no basta.** Medido
> en placa: con un `/manifest.txt` de 0 B —tarjeta sin fotos, que es un estado
> legítimo— `ESPAsyncWebServer` responde **`500 Invalid data in handler`** en vez
> de un cuerpo vacío. El mecanismo está en `WebResponses.cpp:727`, donde
> `AsyncFileResponse` decide si el archivo sirve con `if (!_content.available())`:
> **un archivo válido pero vacío da 0 y es indistinguible de uno que falta**, así
> que se va a buscar el `.gz`, tampoco lo encuentra, y pone un 404 sobre una
> respuesta que el request layer ya juzga inválida. La salida es comprobar
> `size() == 0` y mandar un `200` con cuerpo vacío.

**Validación obligatoria en `/photo` y `/delete`:** el nombre recibido debe cumplir `^[0-9]{8}\.JPG$` exacto, no basta con concatenarlo a `/fotos/`. Sin esa comprobación, `?n=../manifest.txt` sale del directorio. El esquema rígido de nombres (§3) hace que la validación cueste tres líneas y elimine la clase entera de problema.

**El upload nunca escribe a la SD desde su callback.** Corre en el hilo `async_tcp` a prioridad 10, y bloquearlo con I/O de tarjeta provoca watchdog. Se acumula en heap y se escribe una sola vez al terminar, con tope duro de 64 KB. Detalle completo en el BOM.

**`onNotFound` tiene que estar definido y responder corto.** Cinco rutas y ningún catch-all significa que todo lo demás cae ahí, y no es hipotético: el navegador pide `/favicon.ico` solo, sin que nadie se lo mande — medido en Chrome. Un 404 con dos palabras es la respuesta correcta (el icono de pestaña sale genérico y ya); lo que no puede quedar es sin definir.

> **Por eso la página nunca mete rutas nuevas en el historial.** Hay **dos** niveles empujados —rejilla → detalle → editor de recuadro— y los dos usan `history.pushState` con la **misma** URL. Medido en Chrome: `/foto/3` lanza `SecurityError` en `file://` —por el origen `null`, que es coincidencia y no una protección— pero se **acepta sin quejarse sobre `http://`**. O sea que una ruta inventada pasaría todas las pruebas de escritorio y de LAN, y solo daría 404 en el marco, al recargar o compartir el enlace: el único entorno donde nadie va a estar depurando. Si alguna vez hace falta estado en la URL, va en el fragmento, que no viaja al servidor.
>
> **Con dos niveles, quien navega es el botón, no la función de cierre.** El detalle podía permitirse llamar a `history.back()` desde su propio cierre porque su guard convierte en no-op el `popstate` que él mismo provoca. Ese truco se rompe al añadir el editor: su cierre dejaría el estado en nulo y el `popstate` de su propio `back()` cerraría también el detalle, saltándose dos niveles de un golpe. Así que las funciones de cierre solo desmontan y son los botones los que llaman a `history.back()` — el `popstate` encuentra entonces el editor todavía vivo y cierra exactamente un nivel.

### La capa de red

**Bucle propio, no dentro del de preparación, y los dos nunca corren a la vez.** Preparar y subir tienen ritmos distintos —87 ms por foto contra el tiempo de red— y esto permitiría solaparlos, pero no obliga a hacerlo, y la tanda de 30 se prepara entera en 2711 ms medidos. Solapar compra esos dos segundos y paga con dos cancelaciones simultáneas, dos significados para el mismo botón, y la carrera de que el bucle de subida agarre un elemento que el reproceso del recorte está a punto de reemplazar.

**La exclusión mutua es de dos lados, y la segunda mitad es la que se olvida.** Cada bucle mira las dos banderas — si solo la mirase el de subida, elegir fotos a media tanda arrancaría a decodificar 12 MP en el hilo principal con un multipart en vuelo. Y con esa guarda puesta hace falta su contrapeso: **el bucle de subida, al terminar, tiene que relanzar el de preparación**, o lo que se eligió mientras subía se queda en `pendiente` para siempre, porque nadie más va a dispararlo.

La dirección contraria no necesita contrapeso: **la subida siempre la dispara un toque**, nunca el final de la preparación. Una tanda no se sube sola.

**No hay subida de una foto suelta.** El botón del detalle sube la tanda, igual que el de la rejilla. No es pereza: el orden de subida es el del manifiesto y el de reproducción en el marco (§3), así que subir una foto fuera del orden de la rejilla lo rompería.

#### Qué se hace con cada respuesta

| Código | Qué significa | Reintentable | Qué se dice |
|---|---|---|---|
| `200` con `{"ok":true,"n":…}` | subida | — | nada; el mosaico queda con su marca |
| `200` con otra cosa | el marco contestó algo que no es el contrato | sí | «El marco contestó algo que no se entiende.» |
| `400` | **bug de la página**: campo distinto de «foto», o cuerpo vacío | sí | «El marco no aceptó esta foto.» |
| `413` | la foto se pasó del tope duro | **no** | «Pesó más de lo que el marco acepta. Quítala de la tanda.» |
| `503` | **bug de la página**: dos subidas solapadas | sí | «El marco estaba ocupado. Inténtalo otra vez.» |
| `507` | tarjeta llena — **corta la tanda entera** | no | «El marco se quedó sin espacio.» |
| otro | — | sí | «No se pudo subir esta foto.» |
| red o timeout | — | sí | «No se pudo conectar con el marco.» |

**El `413` tiene que distinguirse de «se cayó el WiFi», y no solo en el texto: las acciones son distintas.** Reintentar no arregla una foto que pesa de más, así que su única salida es quitarla de la tanda, y eso es lo que dice el mensaje. El mensaje no lleva ningún número — el tope real se puede mover, y en producción nada técnico es visible.

**El `400` y el `503` no son problema de quien sube fotos: son de esta página.** La concurrencia 1 es disciplina del cliente, así que un `503` significa que el cliente la incumplió. El mensaje que se ve es genérico, pero bajo diagnóstico la barra lo grita con el código y con de quién es el bug.

**Corte tras dos fallos de red seguidos.** Con el módem apagado, 30 fotos por el timeout serían cinco minutos de machacar. Las que no se llegaron a intentar **se quedan en `lista`, no en `fallo`**, así que volver a tocar «Subir al marco» las retoma tal cual.

**Y el corte se anuncia como lo que es: un hecho de la tanda, no de dos fotos.** Verificado en placa, y el primer intento se quedó corto: decir «2 fotos no se pudieron subir, tócalas para ver por qué» es correcto y a la vez inútil justo aquí, porque no explica por qué la subida se detuvo antes de tiempo y esconde en el detalle la única causa sobre la que quien sube **puede actuar**. El aviso de la rejilla dice la causa completa, que el resto de las fotos siguen ahí, y **nombra el botón** con el que se reanuda. Los demás fallos sí son por foto y su motivo se queda en el detalle.

**Sin reintento automático.** Un reintento silencioso enmascara justo los dos códigos que tienen que gritar, y ante una caída real de red el segundo intento inmediato falla igual. El reintento es un toque, y devuelve a `lista` solo los fallos que se pueden reintentar — «se reintentan solo esas, no la tanda completa», sin una segunda ruta de código.

#### Detalles que no se deducen

**Ninguna cabecera propia en la petición de subida.** `multipart/form-data` es *safelisted* y no dispara preflight; basta un `X-Debug` para que salga un `OPTIONS` que, con cinco rutas y sin catch-all, cae en `onNotFound` → 404, y la subida se rompe sin explicación. Lo mismo vale para el `GET` de verificación: **nada de `cache:"no-store"` en el fetch**, porque `Cache-Control` no es una cabecera safelisted. El `no-store` va en la **respuesta**, que es donde lo pone el servidor.

**Timeout por foto con `AbortController`, no con `AbortSignal.timeout()` ni `AbortSignal.any()`** — el segundo es Safari 17.4+, y la única ruta por la que entran fotos al marco no se apuesta a una versión de WebKit. El valor es la suma de los peores casos redondeada hacia arriba: primera conexión de Safari con handshake, el multipart sobre LAN, y la escritura de la tarjeta, que puede ser de cientos de milisegundos cuando el bloque necesita borrado previo. Los timers estrangulados en segundo plano disparan **tarde, nunca antes**, así que no puede haber abortos espurios por eso.

> **Los 10 s quedaron confirmados con margen, y ya no son una estimación**
> (2026-08-07). Los dos sumandos están medidos: **315 ms/foto** del multipart
> desde el iPhone contra el banco, y la escritura a la tarjeta con **241 ms de
> media para crear un archivo nuevo** y **464 ms en el peor caso observado**, un
> borrado previo de bloque. El peor caso completo son ~800 ms, o sea **12× de
> margen**. El valor no cambia; lo que cambia es que ahora tiene base.
>
> Ojo con la cifra de escritura que se cita: la de 67 ms que también aparece en
> el BOM es de **reescribir el mismo archivo**, que no paga asignar clusters ni
> actualizar el directorio y la FAT. La que corresponde a subir una foto es la de
> 241 ms. Y los 464 ms **no son una cota** —salieron de corridas de 10 muestras y
> la cola está submuestreada—, igual que el peor caso de peso de JPEG de esta
> misma sección tampoco se usa como cota.

**Cancelar corta al terminar la foto en curso; no aborta el multipart en vuelo.** Abortarlo deja la duda de si la foto aterrizó o no, y a 34 KB la espera es de menos de un segundo. **Lo ya subido se conserva**: está en la tarjeta del marco y es trabajo válido.

**La pestaña en segundo plano no lleva lógica propia, y ahora está medido que no hace falta.** En un iPhone 11, con una tanda de 30 en curso y el teléfono **bloqueado dos minutos**: la subida se pausa, y al desbloquear continúa donde iba. Ni una foto perdida, ni un `fallo`.

Lo que se estaba arriesgando no era que el `fetch` muriera —eso ya estaba cubierto: esa foto quedaría en `fallo` reintentable y el corte por dos seguidos evitaría que la tanda se desangre— sino lo contrario: que el **timer del timeout siguiera corriendo mientras el JS dormía** y marcara como caída una foto que estaba perfectamente en vuelo. No pasa: se suspenden los dos juntos. Es la misma propiedad que hace seguro el timeout, «los timers estrangulados disparan tarde, nunca antes», llevada al extremo de dos minutos.

Bajo diagnóstico se anota cuántas fotos se subieron con la pestaña oculta, por si algún día el comportamiento cambia.

**El progreso es por foto, nunca dentro del archivo.** `fetch()` no reporta progreso de subida, y los *request streams* con `duplex:"half"` no están en WebKit. A 34 KB sobre LAN el progreso intra-archivo duraría décimas de segundo. Nada de `XMLHttpRequest` por eso.

**El orden del manifiesto es el de la rejilla solo si no falla ninguna.** Medido en placa: de una tanda de 10 con tres fallos provocados, las siete que subieron a la primera se llevaron los nombres `001`–`007` y **las tres del reintento se llevaron `008`–`010`, al final**. El contador del ESP32 no retrocede (§3) y no puede: reutilizar números daría colisiones. O sea que un reintento reordena la reproducción.

Se acepta tal cual. Arreglarlo obligaría a resubir la tanda entera por una foto que falló, que es exactamente lo que este documento prohíbe dos párrafos más arriba, y el efecto visible es que unas fotos salen en otro orden en un marco que las pasa en bucle. Lo que **no** se acepta es que el diagnóstico lo reporte como avería: el botón de comprobar orden dice «esperado: hubo reintentos» cuando el desajuste tiene esa causa.

**No se comprueba en producción**, porque un desajuste no produce ninguna acción que la persona pueda tomar. Bajo diagnóstico sí hay un botón que lo verifica contra `/list`.

**La verificación de integridad es síncrona dentro del bucle, y detrás del diagnóstico.** Vuelve a pedir la foto por `/photo` y compara los bytes contra el blob que se subió. Síncrona no es descuido: un servidor que comparta el buffer entre `/photo` y `/upload` responde `503` a las lecturas mientras hay una subida en curso, así que lanzarla en paralelo para no frenar la tanda se comería `503` sistemáticos. Y **un `404` aquí no es corrupción** —es que el servidor no retuvo esa foto—, así que se reporta distinto de «difiere en el byte N»; confundirlos es salir a perseguir un bug que no existe.

**La base del servidor es `""` — mismo origen — y ese es el valor de producción.** La compuerta que decide si los botones de subir existen se compara **contra `null`, jamás por veracidad**: `""` es *falsy*, y un `if (base)` apagaría los botones justo en el marco, que es el único sitio donde tienen que estar. Abierta a mano desde el disco (`file://`) la página se cae sola a `null`, o sea al modo mock donde «Guardar JPEG» es la acción; y bajo diagnóstico se puede apuntar a otra máquina por el **fragmento** de la URL, que no viaja al servidor.

#### La galería de lo ya cargado, y borrar — HECHO, 7-ago-2026

`POST /delete` era la única ruta del contrato que no llamaba nadie. Ya la llama la galería, que muestra **todas** las fotos de la tarjeta —no solo la tanda en curso— y deja quitar cualquiera. Verificado en placa: `200`, `400` con `../manifest.txt`, `400` con un POST sin cuerpo, y `200` idempotente sobre un nombre válido que ya no existe.

**Sin endpoints nuevos y sin dependencias nuevas.** `/list` y `/photo` bastaban, y el cuerpo JSON se lee con `strstr`/`strchr` como en el banco — ocho líneas, cero heap, cero parser, que es el mismo argumento con el que §3 justifica el manifiesto en texto plano. **ArduinoJson se evaluó y se descartó**: una octava dependencia del stack para leer una clave de formato fijo en un cuerpo de 30 B.

**La galería pagina de 60 en 60.** No es capricho: §4 mide que 30 bitmaps de 320×480 son ~18 MB vivos, y de ahí sale `MAX_TANDA`. Una galería de cientos sin acotar es el mismo problema multiplicado, y `loading="lazy"` ahorra la red pero no garantiza que el navegador suelte lo que sale de pantalla.

**Multi-selección y una sola confirmación**, porque el caso que esta sección nombra —«las diez de aquel viaje»— con una confirmación por foto serían diez. El borrado es **secuencial, concurrencia 1**: cada `/delete` reescribe el manifiesto entero en el marco, y dos a la vez sobre el mismo archivo es pedir corrupción. Misma disciplina que la subida, y el arnés lo comprueba contando peticiones en vuelo.

Cuatro detalles de implementación que no se deducen:

- **El botón de entrada NO puede vivir en `.actions`.** `pintarBarra()` oculta `#cardAcciones` cuando la tanda está vacía — y entrar sin tanda es exactamente el caso de venir solo a curar. Va en la tarjeta del selector, la única siempre visible. Se apaga con la misma compuerta `CFG.BASE === null` que los botones de subir.
- **Los mosaicos de la galería son `<div>`, no `<button>`.** Los de la tanda sí son botones, y anidar un botón dentro de otro es HTML inválido además de hacer burbujear el click. `.mosaico` es solo CSS.
- **Sigue habiendo UN solo listener de `popstate`.** Se le añadió la rama de la galería **antes** de la delegación a `cerrarDetalle(true)`, porque el guard de esa función la convierte en no-op y dejaría la galería montada. Un segundo `addEventListener` haría disparar los dos en cada gesto de atrás.
- **La galería copia el patrón del editor, no el del detalle**: `cerrarGaleria()` solo desmonta y son los botones los que navegan. Con más de un nivel, que la función de cierre llame a `history.back()` hace que su propio `popstate` cierre también el nivel de abajo.

##### La galería tiró el marco, y la causa no era la que gritaban los logs

**Primera versión medida en placa: el marco se reiniciaba en bucle** al abrir la galería desde el teléfono. En pantalla, «No hay fotos»; en la página, «el marco no tiene fotos» sobre una tarjeta con 30. El serie escupía `no free file descriptors`, y **seguir esa pista fue el error**.

El backtrace decodificado dice otra cosa:

```
operator new → __cxa_allocate_exception → std::terminate → abort
  AsyncWebServerRequest::beginResponse  (WebRequest.cpp:1202)
```

Es **heap**: `new` falla y no queda memoria ni para construir el `bad_alloc`, así que sale un `abort()`. Los descriptores agotados son el síntoma de que hay demasiadas peticiones vivas, no la causa de la caída.

**Subir `max_files` del VFS empeoró el fallo, y ésa es la lección cara.** Cada descriptor de FatFs arrastra su buffer de sector: pasar de 5 a 12 costó **~30 KB de heap** medidos —el libre antes de `autoConnect()` cayó de 110,324 a 80,644 B— y con menos heap el marco pasó de caerse con ~30 conexiones a caerse **con 4**. Se revirtió.

**La defensa es un tope de peticiones concurrentes**, no de archivos abiertos: `PHOTO_MAX_VUELO = 2`, contador `volatile` y `onDisconnect` devolviendo el hueco, con `503` por encima —barato, antes de asignar nada grande—. Y no hay alternativa desde el servidor: `ESPAsyncWebServer` solo expone `maxClients` para WebSocket, y el tope real de conexiones vive en `MEMP_NUM_TCP_PCB` de LWIP, dentro de los binarios precompilados del IDF, igual que `FF_FS_EXFAT`.

**Medido después del arreglo: 30 peticiones simultáneas dan 12 `200` y 18 `503`, cero caídas**, y `loop()` sigue pintando con `heap +0`.

Del lado de la página, dos cosas que se derivan:

- **La galería carga de 2 en 2** (`GALERIA_EN_VUELO`). `loading="lazy"` **no sirve** para esto: decide por visibilidad, no por concurrencia, y en una rejilla de tres columnas ya hay una docena de mosaicos a la vista.
- **El `503` hay que reintentarlo de verdad.** Un `<img>` que falla se queda roto para siempre, porque en cuanto tiene `src` sale de la cola. Se le quita el `src` y se reencola, hasta tres veces, con espera creciente.

Y una corrección de `/list` que salió de aquí: **distinguir «no hay manifiesto» de «no se pudo abrir»**. Tratarlos igual es lo que hacía mentir a la página — con el heap agotado contestaba `200` con cuerpo vacío. Ahora `exists()` decide: sin manifiesto, `200` vacío, que es un estado legítimo; con manifiesto ilegible, `503`, que es transitorio y reintentable.

> **El `Content-Type: application/json` no es safelisted, y eso acota dónde se puede probar.** Sobre mismo origen —el valor de producción— da igual; desde otro origen dispara un `OPTIONS` que, con cinco rutas y sin catch-all, cae en `onNotFound` → 404. O sea que **la galería no se puede probar contra el banco con el override `#marco=`**: se prueba contra el marco real o con el `fetch` sustituido del arnés.

**No hace falta inventar contrato: ya está entero.** `/list` devuelve el manifiesto crudo por streaming, `/photo?n=` sirve cada archivo, y no hay miniaturas que generar porque las fotos pesan 32 KB (arriba, en esta misma sección). La galería es una pantalla nueva en `web/index.html`, no endpoints nuevos.

> **El motivo NO es el espacio, y conviene tenerlo claro antes de diseñarla.** §3 mide ~121,000 fotos de capacidad al peso medio real: nadie va a llenar la tarjeta, y un medidor de espacio libre sería un número técnico en pantalla sin ninguna acción detrás — regla 3. Lo que la galería resuelve es **curación**: quitar la foto que salió movida, la que ya no viene a cuento, o las diez de aquel viaje. Diseñarla como «gestión de espacio» llevaría a construir lo que no sirve y a no construir lo que sí.

**Dos trampas del borrado que ya se pueden anotar**, porque salen de decisiones cerradas en §3:

- **Borrar tiene que reescribir `/manifest.txt`, y el contador de NVS NO retrocede.** Reutilizar el número de una foto borrada daría colisiones con lo que siga en el manifiesto. El hueco en la numeración es correcto y no hay que taparlo.
- **Un borrado a medias no puede dejar el marco en negro.** Si el archivo desaparece pero el manifiesto sigue nombrándolo, el fallback de §3 recorre `/fotos/` y lo reconstruye. Eso ya está especificado; lo que hay que respetar es el orden — borrar el archivo y reescribir el índice, nunca al revés.

Y hay un hallazgo medido en placa que no se puede perder mientras tanto: **el cuerpo de `/delete` debe ir con `Content-Type: application/json`**. Con `x-www-form-urlencoded`, `AsyncWebServer` parsea el cuerpo como formulario, el callback de cuerpo nunca lo ve, y el servidor contesta `200` sobre un borrado que no ocurrió.

### Dónde vive la página

**Embebida en el binario, gzipeada, en PROGMEM.** No en SPIFFS, no en la SD.

- **No en la SD** porque una tarjeta muerta dejaría al marco sin página, ni siquiera para diagnosticar por qué no funciona.
- **No en SPIFFS** —aunque `huge_app.csv` reserva 896 KB— porque obligaría a un `pio run -t uploadfs` aparte del flasheo. Un segundo paso que hay que recordar tres años después es exactamente el tipo de cosa que rompe un objeto sin mantenimiento.

Con PROGMEM hay una sola imagen que flashear y es imposible que el firmware y su página queden desincronizados. Consecuencia para `web/`: el script de build no solo concatena a un HTML único, también lo gzipea y emite el array de C.

**El array de C va a un archivo aparte, y ese archivo se decide antes de generarlo.** Tras la etapa 4 la página pesa 65.2 KB en claro y **22.0 KB gzipeados** con `gzip -9` (eran 42.1 y 14.2 tras la etapa 3). En flash sigue siendo irrelevante —3 MB de partición—, pero **lo que importa es el crecimiento**, porque como fuente de C son cinco caracteres por byte (`0xNN,`): esos 22.0 KB son ya **~110 KB de texto que cambia entero cada vez que se toca una línea de la página**, y eran ~71 KB tras la etapa 3.

Con ese número la decisión se aclara: `.gitignore` —y entonces **el build tiene que ser reproducible, o el repo no basta para flashear**— o versionarlo sabiendo que va a ensuciar todos los diffs. Lo que no puede pasar es descubrirlo al hacer el primer commit.

**El empaquetado toma únicamente `web/index.html`.** `web/test/` es el arnés de regresión (ver su README) y no puede acabar en PROGMEM.

---

## 5. Conectividad

### Servidor web: `ESP32Async/ESPAsyncWebServer`

**El repositorio original de `me-no-dev` está archivado desde el 20 de enero de 2025 y es de solo lectura.** El sucesor mantenido por la comunidad es `ESP32Async/ESPAsyncWebServer`, que incorpora los fixes de concurrencia del fork de `yubox-node-org`.

Esto importa aquí en concreto: **el upload multipart es exactamente donde vivían esos bugs**, y el upload multipart es el único camino por el que entran fotos al marco. Arrancar con el repo archivado sería empezar con código congelado hace año y medio en la ruta crítica.

Versiones y sintaxis exacta de `lib_deps` en el BOM.

### Portal cautivo obligatorio (WiFiManager)

Como el marco vivirá en casa ajena, **no se pueden dejar credenciales en el firmware**.

> **Riesgo CERRADO, 6-ago-2026.** El `Guru Meditation Error` del issue #1797 **no ocurre** con `WiFiManager@2.0.17` sobre pioarduino `55.03.311` (arduino-esp32 3.3.11). Provisioning verificado de extremo a extremo en placa: virgen → AP `Marco-Fotos` → portal → guardar → conecta → **reinicio y reconecta solo, sin AP**. El confusor #1 queda descartado con números: `mayorBloque` no se movió de 110,580 B en ninguna de las seis mediciones, y con el portal abierto el mínimo de heap libre fue de 189,108 B. Cifras completas y el matiz de lo que no se midió, en el pendiente #5 del BOM. Los tres planes B quedan sin usar.

#### `getWiFiIsSaved()` no se llama nunca sin arrancar antes el driver

Lo que salió roto en esa prueba no fue el panic. Sobre una placa **sin** credenciales, `wm.getWiFiIsSaved()` devuelve «sí»: acaba en `esp_wifi_get_config(WIFI_IF_STA,&conf)`, que devuelve `ESP_ERR_WIFI_NOT_INIT` y **deja `conf` sin tocar** si el driver no arrancó, y WiFiManager ignora ese código y arma el `String` con pila sin inicializar. Medido: `ESP_ERR_WIFI_NOT_INIT` → «sí»; tras `WiFi.mode(WIFI_STA)`, `ESP_OK` con ssid vacío → «no».

**Por eso `setup()` llama a `WiFi.mode(WIFI_STA)` antes de preguntar.** Es una línea, y sin ella la tabla de más abajo —la que decide entre el QR de setup y la pantalla de «conectando»— se resuelve al revés justo en el primer arranque: quien recibe el marco ve «conectando» y no tiene nada que escanear. **Es la regla 1 entera.**

Y es del tipo que no se descubre probando: con credenciales guardadas las dos lecturas coinciden en «sí», así que el fallo **solo existe en el arranque virgen**, que es el único en el que alguien provisiona el regalo. Tampoco vale fiarse de que `autoConnect()` acierte — acierta por otro camino, con su propia llamada comentada y un `wifiIsSaved = true` a pelo (`WiFiManager.cpp` 2.0.17, línea 283).

#### El portal va en español y de una sola página

Es la única pantalla que quien recibe el marco **tiene** que atravesar para que el regalo funcione, así que dejarla en inglés era la regla 1 a medias.

**El `wm_strings_es.h` que trae la librería no sirve: está sin traducir** — contenido idéntico al inglés, encabezado «SAMPLE», versión 0.0.0, comprobado cadena por cadena. Las cadenas propias viven en `firmware/src/wm_strings_marco.h` y entran por el build flag `WM_STRINGS_FILE`, **nunca editando la librería**, igual que `USER_SETUP_LOADED` con TFT_eSPI. Detalle y verificación en el BOM.

Se traduce lo que se ve en el camino real —raíz → «Conectar a tu WiFi» → lista de redes → contraseña → «Guardar»— más los mensajes de estado, que es donde alguien se atora: «La contraseña no es correcta» tiene que distinguirse de «No se encontró esa red». Y se dice **«Red»** y no «SSID»: nadie tiene por qué saber qué es eso.

**`wm.setMenu({"wifi"})` deja una sola página**, y las que se quitan no eran funcionalidad que valiera la pena conservar: `update` **no puede funcionar** (`huge_app.csv` tiene una sola partición de app, sin `ota_1`, así que `Update.begin()` falla), `erase` es redundante con que el portal vuelva a salir solo cuando cambia el módem —y destructiva a un toque—, e `info` son ~40 filas de diagnóstico, que es exactamente lo que la regla 3 prohíbe. Lo único útil de `info`, la IP, ya lo da el QR de uso diario.

> **Los otros dos confusores del panic siguen documentados por si algún día reaparece:** el **orden de inicialización** no aplica —`server.begin()` del `AsyncWebServer` corre después de que `autoConnect()` retorna, no en paralelo—, y el fade del LED (`asentarLed()`, §9) es landmark de bisección y no sospechoso: `delay()` en arduino-esp32 es `vTaskDelay`, cede CPU y no corrompe memoria.

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
| **Uso diario** | Toque largo con WiFi arriba, **y automáticamente al terminar el provisioning** | `http://192.168.1.47` (IP actual) |

El formato `WIFI:` es reconocido nativamente por Android e iOS: al escanearlo con la cámara se conectan solos. **La persona nunca ve una contraseña.**

#### El QR de uso diario tiene que salir solo al terminar el provisioning

**Pendiente de implementar.** El toque largo no basta y esta sección ya lo admitía dos párrafos más abajo: *«nadie le va a explicar a la persona destinataria que ese gesto existe»*. Eso está bien para una ruta de recuperación, pero no para el camino principal.

El momento en que alguien acaba de meter la contraseña del WiFi es exactamente el momento en que quiere subir fotos, y ahora mismo el único puente hasta la página es teclear una IP que nadie le ha dicho — o descubrir un gesto invisible. Es la regla 1 rota justo en el último metro del arranque.

**El disparador es «se acaba de provisionar», NO «se conectó».** Pintarlo en cada arranque con éxito dejaría un QR unos segundos cada vez que se enchufa el marco: se ve a proyecto y no a producto, que es el mismo argumento por el que el QR de setup no se pinta incondicionalmente.

La condición sale gratis porque ya se calcula: **`getWiFiIsSaved()` era falso antes de `autoConnect()` y la conexión tuvo éxito**. O sea que hubo portal y ahora hay red.

> Esa condición **solo es fiable con `WiFi.mode(WIFI_STA)` delante**. Sin esa línea `getWiFiIsSaved()` devuelve «sí» sobre una placa virgen —ver más arriba en esta misma sección—, y el QR de uso diario no saldría nunca en el único arranque que lo necesita. Es el mismo bug que se comía el QR de setup, con el segundo síntoma escondido detrás.

**Decidido, 7-ago-2026: se quita con la primera petición a `/` o a los 60 s, lo que llegue antes — y hacen falta LAS DOS.** Esta sección las planteaba como alternativas y no lo son:

- Solo el **temporizador** lo quitaría con la persona todavía tecleando la dirección.
- Solo la **petición HTTP** lo dejaría pintado indefinidamente si quien provisiona se distrae o se queda sin batería, y un QR permanente en la sala es regla 3.

Cuesta una bandera `volatile` en el handler de `/` y un deadline comprobado en `loop()`. La bandera se pone en `/` y no en `/list` ni en `/photo`: ésas llegan detrás de `/` de todas formas.

> **El QR en pantalla tiene que BLOQUEAR el cambio periódico de foto**, y es el detalle que se olvida: sin eso, los 30 s del temporizador de §6 pintan una foto encima del QR y la única pista para subir fotos se va sin que nadie la haya usado. Y al retirarlo hay que repintar lo que tapó: si no hay tarjeta, `showNextPhoto()` se sale de inmediato y el QR se quedaría en pantalla pese a haber vencido.

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

> **Esa tabla describe el contain puro, y desde el 7-ago-2026 las filas verticales ya no se ven así en el marco.** Las franjas resultaron visibles sobre el panel real, así que la página **auto-llena** cuando el recorte que hace falta está por debajo del 20 %: la 3:4 (11.1 % del ancho) y la **9:16** (15.6 % del alto) salen en 320×480 sin franjas. Las horizontales y la captura 19.5:9 se quedan tal cual, porque pedirían del 30 al 56 %. La regla completa, con su costo en peso, en §4.

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

`TFT_eSPI` (Bodmer) + `TJpg_Decoder`. Trabaja en bloques MCU de 16×16 empujados directo por SPI. **Por esto no hace falta un ESP32-S3**: nunca se arma el framebuffer completo de 300 KB.

#### La RAM del decodificador: 3,580 B fijos y CERO heap — corrección, 2026-08-07

Este documento decía «buffer del JPEG (12-32 KB según el tamaño de salida; 64 KB en el tope duro) + ~4 KB de workspace», y **eso describe el camino desde un array en memoria, no el camino desde archivo, que es el que usa el marco.**

`TJpgDec.drawFsJpg(x, y, ruta, fs)` **no bufferea el JPEG**: `jd_prepare` recibe el `workspace` estático de la librería y `jd_input` va pidiendo el archivo en trozos de `JD_SZBUF = 512 B` a medida que los necesita (`TJpg_Decoder.cpp:283`). Con `JD_FASTDECODE = 1`, ese workspace son `TJPGD_WORKSPACE_SIZE = 3500 B`.

Medido en el binario y en placa, no deducido:

| | |
|---|---|
| Símbolo `TJpgDec` en `.bss` | **3,580 B** (`nm`: `3ffc3f60 00000dfc B TJpgDec`) |
| Heap durante la decodificación | **+0 B**, en las siete fotos y en las dos vueltas del ciclo |
| Coste de enlazar la librería | **ya estaba pagado**: el binario con `TJpg_Decoder` incluido y **cero llamadas** ya cargaba los mismos 3,580 B. Comprobado recompilando `HEAD` |
| Coste de añadir el pipeline entero | **+64 B de RAM** y **+13.3 KB de flash** |

O sea que la cifra que hay que citar es **3,580 B fijos**, no un buffer que crece con la foto, y el tope duro de 64 KB de §4 **no tiene nada que ver con la visualización** — es del camino de subida.

#### Cuánto tarda en aparecer una foto — medido en placa, 2026-08-07

Con el pipeline real: leer de la tarjeta, decodificar y empujar al ST7796S a 40 MHz. Siete fotos de proporciones distintas, `[env:marco]`.

| Entrada | Salida | Esc. | `getFsJpgSize` | Dibujo | del que empuje | **Total** |
|---|---|---|---|---|---|---|
| 320×480 | 320×480 | 1 | 17 ms | 230 ms | 80.7 ms | **247 ms** |
| 320×427 | 320×427 | 1 | 17 | 213 | 71.7 | 230 |
| 270×480 | 270×480 | 1 | 16 | 199 | 67.8 | 215 |
| 221×480 | 221×480 | 1 | 18 | 171 | 55.8 | 189 |
| 320×240 | 320×240 | 1 | 16 | 127 | 40.6 | 143 |
| 320×213 | 320×213 | 1 | 18 | 125 | 35.8 | 143 |
| 1280×960 | 320×240 | **4** | 18 | 1137 | 131.6 | **1155** |

**Los tres términos, separados.** El empuje se mide exactamente porque el callback *es* el empuje. Separar lectura de decodificación exige quitar la lectura de en medio —`drawFsJpg` las entrelaza—, así que la primera foto de cada arranque se dibuja desde un buffer:

| Término, sobre la 320×480 de 13,326 B | |
|---|---|
| Leer de la SD | **13 ms** |
| **Decodificar** | **121 ms** |
| Empujar al display | **76.9 ms** |

**La decodificación es el término mayor, y nunca se había medido.** Es el 57 % del total, contra un 36 % del empuje y un 6 % de la lectura. El cuello del pipeline de visualización **no es el SPI ni la tarjeta: es la CPU**. Eso refuerza la decisión del pendiente #3 —40 MHz y no 80— desde otro ángulo: subir el reloj del display atacaba el segundo término, y el primero no se movía.

**El empuje confirma el banco con 0.1 ms de error.** Los 77 ms sintéticos de 600 bloques de 16×16 del pendiente #3 predijeron los 76.9 ms reales. Y sobre las siete fotos el término escala **exactamente con los píxeles de salida**, a `0.525 µs/píxel` en las seis proporciones (±0.4 %).

> Sesgo declarado: en la tabla de arriba el empuje sale a 80.7 ms y no a 76.9 porque son dos corridas distintas y porque la instrumentación cobra lo suyo — dos `micros()` por bloque × 600 bloques ≈ 3.7 ms, que es justo la diferencia. La cifra sin instrumentar es la del banco.

**El barrido de la imagen al cambiar de foto NO es un síntoma.** Se ve la foto pintándose bloque a bloque, y es inevitable por construcción: `TJpg_Decoder` entrega MCUs de 16×16 y no hay framebuffer donde componer la imagen antes de mostrarla — **no tenerlo es exactamente lo que descartó la ESP32-S3**. Son los ~230 ms de la tabla hechos visibles. Es el mismo caso que el barrido de `drawFastVLine` que anota el BOM, y hay que dejarlo escrito porque es lo primero que alguien va a reportar como avería.

**La rama de escala es cara, y el motivo no es obvio.** Una foto sobredimensionada cuesta **1,155 ms**, 4.7× una que llena la pantalla, y su empuje *sube* a 131.6 ms pese a salir en 320×240 —la mitad de píxeles que la de 320×480, que empuja en 80.7—. La razón es que **la rejilla de bloques la fija el MCU de ORIGEN, no el destino**: con `scale = 4` los bloques que entrega el decodificador son de 4×4 de salida, así que son 4,800 en vez de 600, y a los ~23 µs de sobrecarga por bloque ya medidos salen ~110 ms. El resto es la decodificación, que también va con los píxeles de origen: 8× más píxeles de entrada, 6.7× más tiempo.

No es un problema: las fotos del navegador nunca llegan sobredimensionadas, y esa rama existe solo para que un archivo arrastrado a mano a `/fotos/` se vea entero en vez de por una esquina (regla 2). Pero explica por qué **no** conviene subir fotos grandes «por si acaso».

**Streaming contra bufferizado, la misma foto por los dos caminos:**

| `00000001.JPG`, 320×480, 13,326 B | Leer | Decodificar | Empujar | Total |
|---|---|---|---|---|
| Bufferizado (`drawJpg` desde heap) | 13 ms | 121 ms | 76.9 ms | **210 ms** |
| **Streaming (`drawFsJpg`) — producción** | 149.3 ms los dos juntos | | 80.7 ms | **230 ms** |

Streaming cuesta **+20 ms, un 9.5 %**, y el exceso está entero en la lectura: 26 peticiones de 512 B contra una sola de 13 KB. **Se queda el streaming**, porque esos 20 ms son invisibles cada 30 s y a cambio el camino de visualización no toca el heap — que es donde `/upload` va a querer sus 64 KB.

Cifras de las tres frecuencias de SPI, en el BOM. **La velocidad del SPI no es una restricción de este diseño.**

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

### La rampa avanza sin histéresis — pendiente

El firmware sube o baja el backlight un paso de PWM por lectura de BH1750 (período de ~200 ms), así que el recorrido completo entre extremos tarda ~40 s — deliberadamente lento, al ritmo al que un cuarto se oscurece de verdad, no un salto brusco.

Lo que falta: **histéresis entre los umbrales de lux.** Con el valor de luz oscilando justo en el borde de un umbral (100 lux, 10 lux), el brillo objetivo puede alternar entre dos niveles lectura a lectura, y aunque la rampa de un paso amortigua el salto, no lo elimina. Marcado `TODO` en el propio código (`updateBrightness()`, `firmware/src/main.cpp`) — no bloquea el resto del firmware, se resuelve **cuando llegue el BH1750 de reposición** y se pueda ver el efecto real en vez de solo razonarlo. (El sensor original sí estuvo en placa y dio `0x23`; se estropeó después, ver más abajo.)

### Degradación elegante

Si el BH1750 falla, la pantalla se queda en **brillo fijo**, no apagada. Nunca debe quedarse en negro por un sensor descompuesto.

> **Y esto dejó de ser hipotético el 7-ago-2026:** el módulo del banco se estropeó de verdad, en mitad de las pruebas, después de haber respondido correctamente. La rama de degradación se ejercitó en hardware sin que nadie la provocara a propósito — el firmware siguió pintando, que es exactamente lo que la regla 2 pide. La avería y su diagnóstico están en el BOM; el sensor hay que reponerlo, pero no bloquea nada del firmware.

**El backlight se maneja directamente desde GPIO19**, con PWM por LEDC a 5 kHz y 8 bits, en sentido intuitivo — duty alto es más brillo. **Sin transistor**: el pin `BL` del AR3952 resultó ser una entrada lógica de ~3.17 kΩ que pasa 0.88 mA a 3.3 V, y el módulo lleva su propio driver para la corriente del backlight (pendiente #1, cerrado). Verificado de vista que la atenuación es gradual, sin parpadeo, y que en duty 0 el backlight se apaga por completo.

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

### El umbral de lectura sigue sin calibrarse — depende del pad, que no existe

El firmware trae hoy un umbral fijo (`TOUCH_THRESHOLD = 40` en `firmware/src/main.cpp`), marcado en el propio código como valor inventado, sin debounce.

La calibración real no es una constante: es tomar la media de N lecturas de `touchRead()` al arrancar y multiplicarla por ~0.6 para fijar el umbral, más exigir 2-3 lecturas consecutivas de flanco antes de aceptar un toque como real (evita falsos positivos de una sola lectura ruidosa). El valor crudo de reposo depende del pad elegido, del grosor real de PLA entre el pad y el dedo, de la humedad y de la temperatura — es const específica de esta unidad física, no una constante de diseño.

**No se fija hoy porque el pad conductor todavía no existe** (ver la sección de touch capacitivo del BOM). Cualquier número que se escriba antes de tener el pad soldado sería tan inventado como el `40` actual.

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

### Cada estado es una fila de una tabla, no una rama de un switch

Color, semiperiodo de parpadeo y si el estado se apaga solo viven en `TABLA`, en
`firmware/src/LedRGB.cpp`. Es la misma información para todos los estados, y
tenerla en dos sitios ya había divergido en la primera versión: el código que
devolvía el LED a su color tras una interacción restauraba «ámbar» como color
fijo, cuando la definición del estado dice parpadeo.

El array va **sin tamaño explícito**, con un `static_assert` contra el centinela
del enum. Declararlo `[TOTAL]` deja que un estado nuevo sin fila se rellene de
ceros en silencio — y un estado negro que ni parpadea ni se apaga se ve
exactamente igual que uno que nadie disparó.

**Los ocho estados están verificados de vista sobre el LED real**, uno a uno desde
el banco: cada color y cada ritmo se leen sin ambigüedad. El único valor que hubo
que corregir fue el ámbar, que en el escritorio se había puesto en `(255, 80, 0)`
suponiendo canales equilibrados y sobre el hardware tiraba a verde — quedó en
`(255, 70, 0)`, y **es un número que depende de las resistencias**: ver el BOM.

### Debe apagarse tras el arranque

Un punto de color respirando en la sala de noche es exactamente lo que no se quiere en un regalo. **Los LEDs sirven durante el armado y la depuración**, no en operación normal. El estado de WiFi se comunica mejor con un icono discreto en la propia pantalla.

**Verificado en placa:** un estado con parpadeo y auto-off se apaga solo a los 30 s.

**Los estados que parpadean también se apagan**, y esto no es obvio en el código:
la rutina de refresco atiende el parpadeo con un retorno temprano —el ritmo tiene
que ser exacto y no puede quedar detrás de un fade—, así que **el auto-off tiene
que evaluarse ANTES de esa rama**. Con el orden inverso, que fue el primero que se
escribió, una subida fallida dejaba el rojo latiendo a 150 ms toda la noche: los
estados con parpadeo llevaban su bandera de auto-off puesta y no la alcanzaban
nunca. Es la regla 3 entera, perdida por el orden de dos bloques.

**La excepción de «sin tarjeta» VENCIÓ, 2026-08-07: ya no hay ninguna fila que se
quede encendida.** Era la única, y su fecha de caducidad estaba escrita aquí —«en
cuanto el firmware sepa pintar ese error en pantalla»—. El firmware ya lo pinta:
`pantallaMensaje()` de `main.cpp` deja **«No se pueden / leer las fotos»** en el
display, así que el LED dejó de ser el único canal capaz de decirlo y `SD_ERROR`
pasa a `autoOff = true` como las demás. El mensaje no se va a ninguna parte
cuando el LED se apaga a los 30 s, que es justo lo que faltaba para que el rojo
permanente dejara de ser necesario.

**Verificado en placa arrancando sin tarjeta**: sale el texto en pantalla, el LED
late en rojo, y se apaga solo. Lo que queda dicho es lo que hay que decir, y por
el canal que no se apaga.

> Las cadenas de esas pantallas van **sin acentos a propósito**, y lleva
> comentario en el código: la fuente 4 de TFT_eSPI cubre ASCII 32-126 y nada más,
> y las suaves (`SMOOTH_FONT`) necesitan un `.vlw` en un sistema de archivos que
> este proyecto no tiene. «Todavía» saldría con un glifo roto, que se ve peor que
> la falta del acento.

### El fade interpola desde un origen congelado

`startFade()` guarda el color de partida en `_from*`. Interpolar desde el color
**actual** —que la escritura al PWM reescribe en cada paso— mueve el origen bajo
la cuenta y convierte la rampa en una exponencial: con 350 ms declarados el LED
llegaba a 252 de 255 a los 170 ms. No se ve mirando el LED, solo cronometrándolo,
y deja las constantes de duración sin calibrar nada. La interpolación está
extraída a una función pura (`LedRGB::lerp`) precisamente para poder comprobarla
sin placa; el banco y el firmware la verifican al arrancar.

**Y el fade lineal en duty se ve lineal: no hace falta tabla de gamma.** Medido
con la rampa del banco sobre el blanco compuesto, que es el caso más exigente.
Era la duda razonable —8 bits de duty lineal sobre un LED suelen verse como un
salto al principio y una meseta al final— y la respuesta en este hardware, a estas
corrientes tan bajas, es que no. No añadir una tabla de gamma «por si acaso».

### El fade se termina a la fuerza justo antes de `autoConnect()`

`setup()` llama a `asentarLed(400)` inmediatamente antes de `wm.autoConnect()`: un bucle que corre `led.update()` y `delay(10)` durante 400 ms para que el fade del LED termine de verdad en vez de quedar congelado a medio camino mientras `autoConnect()` bloquea el hilo —a veces por minutos, si levanta el portal—. Sin esto, lo que se queda pintado en el LED durante todo ese tiempo es el frame de un fade a medias, o directamente negro.

Era el **último código que corre antes del punto en el que el issue #1797 reportaba el `Guru Meditation Error`** (§5), o sea el landmark natural para bisecar el pendiente #5. **Ese pendiente se cerró el 6-ago-2026 sin que el panic apareciera**, así que la función se queda por lo que hace —dejar el LED en un color correcto mientras `autoConnect()` bloquea— y no por su papel de referencia. Nunca fue sospechosa: `delay()` en arduino-esp32 es `vTaskDelay`, que cede CPU al scheduler de FreeRTOS y no toca memoria.

### Los tres canales no dan el mismo brillo al mismo duty

Resistencias de valor alto (220 Ω / 470 Ω) para que queden tenues, y montaje en cara trasera o inferior.

A igual duty el brillo **no** es igual: las resistencias son distintas por diseño y
los voltajes directos también. Lo que no se deduce del cálculo es hacia dónde
desequilibra. Sobre el papel el rojo llevaba el doble de corriente que los otros
dos y se predijo un blanco rosado; en placa **el rojo se ve claramente más
apagado**, porque el ojo pesa el verde unas cuatro veces más (curva fotópica) y
porque el verde y el azul InGaN son más eficientes por mA. Se corrigió en
hardware, bajando el rojo de 1 kΩ a 220 Ω, y con eso el blanco sale blanco.

`LedRGB` lleva además un vector `escala[3]` de compensación por canal, **en
identidad**: el equilibrio se resolvió con resistencias porque bajar verde y azul
por software habría igualado los canales tirando brillo, y el conjunto ya es tenue
a propósito. La perilla se queda para el ajuste fino tras montar la carcasa.
Detalle y cifras en el BOM.

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
| Procesador | ESP32 clásico WROOM, USB-C — **comprado y verificado en placa** (`ESP32-D0WD-V3 rev v3.1`, `PSRAM 0 B`) | ESP32-S3 (innecesario con decodificación por bloques), WROVER (PSRAM ocupa GPIO16/17) |
| Pantalla | ST7796S 3.5" IPS SPI | Nextion (25 fotos precompiladas), all-in-one 5" (presupuesto) |
| Almacenamiento | Tarjeta SD local | Google Drive / nube |
| Driver de la SD | `sdspi` de ESP-IDF con `SPI_IGNORE_DATA_CRC`, envuelto en un `fs::FS` (`firmware/src/tarjeta.h`) | La librería `SD` de Arduino: manda CMD59 y luego exige un OCR de un CMD58 hecho en idle, que estas tarjetas contestan «ocupada». Medido con dos tarjetas; el master de arduino-esp32 tiene el mismo código. SdFat: dependencia nueva y no es un `fs::FS`, rompería el streaming de `/list` |
| Sistema de archivos | FAT32, cluster 32 KB **medido desde el ESP32**, no solo desde Windows (= un cluster por foto dentro de presupuesto) | NTFS (FatFs solo implementa FAT12/16/32); exFAT (`FF_FS_EXFAT=0` en el binario precompilado de arduino-esp32 3.3.11 — no es "no soportado", está deshabilitado en un binario que este proyecto no controla; habilitarlo obligaría a migrar a ESP-IDF) |
| Carga de fotos | Servidor web local + Canvas | App, SD extraíble, USB |
| Redimensionado | En el navegador del celular | En el ESP32 (imposible) |
| Selección de fotos | Multi-selección; foto completa con barras por defecto, recorte manual opcional por foto | Una a la vez (tedioso con 30 fotos), recorte automático al centro (decapita gente en silencio y deja el aviso de barras sin objeto), recorte obligatorio por foto (convierte la subida en una tarea) |
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
| Gesto del recuadro | Arrastrar con Pointer Events + slider de zoom nativo, tope por resolución | Solo arrastrar (una 2:3 no se movería y una 3:4 recorrería el 11 %), pinch de dos punteros (invisible, ~20 líneas más, WebKit sin medir) |
| Sacar una foto de la tanda | «Quitar foto» en su detalle, con confirmación | Solo «Limpiar» (obligaba a rehacer la tanda entera por una foto en error) |
| Capa de subida | `fetch` + `AbortController`, bucle propio excluyente con el de preparación, progreso por foto | Solaparla con la preparación (dos segundos a cambio de dos cancelaciones simultáneas), `XMLHttpRequest` por el progreso intra-archivo (décimas de segundo a 34 KB), reintento automático (enmascara el `400` y el `503`) |
| Subir una foto suelta | No existe: el botón sube la tanda en orden de rejilla | Subir la del detalle primero, que rompería «orden de subida = orden del manifiesto» |
| Config de TFT_eSPI | `build_flags` con `USER_SETUP_LOADED` | Editar `User_Setup.h` en la carpeta de la librería |

---

## 12. Orden de trabajo sugerido

### Se puede hacer HOY, sin hardware

1. **Arnés de diagnóstico, antes que nada.** Un HTML que carga una foto y reporta las dimensiones que devolvió el decodificador, el tag `0x0112` leído a mano de los bytes del JPEG, y la imagen sin transformar. Responde de forma medida —no supuesta— si el navegador ya aplicó la rotación y si hay subsampling con fotos grandes.

   **Estado: hecho.** El arnés existe, ya no en `web/test/` sino integrado en `web/index.html` detrás de `CFG.DIAG`, que se apaga en producción. Corrió sobre fotos reales en Chromium y **en Safari de iOS sobre un iPhone 11**, que es el motor que importa; los resultados están en §4. Lo que queda abierto es acotado: el caso de transpose (EXIF = 5) solo se ha visto en Chromium, y el subsampling pasó a nota de alcance por falta de dispositivo.

2. **Página web con redimensionado por Canvas**, en este orden: lector EXIF → escalador → encoder → rejilla y cola → editor de recuadro → capa de red → empaquetado.

   **Hecho y medido:** lector del tag EXIF y de las dimensiones del marcador SOF por bytes, escalado por halving con ping-pong de dos canvas, encoder con búsqueda binaria acotada y banda de aceptación, presupuesto por densidad, aviso de barras en lenguaje llano, panel de diagnóstico, descarga del JPEG a disco (el modo mock, que permite probar la página completa sin firmware), rejilla y cola con multi-selección, editor de recuadro con «quitar foto», y **la capa de red contra `/upload`** — bucle propio con concurrencia 1, timeout por foto, los ocho desenlaces de la tabla de §4, corte por red caída, reintento de solo las que fallaron y verificación de integridad bajo diagnóstico. 84 comprobaciones automatizadas en Chrome, 38 de ellas de la capa de red.

   **Ya medido, no falta:** la subida en el iPhone contra el banco — tanda de 30 fotos reales a 315 ms/foto, 107.9 KB/s, integridad byte a byte, con `413`, `507`, `500` con reintento parcial, timeout, corte por WiFi caído y un reinicio del marco a media tanda, todos ejercitados en placa (§4 y README tienen las cifras completas).

   **Falta:** solo el empaquetado (gzip → array de C en PROGMEM).

3. **Repositorio en GitHub** con los dos documentos, `web/`, y pruebas desde el celular real. **Hecho, con GitHub Pages descartado a propósito** — no reabrir: nada en el pipeline exige contexto seguro (HTTPS), así que la prueba en dispositivo real se resuelve sirviendo por `python3 -m http.server` en la LAN, y en producción la página vive en PROGMEM servida por el propio ESP32. GitHub Pages nunca fue el camino de producción, solo una opción de prueba que resultó innecesaria.
4. **Instalar PlatformIO** y armar el `platformio.ini` del BOM. Compila sin placa conectada; valida que todas las dependencias resuelvan. **Hecho** — build limpio verificado, ver el BOM.
5. Formatear la SD en FAT32 y crear `/fotos/`. Al hacerlo, confirmar el tamaño de cluster. **Hecho, 2026-08-05** — cluster de 32,768 B confirmado sobre la tarjeta física, label `MARCOFOTOS`, `/fotos/` creado. Detalle completo en el BOM.

Casos de prueba mínimos para los pasos 1 y 2: foto 4:3, 16:9, vertical de iPhone sin editar y sin pasar por WhatsApp (EXIF), y una de 12 MP para cronometrar el escalado en celular.

De esos, **ya se probaron** la 4:3 y dos verticales de iPhone con EXIF 6 y 5 en Chromium, y dos verticales de 8 y 12 MP con EXIF 6 en Safari de iOS. La de 48 MP (8064×6048) que figuraba aquí **sale de la lista**: su única razón de ser era provocar el subsampling de Safari y no hay dispositivo con el que generarla — ver la nota de alcance en §4.

### Requiere los componentes en mano, en este orden

6. ~~**Provisioning de extremo a extremo (pendiente #5).**~~ **Hecho, 6-ago-2026.** Iba primero por ser lo único capaz de forzar un cambio de plataforma, y no lo fuerza: el panic del issue #1797 no ocurre. Se probó en dos etapas para separar «es la librería» de «es la presión de heap» — `firmware/banco/` `[env:wifi]` con WiFiManager y el LED a solas, y `[env:marco]` con los cinco globales ya construidos. De ahí salió el fallo real, que era otro: `getWiFiIsSaved()` mintiendo antes de `WiFi.mode(WIFI_STA)`, corregido. Cifras en el pendiente #5 del BOM.
7. ~~**Prueba del pin `BL`.**~~ **Hecho, 6-ago-2026. Caso B: entrada lógica.** Con 100 Ω en serie, el pin pasa **1.40 mA a 5 V** y **0.88 mA a 3.3 V** —o sea 3.17 kΩ de impedancia de entrada por ajuste de dos puntos— mientras el panel enciende brillante. Esa corriente es ~70× menor que la del backlight, así que la potencia sale de `VIN` por un driver del módulo y `BL` solo lo gobierna. **`BL` va directo a GPIO19, con PWM por LEDC y sin transistor.** Cifras y método en el BOM.
8. ~~Confirmar pinout del PN2222A (zócalo hFE).~~ **Desaparece con el punto 7:** el transistor sale del diseño del backlight, junto con sus 470 Ω de base y el 10 kΩ.
9. ~~Afinar velocidad SPI (27 → 40 → 80 MHz).~~ **Hecho, 7-ago-2026: 40 MHz.** Los tres binarios dieron **imagen limpia**, 80 MHz incluido, así que la elección no fue de estabilidad sino de margen: repintar una foto cuesta 77 ms a 40 MHz contra 45 a 80, y eso es invisible en un marco que cambia de imagen cada varios segundos. Tabla completa en el BOM.
10. ~~Confirmar dirección I2C del BH1750.~~ **Hecho, 6-ago-2026: `0x23`**, con `ADDR` al aire, que es lo que asumía §7. Escaneo desde `[env:display]`. De paso salió que un voltímetro **no** detecta `SDA` y `SCL` intercambiados —las dos líneas leen 3.3 V en reposo en cualquier orden— así que el banco escanea en los dos órdenes; detalle en el BOM.
11. **QR de uso diario al terminar el provisioning.** Necesita el display, así que va aquí; es una decisión de §5 tomada después de cerrar el pendiente #5. Hoy el único puente entre «acabo de configurar el WiFi» y «puedo subir fotos» es un gesto que nadie va a explicarle a quien reciba el marco. El disparador es *se acaba de provisionar*, no *se conectó*, y depende de que `getWiFiIsSaved()` diga la verdad — o sea del `WiFi.mode(WIFI_STA)` de §5.
12. ~~**Galería de lo ya cargado y `POST /delete`.**~~ **Hecho, 7-ago-2026.** Era lo único del contrato HTTP que la página no llamaba. Sin endpoints nuevos —`/list` y `/photo` bastaban— y **sin dependencias nuevas**: el cuerpo JSON se lee con `strstr`/`strchr` como en el banco, y ArduinoJson se descartó. Galería paginada de 60, multi-selección con una sola confirmación, y borrado **secuencial** porque cada `/delete` reescribe el manifiesto entero. Verificado en placa: `200`, `400` con `../manifest.txt`, `400` con un POST **sin cuerpo** —que es un fallo que el banco arrastra y aquí se cerró con guard de identidad—, `200` idempotente sobre un nombre que ya no existe, y **el contador de NVS sin retroceder**: tras borrar la `00000074`, la siguiente subida salió `00000075`. Detalle en §4.
13. **Lector SD y coexistencia de los dos buses.** **Hecho, 7-ago-2026.** Montado
    en placa y probado con el display encendido a la vez, que era el riesgo real
    (issue #3601 de TFT_eSPI) y no que `SD.begin()` funcionara. Dos corridas de
    ~190 s con una tarea en el core 0 martilleando la tarjeta mientras `loop()`
    pintaba en el core 1: **795 ciclos, ~52 MB verificados byte a byte, cero
    discrepancias**, y el display perdiendo un 2.4 % en las rayas de 1 px y un
    2.9 % en los bloques de 16×16. Sin bandas ni píxeles corridos de vista. De
    paso salió que **la librería `SD` de Arduino no monta estas tarjetas** y hubo
    que cambiarla por el driver `sdspi` del IDF; el diagnóstico completo, con su
    control experimental, está en el BOM.
14. **Pipeline de visualización.** **Hecho, 7-ago-2026.** El marco muestra fotos:
    manifiesto con su fallback de reconstrucción y reanclaje del contador,
    `showNextPhoto()` decodificando con `TJpg_Decoder` por bloques MCU con las
    barras negras calculadas en firmware, y cambio periódico cada 30 s. Una foto
    que llena la pantalla aparece en **247 ms**, de los que la **decodificación
    es el 57 %** —el término que faltaba por medir, y el mayor—, con **cero
    heap**. Verificado de vista en las seis proporciones de §6 más una
    sobredimensionada. De paso venció la deuda de §9: `SD_ERROR` ya se apaga
    sola. Cifras en §3 y §6.

    **Falta de esta pieza**: el toque corto no se ha podido probar —depende del
    pad capacitivo, que todavía no existe (§8)— y la histéresis del brillo sigue
    marcada como `TODO`, esperando al BH1750 de reposición.

15. **El marco recibe fotos por sí solo.** **Hecho, 7-ago-2026.** Las tres piezas
    que faltaban para que el regalo no dependa de una Mac, y que eran la misma
    historia: sin página no hay dónde subir, sin `/upload` no hay a dónde, y sin
    QR nadie llega a ninguna de las dos.

    - **`POST /upload`** con el contrato entero de §4. Verificado en placa contra
      **43 subidas**: `200` con su nombre, `400` con campo distinto de `foto`,
      `413` con un archivo de 149 KB, y **`503` con dos multipart a la vez**.
      Contador de NVS monótono y sin huecos, manifiesto anexado, e
      **integridad byte a byte** comprobada devolviendo la foto por `/photo`.
    - **La página en PROGMEM**, gzipeada por un `extra_script` de PlatformIO que
      toma solo `web/index.html`. **31,303 B en el binario**, servida con
      `Content-Encoding: gzip` y verificada de extremo a extremo: el cliente la
      descomprime a los **92,738 B exactos** del archivo original.
    - **Los dos QR**, con el de uso diario saliendo solo al terminar el
      provisioning (§5).

    **Coste**: RAM 56,832 B y flash 1,327,527 B, o sea **+24 B y +39.4 KB** sobre
    el binario del punto 14. Los +24 B de RAM son la comprobación de que los 31 KB
    de la página fueron a `.rodata` y no a DRAM.

    **La tanda de 30 desde el iPhone ya está hecha**, contra el marco y con la
    tarjeta recién vaciada:

    - **30 fotos, nombres `00000044`–`00000073`, cero huecos y estrictamente
      crecientes.** Es la comprobación fuerte de que ninguna escritura falló: el
      número se reserva en NVS *antes* de escribir, así que un fallo habría dejado
      hueco. Y sin huecos tampoco hubo reintentos, o sea que el orden del
      manifiesto **es** el de la rejilla.
    - Peso: media **34.5 KB**, mediana 34,158 B, entre 16,914 y 51,898 B, y **15
      de las 30 por encima del presupuesto** de 32,768 B.
    - **`mayorBloque` sigue en 25,588 B tras la tanda**, el mismo valor que antes
      de ella: la ruta de subida no fragmenta ni después de 30 escrituras reales.
    - **El suelo de heap durante la tanda fue 55,788 B libres** (`minHist`), sin
      un solo reinicio de por medio.
    - Con fotos reales el pipeline de §6 se comporta como predice la tabla:
      `320×427` es el caso dominante —la vertical 3:4 de iPhone— y sale en
      **252-305 ms con `heap +0`**. Algo por encima de los 230 ms de la tabla,
      que se midieron con fotos de prueba: una foto real tiene más entropía y
      decodificar es el 57 % del tiempo.

    **Falta de esta pieza**: el recorrido completo de placa virgen —QR de setup →
    portal → QR de uso diario → abrir la página—, y con él **el heap con el portal
    cautivo abierto**, que es el número que decide si el buffer de 64 KB se queda
    donde está.

16. Integración y modelado de la carcasa.
