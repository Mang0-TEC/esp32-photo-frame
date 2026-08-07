// src/nombre.h — Validación de nombres de foto, compartida por los tres sitios
//
// ANTES estaba copiada a mano en firmware/src/main.cpp y en
// firmware/banco/src/banco.cpp, y las dos copias YA HABÍAN DIVERGIDO: cada una
// probaba un caso malo que la otra no ("00000001.JP" allá, "../../aa.JPG" acá).
// Ninguna de las dos estaba mal, pero la divergencia es la señal de que esto no
// aguanta como copia manual.
//
// Se comparte por la misma regla que tft_comun.ini y no por ahorrar líneas: el
// banco acepta duplicar VERSIONES de librería porque una versión divergente es
// detectable —otro binario, y el encabezado del .ini avisa—, pero esto es el
// guard contra ?n=../manifest.txt, y una copia laxa no se nota por ningún lado
// hasta que alguien se sale de /fotos/.
//
// Va en header con `static inline` a propósito: sin .cpp que añadir a tres
// build_src_filter distintos, y cada env se lleva su copia sin problemas de ODR.
// Los tres .ini que lo usan necesitan -I../src (o -I src en el del marco).

#pragma once

#include <string.h>

// ^[0-9]{8}\.JPG$ EXACTO — el patrón entero, no «mide 12 y termina en .JPG».
//
// Esa comprobación laxa es la trampa: "../../aa.JPG" mide 12 y termina en .JPG,
// cumple las dos cosas y sale del directorio. El esquema rígido de nombres de §3
// es lo que abarata la validación correcta a cuatro líneas.
static inline bool nombreValido(const char *s) {
  if (!s || strlen(s) != 12) return false;
  for (int i = 0; i < 8; i++)
    if (s[i] < '0' || s[i] > '9') return false;
  return strcmp(s + 8, ".JPG") == 0;
}

// Autocomprobación, para correr en cada arranque. Es la unión de los dos juegos
// de casos que tenían las copias por separado; si alguien "simplifica" la
// función, esto lo grita antes de que haya nada montado.
//
// Devuelve el veredicto en vez de imprimirlo: main.cpp lo combina en una sola
// línea con la del fade de LedRGB, y el banco del display con la suya.
static inline bool nombreValidoOk() {
  static const char *malos[] = {"../manifest.txt", "1.JPG",         "00000001.jpg", "000000001.JPG",
                                "0000001.JPG",     "0000000a.JPG",  "00000001.JPGX", "00000001.JP",
                                "../../aa.JPG"};
  bool ok = nombreValido("00000001.JPG") && nombreValido("99999999.JPG");
  for (auto m : malos) ok &= !nombreValido(m);
  ok &= !nombreValido(nullptr);
  return ok;
}
