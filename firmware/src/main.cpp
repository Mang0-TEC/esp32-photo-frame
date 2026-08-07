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
static uint32_t manifiestoOffset = 0;
static bool     yaReconstruido   = false;   // pestillo anti-bucle de reconstrucción
static uint32_t ultimaFotoMs     = 0;
static bool     medirLaProxima   = true;    // la primera foto de cada arranque se mide

// Acumulador del término «empuje». El callback ES el empuje, así que medirlo
// aquí lo separa exactamente del resto sin tocar la librería.
static uint32_t empujeUs = 0;

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

    Serial.printf("[autocomprobacion] nombreValido: %s  lerp: %s (medio=%u)\n",
                  ok ? "ok" : "*** FALLA ***", okFade ? "ok" : "*** FALLA ***", medio);
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
    if (wm.getWiFiIsSaved()) {
        led.setState(LedRGB::State::WIFI_CONNECTING);
    } else {
        led.setState(LedRGB::State::WIFI_SETUP);
        drawSetupQR();
    }
    asentarLed(400);

    medir("etapa2-preAutoConnect");
    const bool wifiOk = wm.autoConnect("Marco-Fotos", "fotos1234");
    medir("etapa2-postAutoConnect");

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

    // La primera foto va DESPUÉS del bloque de WiFi: mientras autoConnect()
    // bloquea, la pantalla es la de §5 y no una foto.
    if (haySD) showNextPhoto();
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

    // showNextPhoto() fija ultimaFotoMs, así que el toque corto reinicia este
    // reloj gratis. El servidor async atiende sus callbacks en otro hilo; nada
    // que bombear aquí.
    if (tarjetaMontada() && millis() - ultimaFotoMs >= FOTO_PERIODO_MS) showNextPhoto();

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
// Servidor web — contrato HTTP (§4)
// ============================================================================
static void setupWebServer() {

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        // TODO: request->send_P(200, "text/html", webpage_gz, webpage_gz_len)
        //       con Content-Encoding: gzip, desde PROGMEM.
        request->send(200, "text/plain", "Marco de Fotos — placeholder");
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
        File m = TARJETA.open("/manifest.txt", FILE_READ);
        const bool vacio = !m || m.size() == 0;
        if (m) m.close();
        if (vacio) {
            request->send(200, "text/plain", "");
            return;
        }
        request->send(TARJETA, "/manifest.txt", "text/plain");
    });

    server.on("/photo", HTTP_GET, [](AsyncWebServerRequest* request) {
        const AsyncWebParameter* p = request->getParam("n");
        if (!p || !nombreValido(p->value().c_str())) {
            request->send(400, "text/plain", "n");
            return;
        }
        const String path = "/fotos/" + p->value();
        if (!TARJETA.exists(path)) {
            request->send(404, "text/plain", "Not Found");
            return;
        }
        request->send(TARJETA, path, "image/jpeg");
    });

    server.on("/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // TODO: escribir el buffer a la SD aquí, no en el callback de datos.
            ledPedido = LedRGB::State::UPLOAD_ERROR;
            request->send(500, "text/plain", "sin implementar");
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            // Hilo async_tcp: prohibido bloquear con I/O de tarjeta. Se acumula en
            // heap con tope duro de 64 KB → 413, y se escribe una sola vez con
            // final == true. Ver el BOM y §4.
            if (index == 0) ledPedido = LedRGB::State::WARNING;
            (void)data; (void)len; (void)final;
        }
    );

    server.on("/delete", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Responder 200 sobre un borrado que no ocurrió es la trampa que §4
        // documenta como medida en placa; mientras no haya tarjeta, se dice que
        // no está. El parser del cuerpo JSON de referencia está en banco.cpp
        // (borrarCuerpo), junto con el hallazgo del Content-Type.
        request->send(503, "text/plain", "sin tarjeta");
    });

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
    // La fracción es 1/2 del bloque contiguo mayor y no 1/4 porque esto corre en
    // el arranque, antes de autoConnect(): el momento con el heap más vacío de
    // toda la vida del firmware, donde nada compite.
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

static void drawSetupQR() {
    // TODO: QR de WIFI:S:Marco-Fotos;T:WPA;P:fotos1234;; con QRCode de ricmoo,
    //       versión 3, escala 8, centrado, con ≥4 módulos de zona tranquila.
}

static void drawUsageQR() {
    // TODO: QR de http://<IP actual>. Se regenera al vuelo: el DHCP doméstico
    //       reasigna, así que la IP de hoy puede no ser la de la semana que viene.
}
