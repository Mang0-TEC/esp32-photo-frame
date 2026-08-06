// src/main.cpp — Esqueleto del firmware del marco de fotos ESP32
//
// Estados del LED en §9 de la especificación funcional; el color y el ritmo de
// cada uno viven en la tabla de LedRGB.cpp, no aquí. Este archivo solo decide
// EN QUÉ estado está el marco.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <BH1750.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <qrcode.h>
#include <esp_heap_caps.h>
#include "LedRGB.h"

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
SPIClass hspi(HSPI);                // HSPI dedicado a SD (VSPI va al display)
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
static void drawSetupQR();
static void drawUsageQR();

// ============================================================================
// Validación de nombres — ^[0-9]{8}\.JPG$ exacto (§4)
//
// Copiada de firmware/banco/src/banco.cpp, donde ya corre con su autocomprobación
// desde el banco de red. Sin esto, ?n=../manifest.txt sale de /fotos/ — y una
// comprobación laxa del tipo «mide 12 y termina en .JPG» deja pasar ../../aa.JPG,
// que cumple las dos cosas. El esquema rígido de nombres de §3 es lo que abarata
// la validación correcta a cuatro líneas.
// ============================================================================
static bool nombreValido(const char* s) {
    if (!s || strlen(s) != 12) return false;
    for (int i = 0; i < 8; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return strcmp(s + 8, ".JPG") == 0;
}

static void autocomprobar() {
    static const char* malos[] = {"../manifest.txt", "1.JPG",         "00000001.jpg", "000000001.JPG",
                                  "0000001.JPG",     "0000000a.JPG",  "00000001.JPGX", "../../aa.JPG"};
    bool ok = nombreValido("00000001.JPG") && nombreValido("99999999.JPG");
    for (auto m : malos) ok &= !nombreValido(m);
    ok &= !nombreValido(nullptr);

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
// tft.begin(), SD.begin() y lux.begin(). La etapa 1 —WiFiManager y LedRGB, nada
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

    hspi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    Wire.begin(21, 22);            // SDA, SCL
    setupBacklight();

    tft.begin();
    tft.setRotation(0);            // Vertical nativo 320×480
    tft.fillScreen(TFT_BLACK);

    const bool haySD = SD.begin(PIN_SD_CS, hspi);
    if (!haySD) Serial.println("[ERROR] SD no montada");

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
    // deja al marco sin nada que mostrar.
    if (!haySD) led.setState(LedRGB::State::SD_ERROR);
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

    // TODO: cambio periódico de foto — showNextPhoto();
    // El servidor async atiende sus callbacks en otro hilo; nada que bombear aquí.

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
        if (!SD.exists("/manifest.txt")) {
            request->send(200, "text/plain", "");
            return;
        }
        request->send(SD, "/manifest.txt", "text/plain");
    });

    server.on("/photo", HTTP_GET, [](AsyncWebServerRequest* request) {
        const AsyncWebParameter* p = request->getParam("n");
        if (!p || !nombreValido(p->value().c_str())) {
            request->send(400, "text/plain", "n");
            return;
        }
        const String path = "/fotos/" + p->value();
        if (!SD.exists(path)) {
            request->send(404, "text/plain", "Not Found");
            return;
        }
        request->send(SD, path, "image/jpeg");
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
// Placeholders — galería y QR
// ============================================================================
static void showNextPhoto() {
    // TODO: siguiente entrada del manifiesto, decodificar por bloques MCU con
    //       TJpg_Decoder y empujar al display centrado, con las barras negras
    //       calculadas aquí (§6) y nunca horneadas en el JPEG.
    led.resetActivityTimer();
}

static void drawSetupQR() {
    // TODO: QR de WIFI:S:Marco-Fotos;T:WPA;P:fotos1234;; con QRCode de ricmoo,
    //       versión 3, escala 8, centrado, con ≥4 módulos de zona tranquila.
}

static void drawUsageQR() {
    // TODO: QR de http://<IP actual>. Se regenera al vuelo: el DHCP doméstico
    //       reasigna, así que la IP de hoy puede no ser la de la semana que viene.
}
