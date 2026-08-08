// src/main.cpp — Esqueleto del firmware del marco de fotos ESP32
//
// Estados del LED en §9 de la especificación funcional; el color y el ritmo de
// cada uno viven en la tabla de LedRGB.cpp, no aquí. Este archivo solo decide
// EN QUÉ estado está el marco.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
// OJO: TJpg_Decoder.h define TJPGD_LOAD_SD_LIBRARY en su User_Config.h y arrastra
// <SD.h> con su objeto global `SD`. Ese objeto NO se usa nunca aquí y compilaría
// sin quejarse si alguien lo escribiera: la tarjeta se monta con montarTarjeta()
// y se accede por TARJETA, porque la librería SD de Arduino no monta estas
// tarjetas. El porqué, medido, está en tarjeta.h.
#include <TJpg_Decoder.h>
#include <BH1750.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <qrcode.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include "LedRGB.h"
#include "nombre.h"
#include "tarjeta.h"   // la SD por sdspi del IDF; ahí está por qué NO se usa SD.h
// GENERADO en cada build por tools/empaquetar_pagina.py, y por eso no está en
// git: son ~183 KB de fuente de C que cambian enteros al tocar el HTML.
#include "pagina.h"

// ============================================================================
// Pines (BOM-marco-fotos-esp32.md, Mapa de GPIOs)
// ============================================================================
constexpr uint8_t PIN_LED_R = 25;   // Rojo, 220 Ω (era 1 kΩ; ver el BOM)
constexpr uint8_t PIN_LED_G = 33;   // Verde, 470 Ω
constexpr uint8_t PIN_LED_B = 4;    // Azul, 470 Ω
constexpr uint8_t PIN_BL    = 19;   // Backlight display (PWM LEDC)
constexpr uint8_t PIN_TOUCH = 32;   // T9 — touch capacitivo

constexpr uint8_t PIN_SD_SCK  = 14;
constexpr uint8_t PIN_SD_MISO = 27;   // NO GPIO12: strapping, ver el BOM
constexpr uint8_t PIN_SD_MOSI = 13;
constexpr uint8_t PIN_SD_CS   = 26;

// ============================================================================
// Periféricos
// ============================================================================
TFT_eSPI tft = TFT_eSPI();
BH1750 lux;
AsyncWebServer server(80);
LedRGB led(PIN_LED_R, PIN_LED_G, PIN_LED_B);

// Los callbacks de /upload corren en el hilo async_tcp, a prioridad 10, NO en
// loop(). Tocar el LED desde ahí compite con led.update() en el otro core: no
// tumba nada —son escrituras de 8 bits— pero da frames de color sucios y
// parpadeos descuadrados. El callback pide, loop() aplica.
static volatile LedRGB::State ledPedido = LedRGB::State::TOTAL;   // TOTAL = nada pendiente

// ============================================================================
// Touch capacitivo
// ============================================================================
static uint32_t touchStartMs = 0;
static bool     touchActive  = false;
static constexpr uint32_t TOUCH_LONG_MS  = 800;
// ponytail: umbral inventado y sin debounce. El valor crudo de reposo depende
// del pad, del grosor del PLA, de la humedad y de la temperatura, así que esto
// va derivado de una media de N lecturas al arrancar (×0.6) más 2-3 lecturas
// consecutivas para validar el flanco. No se fija hoy porque el pad todavía no
// existe y cualquier número escrito sin él es igual de inventado que éste.
static constexpr uint16_t TOUCH_THRESHOLD = 40;

// ============================================================================
// Fotos — manifiesto y reproducción (§3 y §6)
// ============================================================================
static constexpr char DIR_FOTOS[]  = "/fotos";
static constexpr char MANIFIESTO[] = "/manifest.txt";
static constexpr char CRUDO[]      = "/manifest.raw";   // volcado sin ordenar
static constexpr char TEMPORAL[]   = "/manifest.tmp";

// Bandera de NVS que sobrevive al reinicio de después de provisionar y es lo
// único que se acuerda de que hay que pintar el QR de uso diario. Va en el
// namespace «marco», el mismo del contador de fotos.
static constexpr char NVS_QR_USO[] = "qrUso";

// §6 solo pide «cada varios segundos». 30 s es ritmo de sala: se baja a 5 s
// durante el bring-up para no esperar en cada prueba.
static constexpr uint32_t FOTO_PERIODO_MS = 30000;

// Cuántas entradas muertas se saltan antes de dar el índice por roto.
// ponytail: no se valida el manifiesto entero al arrancar. Comprobar que las N
// entradas existen cuesta un recorrido de directorio, que es exactamente lo que
// hace la reconstrucción — así que se paga solo cuando de verdad hace falta.
static constexpr uint8_t SALTOS_MAX = 8;

// Reproducir solo necesita «la siguiente», así que el estado del manifiesto es
// un offset en bytes y no un arreglo en RAM: sin tope de fotos, y sin retener
// ningún descriptor (max_files es 5, y el streaming de /list abre el suyo).
//
// VOLATILE desde que existe /delete: lo lee loop() por siguienteNombre() y lo
// escribe async_tcp al reescribir el manifiesto. Es el mismo cruce de hilos que
// ledPedido y paginaAbierta, y se resuelve igual. Un borrado a media
// reproducción no rompe nada: si el offset queda pasado del nuevo size(),
// siguienteNombre() lo reajusta a 0, y si queda a media línea sale un nombre
// truncado que nombreValido() rechaza gastando uno de los SALTOS_MAX. Las dos
// degradaciones ya existían.
static volatile uint32_t manifiestoOffset = 0;
static bool     yaReconstruido   = false;   // pestillo anti-bucle de reconstrucción
static uint32_t ultimaFotoMs     = 0;
static bool     medirLaProxima   = true;    // la primera foto de cada arranque se mide

// Acumulador del término «empuje». El callback ES el empuje, así que medirlo
// aquí lo separa exactamente del resto sin tocar la librería.
static uint32_t empujeUs = 0;

// ============================================================================
// Subida de fotos (§4)
//
// El buffer se reserva UNA VEZ en setup(), antes de WiFi, y no se libera jamás.
// No es tacañería de código: el callback pide 64 KB CONTIGUOS, y en el banco de
// red se midió que mayorBloque se desploma de 34,804 a 16,372 B en cuanto sube
// AsyncTCP, que reserva por conexión. Un malloc por subida podría fallar en la
// única ruta por la que entran fotos al marco, y ese fallo sale como 507 — que
// corta la tanda entera y no es reintentable. Pedirlo con el heap entero
// convierte un riesgo en una cifra fija.
//
// TOPE_SUBIDA es contrato con la página (TOPE_FIRMWARE en web/index.html) y con
// el caso 413 del arnés. No moverlo por un lado solo.
static constexpr size_t TOPE_SUBIDA = 64 * 1024;
static uint8_t* bufSubida = nullptr;

// Candado de concurrencia 1. La página sube de una en una, pero eso es DISCIPLINA
// DEL CLIENTE y el servidor no puede confiar en ella: dos multipart a la vez
// sobre el mismo buffer dan una foto corrupta en silencio, así que el segundo se
// rechaza con 503 en vez de mezclarse. La identidad de la petición ES el candado.
static AsyncWebServerRequest* dueno   = nullptr;
static AsyncWebServerRequest* intruso = nullptr;
static size_t acumSubida = 0;
static bool   desbordo   = false;

// ============================================================================
// QR en pantalla (§5)
//
// Un QR pintado se retira por lo que llegue antes: que alguien pida la página
// —o sea, que ya esté subiendo fotos— o el minuto de gracia. Hacen falta LAS
// DOS. Solo el temporizador lo quitaría con la persona todavía tecleando; sola
// la petición HTTP lo dejaría pintado para siempre si quien provisiona se
// distrae o se queda sin batería, y un QR permanente en la sala es regla 3.
//
// paginaAbierta la escribe el hilo async_tcp y la lee loop(): volatile, igual
// que ledPedido y por el mismo motivo.
static volatile bool paginaAbierta = false;
static uint32_t qrHasta = 0;   // deadline en ms; 0 = no hay QR en pantalla
// CINCO minutos, y el minuto que había aquí antes se midió corto en el primer
// recorrido de placa virgen (8-ago-2026): el QR de uso diario venció antes de
// que diera tiempo a escanearlo. Lo que se lleva el rato no es sacar el
// teléfono, es que el iPhone tiene que soltar `Marco-Fotos` —que acaba de
// desaparecer— y reasociarse a la red de casa antes de poder abrir nada.
//
// Subirlo no deja un QR permanente en la sala, que es lo que la regla 3
// prohíbe, porque el temporizador NO es quien lo retira en el caso normal: lo
// retira la primera petición a `/`, o sea la persona abriendo la página. El
// reloj es solo la salida para cuando esa visita no llega nunca. Y el mismo
// valor que §5 le da al AP: cinco minutos.
static constexpr uint32_t QR_MS = 300000;

// Versión 3: 29×29 módulos y 53 bytes de capacidad con ECC bajo. Las dos cargas
// de este firmware miden 38 y ~22, así que entran con holgura.
static constexpr uint8_t QR_VERSION = 3;
static constexpr uint8_t QR_LADO    = 4 * QR_VERSION + 17;               // 29
static constexpr size_t  QR_BUF     = (QR_LADO * QR_LADO + 7) / 8;       // 106 B

// ============================================================================
// Brillo automático (BH1750)
// ============================================================================
static uint8_t  currentBacklight = 255;
static bool     bh1750Ok         = false;
static uint32_t ultimaLuzMs      = 0;
// La conversión en CONTINUOUS_HIGH_RES_MODE tarda 120 ms: pedirla en cada loop()
// devolvía doce veces la misma lectura. Y el suavizado va atado al tiempo, no a
// la frecuencia del loop(), que va a cambiar al entrar la decodificación de JPEG.
static constexpr uint32_t LUZ_PERIODO_MS = 200;

// ============================================================================
// Forward declarations
// ============================================================================
static void setupBacklight();
static void setBacklight(uint8_t value);
static void handleTouch();
static void updateBrightness();
static void setupWebServer();
static void showNextPhoto();
static bool reconstruirManifiesto();
static void pantallaMensaje(const char* l1, const char* l2);
static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
static void drawSetupQR();
static void drawUsageQR();
static void subidaFin(AsyncWebServerRequest* request);
static void subidaChunk(AsyncWebServerRequest* request, String filename, size_t index,
                        uint8_t* data, size_t len, bool final);
static void borrarFin(AsyncWebServerRequest* request);
static void borrarCuerpo(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                         size_t index, size_t total);

// ============================================================================
// Autocomprobación de arranque
//
// nombreValido() y su juego de casos viven en src/nombre.h, compartidos con el
// banco de red y el del display. NO es copia manual: es el guard contra
// ?n=../manifest.txt, y una copia que se relaje no se nota por ningún lado hasta
// que alguien se sale de /fotos/. El porqué está en ese archivo.
// ============================================================================
static void autocomprobar() {
    const bool ok = nombreValidoOk();

    // El fade: si alguien vuelve a tomar el color ACTUAL como origen, el punto
    // medio se va a 252 en vez de 127 y esta línea lo grita. No se ve mirando el
    // LED — solo cronometrándolo.
    const uint8_t medio = LedRGB::lerp(0, 255, 175, 350);
    const bool okFade = LedRGB::lerp(0, 255, 0, 350) == 0 &&
                        LedRGB::lerp(0, 255, 350, 350) == 255 &&
                        medio >= 126 && medio <= 129;

    // QR_BUF está derivado de la fórmula de la librería, no copiado de una
    // medición. Si una versión futura cambiara el cálculo, drawQR() desbordaría
    // 106 bytes de pila en silencio — que es justo el fallo que no se diagnostica.
    const bool okQR = qrcode_getBufferSize(QR_VERSION) == QR_BUF;

    Serial.printf("[autocomprobacion] nombreValido: %s  lerp: %s (medio=%u)  qrBuf: %s (%u)\n",
                  ok ? "ok" : "*** FALLA ***", okFade ? "ok" : "*** FALLA ***", medio,
                  okQR ? "ok" : "*** FALLA ***", (unsigned)QR_BUF);
}

// ============================================================================
// Backlight del display (PWM por LEDC)
// ============================================================================
static void setupBacklight() {
    ledcAttach(PIN_BL, 5000, 8);
    setBacklight(255);
}

static void setBacklight(uint8_t value) {
    currentBacklight = value;
    ledcWrite(PIN_BL, value);
}

// ============================================================================
// Heap — pendiente #5, ETAPA 2
//
// Este archivo ES la etapa 2 de la prueba de provisioning: para cuando llega a
// autoConnect() ya están construidos los cinco globales y ya corrieron
// tft.begin(), montarTarjeta() y lux.begin(). La etapa 1 —WiFiManager y LedRGB, nada
// más— vive en firmware/banco/, env:wifi.
//
// El confusor #1 de §5 es el heap libre en este punto: WiFiManager levanta su
// propio WebServer síncrono más un DNSServer encima de todo eso. Si panica aquí
// y no en la etapa 1, la causa es la presión de heap y no la librería, y el plan
// B es distinto. Estas dos cifras son lo que separa las dos hipótesis, así que
// el printf es palabra por palabra el del banco: si no, no se pueden comparar.
// ============================================================================
static void medir(const char* etiqueta) {
    Serial.printf("[%s] heap=%u mayorBloque=%u minHist=%u\n", etiqueta, ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ESP.getMinFreeHeap());
}

// Termina un fade del LED sin depender de loop(). Hace falta justo antes de
// autoConnect(), que bloquea durante segundos —o minutos con el portal abierto—
// y deja el LED congelado en el último valor escrito: sin esto, el frame que se
// queda en pantalla es el de un fade a medias, o directamente el negro.
static void asentarLed(uint32_t ms) {
    for (uint32_t t = millis(); millis() - t < ms; ) {
        led.update();
        delay(10);
    }
}

// ============================================================================
// setup()
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    autocomprobar();

    led.begin();
    led.setState(LedRGB::State::BOOT);
    // Mismo punto que en el banco: LED ya arrancado, periféricos todavía no. La
    // diferencia contra etapa1-arranque es lo que cuestan los globales de más
    // (.bss); la de preAutoConnect añade lo que cuestan sus begin(). Con las dos
    // cifras el confusor #1 se descompone en vez de quedar en un solo delta.
    medir("etapa2-arranque");

    Wire.begin(21, 22);            // SDA, SCL
    setupBacklight();

    tft.begin();
    tft.setRotation(0);            // Vertical nativo 320×480
    tft.fillScreen(TFT_BLACK);

    // TFT_eSPI espera los píxeles con los bytes al revés que TJpg_Decoder. Sin
    // esta línea la foto sale con los colores cambiados, no rota ni corrida.
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tftOutput);

    // 20 MHz: MEDIDO en el banco, no elegido. La lectura escala con el reloj
    // (0.39 → 1.21 MB/s de 4 a 20) pero la escritura satura en ~0.5 MB/s porque
    // el cuello es el tiempo de programación interno de la tarjeta. 20 es el
    // escalón que da el 88 % de la lectura de 25 MHz dejando margen bajo el tope
    // de 25 que fija la especificación — mismo criterio que los 40 MHz del
    // display. El handshake de init corre a 400 kHz pase lo que pase.
    const bool haySD = montarTarjeta(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI,
                                     PIN_SD_CS, 20000000);
    if (!haySD) {
        Serial.println("[ERROR] SD no montada");
        pantallaMensaje("No se pueden", "leer las fotos");
    } else {
        // Solo se mira si FALTA o está vacío. Comprobar que las N entradas
        // existen costaría un recorrido de directorio, que es justo lo que hace
        // la reconstrucción; que apunte a archivos muertos lo detecta el bucle
        // de dibujado saltándose hasta SALTOS_MAX.
        //
        // Y va AQUÍ, antes de autoConnect(), porque es el momento con el heap
        // más vacío: de ahí sale el tamaño del bloque que ordena.
        File m = TARJETA.open(MANIFIESTO, FILE_READ);
        const bool vacio = !m || m.size() == 0;
        if (m) m.close();
        if (vacio) {
            Serial.println("[manifiesto] falta o esta vacio — reconstruyendo");
            yaReconstruido = true;      // ya se pagó el recorrido en este arranque
            reconstruirManifiesto();
        }
    }

    if (lux.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire)) {
        bh1750Ok = true;
        Serial.println("[OK] BH1750 detectado");
    } else {
        // Degradación elegante (§7): brillo fijo, nunca pantalla apagada.
        Serial.println("[WARN] BH1750 no detectado — brillo fijo");
    }

    // El buffer de subida se pide UNA VEZ y no se libera nunca. La posición no es
    // casual, y es lo único de este bloque que hay que respetar al mover código:
    //
    //  - DESPUÉS del manifiesto, porque reconstruirManifiesto() deriva su bloque
    //    de ordenación de largest_free_block/2. Pedir los 64 KB antes bajaría B de
    //    ~13,822 a ~5,630 fotos sin ninguna necesidad.
    //  - ANTES de WiFiManager, porque AsyncTCP reserva por conexión y parte el
    //    heap: en el banco de red mayorBloque cae de 34,804 a 16,372 B en cuanto
    //    sube el WiFi. 64 KB CONTIGUOS pedidos después son una apuesta.
    //
    // Si falla, /upload contesta 507 y el resto del marco sigue mostrando fotos.
    bufSubida = (uint8_t*)malloc(TOPE_SUBIDA);
    if (!bufSubida) Serial.println("[ERROR] sin heap para el buffer de subida — /upload dara 507");
    medir("etapa2-postBufferSubida");

    // --- WiFiManager: provisioning ---
    WiFiManager wm;

    // Portal en español y de una sola página. Las cadenas vienen de
    // wm_strings_marco.h por el flag WM_STRINGS_FILE, nunca editando la
    // librería. DUPLICADA en firmware/banco/src/wifi.cpp, que es donde se
    // prueba: si divergen, el banco deja de probar este portal. El porqué de
    // quitar info/update/erase está escrito allá y en §5.
    wm.setTitle("Marco de fotos");
    {
        const char* menuPortal[] = {"wifi"};
        wm.setMenu(menuPortal, 1);
    }

    // getWiFiIsSaved() MIENTE si el driver de WiFi no ha arrancado, y esta línea
    // es lo que lo arregla. Medido en el banco (env:wifi, 6-ago-2026), placa
    // virgen: sin ella devuelve «sí» y con ella «no», que es la verdad.
    //
    // El mecanismo: getWiFiIsSaved() → WiFi_hasAutoConnect() → WiFi_SSID(true) →
    // esp_wifi_get_config(WIFI_IF_STA,&conf). Esa última devuelve
    // ESP_ERR_WIFI_NOT_INIT y NO TOCA conf si el driver no arrancó; WiFiManager
    // ignora el código de retorno y arma el String con la pila sin inicializar.
    // Medido: ESP_ERR_WIFI_NOT_INIT antes, ESP_OK con ssid vacío después.
    //
    // Lo que costaba es exactamente la regla 1: con un «sí» falso se pinta la
    // pantalla de «conectando» en vez del QR de setup, y quien recibe el marco se
    // queda sin nada que escanear. Y como depende de la pila, pasaría en el banco
    // y fallaría en la sala. No sirve fiarse de autoConnect() para esto: tiene su
    // propia llamada COMENTADA y un `wifiIsSaved = true` a pelo (WiFiManager.cpp
    // 2.0.17, línea 283), así que él acierta por otro camino y nosotros no.
    WiFi.mode(WIFI_STA);

    // autoConnect() intenta primero las credenciales guardadas y solo levanta el
    // AP si eso falla, así que hay que preguntar ANTES: pintar el QR de setup
    // incondicionalmente lo mostraría unos segundos en cada arranque normal.
    const bool habiaCredenciales = wm.getWiFiIsSaved();
    if (habiaCredenciales) {
        led.setState(LedRGB::State::WIFI_CONNECTING);
    } else {
        led.setState(LedRGB::State::WIFI_SETUP);
        drawSetupQR();
    }
    asentarLed(400);

    medir("etapa2-preAutoConnect");
    const bool wifiOk = wm.autoConnect("Marco-Fotos", "fotos1234");
    medir("etapa2-postAutoConnect");

    // Acabamos de provisionar: REINICIAR, y no es cautela sino la única salida
    // que no depende de acertar un tiempo.
    //
    // Medido en placa el 8-ago-2026, primer recorrido de placa virgen: aquí
    // mismo, server.begin() falla con `bind error: -8` —ERR_USE de LWIP— porque
    // el WebServer del portal cautivo todavía tiene tomado el puerto 80. No es
    // un fallo nuestro ni una sorpresa: WiFiManager lo lleva escrito en su
    // propio shutdownConfigPortal(), «many open issues aobut port not clearing
    // for use with other servers», justo encima de un server->stop() que no
    // basta. Con el bind caído el servidor NUNCA escucha, así que el QR de uso
    // diario apunta a una página que no carga — y como `paginaAbierta` la pone
    // el handler de `/`, el QR tampoco se puede retirar salvo por vencimiento.
    //
    // Y esto SOLO pasa aquí: en un arranque normal WiFiManager no levanta su
    // portal, el puerto está libre y el bind entra a la primera. O sea que es un
    // fallo que solo existe en el único arranque en el que alguien provisiona el
    // regalo, y que ninguna prueba con credenciales guardadas podía enseñar. La
    // regla 1 entera, en el último metro.
    //
    // Tras el reinicio no hay portal, el puerto 80 está libre por construcción y
    // la bandera de NVS es la que se acuerda de que hay que pintar el QR de uso
    // diario — porque para entonces getWiFiIsSaved() ya dice «sí» y la condición
    // de abajo, sola, no volvería a cumplirse jamás.
    //
    // Se prefirió a reintentar el bind desde loop(): reintentar se autocura solo
    // si el puerto llega a liberarse, y eso no está medido. Esto no depende del
    // mecanismo.
    if (wifiOk && !habiaCredenciales) {
        Preferences p;
        if (p.begin("marco", false)) {
            p.putBool(NVS_QR_USO, true);
            p.end();
        }
        Serial.println("[provisioning] listo — reiniciando para liberar el puerto 80");
        Serial.flush();
        delay(100);
        ESP.restart();
    }

    if (wifiOk) {
        Serial.print("[OK] WiFi conectado, IP = ");
        Serial.println(WiFi.localIP());
        // Verde, y el auto-off de su propia fila lo apaga a los 30 s. No se pisa
        // con otro estado más abajo: hacerlo dejaba el verde en milisegundos y
        // el auto-off nunca llegaba a existir.
        led.setState(LedRGB::State::WIFI_OK);
    } else {
        // Sin red el marco sigue mostrando fotos (regla 2). Sin nada que indicar.
        Serial.println("[WARN] WiFi no conectado — modo offline");
        led.setState(LedRGB::State::OFF);
    }

    setupWebServer();
    server.begin();

    // El rojo de la tarjeta gana a cualquier otro estado: es el único fallo que
    // deja al marco sin nada que mostrar. Ya no se queda encendido para siempre
    // —su fila pasó a autoOff— porque ahora el error también está en pantalla.
    if (!haySD) led.setState(LedRGB::State::SD_ERROR);

    // El QR de uso diario sale SOLO al terminar el provisioning, y el disparador
    // es «se acaba de provisionar», no «se conectó»: pintarlo en cada arranque
    // con éxito dejaría un QR unos segundos cada vez que se enchufa el marco, que
    // se ve a proyecto y no a producto — el mismo argumento por el que el de
    // setup tampoco se pinta incondicionalmente.
    //
    // Y hace falta que salga solo: el toque largo también lo muestra, pero nadie
    // le va a explicar ese gesto a quien reciba el marco. El momento en que
    // alguien acaba de meter la contraseña del WiFi es exactamente el momento en
    // que quiere subir fotos, y hasta aquí el único puente era teclear una IP que
    // nadie le ha dicho.
    //
    // El disparador NO puede ser `!habiaCredenciales`, y esto cambió el
    // 8-ago-2026: el arranque que provisiona ya no llega hasta aquí, porque
    // reinicia arriba para soltar el puerto 80. En el arranque de después las
    // credenciales YA están guardadas, así que esa condición es falsa siempre y
    // el QR no saldría nunca. Quien se acuerda es la bandera de NVS.
    //
    // Se borra al leerla, o el QR volvería a salir en cada arranque a partir de
    // aquí — que es exactamente el «se ve a proyecto» que esta rama evita. Pero
    // SOLO si el WiFi levantó: si el módem tarda más que el marco en arrancar y
    // esta vuelta sale sin red, borrarla igual dejaría a quien acaba de
    // provisionar sin QR para siempre y sin nada que hubiera hecho mal. Con la
    // bandera intacta, el siguiente arranque lo pinta.
    //
    // El WiFi.mode(WIFI_STA) de más arriba sigue siendo imprescindible, ahora
    // por el otro extremo: es lo que hace que `habiaCredenciales` sea falso de
    // verdad en la placa virgen y por tanto que la bandera llegue a escribirse.
    bool recienProvisionado = false;
    {
        Preferences p;
        if (p.begin("marco", false)) {
            recienProvisionado = p.getBool(NVS_QR_USO, false);
            if (recienProvisionado && wifiOk) p.remove(NVS_QR_USO);
            p.end();
        }
    }

    if (wifiOk && recienProvisionado) {
        drawUsageQR();
    } else if (haySD) {
        // La primera foto va DESPUÉS del bloque de WiFi: mientras autoConnect()
        // bloquea, la pantalla es la de §5 y no una foto.
        showNextPhoto();
    }
}

// ============================================================================
// loop()
// ============================================================================
void loop() {
    const LedRGB::State pedido = ledPedido;
    if (pedido != LedRGB::State::TOTAL) {
        ledPedido = LedRGB::State::TOTAL;
        led.setState(pedido);
    }
    led.update();

    handleTouch();
    updateBrightness();

    // Un QR en pantalla se retira por lo que llegue antes: alguien abrió la
    // página, o se acabó el minuto. Y mientras esté puesto BLOQUEA el cambio
    // periódico de foto de abajo — sin este `else`, los 30 s del temporizador
    // pintan una foto encima del QR y la única pista para subir fotos se va sin
    // que nadie la haya usado.
    if (qrHasta) {
        if (paginaAbierta || (int32_t)(millis() - qrHasta) >= 0) {
            qrHasta = 0;
            // Sin tarjeta, showNextPhoto() se sale de inmediato y el QR se
            // quedaría pintado para siempre pese a haber vencido. Hay que repintar
            // el error que el QR tapó al salir — es el mismo reparto que setup().
            if (tarjetaMontada()) showNextPhoto();
            else                  pantallaMensaje("No se pueden", "leer las fotos");
        }
    } else if (tarjetaMontada() && millis() - ultimaFotoMs >= FOTO_PERIODO_MS) {
        // showNextPhoto() fija ultimaFotoMs, así que el toque corto reinicia este
        // reloj gratis. El servidor async atiende sus callbacks en otro hilo;
        // nada que bombear aquí.
        showNextPhoto();
    }

    delay(10);
}

// ============================================================================
// Touch capacitivo — gestos corto y largo
// ============================================================================
static void handleTouch() {
    const bool touched = (touchRead(PIN_TOUCH) < TOUCH_THRESHOLD);

    if (touched && !touchActive) {
        touchActive = true;
        touchStartMs = millis();
        led.resetActivityTimer();
    } else if (!touched && touchActive) {
        const uint32_t duration = millis() - touchStartMs;
        touchActive = false;

        if (duration < TOUCH_LONG_MS) {
            showNextPhoto();
        } else if (WiFi.status() == WL_CONNECTED) {
            drawUsageQR();          // QR con la IP actual, regenerado al vuelo
        } else {
            // TODO: levantar el AP con timeout de 5 min (§5) antes del QR.
            drawSetupQR();
        }
    }
}

// ============================================================================
// Brillo automático (BH1750)
// ============================================================================
static void updateBrightness() {
    if (!bh1750Ok) return;

    const uint32_t ahora = millis();
    if (ahora - ultimaLuzMs < LUZ_PERIODO_MS) return;
    ultimaLuzMs = ahora;

    const float luxVal = lux.readLightLevel();
    // readLightLevel devuelve negativos en error (-1 sin bus, -2 lectura mala).
    // Sin este guard, un sensor que se rompe a los seis meses cae en la rama de
    // «oscuro» y deja la pantalla al 20 % para siempre. §7 pide brillo fijo, y
    // bh1750Ok solo cubre el fallo de begin(), no el que aparece después.
    if (luxVal < 0.0f) return;

    uint8_t objetivo;
    if      (luxVal > 100.0f) objetivo = 255;
    else if (luxVal >  10.0f) objetivo = 128;
    else                      objetivo = 51;    // ~20 %

    // Un escalón por lectura: el recorrido completo tarda ~40 s, que es el ritmo
    // al que un cuarto se oscurece. TODO: histéresis, o el brillo oscilará en el
    // borde de cada umbral.
    if      (currentBacklight < objetivo) setBacklight(currentBacklight + 1);
    else if (currentBacklight > objetivo) setBacklight(currentBacklight - 1);
}

// ============================================================================
// POST /upload — los dos callbacks (§4)
//
// LOS DOS CORREN EN EL HILO async_tcp, a prioridad 10 y con 16384 B de stack.
// NO en loop(). Es la trampa central de esta ruta y por eso el LED se pide por
// ledPedido en vez de tocarlo aquí.
// ============================================================================

// Callback de datos. Aquí NO se escribe a la tarjeta: se acumula y ya.
static void subidaChunk(AsyncWebServerRequest* request, String filename, size_t index,
                        uint8_t* data, size_t len, bool final) {
    (void)filename; (void)final;

    if (index == 0 && dueno == nullptr) {
        dueno = request;
        // Olvidar al intruso anterior: los AsyncWebServerRequest se destruyen y
        // el asignador puede devolver la misma dirección a otra petición, así que
        // un puntero rancio aquí acabaría etiquetando como «solapada» una que solo
        // venía sin parte de archivo. Cuesta una línea y quita la duda entera.
        intruso = nullptr;
        acumSubida = 0;
        desbordo = false;
        ledPedido = LedRGB::State::WARNING;

        // Rescata el candado si el cliente se cae a media subida: sin esto,
        // `dueno` se queda apuntando a una petición muerta y todas las siguientes
        // comen 503 para siempre.
        //
        // El GUARD DE IDENTIDAD no es defensivo de más: onDisconnect también se
        // dispara al destruirse una petición que terminó bien, y sin comparar
        // contra `request` el de la foto N limpiaría la propiedad que ya tomó la
        // N+1 — abriendo justo la ventana de dos multipart sobre el mismo buffer
        // que el 503 existe para cerrar.
        request->onDisconnect([request]() {
            if (dueno == request) dueno = nullptr;
        });
    }

    // Subida solapada: se drena en silencio y no se toca NADA del dueño legítimo.
    // Se anota quién es para que su respuesta sea 503 y no 400: un POST sin parte
    // de archivo tampoco es dueño, y ése sí es 400.
    if (dueno != request) {
        intruso = request;
        return;
    }

    if (!desbordo && bufSubida) {
        if (acumSubida + len > TOPE_SUBIDA) {
            // Se olvida lo acumulado y se SIGUE DRENANDO. Cerrar el socket haría
            // que fetch() reportara error de red, y la página no podría distinguir
            // «la foto pesa de más» de «se cayó el WiFi» — que son dos mensajes y
            // dos acciones distintas en §4. Con el buffer reservado no hay nada
            // que liberar; la protección es dejar de copiar.
            desbordo = true;
        } else {
            memcpy(bufSubida + acumSubida, data, len);
        }
    }
    acumSubida += len;
}

// Escribe la foto y la anexa al manifiesto. Devuelve false si algo falló, y en
// ese caso no deja rastro a medias en la tarjeta.
static bool guardarFoto(char* nombreOut, size_t cap) {
    // El nombre se RESERVA EN NVS ANTES de escribir. Si la placa se reinicia a
    // media escritura se quema un número, y §3 dice que los huecos son correctos
    // y que el contador no retrocede. Al revés —persistir después— un reinicio
    // reutilizaría el número y colisionaría con lo que ya esté en el manifiesto.
    Preferences p;
    if (!p.begin("marco", false)) return false;
    const uint32_t n = p.getUInt("contador", 0);
    p.putUInt("contador", n + 1);
    p.end();

    snprintf(nombreOut, cap, "%08lu.JPG", (unsigned long)n);
    char ruta[32];
    snprintf(ruta, sizeof ruta, "%s/%s", DIR_FOTOS, nombreOut);

    File f = TARJETA.open(ruta, FILE_WRITE);
    if (!f) return false;
    const size_t escritos = f.write(bufSubida, acumSubida);
    f.close();
    if (escritos != acumSubida) {
        // Tarjeta llena, y de paso cualquier otro fallo de escritura. Se borra el
        // truncado: un JPEG a medias en /fotos/ entraría al manifiesto en la
        // siguiente reconstrucción y se pintaría roto. Comprobar el espacio antes
        // costaría una consulta más y no cubriría el resto de fallos.
        TARJETA.remove(ruta);
        return false;
    }

    // El manifiesto va DESPUÉS de la foto, nunca antes. Un corte entre los dos
    // pasos deja un archivo huérfano en /fotos/, que la reconstrucción recoge; al
    // revés dejaría una entrada muerta que cuesta un getFsJpgSize fallido por
    // vuelta hasta agotar SALTOS_MAX. Es el orden de §4 para el borrado, al revés.
    //
    // Y este append es lo que saca a /manifest.txt del estado de 0 bytes, que es
    // el que hace que /list conteste 500 (ver el atajo de arriba).
    File m = TARJETA.open(MANIFIESTO, FILE_APPEND);
    if (!m) return false;
    const bool okM = m.printf("%s\n", nombreOut) > 0;
    m.close();
    return okM;
}

// Handler de respuesta. La escritura ocurre AQUÍ, en línea, que es lo que manda
// el BOM: la cola de FreeRTOS con tarea dedicada y 202 es más cara y solo se paga
// si se mide que hace falta. Criterio de escalada, en el BOM.
static void subidaFin(AsyncWebServerRequest* request) {
    // Ocupante ilegítimo: sale SIN limpiar `dueno`. Limpiarlo le arrancaría la
    // propiedad al dueño legítimo a media subida, y su siguiente chunk volvería a
    // reclamar el buffer con acumSubida ya avanzado — foto corrupta, en silencio.
    if (dueno != request) {
        const bool solapada = (request == intruso);
        Serial.printf("[subida] -> %d (%s)\n", solapada ? 503 : 400,
                      solapada ? "solapada" : "sin parte de archivo");
        ledPedido = LedRGB::State::UPLOAD_ERROR;
        request->send(solapada ? 503 : 400, "text/plain", "");
        return;
    }

    int code;
    char json[64] = "";
    char nombre[16];

    if (!bufSubida || !tarjetaMontada()) {
        // 507 para «sin tarjeta» es semánticamente imperfecto —§4 no tiene código
        // para eso— pero el comportamiento que la página le da es el correcto:
        // corta la tanda entera y no reintenta. Inventar un sexto código obligaría
        // a tocar web/index.html y §4 para no ganar nada.
        code = 507;
    } else if (!request->hasParam("foto", true, true)) {
        code = 400;   // §4 especifica el campo "foto"
    } else if (desbordo) {
        code = 413;
    } else if (acumSubida == 0) {
        code = 400;   // un JPEG de 0 B rompería la reproducción y la verificación
    } else if (!guardarFoto(nombre, sizeof nombre)) {
        code = 507;
    } else {
        snprintf(json, sizeof json, "{\"ok\":true,\"n\":\"%s\"}", nombre);
        code = 200;
    }

    // El heap va en la MISMA línea, igual que en el banco: el riesgo de esta ruta
    // es que los 64 KB contiguos no estén, y mayorBloque con AsyncTCP arriba y una
    // conexión viva es el único sitio donde ese número se puede leer. Cuesta un
    // printf que solo va al serie.
    Serial.printf("[subida] %u B -> %d%s%s  heap=%u mayorBloque=%u minHist=%u\n",
                  (unsigned)acumSubida, code,
                  code == 200 ? " " : "", code == 200 ? nombre : "",
                  ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  ESP.getMinFreeHeap());

    // Verde es «foto recibida» además de «conectado y operando» (§9). El auto-off
    // de su fila lo apaga a los 30 s de la última.
    ledPedido = (code == 200) ? LedRGB::State::WIFI_OK : LedRGB::State::UPLOAD_ERROR;

    dueno = nullptr;
    if (code == 200) request->send(200, "application/json", json);
    else             request->send(code, "text/plain", "");
}

// ============================================================================
// POST /delete — borrar una foto del marco (§4)
//
// El cuerpo es JSON y llega por onBody, NO por params: medido en placa que con
// x-www-form-urlencoded AsyncWebServer lo parsea como formulario, el callback de
// cuerpo nunca lo ve, y el servidor contesta 200 sobre un borrado que no ocurrió.
// Por eso la ruta se registra con la forma de CINCO argumentos.
//
// Y no hace falta un parser de JSON para leer una clave de formato fijo: la
// técnica es la del banco, strstr/strchr con la longitud como validación de
// forma. Cero heap, cero dependencia — el mismo argumento con el que §3 justifica
// que el manifiesto sea texto plano.
// ============================================================================
// ============================================================================
// Descriptores de archivo — el recurso que la galería agotó
//
// `AsyncFileResponse` retiene un descriptor ABIERTO mientras dura la descarga, y
// el VFS monta con un tope. Sin acotar, una galería que pide decenas de fotos a la
// vez los agota, y el daño NO se queda en la galería: `loop()` deja de poder abrir
// el manifiesto, la pantalla cae a «No hay fotos» y el fallo remata en abort().
// Medido en placa el 7-ago-2026, con 30 fotos y el marco reiniciándose en bucle.
//
// El recurso escaso NO es el descriptor, es el HEAP: cada conexión de AsyncTCP
// cuesta lo suyo, y el abort() sale de un `new` que ni siquiera puede asignar la
// excepción de fallo. Subir max_files empeora el problema en vez de arreglarlo
// (~4 KB de buffer de sector cada uno); está escrito en tarjeta.h.
//
// Así que el tope va aquí, en peticiones concurrentes, y deja margen para lo que
// loop(), /list, /upload y /delete necesitan de los 5 descriptores del VFS.
static volatile uint8_t fotosEnVuelo = 0;
static constexpr uint8_t PHOTO_MAX_VUELO = 2;

static char borrarN[13];
// Identidad del dueño en vez de un bool: `borrarOk` a secas se queda RANCIO si el
// cuerpo no llega nunca —un POST sin body— y borrarFin ejecutaría el nombre de la
// petición anterior, o sea borraría otra foto. Es el mismo guard que /upload.
static AsyncWebServerRequest* borrarDueno = nullptr;

static void borrarCuerpo(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                         size_t index, size_t total) {
    borrarDueno = nullptr;
    // onBody puede llamarse varias veces; con un JSON de 30 B nunca lo hará, pero
    // el código no debe depender de eso. Si llegara partido, se rechaza con 400.
    if (index != 0 || index + len != total) return;

    char cuerpo[128];
    const size_t n = total < sizeof(cuerpo) - 1 ? total : sizeof(cuerpo) - 1;
    memcpy(cuerpo, data, n);
    cuerpo[n] = 0;                       // el data de onBody NO viene terminado

    const char* c = strstr(cuerpo, "\"n\"");
    if (!c || !(c = strchr(c + 3, ':')) || !(c = strchr(c, '"'))) return;
    c++;
    const char* fin = strchr(c, '"');
    // La longitud ES la validación de forma: 12 es el largo de 00000042.JPG. Lo
    // semántico —el guard contra ../manifest.txt— lo hace nombreValido() abajo.
    if (!fin || fin - c != 12) return;
    memcpy(borrarN, c, 12);
    borrarN[12] = 0;
    borrarDueno = request;
}

// Reescribe el manifiesto saltándose una línea. Devuelve false solo si no pudo
// dejar el índice en un estado bueno.
//
// ponytail: la reescritura corre ENTERA en async_tcp y escala con el número de
// fotos. Con los recuentos reales —cientos— son milisegundos; un manifiesto de
// 121,000 entradas son ~1.5 MB de leer y escribir, muy por encima de cualquier
// timeout. Si algún día importa, la salida es la misma que el BOM ya describe
// para la subida: diferirlo a loop() por cola y responder 202.
static bool quitarDelManifiesto(const char* nombre) {
    File in  = TARJETA.open(MANIFIESTO, FILE_READ);
    if (!in) return false;
    File out = TARJETA.open(TEMPORAL, FILE_WRITE);
    if (!out) { in.close(); return false; }

    in.setTimeout(0);                    // igual que siguienteNombre(), y por lo mismo
    char linea[16];
    uint32_t copiadas = 0;
    while (in.available()) {
        const size_t k = in.readBytesUntil('\n', linea, sizeof linea - 1);
        if (!k) break;
        linea[k] = 0;
        if (linea[k - 1] == '\r') linea[k - 1] = 0;   // por si se editó desde la Mac
        if (linea[0] && strcmp(linea, nombre) != 0) {
            out.printf("%s\n", linea);
            copiadas++;
        }
    }
    in.close();
    out.close();

    // Mismo cierre que reconstruirManifiesto(): f_rename NO sobrescribe, así que
    // el destino se borra primero.
    TARJETA.remove(MANIFIESTO);
    if (!TARJETA.rename(TEMPORAL, MANIFIESTO)) {
        TARJETA.remove(TEMPORAL);        // no dejar basura a medias en la raíz
        return false;
    }
    // El offset es en BYTES y el archivo acaba de encogerse: sin esto apuntaría a
    // media línea. Reproducir desde el principio es lo correcto y es lo que hacen
    // las tres sustituciones de reconstruirManifiesto().
    manifiestoOffset = 0;
    Serial.printf("[borrar] %s fuera, quedan %lu\n", nombre, (unsigned long)copiadas);
    return true;
}

static void borrarFin(AsyncWebServerRequest* request) {
    int code;

    if (!tarjetaMontada()) {
        code = 503;
    } else if (borrarDueno != request || !nombreValido(borrarN)) {
        // Cuerpo ausente, partido, de otra petición, o nombre que no cumple el
        // patrón. Los cuatro son el mismo 400 de §4: la página no puede hacer
        // nada distinto con ninguno.
        code = 400;
    } else {
        char ruta[32];
        snprintf(ruta, sizeof ruta, "%s/%s", DIR_FOTOS, borrarN);
        // LA FOTO PRIMERO, el índice después. Un corte entre los dos pasos deja un
        // archivo huérfano que la reconstrucción recoge; al revés dejaría una
        // entrada muerta que cuesta un getFsJpgSize fallido por vuelta hasta
        // agotar SALTOS_MAX. Es el orden que guardarFoto() documenta invertido.
        TARJETA.remove(ruta);            // que no exista no es un error: el estado
                                         // final es el pedido
        code = quitarDelManifiesto(borrarN) ? 200 : 500;
    }

    // El contador de NVS NO se toca (§3): reutilizar el número de una foto borrada
    // colisionaría con lo que siga en el manifiesto. El hueco es correcto.
    if (code != 200) Serial.printf("[borrar] -> %d\n", code);
    borrarDueno = nullptr;

    if (code == 200) request->send(200, "application/json", "{\"ok\":true}");
    else             request->send(code, "text/plain", "");
}

// ============================================================================
// Servidor web — contrato HTTP (§4)
// ============================================================================
static void setupWebServer() {

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Que alguien pida la página ES la señal de que ya abrió el navegador, y
        // es lo que retira el QR de uso diario antes de su minuto. Se pone aquí y
        // no en /list ni en /photo: ésas llegan detrás de ésta de todas formas.
        paginaAbierta = true;

        // Sin Cache-Control: la página vive en flash y cachearla es correcto. El
        // no-store del banco está ahí por otra razón (su buffer compartido) y esa
        // parte no se copia.
        AsyncWebServerResponse* r =
            request->beginResponse(200, "text/html", PAGINA_GZ, PAGINA_GZ_LEN);
        r->addHeader("Content-Encoding", "gzip");
        request->send(r);
    });

    // Streaming directo desde la tarjeta: cero heap, cero parser (§3).
    server.on("/list", HTTP_GET, [](AsyncWebServerRequest* request) {
        // El manifiesto VACÍO es un estado legítimo —tarjeta sin fotos— y hay
        // que atajarlo aquí, porque no basta con exists(). Medido en placa: con
        // un /manifest.txt de 0 B, ESPAsyncWebServer contesta 500 «Invalid data
        // in handler». El mecanismo está en WebResponses.cpp:727, que decide si
        // el archivo sirve con `if (!_content.available())`: un archivo válido
        // pero vacío da 0 y es indistinguible de uno que falta, así que se va a
        // buscar el .gz, tampoco lo encuentra, y pone 404 sobre una respuesta
        // que el request layer ya juzga inválida.
        //
        // Y hay que distinguir «no hay manifiesto» de «no se pudo abrir», porque
        // tratarlos igual hace que la página MIENTA: con los descriptores agotados
        // esto contestaba 200 con cuerpo vacío y el teléfono decía «el marco no
        // tiene fotos» sobre una tarjeta con 30. Medido en placa.
        if (!TARJETA.exists(MANIFIESTO)) {
            request->send(200, "text/plain", "");   // sin fotos: estado legítimo
            return;
        }
        File m = TARJETA.open(MANIFIESTO, FILE_READ);
        if (!m) {
            request->send(503, "text/plain", "");   // transitorio, y reintentable
            return;
        }
        const bool vacio = m.size() == 0;
        m.close();
        if (vacio) {
            request->send(200, "text/plain", "");
            return;
        }
        request->send(TARJETA, MANIFIESTO, "text/plain");
    });

    server.on("/photo", HTTP_GET, [](AsyncWebServerRequest* request) {
        const AsyncWebParameter* p = request->getParam("n");
        if (!p || !nombreValido(p->value().c_str())) {
            request->send(400, "text/plain", "n");
            return;
        }

        // LA GUARDA QUE FALTABA, y costó una caída del marco. AsyncFileResponse
        // retiene un descriptor ABIERTO mientras dura la descarga, y el VFS monta
        // con un tope de descriptores. La galería pide decenas de fotos casi a la
        // vez, así que sin acotar aquí se agotan — y no se queda sin fotos solo la
        // galería: `loop()` deja de poder abrir el manifiesto, la pantalla cae a
        // «No hay fotos», y el fallo remata en abort() y reinicio.
        //
        // El cliente educado no basta: la concurrencia es disciplina suya y el
        // servidor no puede confiar en ella — el mismo razonamiento que el 503 de
        // /upload. Se reserva margen para lo que loop() necesita.
        if (fotosEnVuelo >= PHOTO_MAX_VUELO) {
            request->send(503, "text/plain", "ocupado");
            return;
        }

        const String path = "/fotos/" + p->value();
        // Abrir aquí y no fiarse de exists(): con los descriptores agotados
        // exists() dice «no existe» sobre una foto que sí está, y de ahí salían
        // 404 que mentían. Con el File en mano la distinción es real.
        File f = TARJETA.open(path, FILE_READ);
        if (!f) {
            request->send(TARJETA.exists(path) ? 503 : 404, "text/plain", "");
            return;
        }

        fotosEnVuelo++;
        request->onDisconnect([]() { if (fotosEnVuelo) fotosEnVuelo--; });
        request->send(f, path, "image/jpeg");
    });

    server.on("/upload", HTTP_POST, subidaFin, subidaChunk);

    // CINCO argumentos: el nullptr es el slot de onUpload (no hay multipart) y el
    // último es onBody, que es por donde llega el JSON. Con la forma de tres el
    // cuerpo no se ve nunca.
    server.on("/delete", HTTP_POST, borrarFin, nullptr, borrarCuerpo);

    // No es hipotético: el navegador pide /favicon.ico solo. Cinco rutas y ningún
    // catch-all significa que todo lo demás cae aquí.
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });
}

// ============================================================================
// Pantallas de texto — y con ellas la deuda de §9
//
// LAS CADENAS VAN SIN ACENTOS A PROPÓSITO. La fuente 4 de TFT_eSPI cubre ASCII
// 32-126 y nada más; las suaves (SMOOTH_FONT) necesitan un .vlw en un sistema de
// archivos que este proyecto no tiene. «Todavía» saldría con un glifo roto, que
// es peor que la falta del acento. No «mejorar» la redacción sin resolver eso.
//
// Que este texto exista es lo que deja a LedRGB apagar la fila SD_ERROR: mientras
// el único canal para decir «no hay tarjeta» era el LED, tenía que quedarse
// encendido. Ver la nota de la tabla en LedRGB.cpp.
// ============================================================================
static void pantallaMensaje(const char* l1, const char* l2) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    const int16_t cx = tft.width() / 2, cy = tft.height() / 2;
    if (l2) {
        tft.drawString(l1, cx, cy - 18, 4);
        tft.drawString(l2, cx, cy + 18, 4);
    } else {
        tft.drawString(l1, cx, cy, 4);
    }
}

// ============================================================================
// El manifiesto (§3)
// ============================================================================

// Devuelve la siguiente línea y avanza el offset. Al pasar del final vuelve al
// principio: ESO es el bucle de reproducción.
static bool siguienteNombre(char* out, size_t cap) {
    File f = TARJETA.open(MANIFIESTO, FILE_READ);
    if (!f) return false;

    // Stream::readBytesUntil() gira en timedRead() hasta agotar el timeout —un
    // segundo por defecto— cuando ya no hay bytes. Con las líneas terminadas en
    // \n nunca se llega ahí, pero un manifiesto escrito a mano sin salto final
    // colgaría el loop() un segundo en cada vuelta del ciclo.
    f.setTimeout(0);

    if (manifiestoOffset >= f.size()) manifiestoOffset = 0;
    f.seek(manifiestoOffset);
    const size_t n = f.readBytesUntil('\n', out, cap - 1);
    out[n] = 0;
    manifiestoOffset = f.position();
    f.close();

    if (n && out[n - 1] == '\r') out[n - 1] = 0;   // por si se editó desde la Mac
    return out[0] != 0;
}

// Las cuatro franjas de margen (§6). NUNCA un fillScreen: cuesta 62 ms medidos a
// 40 MHz y parpadea en negro la pantalla entera, mientras que las dos franjas de
// una 3:4 son ~3 ms. Y el área de la foto no se toca hasta que la cubre la foto
// nueva, así que no hay negro intermedio.
static void barrasNegras(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    const int16_t W = tft.width(), H = tft.height();
    if (y > 0)     tft.fillRect(0, 0, W, y, TFT_BLACK);
    if (y + h < H) tft.fillRect(0, y + h, W, H - (y + h), TFT_BLACK);
    if (x > 0)     tft.fillRect(0, y, x, h, TFT_BLACK);
    if (x + w < W) tft.fillRect(x + w, y, W - (x + w), h, TFT_BLACK);
}

// El callback de TJpg_Decoder: recibe bloques MCU de 16x16 ya con el offset
// aplicado (jd_output suma jpeg_x/jpeg_y) y los empuja por SPI.
//
// Devuelve SIEMPRE true, y eso es deliberado. El ejemplo canónico corta con
// `if (y >= tft.height()) return false` para ahorrarse los bloques de abajo,
// pero ese false llega a jd_decomp como JDR_INTR —o sea «fallo»— y aquí el
// valor de retorno decide si la foto se da por pintada o se salta a la
// siguiente. El recorte no hace falta: la macro PI_CLIP de TFT_eSPI ya descarta
// lo que se sale por los cuatro lados, negativos incluidos, así que un bloque
// fuera de pantalla cuesta un par de comparaciones y nada más.
static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    const uint32_t t = micros();
    tft.pushImage(x, y, w, h, bitmap);
    empujeUs += micros() - t;
    return true;
}

static bool dibujarFoto(const char* nombre) {
    char ruta[32];
    snprintf(ruta, sizeof ruta, "%s/%s", DIR_FOTOS, nombre);

    uint16_t w = 0, h = 0;
    const uint32_t t0 = millis();
    if (TJpgDec.getFsJpgSize(&w, &h, ruta, TARJETA) != JDR_OK || !w || !h) {
        Serial.printf("[foto] %s no se pudo leer\n", nombre);
        return false;
    }
    const uint32_t tTam = millis() - t0;   // un open extra por foto; se mide aparte

    // Escala mínima que quepa. Las fotos del navegador ya vienen dentro de
    // 320x480 y esta rama no se dispara con ellas; existe porque a /fotos/ se
    // puede arrastrar un archivo a mano desde la Mac, y un JPEG de 12 MP no
    // puede dejar el marco pintando una esquina (regla 2). TJpgDec toma el
    // DIVISOR, no el exponente, y un valor que no sea 1/2/4/8 cae a 1 en
    // silencio.
    uint8_t div = 1;
    while (div < 8 && (w / div > tft.width() || h / div > tft.height())) div *= 2;
    TJpgDec.setJpgScale(div);
    const uint16_t fw = w / div, fh = h / div;

    const int16_t x = ((int16_t)tft.width()  - (int16_t)fw) / 2;
    const int16_t y = ((int16_t)tft.height() - (int16_t)fh) / 2;
    barrasNegras(x, y, fw, fh);

    empujeUs = 0;
    uint32_t tLectura = 0, tDibujo = 0;
    JRESULT res = JDR_PAR;                        // «todavía no se intentó»
    const uint32_t heapAntes = ESP.getFreeHeap();

    if (medirLaProxima) {
        medirLaProxima = false;
        // Separar «leer» de «decodificar» EXIGE quitar la lectura de en medio:
        // drawFsJpg las entrelaza, porque jd_input pide 512 B cuando le hacen
        // falta. Así que la PRIMERA foto de cada arranque se dibuja desde un
        // buffer. No se decodifica nada dos veces — la foto se pinta por este
        // camino— y de regalo sale el A/B contra el streaming de producción.
        File f = TARJETA.open(ruta, FILE_READ);
        const size_t bytes = f ? f.size() : 0;
        uint8_t* buf = bytes ? (uint8_t*)malloc(bytes) : nullptr;
        if (buf) {
            const uint32_t ta = millis();
            const bool leido = (f.read(buf, bytes) == bytes);
            tLectura = millis() - ta;
            f.close();
            const uint32_t tb = millis();
            if (leido) res = TJpgDec.drawJpg(x, y, buf, bytes);
            tDibujo = millis() - tb;
            free(buf);
            // La resta va con signo: el empuje es un subconjunto del dibujo, pero
            // los dos se truncan a ms por su lado y un -1 en unsigned sale como
            // 4,294,967,295 en el log.
            Serial.printf("[medida] %s %u B  lectura %lu ms · decode %ld ms · empuje %.1f ms\n",
                          nombre, (unsigned)bytes, (unsigned long)tLectura,
                          (long)tDibujo - (long)(empujeUs / 1000), empujeUs / 1000.0f);
        } else if (f) {
            f.close();
        }
    }

    if (res != JDR_OK) {
        // Producción: streaming desde el archivo, CERO heap. jd_prepare recibe
        // el workspace estático de la librería (3,500 B en .bss) y jd_input lee
        // del fs::File en trozos de 512 B — no hay buffer del JPEG por aquí.
        empujeUs = 0;
        const uint32_t tb = millis();
        res = TJpgDec.drawFsJpg(x, y, ruta, TARJETA);
        tDibujo = millis() - tb;
    }

    const int32_t heapDelta = (int32_t)ESP.getFreeHeap() - (int32_t)heapAntes;
    Serial.printf("[foto] %s %ux%u/%u → %lu ms (tam %lu · dibujo %lu, empuje %.1f) heap %+ld\n",
                  nombre, w, h, div, (unsigned long)(tTam + tDibujo),
                  (unsigned long)tTam, (unsigned long)tDibujo, empujeUs / 1000.0f,
                  (long)heapDelta);
    return res == JDR_OK;
}

// Orden numérico. Sin restar: 4e9 - 1 desborda el int que devuelve el comparador.
static int cmpU32(const void* a, const void* b) {
    const uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

// El contador NO retrocede (§3): reanclar a ciegas tras borrar la foto más alta
// reutilizaría su número y colisionaría con lo que siga en el manifiesto.
static void reanclarContador(uint32_t mayor) {
    Preferences p;
    if (!p.begin("marco", false)) return;
    const uint32_t actual = p.getUInt("contador", 0);
    const uint32_t nuevo  = (mayor + 1 > actual) ? mayor + 1 : actual;
    if (nuevo != actual) p.putUInt("contador", nuevo);
    p.end();
    Serial.printf("[manifiesto] contador NVS %lu → %lu\n",
                  (unsigned long)actual, (unsigned long)nuevo);
}

// Reconstrucción en dos fases. La regla 2 en persona: un índice roto nunca puede
// dejar el marco en negro.
static bool reconstruirManifiesto() {
    File dir = TARJETA.open(DIR_FOTOS);
    if (!dir || !dir.isDirectory()) {
        Serial.println("[manifiesto] no se pudo abrir /fotos/");
        return false;
    }
    File crudo = TARJETA.open(CRUDO, FILE_WRITE);
    if (!crudo) { dir.close(); return false; }

    // ── Fase 1: recorrer y volcar en crudo. CERO RAM, y es el paso caro:
    // openNextFile() ABRE cada archivo, y de ahí salen buena parte de los
    // 4,763 us/entrada medidos en el banco.
    uint32_t validas = 0, descartadas = 0, menor = UINT32_MAX, mayor = 0;
    const uint32_t t0 = millis();
    while (File f = dir.openNextFile()) {
        const char* nm = f.name();                 // basename, no la ruta
        if (nombreValido(nm)) {
            const uint32_t num = strtoul(nm, nullptr, 10);
            if (num < menor) menor = num;
            if (num > mayor) mayor = num;
            crudo.printf("%s\n", nm);
            validas++;
        } else {
            descartadas++;
        }
        f.close();                                 // max_files es 5: obligatorio
    }
    const uint32_t tRecorrido = millis() - t0;
    dir.close();
    crudo.close();

    const uint32_t entradas = validas + descartadas;
    Serial.printf("[manifiesto] recorrido: %lu validas, %lu descartadas, %lu ms (%.0f us/entrada)\n",
                  (unsigned long)validas, (unsigned long)descartadas,
                  (unsigned long)tRecorrido,
                  entradas ? tRecorrido * 1000.0f / entradas : 0.0f);

    // ── Fase 2: ordenar por rangos, releyendo el crudo.
    //
    // El nombre ES el número, así que caben 4 B por foto. Con los recuentos
    // reales de §3 —cientos, a lo sumo unos miles— cabe todo de una vez y esto
    // es la ordenación en RAM de siempre, K = 1: la ruta normal se ejercita en
    // cada prueba en placa. Cuando no cabe, la MISMA ruta da varias pasadas y no
    // hay techo de fotos.
    //
    // La fracción es 1/2 del bloque contiguo mayor y no un número fijo. Ojo con
    // la justificación, que describía UN camino de los DOS: sí, esto corre en el
    // arranque antes de autoConnect() —el momento con el heap más entero—, pero
    // showNextPhoto() también lo llama en RUNTIME cuando el índice se rompe, con
    // WiFi arriba, AsyncTCP pidiendo por conexión y los 64 KB de la subida ya
    // reservados. Por eso la fracción se deriva del heap del momento en vez de
    // fijarse: en el arranque B sale ~13,822 fotos y en runtime bastante menos.
    // Que sea menos no es avería —por debajo del piso de 64 cae a la rama de
    // degradación de abajo, que es orden de directorio y regla 2 respetada.
    //
    // ponytail: las pasadas van acotadas por (mayor-menor)/B, o sea por el VALOR
    // del contador y no por el número de fotos. Un contador disperso da pasadas
    // de más sobre un archivo diminuto; si algún día importara, se particiona por
    // rango observado en vez de por valor.
    //
    // El piso de 64 no es decoración: con B = 0 el `lo += B` del bucle de rangos
    // no avanzaría NUNCA y esto colgaría el arranque. Y si el heap no da ni para
    // 64 fotos, el marco tiene un problema bastante mayor que el orden del
    // manifiesto — que es justo lo que hace la rama de degradación de abajo.
    const size_t B = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 2 / sizeof(uint32_t);
    uint32_t* nums = (B >= 64) ? (uint32_t*)malloc(B * sizeof(uint32_t)) : nullptr;

    if (!nums) {
        // Degradación de regla 2, y NO es una rama nueva: el crudo ya está
        // escrito, así que el marco arranca en orden de directorio en vez de
        // quedarse sin índice.
        Serial.println("[manifiesto] sin RAM para ordenar — se deja en orden de directorio");
        TARJETA.remove(MANIFIESTO);
        TARJETA.rename(CRUDO, MANIFIESTO);
        manifiestoOffset = 0;
        if (validas) reanclarContador(mayor);
        return validas > 0;
    }

    File out = TARJETA.open(TEMPORAL, FILE_WRITE);
    if (!out) { free(nums); TARJETA.remove(CRUDO); return false; }

    const uint32_t t1 = millis();
    uint32_t pasadas = 0, escritas = 0;
    char linea[16];
    for (uint32_t lo = menor; lo <= mayor; lo += B) {
        const uint32_t hi = lo + (uint32_t)B - 1;   // mayor <= 99,999,999: no desborda
        File in = TARJETA.open(CRUDO, FILE_READ);
        if (!in) break;
        in.setTimeout(0);
        size_t n = 0;
        while (in.available() && n < B) {
            const size_t k = in.readBytesUntil('\n', linea, sizeof linea - 1);
            if (!k) break;
            linea[k] = 0;
            const uint32_t num = strtoul(linea, nullptr, 10);
            if (num >= lo && num <= hi) nums[n++] = num;
        }
        in.close();
        qsort(nums, n, sizeof *nums, cmpU32);
        for (size_t i = 0; i < n; i++) out.printf("%08lu.JPG\n", (unsigned long)nums[i]);
        escritas += n;
        pasadas++;
    }
    out.close();
    free(nums);
    const uint32_t tOrden = millis() - t1;

    // rename NO sobrescribe: f_rename falla si el destino existe. Y el crudo se
    // borra AL FINAL: hasta que el manifiesto nuevo esté en su sitio, es la red
    // de seguridad.
    TARJETA.remove(MANIFIESTO);
    if (!TARJETA.rename(TEMPORAL, MANIFIESTO)) {
        Serial.println("[manifiesto] fallo el rename — se queda el crudo");
        TARJETA.rename(CRUDO, MANIFIESTO);
        manifiestoOffset = 0;
        return validas > 0;
    }
    TARJETA.remove(CRUDO);
    manifiestoOffset = 0;

    Serial.printf("[manifiesto] ordenado: %lu lineas, %lu ms, %lu pasada(s), B=%u fotos\n",
                  (unsigned long)escritas, (unsigned long)tOrden,
                  (unsigned long)pasadas, (unsigned)B);
    if (validas) reanclarContador(mayor);
    return escritas > 0;
}

// ============================================================================
// La siguiente foto
// ============================================================================
static void showNextPhoto() {
    if (!tarjetaMontada()) return;   // la pantalla de «no se pueden leer» ya está

    char nombre[16];
    // Dos rondas: la primera sobre el manifiesto que haya, la segunda sobre el
    // reconstruido. El pestillo impide reconstruir en bucle si /fotos/ está
    // vacío de verdad, y se suelta en cuanto una foto pinta: una corrupción
    // posterior tiene que poder disparar otra reconstrucción.
    for (uint8_t ronda = 0; ronda < 2; ronda++) {
        for (uint8_t salto = 0; salto < SALTOS_MAX; salto++) {
            if (!siguienteNombre(nombre, sizeof nombre)) break;
            if (!nombreValido(nombre)) continue;
            if (dibujarFoto(nombre)) {
                ultimaFotoMs = millis();
                yaReconstruido = false;
                // NO se toca el LED aquí, y la línea que había se quitó a
                // propósito. resetActivityTimer() revive el LED apagado por
                // inactividad, y esto ya no lo llama solo el toque: corre cada
                // 30 s por su cuenta, así que reviviría el verde en cada cambio
                // de foto y dejaría un punto parpadeando en la sala toda la
                // noche — la regla 3 exacta que §9 vence con el auto-off. El
                // toque sí lo revive, desde handleTouch(), que es donde hay una
                // persona delante.
                return;
            }
        }
        if (yaReconstruido) break;
        Serial.println("[manifiesto] nada pintable — reconstruyendo desde /fotos/");
        yaReconstruido = true;
        if (!reconstruirManifiesto()) break;
    }

    pantallaMensaje("No hay fotos", nullptr);
    ultimaFotoMs = millis();          // no reintentar en cada vuelta de loop()
}

// Pinta un QR versión 3 centrado, con su zona tranquila y dos líneas de texto.
//
// La ZONA TRANQUILA no es margen estético: el estándar pide 4 módulos de claro
// alrededor y sin ellos muchos escáneres sencillamente no lo ven. Aquí se pinta
// como un rectángulo blanco MAYOR que el QR, y por eso el fondo blanco se dibuja
// antes que los módulos en vez de dejar el negro de la pantalla.
//
// LAS CADENAS VAN SIN ACENTOS, por lo mismo que pantallaMensaje(): la fuente 4
// cubre ASCII 32-126 y nada más.
static void drawQR(const char* carga, const char* l1, const char* l2) {
    QRCode qr;
    // 106 B en la pila. La constante se deriva de la fórmula de la librería en
    // vez de escribirse a mano, y autocomprobar() confirma al arrancar que
    // qrcode_getBufferSize(3) sigue devolviendo esto — si una versión futura
    // cambiara el cálculo, un buffer corto se desbordaría en silencio.
    uint8_t modulos[QR_BUF];
    if (qrcode_initText(&qr, modulos, 3, ECC_LOW, carga) < 0) {
        // No debería ocurrir con cargas de tamaño conocido, pero quedarse con la
        // pantalla en negro sí sería regla 2 rota: al menos se dice algo.
        pantallaMensaje("No se pudo", "generar el codigo");
        return;
    }

    constexpr int16_t ESCALA = 8;
    constexpr int16_t QUIETA = 4;                        // módulos de zona tranquila
    const int16_t lado  = qr.size * ESCALA;              // 232 px
    const int16_t marco = lado + 2 * QUIETA * ESCALA;    // 296 px, cabe en 320
    const int16_t x0 = (tft.width()  - marco) / 2;
    // Un poco por encima del centro: el texto de abajo necesita su sitio.
    const int16_t y0 = (tft.height() - marco) / 2 - 40;

    tft.fillScreen(TFT_BLACK);
    tft.fillRect(x0, y0, marco, marco, TFT_WHITE);

    const int16_t qx = x0 + QUIETA * ESCALA, qy = y0 + QUIETA * ESCALA;
    for (uint8_t y = 0; y < qr.size; y++) {
        for (uint8_t x = 0; x < qr.size; x++) {
            if (qrcode_getModule(&qr, x, y)) {
                tft.fillRect(qx + x * ESCALA, qy + y * ESCALA, ESCALA, ESCALA, TFT_BLACK);
            }
        }
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    const int16_t cx = tft.width() / 2, ty = y0 + marco + 30;
    tft.drawString(l1, cx, ty, 4);
    if (l2) tft.drawString(l2, cx, ty + 30, 4);

    paginaAbierta = false;   // limpiar antes de armar, o se retira en la vuelta siguiente
    qrHasta = millis() + QR_MS;
}

static void drawSetupQR() {
    // Codifica una CONSTANTE, y de ahí que este QR pueda pintarse antes de
    // autoConnect() sin necesitar el modo no bloqueante de WiFiManager. El
    // formato WIFI: lo reconocen Android e iOS de fábrica: se conectan solos y
    // la persona nunca ve una contraseña.
    drawQR("WIFI:S:Marco-Fotos;T:WPA;P:fotos1234;;",
           "Escanea este codigo", "para conectar el marco");
}

static void drawUsageQR() {
    // Se REGENERA al vuelo, y no es paranoia: el DHCP doméstico reasigna, así
    // que la IP de hoy puede no ser la de la semana que viene.
    char url[32];
    snprintf(url, sizeof url, "http://%s", WiFi.localIP().toString().c_str());
    // La IP también en texto: cuesta una línea y cubre a quien prefiera teclearla.
    drawQR(url, "Escanea para subir fotos", url + 7);
}
