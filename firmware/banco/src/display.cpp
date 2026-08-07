// ══════════════════════════════════════════════════════════════════════
//  BANCO DEL DISPLAY — NO ES EL FIRMWARE DEL MARCO
//
//  Cuarto env de este proyecto (mock = red, led = LedRGB, wifi = provisioning).
//  Responde tres pendientes del BOM que no se deciden desde el escritorio:
//
//    #1  Tipo del pin BL. La prueba de tres pasos —al aire / 3.3 V con 100 Ω /
//        a GND— se hace ANTES con cables y sin código. Este banco confirma la
//        respuesta manejándolo por PWM: si atenúa de verdad, es entrada lógica
//        (Caso B) y el PN2222A no hace falta.
//    #3  Velocidad SPI máxima estable: 27 → 40 → 80 MHz.
//    #4  Dirección I2C del BH1750, casi seguro 0x23.
//
//  ── El barrido de SPI son TRES BINARIOS, no una tecla ─────────────────
//
//  SPI_FREQUENCY no se puede cambiar en caliente: TFT_eSPI lo mete en el
//  SPISettings de cada beginTransaction (TFT_eSPI.cpp:167), o sea que es macro
//  de compilación. Y aunque se pudiera, probaría lo que no es: la secuencia de
//  init del ST7796S corre a esa misma frecuencia, así que subirla después de
//  arrancar NO reproduce lo que hace producción al encender.
//
//  Así que se edita SPI_FREQUENCY en platformio.ini y se reflashea, tres veces.
//  Por eso lo PRIMERO que imprime el arranque es la frecuencia compilada: van a
//  existir tres binarios casi idénticos y hay que saber cuál está en la placa.
//
//  ── Cómo se juzga «estable» ───────────────────────────────────────────
//
//  No se puede leer el framebuffer de vuelta (SDA-0 sin conectar, TFT_MISO=-1),
//  así que el veredicto es visual. La tecla '1' pinta rayas verticales de UN
//  píxel, que es el patrón que más castiga el bus: la corrupción sale como
//  bandas, píxeles corridos o rayas que cambian de fase. La tecla 'f'
//  cronometra repintados completos y da un número comparable entre binarios,
//  para no decidir solo por vista.
// ══════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>

// Mapa de GPIOs del BOM. Los del display llegan por build_flags —TFT_eSPI los
// necesita como macros—, así que aquí solo van los que este sketch maneja.
constexpr uint8_t PIN_BL      = 19;   // Backlight, PWM por LEDC
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

constexpr uint32_t BL_FREQ_HZ = 5000;   // igual que main.cpp
constexpr uint8_t  BL_BITS    = 8;

TFT_eSPI tft = TFT_eSPI();

static uint8_t brillo = 255;
static bool    invertido = false;

// ──────────────────────────────────────────────────────────────────────

static void ponerBrillo(uint8_t v) {
  brillo = v;
  ledcWrite(PIN_BL, v);
}

// Volcado de la configuración con la que se compiló ESTE binario.
//
// La divergencia de pines contra el marco ya no es posible —los flags entran por
// ../../tft_comun.ini y no copiados—, pero esto se queda por la otra razón, que
// es la que de verdad se usa: el barrido del pendiente #3 produce TRES BINARIOS
// casi idénticos, y ésta es la única forma de saber cuál está en la placa. La
// primera línea de cada medición tiene que ser la frecuencia que se midió.
static void volcarConfig() {
  Serial.println(F("\n─── configuración compilada ─────────────────────────────────"));
  Serial.printf("  SPI_FREQUENCY   %u Hz   (%.1f MHz)  ← el pendiente #3\n",
                (unsigned)SPI_FREQUENCY, SPI_FREQUENCY / 1000000.0);
  Serial.printf("  MOSI %d  SCLK %d  CS %d  DC %d  RST %d  MISO %d\n",
                TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST, TFT_MISO);
  Serial.printf("  panel %dx%d, rotación 0 (vertical nativo)   BL en GPIO%u\n",
                TFT_WIDTH, TFT_HEIGHT, PIN_BL);
  Serial.println(F("  Compáralo con el mapa de GPIOs del BOM. MISO debe ser -1."));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

// ──────────────────────────────────────────────────────────────────────
//  Patrones
// ──────────────────────────────────────────────────────────────────────

// El más exigente para el bus: alterna el color en CADA columna, así que no hay
// ninguna compresión ni ráfaga larga de un mismo valor donde esconder un error.
// Si el SPI va demasiado rápido para el cableado, aquí se ve como bandas, franjas
// que cambian de fase a media pantalla, o columnas corridas.
static void rayasFinas() {
  const uint32_t t0 = millis();
  for (int16_t x = 0; x < tft.width(); x++)
    tft.drawFastVLine(x, 0, tft.height(), (x & 1) ? TFT_WHITE : TFT_BLACK);
  Serial.printf("[1] rayas de 1 px, %d columnas en %lu ms. Busca bandas o fases rotas.\n",
                tft.width(), (unsigned long)(millis() - t0));
}

// Áreas planas grandes: aquí se ven los píxeles corridos y los desplazamientos de
// byte, que en el patrón de rayas se confunden con el propio patrón.
static void barrasColor() {
  const uint16_t c[] = {TFT_RED,  TFT_GREEN, TFT_BLUE,  TFT_YELLOW,
                        TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK};
  const int16_t alto = tft.height() / 8;
  for (uint8_t i = 0; i < 8; i++) tft.fillRect(0, i * alto, tft.width(), alto, c[i]);
  Serial.println("[2] ocho barras planas. Busca píxeles corridos y bordes sucios.");
}

// El texto es el detector más sensible de desplazamiento de bytes: un error que
// en una barra de color pasa por «matiz raro» aquí sale como letras rotas.
static void textoPrueba() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("MARCO DE FOTOS", 10, 20, 4);
  tft.setTextFont(2);
  tft.drawString("Banco del display", 10, 60);
  tft.drawString(String("SPI ") + String(SPI_FREQUENCY / 1000000) + " MHz", 10, 82);
  tft.drawString("320x480 vertical nativo", 10, 104);
  tft.drawString("Si esto se lee limpio, el bus aguanta", 10, 126);
  // Rejilla de 1 px hasta el borde: verifica que no se pierden filas ni columnas
  // en los extremos, que es donde primero se nota un pixel clock al límite.
  for (int16_t x = 0; x < tft.width(); x += 20) tft.drawFastVLine(x, 160, 320, TFT_DARKGREY);
  for (int16_t y = 160; y < tft.height(); y += 20) tft.drawFastHLine(0, y, tft.width(), TFT_DARKGREY);
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_RED);   // marco de 1 px en el borde
  Serial.println("[3] texto + rejilla + borde rojo de 1 px a ras del canto.");
}

// El número que hace comparable un binario con otro. 320x480x2 = 307,200 B por
// pantalla; a 27 MHz el techo teórico son ~91 ms, a 40 ~61 ms y a 80 ~31 ms. Si al
// subir la frecuencia el tiempo NO baja en esa proporción, el cuello no es el SPI.
//
// OJO al comparar con el techo que imprime la segunda línea: el reloj SPI del
// ESP32 sale de dividir los 80 MHz del APB entre un entero, así que
// SPI_FREQUENCY=27000000 corre de verdad a 26.67 MHz (80/3) y el techo calculado
// del macro nominal queda ~1.3 % optimista SOLO en esa fila. 40 y 80 son exactos.
static void cronometrar() {
  const uint8_t N = 10;
  const uint16_t colores[] = {TFT_BLACK, TFT_WHITE};
  const uint32_t t0 = millis();
  for (uint8_t i = 0; i < N; i++) tft.fillScreen(colores[i & 1]);
  const uint32_t dt = millis() - t0;

  const uint32_t bytes = (uint32_t)tft.width() * tft.height() * 2 * N;
  Serial.printf("[f] %u pantallas completas en %lu ms → %.1f ms/pantalla, %.2f MB/s\n",
                N, (unsigned long)dt, (float)dt / N, bytes / (dt * 1000.0f));
  Serial.printf("    a %u MHz el techo teórico son %.1f ms/pantalla\n",
                (unsigned)(SPI_FREQUENCY / 1000000),
                (tft.width() * tft.height() * 2 * 8 * 1000.0f) / SPI_FREQUENCY);

  // La misma pantalla por el CAMINO DE PRODUCCIÓN, que no es fillScreen.
  //
  // fillScreen empuja un color repetido en una sola transacción: es el mejor caso
  // del bus y nada más. TJpg_Decoder entrega bloques MCU de 16x16, o sea 600
  // pushImage por pantalla, cada uno con su setWindow y su ciclo de CS. Ese
  // overhead por bloque NO baja al subir la frecuencia.
  //
  // Hacen falta los dos números para contestar «¿el cuello es el SPI?»: si al
  // pasar de 40 a 80 MHz fillScreen casi se parte a la mitad y esta cifra no se
  // mueve, el límite del marco está en el overhead por bloque y subir el reloj no
  // compra nada. Y de paso, ésta es la que dice cuánto tarda en aparecer una foto.
  static uint16_t blk[16 * 16];
  for (uint16_t i = 0; i < 16 * 16; i++) blk[i] = (i & 1) ? TFT_WHITE : TFT_NAVY;

  const uint32_t t1 = millis();
  uint16_t bloques = 0;
  for (int16_t y = 0; y + 16 <= tft.height(); y += 16)
    for (int16_t x = 0; x + 16 <= tft.width(); x += 16, bloques++)
      tft.pushImage(x, y, 16, 16, blk);
  const uint32_t dt1 = millis() - t1;

  Serial.printf("    en bloques de 16x16 (el camino de TJpg_Decoder): %u bloques en %lu ms\n",
                bloques, (unsigned long)dt1);
  Serial.printf("    → %.2fx lo que tarda fillScreen. El exceso es overhead por bloque,\n"
                "      no bus: si no baja al subir MHz, el cuello dejó de ser el SPI.\n",
                dt1 / ((float)dt / N));
}

// ──────────────────────────────────────────────────────────────────────
//  Pendiente #4 — dirección del BH1750
// ──────────────────────────────────────────────────────────────────────

// Escaneo I2C a pelo, sin la librería BH1750: la pregunta es en qué dirección
// responde el módulo, y para eso basta el ACK. Meter la librería aquí añadiría un
// modo de fallo («no detectado») que no distingue «no está» de «está en 0x5C».
static void escanearI2C() {
  Serial.println("[i] escaneando bus I2C (SDA 21, SCL 22)...");
  uint8_t encontrados = 0;
  for (uint8_t dir = 1; dir < 127; dir++) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) {
      encontrados++;
      Serial.printf("    0x%02X responde", dir);
      if (dir == 0x23)      Serial.print("  ← BH1750 con ADDR al aire o a GND (lo que espera el BOM)");
      else if (dir == 0x5C) Serial.print("  ← BH1750 con ADDR a VCC: hay que cambiar la constante de main.cpp");
      Serial.println();
    }
  }
  if (!encontrados)
    Serial.println("    nada. Revisa VCC a 3.3 V (NO 5 V), GND, y que SDA y SCL no estén cruzados.");
}

// ──────────────────────────────────────────────────────────────────────

static void menu() {
  Serial.println(F("\n─── banco del display ───────────────────────────────────────"));
  Serial.println(F("  1  rayas verticales de 1 px → el patrón que revela corrupción"));
  Serial.println(F("  2  ocho barras planas → píxeles corridos y bordes sucios"));
  Serial.println(F("  3  texto + rejilla + borde de 1 px a ras del canto"));
  Serial.println(F("  f  cronometra 10 pantallas → ms/pantalla y MB/s comparables"));
  Serial.println(F("  + -  brillo del backlight   ·   0  apagado   ·   9  al máximo"));
  Serial.println(F("       (muertas si BL está atado a GND — Caso A sin PN2222A)"));
  Serial.println(F("  v  invierte los colores del panel (¿es IPS y responde?)"));
  Serial.println(F("  i  escanea I2C → dirección del BH1750 (pendiente #4)"));
  Serial.println(F("  p  vuelca la configuración compilada   ·   ?  este menú"));
  Serial.println(F("  Para el pendiente #3: edita SPI_FREQUENCY en platformio.ini"));
  Serial.println(F("  y reflashea. 27 → 40 → 80 MHz. NO se puede cambiar en caliente."));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

static void tecla(char c) {
  switch (c) {
    case '1': rayasFinas(); break;
    case '2': barrasColor(); break;
    case '3': textoPrueba(); break;
    case 'f': cronometrar(); break;
    case 'i': escanearI2C(); break;
    case 'p': volcarConfig(); break;

    case '+': ponerBrillo(brillo > 235 ? 255 : brillo + 20); Serial.printf("[bl] %u\n", brillo); break;
    case '-': ponerBrillo(brillo <  20 ?   0 : brillo - 20); Serial.printf("[bl] %u\n", brillo); break;
    // El diagnóstico de esta tecla SOLO vale con BL cableado a GPIO19, o sea en
    // Caso B. Con BL atado a GND para el barrido de SPI —lo que toca hacer en
    // Caso A mientras no esté el PN2222A— la pantalla no se apaga nunca y eso es
    // lo correcto, no un síntoma. Por eso el mensaje pregunta en vez de afirmar.
    case '0': ponerBrillo(0);   Serial.println("[bl] 0 — ¿se apagó? Si NO: o BL está atado a GND (Caso A,\n"
                                               "         esperado durante el barrido), o BL no es lo que creemos."); break;
    case '9': ponerBrillo(255); Serial.println("[bl] 255"); break;

    case 'v':
      invertido = !invertido;
      tft.invertDisplay(invertido);
      Serial.printf("[v] invertido=%s\n", invertido ? "sí" : "no");
      break;

    default: menu();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== BANCO DEL DISPLAY — no es el firmware del marco ===");
  volcarConfig();

  // El backlight ANTES de tft.begin(): sin esto el primer frame del arranque se
  // ve como un flash a pantalla llena, que es justo lo que la regla 3 no quiere
  // en el marco. Aquí no importa, pero el orden es el que va a acabar en main.cpp.
  ledcAttach(PIN_BL, BL_FREQ_HZ, BL_BITS);
  ponerBrillo(0);

  tft.begin();
  tft.setRotation(0);            // vertical nativo 320×480, sin transformación
  tft.fillScreen(TFT_BLACK);
  ponerBrillo(255);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  textoPrueba();
  escanearI2C();
  menu();
}

void loop() {
  if (Serial.available()) {
    const char c = Serial.read();
    if (c != '\n' && c != '\r' && c != ' ') tecla(c);
  }
  delay(5);
}
