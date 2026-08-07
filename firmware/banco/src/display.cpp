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
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>

#include "nombre.h"    // nombreValido(), compartida con el marco y el banco de red
#include "tarjeta.h"   // el montaje por sdspi del IDF, y POR QUÉ no se usa TARJETA.h

// Mapa de GPIOs del BOM. Los del display llegan por build_flags —TFT_eSPI los
// necesita como macros—, así que aquí solo van los que este sketch maneja.
constexpr uint8_t PIN_BL      = 19;   // Backlight, PWM por LEDC
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// Lector SD H95, en HSPI. El display va en VSPI por defecto de TFT_eSPI, y
// USE_HSPI_PORT NO se define nunca: ésa es toda la separación de buses.
constexpr uint8_t PIN_SD_SCK  = 14;
constexpr uint8_t PIN_SD_MISO = 27;   // GPIO12 está PROHIBIDO: es strapping, alto
                                      // al arrancar deja la flash a 1.8 V y la
                                      // placa no bootea. El H95 no trae buffer.
constexpr uint8_t PIN_SD_MOSI = 13;
constexpr uint8_t PIN_SD_CS   = 26;

constexpr uint32_t BL_FREQ_HZ = 5000;   // igual que main.cpp
constexpr uint8_t  BL_BITS    = 8;

// El peso de una foto que llena la pantalla: 32,768 B, que es además un cluster
// exacto de esta tarjeta (§4). Todo lo que se cronometra aquí usa este tamaño,
// porque el número que se busca es el de una foto real y no el de un benchmark.
constexpr size_t FOTO = 32768;

// Frecuencias de montaje. A diferencia de SPI_FREQUENCY —macro de compilación en
// TFT_eSPI, tres binarios—, ésta es argumento de TARJETA.begin() y se puede barrer en
// caliente: el handshake de init corre a 400 kHz pase lo que pase
// (sd_diskio.cpp:502), así que remontar SÍ reproduce lo que hace producción al
// encender. El driver satura a 25 MHz (sd_diskio.cpp:645); pedir más se ignora
// en silencio. main.cpp hoy monta con el DEFAULT de la librería, 4 MHz.
constexpr uint32_t SD_FREQS[] = {4000000, 10000000, 20000000, 25000000};

// Directorio de trabajo del banco. NO se escribe en /fotos/: la estructura FAT es
// idéntica, así que la respuesta es la misma, y la tarjeta de producción se queda
// limpia y con su numeración intacta.
constexpr const char *DIR_BANCO = "/banco";

// Línea base del display SIN SD, medida en este mismo binario al cerrar el
// pendiente #3. Solo vale a 40 MHz: a otra frecuencia la comparación no aplica y
// el informe lo dice en vez de comparar contra un número ajeno.
constexpr uint16_t BASE_RAYAS_MS   = 67;   // 320 drawFastVLine
constexpr uint16_t BASE_BLOQUES_MS = 77;   // 600 pushImage de 16x16

TFT_eSPI tft = TFT_eSPI();
SPIClass hspi(HSPI);

static uint8_t brillo = 255;
static bool    invertido = false;

static uint32_t sdFreq = SD_FREQS[0];
static uint8_t *bufW = nullptr, *bufR = nullptr;   // dos de FOTO, de malloc

// El bloque de 16x16 que se empuja en el camino de TJpg_Decoder. A nivel de
// archivo porque lo usan cronometrar() y la prueba de coexistencia, y tienen que
// empujar EXACTAMENTE lo mismo o sus tiempos no son comparables.
static uint16_t blk[16 * 16];

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
static uint8_t escanearEn(uint8_t sda, uint8_t scl) {
  Wire.end();
  Wire.begin(sda, scl);
  delay(50);
  Serial.printf("[i] escaneando con SDA %u, SCL %u...\n", sda, scl);
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
  return encontrados;
}

// Recuperación de un bus I2C TRABADO — 9 pulsos de reloj y un STOP.
//
// Un esclavo interrumpido a media transferencia se queda esperando los relojes que
// le faltan y mantiene SDA BAJO todo ese tiempo. Resetear el ESP32 NO lo arregla:
// quien sujeta la línea es el esclavo, no el maestro. Solo lo suelta terminando de
// darle los relojes del byte, o quitándole la alimentación.
//
// Medido en este banco: tras correr el SPI a 80 MHz el BH1750 quedó así, y siguió
// trabado a través de un reset Y de un reflasheo a 40 MHz.
//
// Lo PRIMERO que imprime son los niveles en reposo, que es el diagnóstico de
// verdad: si SDA ya está alto, el bus NO está trabado y el problema es otro —un
// cable suelto o el sensor muerto— y los pulsos no van a arreglar nada.
static bool recuperarI2C() {
  Wire.end();

  // pinMode() NO basta: en el ESP32 deja la salida del periférico enrutada por la
  // matriz de GPIO, así que un I2C atascado manejando SCL bajo lo sigue manejando
  // y la lectura de abajo mide el periférico en vez del cable. Hay que soltar la
  // matriz primero. Costó una lectura falsa de «SCL BAJO» averiguarlo.
  pinMatrixOutDetach(PIN_I2C_SDA, false, false);
  pinMatrixOutDetach(PIN_I2C_SCL, false, false);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delay(2);   // µs no alcanzan si el cable trae capacidad de protoboard

  Serial.printf("[r] en reposo: SDA %s, SCL %s\n",
                digitalRead(PIN_I2C_SDA) ? "alto" : "BAJO  <- trabado",
                digitalRead(PIN_I2C_SCL) ? "alto" : "BAJO  <- trabado");

  uint8_t pulsos = 0;
  while (digitalRead(PIN_I2C_SDA) == LOW && pulsos < 9) {
    pinMode(PIN_I2C_SCL, OUTPUT);
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(5);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);   // soltar, no forzar alto: es colector abierto
    delayMicroseconds(5);
    pulsos++;
  }

  // STOP: SDA de bajo a alto con SCL en alto. Deja al esclavo en estado conocido.
  pinMode(PIN_I2C_SDA, OUTPUT);
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(5);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  delayMicroseconds(5);

  const bool libre = digitalRead(PIN_I2C_SDA) == HIGH;
  Serial.printf("[r] %u pulsos -> SDA %s\n", pulsos, libre ? "alto, bus libre" : "SIGUE BAJO");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  return libre;
}

// Escanea en el orden del BOM y, si sale vacío, repite con SDA y SCL cambiados.
//
// Es la causa más común y la única que un voltímetro NO descarta: con los dos
// cables invertidos las dos líneas siguen leyendo 3.3 V en reposo, porque las
// pull-ups del GY-302 están igual. Probarlo por software ahorra el viaje de
// mover cables, medir y volver a preguntar.
//
// Si aparece en el orden invertido, se corrigen LOS CABLES, no el firmware: el
// mapa del BOM es el que va a acabar soldado.
static void escanearI2C() {
  if (escanearEn(PIN_I2C_SDA, PIN_I2C_SCL)) return;

  Serial.println("    nada en el orden del BOM. Probando invertidos...");
  if (escanearEn(PIN_I2C_SCL, PIN_I2C_SDA)) {
    Serial.println("    *** SDA y SCL ESTÁN INVERTIDOS. Cambia los cables:");
    Serial.printf("    ***   SDA del sensor -> GPIO%u,  SCL -> GPIO%u\n",
                  PIN_I2C_SDA, PIN_I2C_SCL);
    escanearEn(PIN_I2C_SDA, PIN_I2C_SCL);   // dejar el bus como manda el BOM
    return;
  }

  escanearEn(PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.println("    nada en ninguno de los dos órdenes. Con VCC a 3.3 V y las dos");
  Serial.println("    líneas en reposo alto, el sospechoso es ADDR flotando a media");
  Serial.println("    escala: el BH1750 pide <0.3xVCC para 0x23 y >0.7xVCC para 0x5C,");
  Serial.println("    y en medio no contesta a ninguna. Ata ADDR a GND.");
}

// ──────────────────────────────────────────────────────────────────────
//  Lector SD H95 — HSPI, y la coexistencia con el display en VSPI
// ──────────────────────────────────────────────────────────────────────

static void diagnosticarSD();   // definida más abajo; montarSD la llama al fallar

// El bus SPI2 lo pueden querer DOS dueños incompatibles, y hay que turnarlos a
// mano. El SPIClass de Arduino (`hspi`) lo arranca con spiStartBus de
// esp32-hal-spi y lo usan las sondas de comandos crudos; el driver sdspi del IDF
// lo arranca con spi_bus_initialize y es el que monta el sistema de archivos.
// Dejar los dos activos a la vez es pedir que uno pise la configuración del
// otro. Estas dos funciones son todo el protocolo de traspaso.
static bool arduinoTieneElBus = false;

static void tomarBusArduino() {
  if (arduinoTieneElBus) return;
  desmontarTarjeta();                                          // libera SPI2 del IDF
  hspi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  arduinoTieneElBus = true;
}

static void soltarBusArduino() {
  if (!arduinoTieneElBus) return;
  hspi.end();
  arduinoTieneElBus = false;
}

static bool montarSD(uint32_t hz) {
  soltarBusArduino();
  desmontarTarjeta();
  sdFreq = hz;
  const bool ok = montarTarjeta(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS, hz);
  Serial.printf("[sd] montaje a %.1f MHz por el driver sdspi del IDF: %s\n",
                hz / 1000000.0, ok ? "ok" : "*** FALLA ***");
  if (!tarjetaMontada()) {
    Serial.println("     Con este driver el CMD59 NO se manda (flag IGNORE_DATA_CRC),");
    Serial.println("     así que el fallo del CRC que tumbaba a SD.h queda descartado.");
    Serial.println("     Orden de sospecha del cableado:");
    Serial.println("       1. MISO y MOSI intercambiados — un voltímetro NO lo detecta,");
    Serial.println("          igual que con SDA/SCL. Se comprueba por continuidad.");
    Serial.println("       2. Tarjeta no insertada. No hay hot-swap.");
    Serial.println("       3. Riel bajo en el pin +5 del propio módulo (<4.5 V).");
    Serial.println("       4. Sistema de archivos: exFAT NO monta, tiene que ser FAT32.");
    diagnosticarSD();   // separa «no contesta nadie» de «contesta y falla arriba»
    return false;
  }
  if (!TARJETA.exists(DIR_BANCO)) TARJETA.mkdir(DIR_BANCO);
  return true;
}

// ── Sondas de comandos crudos, sin FatFs ni driver de por medio ───────
//
// Son las que convirtieron «no monta» en una causa con número, y se quedan: si
// algún día vuelve a fallar el montaje, distinguen «no contesta nadie»
// —cableado, alimentación, tarjeta ausente— de «contesta y falla más arriba».
//
// PIDEN EL BUS a Arduino, así que DESMONTAN la tarjeta mientras corren. Después
// hay que volver a montar con 'M'.

// CRC7 del paquete de comando, polinomio x^7+x^3+1, el mismo del driver. Se
// calcula siempre en vez de llevar 0x95 y 0x87 hardcodeados: así la sonda es
// correcta esté el CRC de la tarjeta encendido o apagado, que es justo la
// variable que hace falta aislar.
static uint8_t crc7(const uint8_t *d, uint8_t n) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t b = d[i];
    for (uint8_t j = 0; j < 8; j++) {
      crc <<= 1;
      if ((b ^ crc) & 0x80) crc ^= 0x09;
      b <<= 1;
    }
  }
  return (crc << 1) | 0x01;
}

// Un comando en modo SPI. Devuelve R1; si `cola` no es nulo le deja los 4 bytes
// extra de una R3/R7. El bit 7 de R1 es 0 en toda respuesta válida, y ésa es la
// condición de espera — no «distinto de 0xFF», que confunde una línea en reposo
// con una respuesta legítima.
static uint8_t cmdSPI(uint8_t cmd, uint32_t arg, uint8_t *cola = nullptr) {
  const uint8_t paq[5] = {(uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24),
                          (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg};
  hspi.transfer(0xFF);
  for (uint8_t b : paq) hspi.transfer(b);
  hspi.transfer(crc7(paq, 5));

  uint8_t r = 0xFF;
  for (uint8_t i = 0; i < 16 && (r & 0x80); i++) r = hspi.transfer(0xFF);
  if (cola) for (uint8_t i = 0; i < 4; i++) cola[i] = hspi.transfer(0xFF);
  return r;
}

// Los ≥74 ciclos con CS alto y MOSI alto son cómo la tarjeta entra en modo SPI
// tras alimentarse. Sin esto una tarjeta recién encendida ignora el CMD0.
static void despertarTarjeta() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  for (uint8_t i = 0; i < 20; i++) hspi.transfer(0xFF);
}

// CMD10 SEND_CID: R1, luego bloque de datos (token 0xFE + 16 B + CRC16). Es la
// identidad de la tarjeta y sirve para comprobar que un cambio de tarjeta es de
// verdad un cambio y no la misma otra vez.
static bool leerCID(uint8_t *cid) {
  const uint8_t r = cmdSPI(10, 0);
  if (r > 0x01) return false;
  uint8_t t = 0xFF;
  for (uint16_t i = 0; i < 2000 && t != 0xFE; i++) t = hspi.transfer(0xFF);
  if (t != 0xFE) return false;
  for (uint8_t i = 0; i < 16; i++) cid[i] = hspi.transfer(0xFF);
  hspi.transfer(0xFF); hspi.transfer(0xFF);   // CRC16 del bloque, se descarta
  return true;
}

static void volcarCID() {
  uint8_t c[16];
  if (!leerCID(c)) { Serial.println(F("  CMD10 SEND_CID           *** no se pudo leer")); return; }
  const uint32_t serie = ((uint32_t)c[9] << 24) | ((uint32_t)c[10] << 16) |
                         ((uint32_t)c[11] << 8) | c[12];
  const uint16_t mdt = ((uint16_t)c[13] << 8) | c[14];
  Serial.printf("  CMD10 SEND_CID           fab 0x%02X  OID %c%c  modelo %c%c%c%c%c\n",
                c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
  Serial.printf("        rev %u.%u  serie 0x%08lX  fabricada %u/%u\n",
                c[8] >> 4, c[8] & 0x0F, (unsigned long)serie,
                mdt & 0x0F, 2000 + ((mdt >> 4) & 0xFF));
  Serial.println(F("        La SERIE es lo que distingue una tarjeta de otra."));
}

static uint8_t sondearCMD0() {
  tomarBusArduino();
  hspi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  despertarTarjeta();
  digitalWrite(PIN_SD_CS, LOW);
  const uint8_t r = cmdSPI(0, 0);             // GO_IDLE_STATE
  digitalWrite(PIN_SD_CS, HIGH);
  hspi.transfer(0xFF);
  hspi.endTransaction();
  return r;
}

// La secuencia de init entera, paso a paso. `crc59` intercala CMD59
// (CRC_ON_OFF) tras el CMD0, que era la ÚNICA diferencia estructural contra la
// secuencia de sd_diskio.cpp:542, y con la que se cerró el diagnóstico:
//
//    -1  sin CMD59            ('d')   OCR = 0x40FF8000  ✓
//     1  CMD59 con CRC ON     ('D')   OCR = 0x00000000  ✗  ← el fallo de SD.h
//     0  CMD59 con CRC OFF    ('C')   OCR = 0x40FF8000  ✓  ← el CONTROL
//
// El control es lo que lo hace concluyente: manda el MISMO número de comandos
// que la ruta que falla, así que la variable es el CRC y no el tiempo.
static void sondearInit(int crc59) {
  tomarBusArduino();
  Serial.printf("\n─── init a mano, 400 kHz — CMD59: %s ───────────────\n",
                crc59 < 0 ? "ninguno" : (crc59 ? "CRC ON, como el driver" : "CRC OFF, control"));
  uint8_t c[4];

  hspi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  despertarTarjeta();
  digitalWrite(PIN_SD_CS, LOW);

  uint8_t r = cmdSPI(0, 0);
  Serial.printf("  CMD0  GO_IDLE_STATE      R1=0x%02X %s\n", r,
                r == 0x01 ? "→ en idle, ok" : "→ *** se esperaba 0x01");

  if (crc59 >= 0) {
    r = cmdSPI(59, (uint32_t)crc59);
    Serial.printf("  CMD59 CRC_ON_OFF (=%d)    R1=0x%02X %s\n", crc59, r,
                  r != 0x01 ? (r == 0x05 ? "→ 0x05, comando ilegal: no lo soporta"
                                         : "→ *** respuesta inesperada")
                            : (crc59 ? "→ aceptado, CRC ENCENDIDO"
                                     : "→ aceptado, CRC apagado"));
    r = 0x01;   // seguir con el resto de la secuencia pase lo que pase
  }

  if (r == 0x01) {
    r = cmdSPI(8, 0x1AA, c);
    Serial.printf("  CMD8  SEND_IF_COND       R1=0x%02X  eco=%02X%02X%02X%02X %s\n",
                  r, c[0], c[1], c[2], c[3],
                  (r == 0x01 && c[3] == 0xAA) ? "→ v2.0, 2.7-3.6 V, ok" : "→ *** eco malo");

    r = cmdSPI(58, 0, c);
    const uint32_t ocr = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) |
                         ((uint32_t)c[2] << 8) | c[3];
    Serial.printf("  CMD58 READ_OCR           R1=0x%02X  OCR=%08lX\n", r, (unsigned long)ocr);
    Serial.printf("        bit20 (3.2-3.3 V) = %d  ← es el que SD.h exige AQUÍ\n",
                  (int)((ocr >> 20) & 1));
    if (!ocr) Serial.println(F("        Cuatro ceros NO son un OCR vacío: en SPI una tarjeta\n"
                               "        ocupada mantiene DO en bajo. Está contestando «ocupada»."));

    uint16_t vueltas = 0;
    const uint32_t t0 = millis();
    do {
      cmdSPI(55, 0);                             // APP_CMD
      r = cmdSPI(41, 0x40000000);                // ACMD41 con HCS
      vueltas++;
    } while (r == 0x01 && millis() - t0 < 2000);
    Serial.printf("  ACMD41 (CMD55+41)        R1=0x%02X tras %u vueltas y %lu ms %s\n",
                  r, vueltas, (unsigned long)(millis() - t0),
                  r == 0x00 ? "→ inicializada, ok" : "→ *** no salió de idle");

    if (r == 0x00) {
      r = cmdSPI(58, 0, c);
      const uint32_t ocr2 = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) |
                            ((uint32_t)c[2] << 8) | c[3];
      Serial.printf("  CMD58 READ_OCR (2ª)      R1=0x%02X  OCR=%08lX → %s\n", r,
                    (unsigned long)ocr2, (ocr2 & (1UL << 30)) ? "SDHC/SDXC" : "SDSC");
      Serial.println(F("        El MISMO comando, ya inicializada, sí devuelve el OCR bueno."));
      volcarCID();
    }
  }

  digitalWrite(PIN_SD_CS, HIGH);
  hspi.transfer(0xFF);
  hspi.endTransaction();
  Serial.println(F("  (la tarjeta quedó DESMONTADA: 'M' para volver a montarla)"));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

// El equivalente de escanearI2C() para la SD, y por la misma razón: la causa más
// probable de un montaje fallido es un par de cables cambiados, y NO hay forma
// de verlo con un multímetro. Se prueba el orden del BOM y, si no contesta, el
// invertido. Si aparece en el invertido se corrigen LOS CABLES, no el firmware:
// el mapa del BOM es el que va a acabar soldado.
static void diagnosticarSD() {
  Serial.println(F("\n─── sonda de la tarjeta (CMD0 crudo, 400 kHz) ───────────────"));
  Serial.println(F("  (desmonta la tarjeta: el bus SPI2 se le presta a Arduino)"));

  const uint8_t r = sondearCMD0();
  Serial.printf("  orden del BOM (MISO %u, MOSI %u): R1 = 0x%02X\n",
                PIN_SD_MISO, PIN_SD_MOSI, r);

  if (r == 0x01) {
    Serial.println(F("  → LA TARJETA CONTESTA. Cableado y alimentación están bien, y el"));
    Serial.println(F("    fallo de montaje está por encima. Sigue la secuencia completa:"));
    Serial.println(F("─────────────────────────────────────────────────────────────"));
    sondearInit(-1);
    return;
  }

  // hspi.end() suelta la matriz de GPIO; sin eso begin() con otros pines no
  // reasigna nada (SPI.cpp:67 retorna si ya está inicializado), que es la misma
  // trampa que pinMatrixOutDetach en el I2C.
  hspi.end();
  hspi.begin(PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_CS);   // ← cambiados
  hspi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  despertarTarjeta();
  digitalWrite(PIN_SD_CS, LOW);
  const uint8_t rInv = cmdSPI(0, 0);
  digitalWrite(PIN_SD_CS, HIGH);
  hspi.endTransaction();
  Serial.printf("  invertidos     (MISO %u, MOSI %u): R1 = 0x%02X\n",
                PIN_SD_MOSI, PIN_SD_MISO, rInv);

  hspi.end();
  hspi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);   // dejarlo como el BOM

  if (rInv == 0x01) {
    Serial.println(F("  *** MISO y MOSI ESTÁN CAMBIADOS. Corrige LOS CABLES:"));
    Serial.printf ("  ***   DO/MISO del módulo -> GPIO%u,  DI/MOSI -> GPIO%u\n",
                   PIN_SD_MISO, PIN_SD_MOSI);
    Serial.println(F("  *** El firmware NO se toca: ése es el mapa que se va a soldar."));
  } else if (r == 0xFF && rInv == 0xFF) {
    Serial.println(F("  → NADIE CONTESTA en ninguno de los dos órdenes. MISO se queda en"));
    Serial.println(F("    alto, que es lo que se lee cuando no hay nada tirando de la"));
    Serial.println(F("    línea. Sospechosos, en orden:"));
    Serial.println(F("      1. Tarjeta no insertada, o no asentada en el zócalo."));
    Serial.println(F("      2. MISO sin conectar, o en el pin equivocado."));
    Serial.println(F("      3. SCK o CS sin conectar — sin reloj no hay respuesta."));
    Serial.println(F("      4. El riel de 3.3 V del módulo caído bajo carga."));
  } else if (r == 0x00) {
    Serial.println(F("  → R1 = 0x00: la línea está clavada BAJA. O MISO va a GND, o la"));
    Serial.println(F("    tarjeta ya estaba inicializada. Comprueba continuidad de MISO."));
  } else {
    Serial.printf ("  → Contesta pero con bits de error (0x%02X). La tarjeta está ahí y\n", r);
    Serial.println(F("    el cableado transmite; sospecha de integridad de señal o riel."));
  }
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

static const char *tipoTarjeta() {
  const sdmmc_card_t *c = tarjetaCard();
  if (!c) return "ninguna";
  if (c->is_mmc) return "MMC";
  return c->ocr & (1UL << 30) ? "SDHC/SDXC" : "SD (SDSC)";
}

// [s] — lo que el ESP32 ve de la tarjeta, contra lo que se midió desde Windows.
static void identidad() {
  const sdmmc_card_t *c = tarjetaCard();
  if (!c) { Serial.println("[s] sin tarjeta montada."); return; }

  Serial.println(F("\n─── tarjeta, vista por el ESP32 ─────────────────────────────"));
  Serial.printf("  tipo            %s\n", tipoTarjeta());
  Serial.printf("  fabricante      0x%02X  modelo %s  serie 0x%08lX\n",
                c->cid.mfg_id, c->cid.name, (unsigned long)c->cid.serial);
  Serial.printf("  montada a       %.1f MHz  (real %d kHz)\n",
                sdFreq / 1000000.0, c->max_freq_khz);
  Serial.printf("  capacidad       %llu B  (%.2f GB)\n",
                (uint64_t)c->csd.capacity * c->csd.sector_size,
                (double)c->csd.capacity * c->csd.sector_size / 1e9);
  Serial.printf("  sectores        %d de %d B\n", c->csd.capacity, c->csd.sector_size);
  Serial.println(F("  OJO: ese 512 es el SECTOR de la tarjeta, NO el cluster de FAT32."));
  Serial.println(F("       Compararlo con los 32,768 B del BOM es el error obvio; el"));
  Serial.println(F("       cluster se mide con la tecla 'k', que no lo lee de ninguna API."));

  // La PRIMERA consulta recorre la FAT entera para contar clusters libres
  // (~242,000 en esta tarjeta). Es lenta una sola vez y FatFs cachea el
  // resultado. Se cronometra para que no se confunda con lentitud de la tarjeta.
  uint64_t total = 0, usado = 0;
  const uint32_t t0 = millis();
  if (tarjetaEspacio(total, usado))
    Serial.printf("  formateado      %llu B usados de %llu  (f_getfree tardó %lu ms)\n",
                  usado, total, (unsigned long)(millis() - t0));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

// [k] — tamaño de cluster MEDIDO, no leído.
//
// La API de Arduino no expone csize de FatFs, así que se mide por consumo: un
// archivo de UN byte ocupa un cluster entero, y el delta de usedBytes() ES ese
// cluster. Encaja además con el criterio del proyecto de medir en vez de asumir.
static void medirCluster() {
  if (!tarjetaMontada()) { Serial.println("[k] sin tarjeta montada."); return; }

  uint64_t total = 0, antes = 0, despues = 0;
  tarjetaEspacio(total, antes);
  File f = TARJETA.open("/banco/CLUSTER.TMP", FILE_WRITE);
  if (!f) { Serial.println("[k] no se pudo crear el archivo de prueba."); return; }
  f.write('x');
  f.close();
  tarjetaEspacio(total, despues);
  TARJETA.remove("/banco/CLUSTER.TMP");

  const int64_t delta = (int64_t)despues - (int64_t)antes;
  Serial.printf("[k] un archivo de 1 B consumió %lld B → ése es el cluster\n", delta);
  if (delta == 32768) {
    Serial.println("    coincide con los 32,768 B medidos desde Windows. El presupuesto");
    Serial.println("    de 0.213 B/pixel de §4 sigue cayendo en un cluster exacto.");
  }
  if (delta != 32768 && delta > 0)
    Serial.printf("    *** NO coincide con los 32,768 B del BOM. Con %lld B, una foto\n"
                  "    *** dentro de presupuesto ya no ocupa un cluster exacto y hay que\n"
                  "    *** rehacer la tabla de capacidad de §3.\n", delta);
  if (delta <= 0)
    Serial.println("    delta no positivo: FatFs pudo servir el dato de su caché.");
}

// Patrón pseudoaleatorio, no una constante: un buffer relleno con el mismo byte
// esconde un sector duplicado o uno viejo que no se reescribió, que es justo el
// tipo de corrupción que esta prueba busca. El LCG da un patrón distinto por
// semilla y reproducible sin guardarlo.
static void llenarPatron(uint8_t *b, uint32_t semilla) {
  uint32_t x = semilla | 1u;
  for (size_t i = 0; i < FOTO; i++) {
    x = x * 1664525u + 1013904223u;
    b[i] = (uint8_t)(x >> 24);
  }
}

// Escribe FOTO bytes y devuelve los ms de open+write+close COMO UNIDAD, o 0 si
// falla. El close() es donde se actualiza la FAT, así que cronometrar solo el
// write daría un número optimista que no es el que paga una subida.
static uint32_t escribirFoto(const char *ruta) {
  const uint32_t t0 = millis();
  File f = TARJETA.open(ruta, FILE_WRITE);
  if (!f) return 0;
  const size_t n = f.write(bufW, FOTO);
  f.close();
  return (n == FOTO) ? (millis() - t0) : 0;
}

// [w] — el número que alimenta el timeout por foto de §4.
static void medirIO(uint8_t n) {
  if (!tarjetaMontada()) { Serial.println("[w] sin tarjeta montada."); return; }

  llenarPatron(bufW, millis());
  uint32_t t[32];
  if (n > 32) n = 32;

  for (uint8_t i = 0; i < n; i++) {
    t[i] = escribirFoto("/banco/BENCH.TMP");
    if (!t[i]) { Serial.printf("[w] escritura %u FALLÓ\n", i); return; }
  }

  uint32_t orden[32];
  memcpy(orden, t, n * sizeof orden[0]);
  for (uint8_t i = 1; i < n; i++)                       // insercion, n<=32
    for (uint8_t j = i; j && orden[j] < orden[j - 1]; j--) {
      const uint32_t tmp = orden[j]; orden[j] = orden[j - 1]; orden[j - 1] = tmp;
    }
  const uint32_t mediana = orden[n / 2];

  Serial.printf("\n[w] %u escrituras de %u B a %.1f MHz (open+write+close)\n",
                n, (unsigned)FOTO, sdFreq / 1000000.0);
  Serial.printf("    min %lu ms · mediana %lu ms · max %lu ms  →  %.2f MB/s en la mediana\n",
                (unsigned long)orden[0], (unsigned long)mediana,
                (unsigned long)orden[n - 1], FOTO / (mediana * 1000.0f));
  Serial.println(F("    La MEDIANA es lo que tarda una foto; el MAX es lo que tiene que"));
  Serial.println(F("    caber en el timeout de §4, porque una escritura que necesita"));
  Serial.println(F("    borrado previo del bloque es la que se lleva la peor cifra."));

  // Relectura y comparación byte a byte. Este lado del banco SÍ se autoverifica,
  // al revés que el display: con TFT_MISO=-1 no hay framebuffer que leer de vuelta.
  memset(bufR, 0, FOTO);
  const uint32_t t0 = millis();
  File f = TARJETA.open("/banco/BENCH.TMP", FILE_READ);
  const size_t leidos = f ? f.read(bufR, FOTO) : 0;
  if (f) f.close();
  const uint32_t dtLec = millis() - t0;

  if (leidos != FOTO) {
    Serial.printf("    *** relectura devolvió %u B de %u\n", (unsigned)leidos, (unsigned)FOTO);
  } else {
    Serial.printf("    lectura %lu ms → %.2f MB/s   integridad: %s\n",
                  (unsigned long)dtLec, FOTO / (dtLec * 1000.0f),
                  memcmp(bufW, bufR, FOTO) == 0 ? "ok, byte a byte" : "*** DIFIERE ***");
  }
  TARJETA.remove("/banco/BENCH.TMP");
}

// [n] — crea archivos con el esquema de nombres real, para poder medir el
// recorrido. De paso es la prueba de escritura en volumen.
static void crearArchivos(uint16_t n) {
  if (!tarjetaMontada()) { Serial.println("[n] sin tarjeta montada."); return; }

  llenarPatron(bufW, 0xC0FFEE);
  Serial.printf("[n] creando %u archivos de %u B en %s...\n", n, (unsigned)FOTO, DIR_BANCO);

  char ruta[32];
  const uint32_t t0 = millis();
  uint16_t hechos = 0;
  for (uint16_t i = 1; i <= n; i++) {
    snprintf(ruta, sizeof ruta, "%s/%08u.JPG", DIR_BANCO, i);
    if (!escribirFoto(ruta)) { Serial.printf("    FALLÓ en el %u — ¿tarjeta llena?\n", i); break; }
    hechos++;
    if (i % 25 == 0) Serial.printf("    %u...\n", i);
  }
  const uint32_t dt = millis() - t0;
  Serial.printf("    %u archivos en %lu ms → %.1f ms/archivo, %.2f MB/s sostenidos\n",
                hechos, (unsigned long)dt, (float)dt / (hechos ? hechos : 1),
                (float)hechos * FOTO / (dt * 1000.0f));
}

// [l] — coste de recorrer el directorio, que es lo que decide si el fallback de
// reconstrucción del manifiesto de §3 es viable con miles de fotos.
//
// Devuelve además el nombre válido más alto: ése + 1 es el valor al que hay que
// REANCLAR el contador de NVS al reconstruir. Sin ese paso, un erase_flash deja
// el contador en cero y las siguientes subidas sobrescriben en silencio.
static void recorrer() {
  if (!tarjetaMontada()) { Serial.println("[l] sin tarjeta montada."); return; }

  File dir = TARJETA.open(DIR_BANCO);
  if (!dir || !dir.isDirectory()) { Serial.println("[l] no se pudo abrir el directorio."); return; }

  uint32_t validos = 0, invalidos = 0, mayor = 0;
  const uint32_t t0 = micros();
  while (File f = dir.openNextFile()) {
    const char *nm = f.name();          // basename, no la ruta (vfs_api.cpp:445)
    if (nombreValido(nm)) {
      validos++;
      const uint32_t num = strtoul(nm, nullptr, 10);
      if (num > mayor) mayor = num;
    } else {
      invalidos++;
    }
    f.close();                          // obligatorio: max_files por defecto es 5
  }
  const uint32_t dt = micros() - t0;
  dir.close();

  const uint32_t total = validos + invalidos;
  Serial.printf("\n[l] %s recorrido en %.1f ms — %lu entradas (%lu válidas, %lu descartadas)\n",
                DIR_BANCO, dt / 1000.0f, (unsigned long)total,
                (unsigned long)validos, (unsigned long)invalidos);
  if (total) {
    const float us = (float)dt / total;
    Serial.printf("    %.0f us por entrada → %.1f s para 121,000 fotos (la capacidad de §3)\n",
                  us, us * 121000 / 1e6f);
    Serial.println(F("    Ese número decide si el fallback de §3 es viable: se paga una vez,"));
    Serial.println(F("    solo cuando el manifiesto falta o está corrupto, y a cambio el"));
    Serial.println(F("    marco nunca se queda en negro por un índice roto."));
  }
  Serial.printf("    nombre válido más alto: %08lu → el contador de NVS se REANCLA en %lu\n",
                (unsigned long)mayor, (unsigned long)mayor + 1);
}

// [x] — deja la tarjeta como estaba. /fotos/ no se toca nunca desde aquí.
//
// Se reabre el directorio en cada vuelta y se borra UNA entrada, en vez de
// recorrerlo borrando sobre la marcha: f_unlink a media f_readdir invalida el
// cursor del directorio y se salta entradas o repite. El coste es cuadrático en
// aperturas de directorio, y aquí da igual — es una tecla de limpieza con 100
// archivos, no una ruta del firmware.
// ponytail: O(n²) en aperturas; si algún día limpia miles, juntar los nombres en
// un arreglo primero y borrar con el directorio ya cerrado.
static void limpiar() {
  if (!tarjetaMontada()) { Serial.println("[x] sin tarjeta montada."); return; }

  uint32_t n = 0;
  char ruta[40];
  for (;;) {
    File dir = TARJETA.open(DIR_BANCO);
    if (!dir) break;
    File f = dir.openNextFile();
    if (!f) { dir.close(); break; }
    snprintf(ruta, sizeof ruta, "%s/%s", DIR_BANCO, f.name());
    f.close();
    dir.close();
    if (!TARJETA.remove(ruta)) break;
    n++;
  }
  Serial.printf("[x] %lu archivos borrados de %s\n", (unsigned long)n, DIR_BANCO);
}

// ──────────────────────────────────────────────────────────────────────
//  Coexistencia — el display pintando MIENTRAS la SD trabaja
//
//  Ésta es la razón de que el lector entre en este env y no en uno propio. Que
//  TARJETA.begin() monte la tarjeta no informa de nada: el riesgo documentado es que
//  el display en VSPI y la SD en HSPI se pisen (issue #3601 de TFT_eSPI), y eso
//  solo sale con los dos trabajando a la vez y sostenido.
//
//  CONCURRENTE, no alternado, porque alternar no es lo que hace producción: la
//  escritura a la tarjeta ocurre en el callback de /upload —hilo async_tcp,
//  prioridad 10— mientras loop() puede estar empujando píxeles al display.
//
//  Reparto: la tarea toca SOLO la SD en el core 0, loop() toca SOLO el display
//  en el core 1. Cero estado compartido, dos buses. Que eso sea cierto es
//  justamente la hipótesis que se está poniendo a prueba.
//
//  El veredicto es ASIMÉTRICO y conviene tenerlo claro antes de leer el informe:
//  el lado de la SD se autoverifica byte a byte con la relectura, pero el
//  display NO se puede leer de vuelta (TFT_MISO=-1, SDA-0 sin conectar), así que
//  su veredicto sigue siendo visual más el tiempo de repintado.
// ──────────────────────────────────────────────────────────────────────

static volatile bool coexCorriendo = false, coexTareaViva = false;
static bool coexInformePendiente = false;
static volatile uint32_t coexIter, coexFallosEsc, coexFallosLec, coexDifieren;
static volatile uint32_t coexMsEsc, coexMsLec;
static uint32_t coexPasadas, coexRayasMin, coexRayasMax, coexRayasSum;
static uint32_t coexBloquesMin, coexBloquesMax, coexBloquesSum, coexT0;

// Solo cuenta; no imprime. Dos hilos escribiendo al Serial dan una salida
// entrelazada e ilegible justo cuando hay que leer números.
static void tareaSD(void *) {
  uint32_t semilla = 1;
  while (coexCorriendo) {
    llenarPatron(bufW, semilla++);

    const uint32_t dtE = escribirFoto("/banco/COEX.TMP");
    if (!dtE) {
      coexFallosEsc = coexFallosEsc + 1;   // no `++`: deprecado sobre volatile en C++20
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    coexMsEsc = coexMsEsc + dtE;

    memset(bufR, 0, FOTO);
    const uint32_t t0 = millis();
    File f = TARJETA.open("/banco/COEX.TMP", FILE_READ);
    const size_t leidos = f ? f.read(bufR, FOTO) : 0;
    if (f) f.close();
    coexMsLec = coexMsLec + (millis() - t0);

    if (leidos != FOTO)                     coexFallosLec = coexFallosLec + 1;
    else if (memcmp(bufW, bufR, FOTO) != 0) coexDifieren  = coexDifieren + 1;

    coexIter = coexIter + 1;
    vTaskDelay(1);   // cede el core 0 al idle: sin esto salta el task watchdog
  }
  coexTareaViva = false;
  vTaskDelete(nullptr);
}

// Las dos pasadas que castigan el bus del display, cronometradas. Rayas de 1 px
// porque es el patrón sin ráfagas largas donde esconder un error, y bloques de
// 16x16 porque es el camino real de TJpg_Decoder.
static void pasadaDisplay(uint32_t &msRayas, uint32_t &msBloques) {
  uint32_t t = millis();
  for (int16_t x = 0; x < tft.width(); x++)
    tft.drawFastVLine(x, 0, tft.height(), (x & 1) ? TFT_WHITE : TFT_BLACK);
  msRayas = millis() - t;

  t = millis();
  for (int16_t y = 0; y + 16 <= tft.height(); y += 16)
    for (int16_t x = 0; x + 16 <= tft.width(); x += 16)
      tft.pushImage(x, y, 16, 16, blk);
  msBloques = millis() - t;
}

static void coexInforme() {
  Serial.println(F("\n─── coexistencia VSPI + HSPI ────────────────────────────────"));
  Serial.printf("  duración          %.1f s a %.1f MHz de SD, %u MHz de display\n",
                (millis() - coexT0) / 1000.0f, sdFreq / 1000000.0,
                (unsigned)(SPI_FREQUENCY / 1000000));

  Serial.printf("  SD  %lu ciclos escribir+releer+comparar de %u B\n",
                (unsigned long)coexIter, (unsigned)FOTO);
  Serial.printf("      fallos de escritura %lu · de lectura %lu · DISCREPANCIAS %lu\n",
                (unsigned long)coexFallosEsc, (unsigned long)coexFallosLec,
                (unsigned long)coexDifieren);
  if (coexIter)
    Serial.printf("      medias: escribir %.1f ms, leer %.1f ms\n",
                  (float)coexMsEsc / coexIter, (float)coexMsLec / coexIter);

  Serial.printf("  display  %lu pasadas\n", (unsigned long)coexPasadas);
  if (coexPasadas) {
    Serial.printf("      rayas 1 px   min %lu · media %.1f · max %lu ms\n",
                  (unsigned long)coexRayasMin, (float)coexRayasSum / coexPasadas,
                  (unsigned long)coexRayasMax);
    Serial.printf("      bloques 16x16 min %lu · media %.1f · max %lu ms\n",
                  (unsigned long)coexBloquesMin, (float)coexBloquesSum / coexPasadas,
                  (unsigned long)coexBloquesMax);
    if (SPI_FREQUENCY == 40000000)
      Serial.printf("      línea base SIN SD, medida en este binario: %u y %u ms\n",
                    BASE_RAYAS_MS, BASE_BLOQUES_MS);
    else
      Serial.println(F("      (la línea base de 67/77 ms es de 40 MHz: aquí no aplica)"));
  }

  Serial.println(F("  VEREDICTO: el lado SD es el de arriba y se autoverifica byte a"));
  Serial.println(F("  byte. El del display NO se puede leer de vuelta (TFT_MISO=-1), así"));
  Serial.println(F("  que es de vista: bandas, píxeles corridos o rayas que cambian de"));
  Serial.println(F("  fase. Si algo salió sucio, el primer sospechoso NO es la"));
  Serial.println(F("  arquitectura sino la diafonía del protoboard y el riel de 5 V —"));
  Serial.println(F("  un multímetro promedia y no ve el hundimiento de una escritura."));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

static void coexToggle() {
  // El informe lo imprime loop(), cuando la tarea ya haya salido: leerle los
  // contadores mientras sigue girando daría cifras a medias.
  if (coexCorriendo) { coexCorriendo = false; coexInformePendiente = true; return; }
  if (!tarjetaMontada())  { Serial.println("[c] sin tarjeta montada."); return; }
  if (!bufW || !bufR) { Serial.println("[c] sin buffers: falló el malloc de arranque."); return; }

  coexIter = 0; coexFallosEsc = 0; coexFallosLec = 0; coexDifieren = 0;
  coexMsEsc = 0; coexMsLec = 0;
  coexPasadas = coexRayasMax = coexRayasSum = coexBloquesMax = coexBloquesSum = 0;
  coexRayasMin = coexBloquesMin = UINT32_MAX;
  coexT0 = millis();

  coexCorriendo = true;
  // Se marca viva AQUÍ y no dentro de la tarea: si se parase antes de que el
  // scheduler la arrancara, loop() vería la bandera en falso y sacaría el
  // informe con la tarea todavía por correr.
  coexTareaViva = true;
  // Prioridad 5 y core 0: loop() corre en el core 1 a prioridad 1. Lo que se
  // busca es contención de bus y de riel, no inversión de prioridades, así que
  // no hace falta subirla a los 10 de async_tcp. 8192 B de pila porque FatFs y
  // el driver de SPI no son baratos; los buffers de 32 KB son de heap, nunca
  // de la pila de la tarea.
  xTaskCreatePinnedToCore(tareaSD, "bancoSD", 8192, nullptr, 5, nullptr, 0);
  Serial.println("[c] coexistencia EN MARCHA. Mira la pantalla. 'c' otra vez para parar.");
}

// ──────────────────────────────────────────────────────────────────────

static void menu() {
  Serial.println(F("\n─── banco del display y la SD ───────────────────────────────"));
  Serial.println(F("  DISPLAY"));
  Serial.println(F("  1  rayas verticales de 1 px → el patrón que revela corrupción"));
  Serial.println(F("  2  ocho barras planas → píxeles corridos y bordes sucios"));
  Serial.println(F("  3  texto + rejilla + borde de 1 px a ras del canto"));
  Serial.println(F("  f  cronometra 10 pantallas → ms/pantalla y MB/s comparables"));
  Serial.println(F("  + -  brillo del backlight   ·   0  apagado   ·   9  al máximo"));
  Serial.println(F("  v  invierte los colores del panel (¿es IPS y responde?)"));
  Serial.println(F("  TARJETA SD"));
  Serial.println(F("  d  sonda CMD0 cruda + prueba con MISO/MOSI cambiados"));
  Serial.println(F("  D C  init a mano con CMD59 en ON / en OFF — el CRC contra el OCR"));
  Serial.println(F("  M  reintenta el montaje (tras mover cables o meter la tarjeta)"));
  Serial.println(F("  s  tipo, capacidad y sectores que ve el ESP32"));
  Serial.println(F("  k  tamaño de cluster MEDIDO por consumo → contra los 32,768 B"));
  Serial.println(F("  w  10 escrituras + relectura de 32 KB → el timeout por foto de §4"));
  Serial.println(F("  m  remonta a la siguiente frecuencia: 4 → 10 → 20 → 25 MHz"));
  Serial.println(F("  n  crea 100 archivos NNNNNNNN.JPG de 32 KB en /banco"));
  Serial.println(F("  l  recorre /banco → us por entrada y el reanclaje del contador"));
  Serial.println(F("  x  borra /banco entero. /fotos/ NO se toca nunca desde aquí"));
  Serial.println(F("  LOS DOS A LA VEZ — lo que de verdad hay que probar"));
  Serial.println(F("  c  coexistencia: la SD martillea en el core 0 mientras el display"));
  Serial.println(F("     pinta en el core 1. Sostenido. 'c' otra vez para el informe"));
  Serial.println(F("  OTROS"));
  Serial.println(F("  i  escanea I2C → dirección del BH1750 (pendiente #4)"));
  Serial.println(F("  r  niveles en reposo del bus I2C + recuperación de esclavo trabado"));
  Serial.println(F("  p  vuelca la configuración compilada   ·   ?  este menú"));
  Serial.println(F("  SPI_FREQUENCY del display se edita en platformio.ini y se"));
  Serial.println(F("  reflashea; la de la SD sí se cambia en caliente con 'm'."));
  Serial.println(F("─────────────────────────────────────────────────────────────"));
}

static void tecla(char c) {
  switch (c) {
    case '1': rayasFinas(); break;
    case '2': barrasColor(); break;
    case '3': textoPrueba(); break;
    case 'f': cronometrar(); break;

    case 'd': diagnosticarSD(); break;
    case 'D': sondearInit(1); break;   // con CRC ON: reproduce el fallo del driver
    case 'C': sondearInit(0); break;   // con CRC OFF: el control de la prueba
    case 'M': montarSD(sdFreq); break;   // reintenta el montaje tras mover cables
    case 's': identidad(); break;
    case 'k': medirCluster(); break;
    case 'w': medirIO(10); break;
    case 'n': crearArchivos(100); break;
    case 'l': recorrer(); break;
    case 'x': limpiar(); break;
    case 'c': coexToggle(); break;
    case 'm': {
      uint8_t i = 0;
      while (i < sizeof SD_FREQS / sizeof SD_FREQS[0] && SD_FREQS[i] != sdFreq) i++;
      montarSD(SD_FREQS[(i + 1) % (sizeof SD_FREQS / sizeof SD_FREQS[0])]);
      break;
    }

    case 'i': escanearI2C(); break;
    case 'r': recuperarI2C(); escanearEn(PIN_I2C_SDA, PIN_I2C_SCL); break;
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
  Serial.println("\n=== BANCO DEL DISPLAY Y LA SD — no es el firmware del marco ===");
  volcarConfig();
  Serial.printf("[autocomprobacion] nombreValido: %s\n",
                nombreValidoOk() ? "ok" : "*** FALLA ***");

  // El backlight ANTES de tft.begin(): sin esto el primer frame del arranque se
  // ve como un flash a pantalla llena, que es justo lo que la regla 3 no quiere
  // en el marco. Aquí no importa, pero el orden es el que va a acabar en main.cpp.
  ledcAttach(PIN_BL, BL_FREQ_HZ, BL_BITS);
  ponerBrillo(0);

  tft.begin();
  tft.setRotation(0);            // vertical nativo 320×480, sin transformación
  tft.fillScreen(TFT_BLACK);
  ponerBrillo(255);

  for (uint16_t i = 0; i < 16 * 16; i++) blk[i] = (i & 1) ? TFT_WHITE : TFT_NAVY;

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // hspi.begin() con los pines ANTES de TARJETA.begin(): ver el comentario largo de
  // montarSD(). Invertirlo pone MISO en GPIO12 y la placa deja de bootear.
  hspi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  montarSD(SD_FREQS[0]);   // el default de la librería, que es lo que corre main.cpp

  // Un malloc de arranque que no se libera nunca: 64 KB no caben en .bss y un
  // bloque que jamás se redimensiona tampoco puede fragmentar. Mismo criterio
  // que los dos buffers del banco de red.
  bufW = (uint8_t *)malloc(FOTO);
  bufR = (uint8_t *)malloc(FOTO);
  Serial.printf("[sd] buffers 2x%u B: %s   heap libre %u B\n", (unsigned)FOTO,
                (bufW && bufR) ? "ok" : "*** FALLÓ EL MALLOC ***", ESP.getFreeHeap());

  textoPrueba();
  escanearI2C();
  if (tarjetaMontada()) identidad();
  menu();
}

void loop() {
  if (Serial.available()) {
    const char c = Serial.read();
    if (c != '\n' && c != '\r' && c != ' ') tecla(c);
  }

  if (coexCorriendo) {
    uint32_t rayas, bloques;
    pasadaDisplay(rayas, bloques);
    coexPasadas++;
    coexRayasSum += rayas;  coexBloquesSum += bloques;
    if (rayas   < coexRayasMin)   coexRayasMin   = rayas;
    if (rayas   > coexRayasMax)   coexRayasMax   = rayas;
    if (bloques < coexBloquesMin) coexBloquesMin = bloques;
    if (bloques > coexBloquesMax) coexBloquesMax = bloques;

    static uint32_t ultimo = 0;
    if (millis() - ultimo > 2000) {
      ultimo = millis();
      Serial.printf("[c] %lu ciclos de SD · %lu pasadas · discrepancias %lu · "
                    "rayas %lu ms · bloques %lu ms\n",
                    (unsigned long)coexIter, (unsigned long)coexPasadas,
                    (unsigned long)coexDifieren, (unsigned long)rayas,
                    (unsigned long)bloques);
    }
    return;   // sin el delay de abajo: la prueba tiene que ser sostenida
  }

  // Al parar, el informe espera a que la tarea haya salido de verdad.
  if (coexInformePendiente && !coexTareaViva) {
    coexInformePendiente = false;
    TARJETA.remove("/banco/COEX.TMP");
    coexInforme();
  }

  delay(5);
}
