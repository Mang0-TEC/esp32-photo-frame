// ══════════════════════════════════════════════════════════════════════
//  BANCO DEL PROVISIONING — ETAPA 1. NO ES EL FIRMWARE DEL MARCO.
//
//  Pendiente #5 del BOM: WiFiManager@2.0.17 sobre arduino-esp32 3.3.11 es
//  la combinación exacta del issue #1797 de tzapu, que reporta un
//  «Guru Meditation Error: Core 1 panic'ed» al levantar el portal. Es la
//  única dependencia capaz de forzar un cambio de plataforma, así que se
//  prueba antes que ningún otro hardware (§12, paso 6).
//
//  Este sketch es la ETAPA 1: WiFiManager y LedRGB, y NADA más. Sin tft,
//  sin hspi, sin SD, sin BH1750, sin AsyncWebServer.
//
//  La ETAPA 2 no vive aquí: es ../platformio.ini env:marco tal cual, que
//  ya construye los cinco globales y corre tft.begin()/SD.begin() antes de
//  autoConnect(). Replicar esa vecindad a mano daría una copia que diverge
//  de lo que de verdad va a correr.
//
//  Partirlo en dos es lo que separa las dos hipótesis. El confusor #1 de §5
//  es el HEAP LIBRE en el momento de autoConnect(): WiFiManager levanta su
//  propio WebServer síncrono más un DNSServer encima de todo lo demás. Si
//  panica en la etapa 2 y no en la 1, la causa es la presión de heap y no
//  la librería — y el plan B es distinto. Sin partirlo, las dos quedan
//  confundidas. De ahí que medir() imprima con el MISMO formato que el
//  banco de red: los números de las dos etapas tienen que poder ponerse
//  uno al lado del otro.
//
//  Es el tercer env de este proyecto: env:mock es el banco de RED y env:led
//  el del LED. Ver el encabezado de ./platformio.ini.
// ══════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <WiFiManager.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

#include "LedRGB.h"

// Mapa de GPIOs del BOM. Cátodo común a GND.
constexpr uint8_t PIN_R = 25;  // 220 Ω
constexpr uint8_t PIN_G = 33;  // 470 Ω
constexpr uint8_t PIN_B = 4;   // 470 Ω

// Idénticos a los de main.cpp: son los que codifica el QR de setup de §5
// («WIFI:S:Marco-Fotos;T:WPA;P:fotos1234;;»). Probar otros no probaría nada.
constexpr char AP_SSID[] = "Marco-Fotos";
constexpr char AP_PASS[] = "fotos1234";

LedRGB led(PIN_R, PIN_G, PIN_B);

// ──────────────────────────────────────────────────────────────────────
//  Utilidades. Las dos son copia deliberada — ver el encabezado.
// ──────────────────────────────────────────────────────────────────────

static inline uint32_t mayorBloque() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

// Mismo printf que banco.cpp y que main.cpp, palabra por palabra. Cambiar el
// formato en un sitio y no en los otros hace ilegible la comparación entre
// etapas, que es el entregable entero de esta prueba.
static void medir(const char *etiqueta) {
  Serial.printf("[%s] heap=%u mayorBloque=%u minHist=%u\n", etiqueta, ESP.getFreeHeap(), mayorBloque(),
                ESP.getMinFreeHeap());
}

// Copia literal de main.cpp, y con el MISMO nombre a propósito: §5, §9 y el
// pendiente #5 lo nombran como el landmark de bisección —el último código que
// corre antes del punto donde el issue #1797 reporta el panic—. No es un
// sospechoso: delay() en arduino-esp32 es vTaskDelay, cede CPU y no toca
// memoria. Termina el fade sin depender de loop(), que durante autoConnect()
// no corre: sin esto el LED se queda congelado en un frame de fade a medias
// durante todo el tiempo que el portal esté abierto.
static void asentarLed(uint32_t ms) {
  for (uint32_t t = millis(); millis() - t < ms;) {
    led.update();
    delay(10);
  }
}

static void menu() {
  Serial.println(F("\n─── banco del provisioning (etapa 1) ────────────────────────"));
  Serial.println(F("  z   borra las credenciales y reinicia → vuelve a salir el"));
  Serial.println(F("      portal. Es el «borrar NVS» del paso 1, y solo toca el"));
  Serial.println(F("      WiFi: erase_flash además borraría la app"));
  Serial.println(F("  r   reinicia SIN borrar → tiene que reconectar solo, sin AP."));
  Serial.println(F("      Éste es el paso que cierra el pendiente #5"));
  Serial.println(F("  m   mide el heap ahora   ·   ?  este menú"));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

static void tecla(char c) {
  switch (c) {
    case 'z': {
      // Instancia temporal: la de setup() era local y ya no existe. Aquí el
      // heap ya está medido, así que lo que reserve da igual.
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("[z] credenciales borradas — reiniciando");
      delay(100);  // que salga el último printf antes del reset
      ESP.restart();
      break;
    }

    case 'r':
      Serial.println("[r] reiniciando SIN borrar — debe reconectar sin levantar el AP");
      delay(100);
      ESP.restart();
      break;

    case 'm': medir("ahora"); break;

    default: menu();
  }
}

// Deja el portal como lo va a ver quien reciba el marco: en español (las
// cadenas vienen de ../../src/wm_strings_marco.h vía WM_STRINGS_FILE) y con una
// sola página.
//
// DUPLICADA en firmware/src/main.cpp, y aquí la duplicación SÍ tiene invariante:
// si las dos divergen, el banco deja de probar el portal que corre en el marco,
// que es el único motivo de que este mock esté en el ESP32 y no en Python.
//
// Por qué se quitan las demás páginas del menú, que es la pregunta de verdad:
//   info    → ~40 filas de diagnóstico (chip id, heap, IDF, MAC). La regla 3
//             dice que nada técnico es visible, y lo único útil de ahí —la IP—
//             ya lo da el QR de uso diario con la IP en texto debajo (§5).
//   update  → NO PUEDE funcionar. `huge_app.csv` tiene una sola partición de
//             app (`app0`, sin `ota_1`), así que `Update.begin()` falla. Es un
//             botón muerto por construcción.
//   erase   → redundante y además destructiva. Si cambian el módem,
//             `autoConnect()` falla al arrancar y el portal vuelve a salir
//             solo; más el toque largo de §8. Un botón que borra la
//             configuración dentro de un regalo solo puede restar.
//   restart / exit / close → tras guardar, WiFiManager cierra el portal solo.
static void configurarPortal(WiFiManager &wm) {
  wm.setTitle("Marco de fotos");
  const char *menu[] = {"wifi"};
  wm.setMenu(menu, 1);
}

// ──────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== BANCO DEL PROVISIONING, ETAPA 1 — no es el firmware del marco ===");
  Serial.printf("[placa] rev v%u.%u  flash %u MB\n", ESP.getChipRevision() / 100,
                ESP.getChipRevision() % 100, ESP.getFlashChipSize() / (1024 * 1024));

  led.begin();
  led.setState(LedRGB::State::BOOT);
  medir("etapa1-arranque");

  // Local, igual que en main.cpp. Su constructor reserva poco, pero la cifra
  // que importa se toma abajo y con el objeto ya construido de todas formas.
  WiFiManager wm;
  configurarPortal(wm);

  // autoConnect() intenta primero las credenciales guardadas y solo levanta el
  // AP si eso falla, así que hay que preguntar ANTES (§5). En el marco esto
  // decide entre el QR de setup y la pantalla de «conectando»; aquí no hay
  // display, así que solo decide el color del LED.
  // ── Probe: ¿getWiFiIsSaved() dice la verdad antes de arrancar el driver? ──
  //
  // La primera corrida en placa lo contradijo: getWiFiIsSaved() devolvió «sí» y
  // acto seguido WiFiManager imprimió «No wifi saved, skipping». La cadena es
  // getWiFiIsSaved() → WiFi_hasAutoConnect() → WiFi_SSID(true) →
  // esp_wifi_get_config(WIFI_IF_STA,&conf), y esa última devuelve
  // ESP_ERR_WIFI_NOT_INIT SIN TOCAR conf si el driver no ha arrancado.
  // WiFiManager ignora el código de retorno y construye el String con lo que
  // hubiera en la pila. Se imprime el código de error para no adivinarlo.
  wifi_config_t conf{};
  const esp_err_t eAntes = esp_wifi_get_config(WIFI_IF_STA, &conf);
  const bool antes = wm.getWiFiIsSaved();
  Serial.printf("[nvs] ANTES de WiFi.mode(STA): esp_wifi_get_config=%s ssid=\"%s\" → guardadas=%s\n",
                esp_err_to_name(eAntes), (const char *)conf.sta.ssid, antes ? "sí" : "no");

  WiFi.mode(WIFI_STA);
  wifi_config_t conf2{};
  const esp_err_t eDespues = esp_wifi_get_config(WIFI_IF_STA, &conf2);
  const bool guardadas = wm.getWiFiIsSaved();
  Serial.printf("[nvs] TRAS  WiFi.mode(STA): esp_wifi_get_config=%s ssid=\"%s\" → guardadas=%s\n",
                esp_err_to_name(eDespues), (const char *)conf2.sta.ssid, guardadas ? "sí" : "no");
  if (antes != guardadas)
    Serial.println("[nvs] *** las dos lecturas DIFIEREN: sin WiFi.mode(STA) la respuesta es basura ***");
  if (guardadas) {
    Serial.println("      esperado: azul parpadeante, y conecta SIN levantar el AP");
    led.setState(LedRGB::State::WIFI_CONNECTING);
  } else {
    Serial.printf("      esperado: azul fijo, y aparece el AP «%s»\n", AP_SSID);
    led.setState(LedRGB::State::WIFI_SETUP);
  }
  asentarLed(400);

  // Con el portal abierto autoConnect() bloquea —minutos si hace falta—, así
  // que loop() no corre: ni el menú responde ni el LED anima. Se queda en el
  // color que acaba de asentar, que es exactamente para lo que sirve la línea
  // de arriba. Si la prueba dice que hace falta animación de verdad, la salida
  // es setConfigPortalBlocking(false) + wm.process() en loop().
  medir("etapa1-preAutoConnect");
  const bool ok = wm.autoConnect(AP_SSID, AP_PASS);
  medir("etapa1-postAutoConnect");

  if (ok) {
    Serial.printf("[OK] WiFi conectado  IP %s  RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    led.setState(LedRGB::State::WIFI_OK);  // verde, y su fila lo apaga a los 30 s
  } else {
    Serial.println("[WARN] WiFi no conectado");
    led.setState(LedRGB::State::OFF);
  }

  menu();
}

void loop() {
  led.update();

  if (Serial.available()) {
    const char c = Serial.read();
    if (c != '\n' && c != '\r' && c != ' ') tecla(c);
  }

  delay(5);
}
