// ══════════════════════════════════════════════════════════════════════
//  BANCO DEL LED — NO ES LA BASE DEL FIRMWARE REAL
//
//  Verifica LedRGB (../../src/LedRGB.cpp, compilado en su sitio) contra el
//  LED real de cátodo común en los GPIOs del BOM. Responde cuatro cosas que
//  no se pueden decidir desde el escritorio:
//
//    1. El pinout medido con multímetro (azul-verde-GND-rojo) es el real.
//    2. Cuánta corriente pasa de verdad por cada canal. Con los Vf genéricos
//       del BOM el margen sobre el verde y el azul es de 0.3 V, tan poco que
//       la corriente la gobierna la curva del diodo y no la resistencia.
//    3. Si a igual duty los tres canales dan el mismo brillo. No lo hacen —
//       resistencias distintas y Vf distintos— y de aquí salen los factores
//       de LedRGB::escala.
//    4. Si un fade lineal en duty se VE lineal, o si hace falta gamma.
//
//  Es el otro env de este proyecto: env:mock es el banco de RED y no usa
//  ningún GPIO. Ver el encabezado de ./platformio.ini.
// ══════════════════════════════════════════════════════════════════════

#include <Arduino.h>

#include "LedRGB.h"

// Mapa de GPIOs del BOM. Cátodo común a GND.
constexpr uint8_t PIN_R = 25;  // 220 Ω (era 1 kΩ; se bajó tras verse apagado)
constexpr uint8_t PIN_G = 33;  // 470 Ω
constexpr uint8_t PIN_B = 4;   // 470 Ω

LedRGB led(PIN_R, PIN_G, PIN_B);

static const char *NOMBRES[] = {"OFF",      "BOOT",         "WIFI_SETUP",   "WIFI_CONNECTING",
                                "WIFI_OK",  "SD_ERROR",     "UPLOAD_ERROR", "WARNING"};
static_assert(sizeof(NOMBRES) / sizeof(*NOMBRES) == (size_t)LedRGB::State::TOTAL,
              "el banco se quedó atrás respecto del enum State");

// ──────────────────────────────────────────────────────────────────────
//  Autocomprobación. Corre en cada arranque, sin mirar el LED.
// ──────────────────────────────────────────────────────────────────────
static void autocomprobar() {
  const uint8_t medio = LedRGB::lerp(0, 255, 175, 350);
  const uint8_t medioInv = LedRGB::lerp(255, 0, 175, 350);

  bool ok = LedRGB::lerp(0, 255, 0, 350) == 0;        // arranca en el origen
  ok &= LedRGB::lerp(0, 255, 350, 350) == 255;        // llega justo al plazo
  ok &= LedRGB::lerp(0, 255, 999, 350) == 255;        // y no se pasa después
  ok &= medio >= 126 && medio <= 129;                 // el punto medio, en medio
  ok &= medioInv >= 126 && medioInv <= 129;           // e igual bajando
  ok &= LedRGB::lerp(0, 255, 100, 0) == 255;          // duración cero: sin dividir entre 0

  // Los dos valores del medio son la prueba entera. Tomar como origen el color
  // ACTUAL —que writeHardware reescribe en cada paso— mueve el origen bajo la
  // cuenta y convierte la rampa en una exponencial: simulado con el loop de
  // 10 ms, el punto medio sale en 252 en vez de 127, y el fade termina en menos
  // de la mitad del plazo declarado. Mirando el LED no se distingue; aquí sí.
  Serial.printf("[autocomprobacion] lerp: %s (medio=%u, medioInv=%u)\n", ok ? "ok" : "*** FALLA ***", medio,
                medioInv);
}

// WROOM o WROVER. Es requisito duro del BOM y no se ve en la etiqueta con la
// fiabilidad suficiente: la WROVER ocupa GPIO16 y GPIO17 con la PSRAM, y ahí van
// RST y DC del display. Descubrirlo después de soldar cuesta la pantalla entera,
// y aquí sale gratis porque el flasheo del banco ya se iba a hacer.
static void identificarPlaca() {
  const uint32_t psram = ESP.getPsramSize();
  const uint16_t rev = ESP.getChipRevision();  // IDF 5.x: mayor*100 + menor
  Serial.printf("[placa] rev v%u.%u  flash %u MB  PSRAM %u B\n", rev / 100, rev % 100,
                ESP.getFlashChipSize() / (1024 * 1024), psram);
  Serial.printf("[placa] %s\n", psram ? "*** WROVER: PSRAM ocupa GPIO16/17, donde van RST y DC ***"
                                      : "WROOM (sin PSRAM) — GPIO16/17 libres, como pide el BOM");
}

static void volcarTabla() {
  Serial.println("  #  estado             R    G    B   semi  autoOff");
  for (uint8_t i = 0; i < (uint8_t)LedRGB::State::TOTAL; i++) {
    const LedRGB::Perfil &p = LedRGB::perfil((LedRGB::State)i);
    Serial.printf("  %u  %-16s %4u %4u %4u %6u  %s\n", i, NOMBRES[i], p.r, p.g, p.b, p.semiperiodoMs,
                  p.autoOff ? "sí" : "NO");
  }
  Serial.printf("  escala actual: R=%u G=%u B=%u\n", led.escala[0], led.escala[1], led.escala[2]);
}

static void menu() {
  Serial.println(F("\n─── banco del LED ───────────────────────────────────────────"));
  Serial.println(F("  r g b   un canal solo a full → pinout, y multímetro sobre su"));
  Serial.println(F("          resistencia para la corriente real (BOM: ¿0.64 mA?)"));
  Serial.println(F("  w       los tres a full → ¿blanco o rosa?"));
  Serial.println(F("  f       rampa 0→255→0 en 3 s → ¿se ve lineal o hace falta gamma?"));
  Serial.println(F("  0-7     cada estado de §9 (usa '?' para verlos)"));
  Serial.println(F("  a       WARNING y espera: a los 30 s tiene que APAGARSE aunque"));
  Serial.println(F("          esté parpadeando"));
  Serial.println(F("  c R G B color crudo. El ámbar calibrado es 'c 255 70 0'"));
  Serial.println(F("  e R G B fija LedRGB::escala en caliente, p.ej. 'e 120 255 255'"));
  Serial.println(F("  ?       vuelca la tabla   ·   x  apaga"));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

// ──────────────────────────────────────────────────────────────────────

// La rampa NO pasa por el fade del módulo: la pregunta es de percepción —si 8
// bits de duty lineal se ven lineales en este LED— y para eso conviene la fuente
// más simple posible, sin ritmo ni estado de por medio.
static void rampa() {
  Serial.println("[f] 0→255→0 en 3 s, blanco");
  for (int v = 0; v <= 255; v += 3) {
    led.setRaw(v, v, v);
    delay(18);
  }
  for (int v = 255; v >= 0; v -= 3) {
    led.setRaw(v, v, v);
    delay(18);
  }
  Serial.println("[f] fin");
}

static uint32_t espera;  // arranque de la prueba de auto-off; 0 = inactiva

static void tecla(char c) {
  espera = 0;
  switch (c) {
    case 'r': led.setRaw(255, 0, 0);     Serial.println("[raw] ROJO   (GPIO25, 220 Ω)"); break;
    case 'g': led.setRaw(0, 255, 0);     Serial.println("[raw] VERDE  (GPIO33, 470 Ω)"); break;
    case 'b': led.setRaw(0, 0, 255);     Serial.println("[raw] AZUL   (GPIO4,  470 Ω)"); break;
    case 'w': led.setRaw(255, 255, 255); Serial.println("[raw] los tres a full"); break;
    case 'x': led.forceOff();            Serial.println("[raw] apagado"); break;
    case 'f': rampa(); break;
    case '?': volcarTabla(); break;

    // Barrido de color a mano. Existe para calibrar el ámbar de WARNING, que es
    // el único color compuesto de la tabla: su (255,80,0) se eligió suponiendo
    // canales equilibrados y el 80 hay que encontrarlo mirando, no calculándolo.
    case 'c': {
      const long r = Serial.parseInt(), g = Serial.parseInt(), b = Serial.parseInt();
      led.setRaw(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
      Serial.printf("[raw] rgb(%ld,%ld,%ld)\n", constrain(r, 0, 255), constrain(g, 0, 255),
                    constrain(b, 0, 255));
      break;
    }

    case 'e': {
      const long r = Serial.parseInt(), g = Serial.parseInt(), b = Serial.parseInt();
      led.escala[0] = constrain(r, 0, 255);
      led.escala[1] = constrain(g, 0, 255);
      led.escala[2] = constrain(b, 0, 255);
      Serial.printf("[escala] R=%u G=%u B=%u — repite 'w' para juzgar el blanco\n", led.escala[0],
                    led.escala[1], led.escala[2]);
      break;
    }

    case 'a':
      led.setState(LedRGB::State::WARNING);
      espera = millis();
      Serial.println("[a] WARNING (ámbar, parpadeo, autoOff). Tiene que apagarse a los 30 s.");
      break;

    default:
      if (c >= '0' && c < '0' + (int)LedRGB::State::TOTAL) {
        const uint8_t i = c - '0';
        led.setState((LedRGB::State)i);
        const LedRGB::Perfil &p = LedRGB::perfil((LedRGB::State)i);
        Serial.printf("[estado] %u %s — rgb(%u,%u,%u) semi=%u autoOff=%s\n", i, NOMBRES[i], p.r, p.g, p.b,
                      p.semiperiodoMs, p.autoOff ? "sí" : "no");
      } else {
        menu();
      }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== BANCO DEL LED — no es el firmware del marco ===");
  identificarPlaca();
  autocomprobar();
  led.begin();
  volcarTabla();
  menu();
}

void loop() {
  led.update();

  if (Serial.available()) {
    const char c = Serial.read();
    if (c != '\n' && c != '\r' && c != ' ') tecla(c);
  }

  // Marca los segundos de la prueba de auto-off para no cronometrar a ojo.
  if (espera) {
    static uint32_t ultimo;
    const uint32_t s = (millis() - espera) / 1000;
    if (s != ultimo) {
      ultimo = s;
      if (s % 5 == 0 && s) Serial.printf("[a] %lu s\n", (unsigned long)s);
      if (s == 31) {
        Serial.println("[a] pasaron 30 s: el LED debe estar apagado. Si sigue parpadeando,");
        Serial.println("    el auto-off volvió a quedar detrás del early-return del parpadeo.");
        espera = 0;
      }
    }
  }

  delay(5);
}
