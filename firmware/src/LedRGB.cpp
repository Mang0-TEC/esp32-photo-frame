#include "LedRGB.h"

// ════════════════════════════════════════════════════════════════════════════
//  La tabla. Una fila por estado, en el orden del enum.
//
//  Todo lo que distingue a un estado de otro está aquí: color, ritmo y si se
//  apaga solo. Nada de esto debe volver a un switch — la versión anterior tenía
//  los colores en dos sitios y ya se habían desincronizado.
// ════════════════════════════════════════════════════════════════════════════
static const LedRGB::Perfil TABLA[] = {
    //   R    G    B   semi   autoOff
    {    0,   0,   0,     0,  false },  // OFF
    {    0,   0, 255,   400,  false },  // BOOT            — azul pulsante (§9)
    {    0,   0, 255,     0,  false },  // WIFI_SETUP      — azul fijo: el portal espera
    {    0,   0, 255,   250,  false },  // WIFI_CONNECTING — más rápido que BOOT
    {    0, 255,   0,     0,  true  },  // WIFI_OK         — y «foto recibida»
    {  255,   0,   0,  1000,  false },  // SD_ERROR        — ver nota abajo
    {  255,   0,   0,   150,  true  },  // UPLOAD_ERROR
    {  255,  70,   0,   600,  true  },  // WARNING         — el 70 es MEDIDO, ver abajo
};

// El array va SIN tamaño explícito a propósito: declararlo [TOTAL] deja que un
// estado nuevo sin fila se rellene de ceros en silencio, y un estado negro que
// no parpadea ni se apaga se ve exactamente igual que uno que nadie disparó.
static_assert(sizeof(TABLA) / sizeof(*TABLA) == (size_t)LedRGB::State::TOTAL,
              "falta (o sobra) una fila en TABLA para algún State");

// El 70 del ámbar es el único número de la tabla que salió de mirar el LED, no de
// elegirlo: barrido con la tecla 'c' del banco sobre el hardware real. Y depende
// del hardware — vale para 220 Ω en el rojo y 470 Ω en el verde. Si esas
// resistencias cambian, este número deja de valer y se vuelve a barrer. El 80
// original tiraba a verde, que es el peor fallo posible en esta fila: ámbar es
// «subiendo» y rojo es «falló», y tienen que distinguirse de un vistazo.

// ponytail: SD_ERROR es el único estado que no se apaga, y es una excepción con
// fecha de caducidad. Hoy el display no existe y el LED es el único canal para
// decir «no hay tarjeta»; un rojo permanente contradice la regla 3, pero un marco
// sin tarjeta ya está roto de forma visible. En cuanto el firmware pinte ese
// error en pantalla, esta fila pasa a autoOff = true como todas las demás.

const LedRGB::Perfil& LedRGB::perfil(State s) {
    return TABLA[(uint8_t)s];
}

// ════════════════════════════════════════════════════════════════════════════
//  Interpolación
// ════════════════════════════════════════════════════════════════════════════

// Aritmética entera: 255 × 65535 cabe de sobra en int32 y evita el coste del
// float en el camino que corre en cada loop().
uint8_t LedRGB::lerp(uint8_t desde, uint8_t hasta, uint32_t transcurrido, uint16_t dur) {
    if (dur == 0 || transcurrido >= dur) return hasta;
    const int32_t delta = (int32_t)hasta - (int32_t)desde;
    return (uint8_t)((int32_t)desde + delta * (int32_t)transcurrido / (int32_t)dur);
}

// ════════════════════════════════════════════════════════════════════════════
//  Construcción e inicialización
// ════════════════════════════════════════════════════════════════════════════
LedRGB::LedRGB(uint8_t pinR, uint8_t pinG, uint8_t pinB)
    : _pinR(pinR), _pinG(pinG), _pinB(pinB),
      _state(State::OFF),
      _curR(0), _curG(0), _curB(0),
      _fromR(0), _fromG(0), _fromB(0),
      _tgtR(0), _tgtG(0), _tgtB(0),
      _fading(false), _fadeStartMs(0), _fadeDurationMs(FADE_DEFAULT_MS),
      _blinking(false), _blinkLit(false), _semiperiodoMs(500), _ultimoCambio(0),
      _lastActivityMs(0), _autoOff(false), _fadingOff(false), _desviado(false) {}

void LedRGB::begin() {
    ledcAttach(_pinR, 5000, 8);
    ledcAttach(_pinG, 5000, 8);
    ledcAttach(_pinB, 5000, 8);
    writeHardware(0, 0, 0);
    _lastActivityMs = millis();
}

void LedRGB::writeHardware(uint8_t r, uint8_t g, uint8_t b) {
    ledcWrite(_pinR, (uint16_t)r * escala[0] / 255);
    ledcWrite(_pinG, (uint16_t)g * escala[1] / 255);
    ledcWrite(_pinB, (uint16_t)b * escala[2] / 255);
    // Se guarda el valor PEDIDO, no el escalado: la escala es compensación de
    // hardware y no debe realimentarse al origen del siguiente fade.
    _curR = r; _curG = g; _curB = b;
}

// ════════════════════════════════════════════════════════════════════════════
//  Fade y parpadeo
// ════════════════════════════════════════════════════════════════════════════
void LedRGB::startFade(uint16_t durationMs) {
    // El origen se congela AQUÍ. Interpolar desde _cur* en cada paso lo mueve
    // bajo la cuenta y produce una exponencial: con 350 ms declarados el LED
    // estaba en 252 de 255 a los 170 ms, o sea que la constante no calibraba nada.
    _fromR = _curR; _fromG = _curG; _fromB = _curB;
    _fading = true;
    _fadeStartMs = millis();
    _fadeDurationMs = durationMs;
}

void LedRGB::startBlink(uint16_t semiperiodoMs) {
    _blinking = true;
    _fading = false;              // el parpadeo sale antes que el fade en update();
                                  // dejarlo vivo lo resucitaría al salir de aquí
    _semiperiodoMs = semiperiodoMs;
    _ultimoCambio = millis();
    _blinkLit = true;
    writeHardware(_tgtR, _tgtG, _tgtB);   // salto directo: un fade de entrada se
                                          // comería el primer semiperiodo entero
}

void LedRGB::stopBlink() {
    _blinking = false;
    _blinkLit = false;
}

// ════════════════════════════════════════════════════════════════════════════
//  Estado
// ════════════════════════════════════════════════════════════════════════════
void LedRGB::aplicarPerfil() {
    const Perfil& p = TABLA[(uint8_t)_state];
    _tgtR = p.r; _tgtG = p.g; _tgtB = p.b;
    _autoOff = p.autoOff;
    _fadingOff = _desviado = false;
    if (p.semiperiodoMs) {
        startBlink(p.semiperiodoMs);
    } else {
        stopBlink();
        startFade(FADE_DEFAULT_MS);
    }
}

// Devuelve el LED al color de su estado cuando la salida se ha desviado de él —
// por el auto-off o por un setRaw. Lee de la tabla, así que no puede divergir de
// setState; el switch de colores duplicado que había aquí ya divergía en WARNING.
void LedRGB::revivir() {
    if (_fadingOff || _desviado) aplicarPerfil();
}

void LedRGB::setState(State state) {
    // Antes del guard de estado repetido: treinta subidas seguidas terminan todas
    // en WIFI_OK, y cada una tiene que reiniciar la cuenta de los 30 s.
    _lastActivityMs = millis();
    if (_state == state) { revivir(); return; }
    _state = state;
    aplicarPerfil();
}

void LedRGB::setRaw(uint8_t r, uint8_t g, uint8_t b) {
    stopBlink();
    _fading = false;
    _autoOff = _fadingOff = false;
    // Desviado, no apagado: _state sigue diciendo una cosa y el LED muestra otra.
    // Sin esta línea, volver a pedir ese mismo estado se sale por el guard de
    // setState y el color crudo se queda pegado — que es justo lo que pasa en el
    // banco al pulsar un estado después de una tecla de canal.
    _desviado = true;
    _tgtR = r; _tgtG = g; _tgtB = b;
    writeHardware(r, g, b);
    _lastActivityMs = millis();
}

void LedRGB::forceOff() {
    setRaw(0, 0, 0);
    _state = State::OFF;
}

void LedRGB::resetActivityTimer() {
    _lastActivityMs = millis();
    revivir();
}

// ════════════════════════════════════════════════════════════════════════════
//  update() — en cada loop()
// ════════════════════════════════════════════════════════════════════════════
void LedRGB::update() {
    const uint32_t ahora = millis();

    // 1. Auto-off. VA PRIMERO, antes de la rama de parpadeo. Evaluarlo después
    //    —que es lo que hacía— deja fuera a los estados que parpadean, porque el
    //    parpadeo retorna temprano: una subida fallida dejaba el rojo latiendo a
    //    150 ms indefinidamente en la sala de otra persona. §9 y regla 3.
    if (_autoOff && !_fadingOff && !_desviado && ahora - _lastActivityMs >= AUTO_OFF_MS) {
        stopBlink();
        _tgtR = _tgtG = _tgtB = 0;
        _fadingOff = true;
        startFade(FADE_OFF_MS);
    }

    // 2. Parpadeo: ritmo exacto, sin fade. Un fade de 350 ms sobre un semiperiodo
    //    de 150 ms se comería el parpadeo entero.
    if (_blinking) {
        if (ahora - _ultimoCambio >= _semiperiodoMs) {
            _ultimoCambio = ahora;
            _blinkLit = !_blinkLit;
            if (_blinkLit) writeHardware(_tgtR, _tgtG, _tgtB);
            else           writeHardware(0, 0, 0);
        }
        return;
    }

    // 3. Fade en curso
    if (_fading) {
        const uint32_t transcurrido = ahora - _fadeStartMs;
        if (transcurrido >= _fadeDurationMs) {
            writeHardware(_tgtR, _tgtG, _tgtB);
            _fading = false;
            if (_fadingOff) { _fadingOff = false; _desviado = true; }
        } else {
            writeHardware(lerp(_fromR, _tgtR, transcurrido, _fadeDurationMs),
                          lerp(_fromG, _tgtG, transcurrido, _fadeDurationMs),
                          lerp(_fromB, _tgtB, transcurrido, _fadeDurationMs));
        }
    }
}
