// src/luz.h — Sensor de luz TSL2561, compartido por el marco y el banco
//
// Sustituye al BH1750 (GY-302), que se estropeó en el banco el 7-ago-2026 tras
// dar su medición. El módulo nuevo es un TSL2561 de Handson Technology, SKU
// SSR1026. NO es un reemplazo pin a pin: lleva LDO propio y va al riel de 5 V,
// y el chip entrega DOS canales crudos en vez de un registro de lux ya hecho.
//
// ── Por qué esto y no una librería ────────────────────────────────────────
//
// Hay tres candidatas razonables (Adafruit, SparkFun, Joba) y se descartaron
// las tres, pero NO por deporte: de lo que una librería de TSL2561 aporta, lo
// único que no es trivial es la AUTOGANANCIA, y este marco no la usa. §7
// colapsa la salida del chip a tres bandas (>100 lux, >10 lux, resto), y con
// ganancia 16x y 101 ms la saturación cae en ~1,020 lux con incandescente — o
// sea DIEZ VECES por encima del umbral superior. Cuando el sensor satura, la
// respuesta correcta ya es «brillante»: la rama de saturación de abajo y la
// autoganancia dan la misma respuesta en todo el rango que este marco
// distingue. El resto de la librería son cuatro llamadas a Wire.
//
// El coste concreto de cada una, para no reabrirlo: Adafruit arrastra Unified
// Sensor (el set pineado subiría de 7 a 8, metiendo 2 para sacar 1); SparkFun
// no se toca desde 2015, que es el mismo argumento con el que se descartó
// Croppr.js; Joba sí trae la autoganancia pero es de un solo mantenedor, en la
// pieza que gobierna el backlight de un objeto que debe durar cinco años.
//
// LA CONDICIÓN QUE INVALIDA ESTA DECISIÓN, escrita por adelantado: si el marco
// alguna vez tiene que reportar lux DE VERDAD —a la pantalla, a un log, a la
// página— la librería vuelve a la mesa. Con tres bandas, no.
//
// Va en header con `static inline` por la misma razón que nombre.h: sin .cpp
// que añadir a dos build_src_filter distintos. Los dos .ini ya traen el -I que
// hace falta.

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ── Dirección ────────────────────────────────────────────────────────────
//
// ADDR SEL es un pin de TRES estados (datasheet, Tabla 1), y en este módulo no
// sale al header: lo fija un puente de tres pads soldables marcados H|centro|L.
//
//     pad al aire (como viene)  → 0x39
//     centro puenteado a «L»    → 0x29
//     centro puenteado a «H»    → 0x49
//
// MEDIDO el 8-ago-2026 con la tecla `i` del banco del display: responde en 0x39
// y es el ÚNICO dispositivo del bus, o sea que el pad viene al aire de fábrica.
//
// Hubo que medirlo porque las dos mitades del PDF del módulo se contradicen: su
// texto y sus tres fotos dicen 0x39, pero su ESQUEMÁTICO dibuja un interruptor de
// dos estados con pull-down de 4.7k, que daría 0x29. Ganaron el texto y las
// fotos — el mismo esquemático ya había fallado en otros dos puntos.
static constexpr uint8_t LUZ_DIR = 0x39;

// ── Registros (datasheet, Tabla 2) ───────────────────────────────────────
static constexpr uint8_t LUZ_CMD      = 0x80;   // bit CMD, obligatorio en todo byte de comando
static constexpr uint8_t LUZ_WORD     = 0x20;   // bit WORD, para leer los pares de 16 bits
static constexpr uint8_t LUZ_CONTROL  = 0x00;
static constexpr uint8_t LUZ_TIMING   = 0x01;
static constexpr uint8_t LUZ_DATA0    = 0x0C;   // canal 0: visible + infrarrojo
static constexpr uint8_t LUZ_DATA1    = 0x0E;   // canal 1: solo infrarrojo

// GAIN=1 (16x) e INTEG=01 (101 ms). La combinación NO es libre y se eligió con
// los números del datasheet delante:
//
//   402 ms queda descartado por SATURACIÓN, no por lentitud: a 16x satura en
//   ~455 lux con luz incandescente (65535 counts / 144 counts·lux⁻¹), que es una
//   sala normal junto a una ventana. Además obligaría a subir LUZ_PERIODO_MS de
//   200 a 450 ms y la rampa de §7 se iría de ~41 s a ~115 s.
//
//   13.7 ms tiene el mismo techo que 101 ms pero 7.4x menos resolución.
//
//   101 ms deja el techo en ~4,100 lux (fluorescente) / ~1,020 (incandescente) y
//   da 91 counts a 10 lux, que es el umbral bajo de §7, contra un offset de
//   oscuridad de 0-4 counts.
static constexpr uint8_t LUZ_TIMING_VAL = 0x11;

// Peor caso del tiempo de integración que fija LUZ_TIMING_VAL: 81 × 918 ciclos
// con fosc en su MÍNIMO de 690 kHz (el nominal, 735 kHz, da los 101 ms de
// catálogo). Quien sondee el sensor tiene que esperar más que esto o leerá la
// misma conversión dos veces — que es justo lo que le pasaba al BH1750 y por lo
// que existe LUZ_PERIODO_MS. Vive aquí y no en main.cpp porque es propiedad del
// driver: si alguien sube LUZ_TIMING_VAL a 402 ms, este número sube con él y el
// static_assert del marco rompe el build en vez de degradar en silencio.
static constexpr uint32_t LUZ_INTEGRACION_MS = 108;

// Fondo de escala a 101 ms: ((81 × 918)/2 − 2). Es del datasheet (Nota 6), no
// 65535: ese solo se alcanza a partir de 178 ms.
static constexpr uint16_t LUZ_FONDO_ESCALA = 37177;

// El formulario de lux está definido a 402 ms, y 322/81 son los ciclos de reloj
// de los dos tiempos de integración (Nota 5). No es un factor redondeado.
static constexpr float LUZ_A_402MS = 322.0f / 81.0f;

// ── Acceso al bus ────────────────────────────────────────────────────────

static inline bool luzEscribir(uint8_t dir, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(dir);
  Wire.write(uint8_t(LUZ_CMD | reg));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static inline bool luzLeerPalabra(uint8_t dir, uint8_t reg, uint16_t *out) {
  Wire.beginTransmission(dir);
  Wire.write(uint8_t(LUZ_CMD | LUZ_WORD | reg));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(dir, uint8_t(2)) != 2) return false;
  const uint8_t bajo = Wire.read();   // BYTE BAJO PRIMERO (datasheet, Figura 15)
  const uint8_t alto = Wire.read();
  *out = uint16_t(uint16_t(alto) << 8 | bajo);
  return true;
}

// Enciende el chip y fija ganancia y tiempo de integración.
//
// La comprobación de presencia sale gratis y no necesita el registro ID: el
// datasheet dice literal que si se escribe 0x03 al control, una lectura
// devuelve 0x03. Y el ID no serviría para lo que uno esperaría — solo distingue
// TSL2560 de TSL2561, NO el encapsulado, que es lo que elige los coeficientes.
static inline bool luzIniciar(uint8_t dir = LUZ_DIR) {
  if (!luzEscribir(dir, LUZ_CONTROL, 0x03)) return false;

  Wire.beginTransmission(dir);
  Wire.write(uint8_t(LUZ_CMD | LUZ_CONTROL));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(dir, uint8_t(1)) != 1) return false;
  if ((Wire.read() & 0x03) != 0x03) return false;

  return luzEscribir(dir, LUZ_TIMING, LUZ_TIMING_VAL);
}

// ponytail: dos transacciones, no un block read. Los canales se integran a la
// vez pero se leen por separado, así que un ciclo que termine entre las dos
// mezcla el ch0 de una integración con el ch1 de la siguiente. A 101 ms de
// integración contra 200 ms de sondeo, y con la luz de una sala cambiando en
// segundos, eso desplaza el cociente lo justo para una muestra — y detrás están
// la histéresis y la rampa de un paso, que se comen de sobra una muestra suelta.
// Si alguna vez estorba, el protocolo Block Read trae los cuatro bytes de golpe.
static inline bool luzLeer(uint16_t *ch0, uint16_t *ch1, uint8_t dir = LUZ_DIR) {
  return luzLeerPalabra(dir, LUZ_DATA0, ch0) &&
         luzLeerPalabra(dir, LUZ_DATA1, ch1);
}

// ── Lux ──────────────────────────────────────────────────────────────────

// Función PURA, y extraída precisamente para poder comprobarla sin sensor —
// mismo motivo que LedRGB::lerp. Recibe los counts CRUDOS a 101 ms y 16x.
//
// Coeficientes del juego «T, FN y CL» (datasheet, pág. 22). El otro juego es el
// del encapsulado CS, y la numeración de pines del esquemático del módulo
// (1=VDD, 2=ADDR, 3=GND, 4=SCL, 5=INT, 6=SDA) descarta CL pero no CS. Se
// distinguen con calibrador sobre la pieza: T mide 3.80 × 2.60 mm y CS mide
// 1.75 × 1.25 mm, o sea el doble, y no se confunden.
//
// Elegir mal cuesta ~4 % con luz fluorescente y ≤30 % con incandescente, POR
// DEBAJO del ±35-40 % de tolerancia del propio chip (Nota 8) — así que los dos
// errores los absorbe calibrar los umbrales de §7 contra la sala real, que es
// lo que hay que hacer de todas formas.
static inline float luzLux(uint16_t ch0, uint16_t ch1) {
  if (ch0 == 0) return 0.0f;

  // El cociente no depende de la escala, así que se saca antes de normalizar.
  const float r  = float(ch1) / float(ch0);
  const float c0 = float(ch0) * LUZ_A_402MS;
  const float c1 = float(ch1) * LUZ_A_402MS;

  float lux;
  if      (r <= 0.50f) lux = 0.0304f  * c0 - 0.062f   * c0 * powf(r, 1.4f);
  else if (r <= 0.61f) lux = 0.0224f  * c0 - 0.031f   * c1;
  else if (r <= 0.80f) lux = 0.0128f  * c0 - 0.0153f  * c1;
  else if (r <= 1.30f) lux = 0.00146f * c0 - 0.00112f * c1;
  else                 lux = 0.0f;    // todo infrarrojo: el chip no ve luz visible

  return lux > 0.0f ? lux : 0.0f;     // los tramos pueden cruzar cero en su borde
}

// ── Bandas de brillo, con histéresis ─────────────────────────────────────

// Devuelve la banda que toca (0 oscuro, 1 tenue, 2 brillante) a partir de la
// banda ACTUAL y de la lectura. Que dependa de la banda actual es toda la
// gracia: el umbral que hay que cruzar se relaja un 20 % en el sentido que
// mantiene el estado, así que un valor oscilando justo encima de `alto` no
// cambia de banda en cada lectura. Sin eso el brillo pasa la noche yendo y
// viniendo — la rampa de un paso amortigua el salto pero no lo elimina, que es
// lo que decía el TODO que esto cierra.
//
// Pura y separada de updateBrightness() por el mismo motivo que LedRGB::lerp:
// una histéresis mal puesta no se ve mirando la pantalla, solo cronometrándola.
// Los umbrales entran por parámetro porque son política de §7, no del sensor.
static inline uint8_t luzBanda(uint8_t nivel, float lux, float alto, float bajo) {
  const float subeAlto = (nivel >= 2) ? alto * 0.8f : alto * 1.2f;
  const float subeBajo = (nivel >= 1) ? bajo * 0.8f : bajo * 1.2f;
  return (lux > subeAlto) ? 2 : (lux > subeBajo) ? 1 : 0;
}

// Autocomprobación, para correr en cada arranque igual que nombreValidoOk().
//
// Los dos pares salen de la tabla de responsividad del datasheet (pág. 4), que
// da counts/lux a 402 ms y 16x para dos fuentes de luz muy distintas:
//
//     fluorescente   Ch0 = 36  counts/lux,  Ch1 = 4  counts/lux
//     incandescente  Ch0 = 144 counts/lux,  Ch1 = 72 counts/lux
//
// Escalados a 101 ms (x0.252) y a 100 lux salen los números de abajo. Que las
// DOS fuentes tengan que devolver ~100 lux con counts que difieren 4x es
// exactamente el trabajo que hace la fórmula, y por eso este par de casos vale
// como prueba: atrapa un coeficiente mal tecleado, un borde de tramo cambiado,
// la normalización olvidada y los dos canales intercambiados.
static inline bool luzLuxOk() {
  const float fluo = luzLux(907, 101);     // ~99.3
  const float inca = luzLux(3629, 1814);   // ~99.8
  bool ok = fabsf(fluo - 100.0f) < 5.0f && fabsf(inca - 100.0f) < 5.0f;
  ok &= luzLux(0, 0) == 0.0f;              // sin luz no se divide entre cero
  ok &= luzLux(1000, 2000) == 0.0f;        // cociente 2.0 > 1.30: solo infrarrojo

  // La histéresis, con los umbrales de §7. Lo que se comprueba NO es que
  // clasifique —eso es trivial— sino que RETENGA: 90 lux tiene que leerse como
  // «brillante» si se venía de brillante y como «tenue» si se venía de tenue.
  // Si alguien colapsa las dos líneas a una comparación sola, las dos primeras
  // siguen pasando y estas dos gritan.
  ok &= luzBanda(2,  90.0f, 100.0f, 10.0f) == 2;   // retiene arriba
  ok &= luzBanda(1,  90.0f, 100.0f, 10.0f) == 1;   // no sube todavía
  ok &= luzBanda(1,   9.0f, 100.0f, 10.0f) == 1;   // retiene abajo
  ok &= luzBanda(0,   9.0f, 100.0f, 10.0f) == 0;   // no sube todavía
  ok &= luzBanda(0, 130.0f, 100.0f, 10.0f) == 2;   // un salto grande sí cruza
  ok &= luzBanda(2,   1.0f, 100.0f, 10.0f) == 0;   // y en el otro sentido igual
  return ok;
}
