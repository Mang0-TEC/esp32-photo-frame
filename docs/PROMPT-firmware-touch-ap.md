# Firmware: robustez del touch y del WiFi (tras el merge 34715d2)

Eres desarrollador de firmware ESP32 (PlatformIO + framework Arduino, pioarduino
`55.03.311`, arduino-esp32 3.3.11, WiFiManager 2.0.17, ESPAsyncWebServer 3.12.0) y
sigues un proyecto ya avanzado: un marco de fotos digital que es un **regalo** y vivirá
24/7 en casa de otra persona. El touch y el AP de recuperación **ya están
implementados** (commit `bbc7035`, unido en el merge `34715d2`). Esta sesión corrige
tres puntos que salieron al revisar ese código. Todo se hace y se prueba en el banco,
sin la carcasa.

## Lee primero (no reinventes)

- `docs/ESPECIFICACION-funcional.md`:
  - §1, las tres reglas: **1)** funciona sin que nadie entienda nada; **2)** degrada
    con elegancia; **3)** se ve a producto, no a proyecto.
  - §5 completo: el AP de recuperación con sus dos relojes, «Regla de oro: el marco
    debe funcionar sin WiFi», y el reinicio tras provisionar por el puerto 80 que el
    portal de WiFiManager no suelta (`bind error: -8`).
  - §7, la rampa de brillo, y §8 completo: la base que persigue, `TOQUE_DELTA`, y el
    toque que despierta la pantalla.
- `docs/BOM-marco-fotos-esp32.md`, sección «Touch capacitivo»: la cinta de cobre, las
  tres trampas y la tabla «El umbral y el grosor de pared» (tecla `t`).
- `firmware/src/toque.h`: `toqueActualizar()` y las siete autocomprobaciones de
  `toqueOk()`.
- `firmware/src/main.cpp`:
  - `handleTouch()` y `levantarAP()`.
  - Los dos relojes del AP en `loop()`.
  - El bloque de `wm.autoConnect("Marco-Fotos", "fotos1234")` en `setup()`, con
    `habiaCredenciales` y el reinicio tras provisionar.
- `firmware/banco/src/display.cpp`: la tecla `t`, y `firmware/banco/` en general.

## Punto 1: la base del touch baja demasiado rápido (`toque.h`)

Hoy, mientras no hay dedo, la base persigue el reposo **a un paso por lectura en las dos
direcciones**: unas 100 counts/s con `loop()` a ~10 ms. El problema:

- A través de 1.5–2 mm de PETG no hay un «contacto»: la lectura baja **gradualmente**
  mientras el dedo se acerca.
- Un acercamiento lento, o un dedo que se apoya despacio, lo va siguiendo la base, y el
  delta nunca llega a `TOQUE_DELTA`: **el toque se pierde**.
- Además, durante las 3 lecturas del debounce la base sigue bajando, porque `dentro`
  todavía es falso.

Propuesta a evaluar e implementar: **persecución asimétrica**.

- **Hacia arriba, rápido** (como hoy): es lo que recupera el caso del dedo puesto al
  arrancar, y la autocomprobación 5 lo cubre.
- **Hacia abajo, lento**: por ejemplo un count por segundo, **medido en tiempo y no por
  vuelta de `loop()`**, que va de ~10 ms a ~290 ms cuando se decodifica una foto. Eso
  basta contra la deriva de minutos y horas, y el objeto apoyado ya lo cubre la salida
  de `TOQUE_PEGADO_MS`.
- **Un octavo caso en `toqueOk()`**: un acercamiento lento (rampa de varias centenas de
  ms hasta `DEDO`) debe dar CORTO al soltar. Los siete casos existentes deben seguir
  pasando, sobre todo el 5 (dedo al arrancar) y el 6 (deriva de 60 counts).
- **La tecla `t` del banco usa la misma función.** Revisa que su medición de ruido y
  delta siga siendo válida con la base nueva.

## Punto 2: el portal de WiFiManager no tiene timeout al arrancar (`main.cpp`)

`wm.autoConnect()` se llama **sin `setConfigPortalTimeout`**, y en WiFiManager eso
significa que el portal no se cierra nunca.

**Escenario a reproducir: un apagón.** Al volver la luz, el ESP32 arranca en segundos y
el módem tarda uno o dos minutos. `autoConnect()` falla con las credenciales guardadas,
abre el portal y **se queda bloqueado para siempre** en la pantalla de «conectando», sin
mostrar fotos, hasta que alguien reconfigure el WiFi. Eso rompe la regla 2 en el caso
más común de todos.

Qué hacer:

- **Con credenciales guardadas** (`habiaCredenciales`): darle al portal un timeout de
  pocos minutos y a la conexión un tiempo razonable (revisa `setConnectTimeout` y
  `setConnectRetries` en 2.0.17). Al vencer, el marco **sigue con sus fotos**; el STA
  debe reconectar solo cuando vuelva el módem, y el toque largo sin WiFi (el AP de
  recuperación) queda como ruta para subir fotos.
- **Sin credenciales** (placa virgen): el comportamiento actual se queda. El portal
  espera a que alguien provisione, porque es la regla 1.
- **Cuidado con el puerto 80.** Si el portal arrancó y venció sin provisionar, revisa si
  el `WebServer` del portal deja tomado el 80 igual que en el arranque que provisiona.
  Si es así, aplica el mismo patrón ya probado (bandera en NVS + `ESP.restart()`) o
  justifica la alternativa **con medidas**.
- **Qué pasa con el reinicio tras provisionar** si las credenciales se cambian durante
  ese portal con timeout: debe seguir el flujo verificado (reinicio y QR de uso diario).
- **Prueba en el banco:** marco con credenciales guardadas y el router apagado →
  arranca, vence el portal, muestra fotos → se prende el router → reconecta solo, y el
  QR de uso diario del toque largo apunta a la IP correcta.

## Punto 3: el AP en `WIFI_AP_STA` con el módem caído (verificar)

`levantarAP()` usa `WIFI_AP_STA` a propósito, para que el STA reconecte solo si el módem
vuelve. Pero en el ESP32 el AP y el STA comparten radio y canal, y los intentos de
reconexión y escaneo del STA pueden **cambiar el canal del AP o tumbar al teléfono
asociado** a media subida.

- **Mídelo en el banco:** AP arriba, router apagado, teléfono asociado subiendo una tanda
  de 30 fotos desde la página. ¿Se corta la asociación o se pierde alguna subida?
- **Si se corta:** pausar la reconexión del STA mientras haya clientes en el AP (o
  mientras el AP esté arriba) y reanudarla al bajarlo, conservando la reconexión
  automática que justifica `AP_STA`.
- **Si no se corta:** documentarlo como medido y dejarlo igual.

## Cómo trabajar

- **Estilo de la casa:** código y comentarios en español; comentarios que explican *por
  qué*, no *qué*; las decisiones se documentan con lo medido y la fecha. Imita la
  densidad de comentarios de `main.cpp` y `toque.h`.
- **Compila** con `cd firmware && pio run`. Las autocomprobaciones corren al arrancar y
  salen por serie (`[autocomprobacion] ... toque: ok`).
- **No flashees la placa sin preguntarme:** dime qué env y qué probar, y yo te paso el
  monitor serie (115200).
- **Actualiza la documentación** con lo implementado y lo medido: §5 y §8 de la
  especificación, la sección de touch del BOM y el README («Firmware del marco — qué
  corre y qué no»).
- **No hagas commit** sin preguntarme.

## Fuera de esta sesión

- Fijar `TOQUE_DELTA`: necesita el pad real y los cupones de PETG (sesión de la
  carcasa, `docs/PROMPT-carcasa-testfit.md`).
- La atenuación del sensor con la carcasa cerrada.
