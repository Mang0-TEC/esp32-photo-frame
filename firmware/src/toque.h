// src/toque.h — Touch capacitivo: de una serie de lecturas crudas a un gesto
//
// Compartido por el marco y el banco del display por la misma razón que luz.h, y
// aquí es todavía más directa: el banco es donde se MIDE el umbral, así que si
// probara otra lógica que la del marco el número no transferiría. La tecla `t`
// de [env:display] alimenta esta misma función con touchRead() real.
//
// Va en header con `static inline` por la misma razón que nombre.h: sin .cpp que
// añadir a dos build_src_filter distintos. Los dos .ini ya traen el -I.
//
// ── Por qué la base PERSIGUE en vez de calibrarse al arrancar ─────────────
//
// El comentario que este archivo sustituye proponía una media de N lecturas al
// arrancar por 0.6, y tiene tres fallos que no se ven probándolo una tarde:
//
//   1. Un dedo apoyado en el pad MIENTRAS arranca deja la base mal PARA SIEMPRE.
//      El marco vive enchufado a la pared: cualquiera que lo mueva y lo vuelva a
//      conectar agarrándolo por donde se toca cae justo en ese caso, y el touch
//      se queda muerto sin que nada lo diga.
//   2. La deriva térmica y la humedad mueven el reposo a lo largo del día, y un
//      absoluto calibrado en frío envejece en un objeto que vive 24/7.
//   3. Obliga a medir esa deriva antes de saber cuánto margen dejar.
//
// La base persiguiendo el reposo A UN PASO POR LECTURA —la misma rampa que
// updateBrightness() usa para el backlight— resuelve las tres de golpe. Y de
// regalo cambia lo que hay que medir: lo estable no es la base, es el DELTA. Por
// eso la comprobación de deriva son dos corridas del banco, frío y caliente,
// mirando que el delta no se mueva — y no una corrida larga de horas.
//
// La base solo avanza cuando NO hay dedo. Si avanzara siempre, un toque se
// arrastraría su propia referencia y el gesto se cancelaría solo.

#pragma once

#include <stdint.h>

// Counts que hay que BAJAR respecto de la base para contar como dedo.
//
// ponytail: 15 es PROVISIONAL y no sale de ninguna medición — es el hueco que
// deja el pad que todavía no está soldado. Se fija con la tecla `t` de
// [env:display], y el criterio es doble: la MITAD del delta pico más pequeño
// medido a través del cupón de PETG más grueso que vaya a llevar la carcasa, y
// que ese valor quede por encima de 5 sigma del ruido de reposo. Si no cumple
// las dos, la pared es demasiado gruesa — y eso es un dato para el diseño de la
// carcasa, no un fallo del firmware.
static constexpr uint16_t TOQUE_DELTA = 15;

// Lecturas seguidas que confirman un flanco. touchRead() del ESP32 clásico es
// ruidoso, y sin esto un solo valor espurio dispara showNextPhoto() — que se
// lleva ~290 ms decodificando. Cuesta ~3 vueltas de loop() de latencia por
// flanco, o sea unas décimas de nada contra los 800 ms que separan corto de
// largo.
static constexpr uint8_t TOQUE_FIRMES = 3;

static constexpr uint32_t TOQUE_LARGO_MS = 800;

// Un dedo no dura un minuto. Pasado esto es un objeto apoyado encima del marco,
// humedad, o el pad tocando algo dentro de la carcasa. Se readapta la base y no
// se emite gesto; sin esta salida el touch se queda muerto hasta desenchufar,
// que es exactamente lo que la regla 2 no permite.
static constexpr uint32_t TOQUE_PEGADO_MS = 60000;

enum class Gesto : uint8_t { NADA, CORTO, LARGO };

struct Toque {
  uint16_t base     = 0;
  uint16_t pico     = 0;       // crudo mínimo del toque en curso → el delta real
  uint8_t  firmes   = 0;
  bool     dentro   = false;
  bool     sembrada = false;
  uint32_t desdeMs  = 0;
  uint32_t durMs    = 0;       // duración del último gesto emitido
};

// Consume UNA lectura y devuelve el gesto que se acaba de completar, si alguno.
// El gesto sale al SOLTAR: hasta entonces no se sabe si es corto o largo.
//
// No es pura —lleva el estado en `t`— pero sí es comprobable sin hardware, que
// es la propiedad que aquí importa y la que explota toqueOk(). Mismo criterio
// que LedRGB::lerp y luzBanda().
static inline Gesto toqueActualizar(Toque &t, uint16_t crudo, uint32_t ms) {
  if (!t.sembrada) {
    t.base = crudo;
    t.sembrada = true;
  }

  // touchRead() BAJA al tocar: el dedo suma capacitancia y el pad tarda más en
  // cargarse. Se suma al crudo en vez de restar a la base para no depender de
  // que la base sea mayor que el delta mientras todavía se está sembrando.
  const bool bajo = (uint32_t(crudo) + TOQUE_DELTA <= t.base);

  Gesto g = Gesto::NADA;

  if (bajo == t.dentro) {
    t.firmes = 0;                       // de acuerdo con el estado: nada que confirmar
  } else if (++t.firmes >= TOQUE_FIRMES) {
    t.firmes = 0;
    t.dentro = bajo;
    if (bajo) {
      t.desdeMs = ms;
      t.pico    = crudo;
    } else {
      t.durMs = ms - t.desdeMs;
      g = (t.durMs >= TOQUE_LARGO_MS) ? Gesto::LARGO : Gesto::CORTO;
    }
  }

  if (!t.dentro) {
    // La velocidad de persecución queda atada a la frecuencia de loop(), que va
    // de ~10 ms a los ~290 ms de una decodificación. Da igual: lo que se
    // persigue son minutos y horas, y hasta el peor caso sigue siendo tres
    // órdenes de magnitud más rápido que la deriva.
    if      (crudo > t.base) t.base++;
    else if (crudo < t.base) t.base--;
  } else {
    if (crudo < t.pico) t.pico = crudo;

    if (ms - t.desdeMs > TOQUE_PEGADO_MS) {
      t.dentro = false;
      t.firmes = 0;
      t.base   = crudo;                 // readapta, y deliberadamente SIN gesto
    }
  }

  return g;
}

// ── Autocomprobación ─────────────────────────────────────────────────────

// Empuja n lecturas del mismo valor. Devuelve el ÚLTIMO gesto emitido y no el
// primero: lo que cada caso de abajo afirma es siempre cómo termina la serie.
static inline Gesto toqueSerie(Toque &t, uint16_t crudo, uint32_t &ms,
                               uint16_t n, uint32_t pasoMs) {
  Gesto ultimo = Gesto::NADA;
  for (uint16_t i = 0; i < n; i++) {
    const Gesto g = toqueActualizar(t, crudo, ms);
    if (g != Gesto::NADA) ultimo = g;
    ms += pasoMs;
  }
  return ultimo;
}

// Corre en cada arranque, igual que nombreValidoOk() y luzLuxOk(), y sin pad:
// los valores son de juguete a propósito porque lo que se prueba es la lógica y
// no el hardware. Cada caso es un fallo que no se ve mirando el marco — un
// debounce mal puesto o una base que no converge se manifiestan como «el toque
// va raro a veces», que es lo que nadie puede depurar desde otra ciudad.
static inline bool toqueOk() {
  const uint16_t REPOSO = 70;
  const uint16_t DEDO   = REPOSO - TOQUE_DELTA - 5;   // holgadamente por debajo
  const uint16_t ROCE   = REPOSO - TOQUE_DELTA + 1;   // justo por encima: NO es toque

  uint32_t ms = 0;
  Toque t;

  // 1. Un toque de 200 ms es CORTO — y no emite nada hasta que se suelta.
  toqueSerie(t, REPOSO, ms, 50, 10);
  bool ok = toqueSerie(t, DEDO, ms, 20, 10) == Gesto::NADA;
  ok &= toqueSerie(t, REPOSO, ms, 10, 10) == Gesto::CORTO;

  // 2. Uno de un segundo es LARGO. Ese borde es lo único que separa «cambiar de
  //    foto» de «levantar un AP», así que tiene que estar donde se cree.
  toqueSerie(t, DEDO, ms, 100, 10);
  ok &= toqueSerie(t, REPOSO, ms, 10, 10) == Gesto::LARGO;

  // 3. Lo único que compra el debounce: una lectura espuria suelta no dispara.
  ok &= toqueActualizar(t, DEDO, ms) == Gesto::NADA;
  ms += 10;
  ok &= toqueSerie(t, REPOSO, ms, 20, 10) == Gesto::NADA;

  // 4. El dedo cerca sin llegar a tocar tampoco. Es el margen contra el falso
  //    positivo, y es la quinta condición que mide el banco.
  ok &= toqueSerie(t, ROCE, ms, 50, 10) == Gesto::NADA;
  toqueSerie(t, REPOSO, ms, 50, 10);

  // 5. LA TRAMPA del calibrado al arrancar: el dedo puesto mientras se siembra
  //    la base. Al soltar converge sola y el toque siguiente se detecta igual.
  //    Con una media de N lecturas ×0.6 al arrancar ese pad se queda muerto para
  //    siempre, y es el caso que motiva todo el diseño de este archivo.
  Toque t2; uint32_t ms2 = 0;
  toqueSerie(t2, DEDO,   ms2, 30, 10);      // arranca con el dedo encima
  toqueSerie(t2, REPOSO, ms2, 60, 10);      // suelta: la base sube sola
  ok &= toqueSerie(t2, DEDO,   ms2, 20, 10) == Gesto::NADA;
  ok &= toqueSerie(t2, REPOSO, ms2, 10, 10) == Gesto::CORTO;

  // 6. Deriva: el reposo se va 60 counts —temperatura, humedad, horas— y el
  //    toque se sigue detectando sin recalibrar nada.
  Toque t3; uint32_t ms3 = 0;
  toqueSerie(t3, 70, ms3, 20, 10);
  for (uint16_t v = 70; v <= 130; v++) toqueSerie(t3, v, ms3, 3, 10);
  ok &= toqueSerie(t3, 130 - TOQUE_DELTA - 5, ms3, 20, 10) == Gesto::NADA;
  ok &= toqueSerie(t3, 130, ms3, 10, 10) == Gesto::CORTO;

  // 7. Pegado: 70 s dentro no emiten gesto al retirarlo —eso no fue un toque— y
  //    el touch SIGUE VIVO después, que es la mitad que de verdad importa.
  Toque t4; uint32_t ms4 = 0;
  toqueSerie(t4, REPOSO, ms4, 20, 10);
  toqueSerie(t4, DEDO,   ms4, 700, 100);    // 70 s
  ok &= toqueSerie(t4, REPOSO, ms4, 20, 10) == Gesto::NADA;
  ok &= toqueSerie(t4, DEDO,   ms4, 20, 10) == Gesto::NADA;
  ok &= toqueSerie(t4, REPOSO, ms4, 10, 10) == Gesto::CORTO;

  return ok;
}
