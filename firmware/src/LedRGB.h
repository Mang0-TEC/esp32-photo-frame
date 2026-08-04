#ifndef LEDRGB_H
#define LEDRGB_H

#include <Arduino.h>

// Indicador de estado del marco — §9 de la especificación funcional.
//
// Color, ritmo y auto-off de cada estado viven en una TABLA (ver LedRGB.cpp), no
// en un switch. Es la misma información para todos los estados, y tenerla en dos
// sitios ya había divergido: resetActivityTimer restauraba WARNING como color
// fijo cuando setState lo define como parpadeo.
//
// LED de CÁTODO COMÚN: el duty va en sentido intuitivo, 0 = apagado.
class LedRGB {
public:
    enum class State : uint8_t {
        OFF,               // Apagado
        BOOT,              // Arrancando
        WIFI_SETUP,        // Portal cautivo arriba, esperando configuración
        WIFI_CONNECTING,   // Intentando conectar a la red guardada
        WIFI_OK,           // Conectado y operando — también «foto recibida»
        SD_ERROR,          // Sin tarjeta. NO se apaga; ver la nota de la tabla
        UPLOAD_ERROR,      // Falló una subida
        WARNING,           // Escribiendo en SD / subida en curso
        TOTAL              // centinela para recorrer la tabla, no es un estado
    };

    // semiperiodoMs es el tiempo ENTRE cambios, no el ciclo completo: un 250 aquí
    // late a 2 Hz. Cero significa color fijo, al que se llega con fade.
    struct Perfil {
        uint8_t  r, g, b;
        uint16_t semiperiodoMs;
        bool     autoOff;
    };
    static const Perfil& perfil(State s);

    LedRGB(uint8_t pinR, uint8_t pinG, uint8_t pinB);

    void begin();
    void setState(State state);
    void setRaw(uint8_t r, uint8_t g, uint8_t b);   // inmediato, sin fade ni parpadeo
    void forceOff();
    void update();                                  // en cada loop(); no bloquea
    void resetActivityTimer();                      // tras un toque, un cambio de foto…

    State currentState() const { return _state; }

    // Compensación de brillo por canal, 0-255. El rojo lleva 1 kΩ y el verde y el
    // azul 470 Ω, con Vf distintos: a igual duty NO dan el mismo brillo, así que
    // el blanco tira a rosa y el ámbar de WARNING sale más rojo que su fila de la
    // tabla. Los valores se miden en el banco (env:led, tecla 'w'); identidad
    // mientras tanto, que es el único valor honesto sin haber visto el LED.
    uint8_t escala[3] = {255, 255, 255};

    // Interpolación del fade, libre y pura para poder comprobarla sin placa: el
    // defecto que evita —tomar como origen el color ACTUAL, que se mueve en cada
    // paso— no se ve mirando el LED, solo cronometrándolo.
    static uint8_t lerp(uint8_t desde, uint8_t hasta, uint32_t transcurrido, uint16_t dur);

private:
    uint8_t _pinR, _pinG, _pinB;
    State   _state;

    uint8_t _curR,  _curG,  _curB;    // lo que está en el PWM ahora mismo
    uint8_t _fromR, _fromG, _fromB;   // origen del fade — fijo mientras dura
    uint8_t _tgtR,  _tgtG,  _tgtB;    // destino

    bool     _fading;
    uint32_t _fadeStartMs;
    uint16_t _fadeDurationMs;
    static constexpr uint16_t FADE_DEFAULT_MS = 350;   // transición estado → estado
    static constexpr uint16_t FADE_OFF_MS     = 800;   // auto-off, más lento a propósito

    bool     _blinking, _blinkLit;
    uint16_t _semiperiodoMs;
    uint32_t _ultimoCambio;

    uint32_t _lastActivityMs;
    bool     _autoOff;       // este estado se apaga solo
    bool     _fadingOff;     // apagándose ahora mismo por inactividad
    // La salida ya no corresponde al perfil del estado: o el auto-off la apagó,
    // o alguien llamó a setRaw(). Es lo que hace que volver a pedir el MISMO
    // estado sí repinte, en vez de salirse por el guard de estado repetido.
    bool     _desviado;
    static constexpr uint32_t AUTO_OFF_MS = 30000;

    void aplicarPerfil();
    void revivir();
    void startFade(uint16_t durationMs);
    void startBlink(uint16_t semiperiodoMs);
    void stopBlink();
    void writeHardware(uint8_t r, uint8_t g, uint8_t b);
};

#endif
