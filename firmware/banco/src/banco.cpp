// ══════════════════════════════════════════════════════════════════════
//  BANCO DE PRUEBAS — NO ES LA BASE DEL FIRMWARE REAL
//
//  Implementa el contrato HTTP de §4 contra el ESP32, sin SD y sin display,
//  para desarrollar y medir la capa de red de web/index.html (etapa 5).
//  No usa ningún GPIO. No copiar de aquí al firmware del marco: lo que aquí
//  es una espera ocupada configurable, allá es una escritura a la tarjeta.
//
//  Vive en un proyecto PlatformIO aparte, ../platformio.ini es el de
//  producción y no lo toca. Ver el encabezado de ./platformio.ini.
// ══════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "wifi_credentials.h"

#ifdef BANCO_CORS
#warning "═══ CORS ACTIVO: cabecera de banco, NO puede sobrevivir al firmware final ═══"
#endif

static const uint8_t MAX_NOMBRES = 64;

AsyncWebServer server(80);

// ── Los dos buffers. Reservados UNA vez en setup() y NUNCA liberados: no
//    caben en .bss —el enlazador da 74,392 B y hacen falta 114,688— pero un
//    malloc de arranque que nunca se libera ni se redimensiona tampoco puede
//    fragmentar, que era la propiedad que se quería. La ruta de subida sigue
//    sin reservar nada, así que /dbg?heap=1 mide una sola variable.
static uint8_t *bufSubida;    // /upload escribe aquí
static uint8_t *bufRetenida;  // /photo sirve de aquí
static uint8_t enUso;         // cuántos /photo en vuelo sobre bufRetenida

static size_t retLen;
static char retNombre[13];

static void *dueno;    // qué petición posee el buffer de subida ahora mismo
static void *intruso;  // la última que llegó con el buffer ya tomado
static size_t acum;
static uint32_t chunks, tamChunk, t0;

enum : uint8_t { EST_OK, EST_BUF_EN_USO, EST_DESBORDO, EST_SIN_MEM };
static uint8_t estado;

static uint32_t contador = 1;  // en RAM: arranca en 1 en cada reinicio
static char nombres[MAX_NOMBRES][13];
static uint8_t nNombres;
static char lista[MAX_NOMBRES * 13 + 1];

// ── Perillas de banco (ver /dbg). En RAM a propósito: si el barrido del
//    retardo tumba la placa, el reinicio las devuelve a un valor sano sola.
static size_t topeCfg = BANCO_TOPE;
static uint32_t ocupadoMs = BANCO_RETARDO_SD_MS;
static uint32_t cederMs, fallarCada, fallarN, intentos;
static bool forzar507, modoHeap;
static uint8_t *heapBuf;  // solo con modoHeap; ver /dbg?heap=1

static bool hayFS;

// ──────────────────────────────────────────────────────────────────────
//  Utilidades
// ──────────────────────────────────────────────────────────────────────

static inline uint32_t mayorBloque() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

// ^[0-9]{8}\.JPG$ exacto. Sin esta comprobación, ?n=../manifest.txt sale del
// directorio en el firmware real. El esquema rígido de nombres la abarata a
// cuatro líneas, y aquí se prueba aunque no haya tarjeta que proteger.
static bool nombreValido(const char *s) {
  if (!s || strlen(s) != 12) return false;
  for (int i = 0; i < 8; i++)
    if (s[i] < '0' || s[i] > '9') return false;
  return strcmp(s + 8, ".JPG") == 0;
}

// Corre en cada arranque. nombreValido es el guard contra ?n=../manifest.txt y
// es la única lógica de este sketch que se puede comprobar sin red ni tarjeta;
// si alguien la "simplifica", el banco lo grita antes de levantar el servidor.
static void autocomprobar() {
  static const char *malos[] = {"../manifest.txt", "1.JPG",        "00000001.jpg", "000000001.JPG",
                                "0000001.JPG",     "0000000a.JPG", "00000001.JPGX", "00000001.JP"};
  bool ok = nombreValido("00000001.JPG") && nombreValido("99999999.JPG");
  for (auto m : malos) ok &= !nombreValido(m);
  ok &= !nombreValido(nullptr);
  Serial.printf("[autocomprobacion] nombreValido: %s\n", ok ? "ok" : "*** FALLA ***");
}

static void liberarHeap() {
  if (heapBuf) {
    free(heapBuf);
    heapBuf = nullptr;
  }
}

static void medir(const char *etiqueta) {
  Serial.printf("[%s] heap=%u mayorBloque=%u minHist=%u\n", etiqueta, ESP.getFreeHeap(), mayorBloque(),
                ESP.getMinFreeHeap());
}

// ──────────────────────────────────────────────────────────────────────
//  POST /upload
// ──────────────────────────────────────────────────────────────────────

static void subidaChunk(AsyncWebServerRequest *req, const String &, size_t index, uint8_t *data, size_t len,
                        bool fin) {
  if (index == 0 && dueno == nullptr) {
    dueno = req;
    acum = chunks = tamChunk = 0;
    t0 = millis();
    // Mitad del candado. /photo sirve bufRetenida POR REFERENCIA y lo drena a
    // lo largo de varios ACK; el memcpy del final lo pisaría a media descarga.
    // La otra mitad está en /photo: mientras dueno != nullptr responde 503, así
    // que enUso no puede subir después de esta línea. Dos comparaciones y la
    // exclusión es total, en los dos sentidos.
    estado = enUso ? EST_BUF_EN_USO : EST_OK;
    if (estado == EST_OK && !bufSubida) estado = EST_SIN_MEM;  // falló el malloc de arranque
    if (estado == EST_OK && modoHeap) {
      heapBuf = (uint8_t *)malloc(topeCfg);
      if (!heapBuf) estado = EST_SIN_MEM;
    }
    // Rescata el buffer si el cliente se cae a media subida: sin esto, dueno
    // se queda apuntando a una petición muerta y bloquea todas las siguientes.
    req->onDisconnect([req]() {
      if (dueno == req) {
        liberarHeap();
        dueno = nullptr;
      }
    });
  }

  // Subida solapada: se drena en silencio y NO se toca nada del dueño legítimo.
  // Se anota quién es para que su onRequest conteste 503 y no 400: un POST sin
  // parte de archivo tampoco es dueño, y ésos sí son 400.
  if (dueno != req) {
    intruso = req;
    return;
  }

  chunks++;
  if (len > tamChunk) tamChunk = len;

  if (estado == EST_OK) {
    if (acum + len > topeCfg) {
      // B4: se olvida lo acumulado y se sigue drenando. Cerrar el socket haría
      // que fetch() reportara error de red, y la página no podría distinguir
      // «foto muy grande» de «se cayó el WiFi» — que es justo lo que la etapa 5
      // tiene que distinguir.
      estado = EST_DESBORDO;
      liberarHeap();
    } else {
      memcpy((modoHeap ? heapBuf : bufSubida) + acum, data, len);
    }
  }
  acum += len;

  if (fin) {
    // La línea sale ANTES de la espera: a 3000-6000 ms la placa puede entrar en
    // pánico dentro del while, y el punto que truena es justo el que interesa.
    Serial.printf("[subida] bytes=%u chunks=%u chunkMax=%u ms=%lu heap=%u mayorBloque=%u minHist=%u\n", acum,
                  chunks, tamChunk, (unsigned long)(millis() - t0), ESP.getFreeHeap(), mayorBloque(),
                  ESP.getMinFreeHeap());

    // ── AQUÍ se simula la escritura a la SD. Espera OCUPADA, no delay():
    //    delay() en Arduino-ESP32 es vTaskDelay y CEDE el hilo; la escritura
    //    SPI real gira en bucles de sondeo y no cede. Simular con delay()
    //    mediría un riesgo distinto del que documenta el BOM.
    uint32_t t = micros();
    while (micros() - t < ocupadoMs * 1000UL) {
    }
    if (cederMs) delay(cederMs);  // control: aísla «bloquear» de «tardar»
  }
}

static void subidaFin(AsyncWebServerRequest *req) {
  // Ocupante ilegítimo: sale SIN limpiar dueno. Si lo limpiara, le arrancaría
  // la propiedad al dueño legítimo a media subida y su siguiente chunk volvería
  // a reclamar el buffer con acum ya avanzado — foto corrupta, en silencio.
  if (dueno != req) {
    // Se anota: el intruso no pasa por el printf del chunk —sale antes— y sin esta
    // línea el banco rechaza en SILENCIO, que es lo último que quieres cuando estás
    // depurando por qué salió o no salió un 503.
    Serial.printf("[subida] -> %d (%s)\n", req == intruso ? 503 : 400,
                  req == intruso ? "solapada" : "sin parte de archivo");
    if (req == intruso) req->send(503, "text/plain", "ocupado");
    else req->send(400, "text/plain", "foto");  // POST sin parte de archivo
    return;
  }

  intentos++;
  int code;
  char json[64] = "";

  if (estado == EST_BUF_EN_USO) {
    code = 503;  // /photo en vuelo sobre el buffer destino
  } else if (estado == EST_SIN_MEM) {
    code = 507;
  } else if (!req->hasParam("foto", true, true)) {
    code = 400;  // §4 especifica el campo "foto"; el callback solo da filename
  } else if (estado == EST_DESBORDO) {
    code = 413;
  } else if (forzar507) {
    code = 507;
    forzar507 = false;
  } else if (fallarN ? (fallarN--, true) : (fallarCada && intentos % fallarCada == 0)) {
    code = 500;  // de banco: §4 no lo documenta, es simulación de fallo
  } else if (acum == 0) {
    code = 400;  // un JPEG de 0 B rompería la verificación de /photo
  } else {
    char n[13];
    snprintf(n, sizeof n, "%08u.JPG", (unsigned)contador++);
    if (nNombres < MAX_NOMBRES) strcpy(nombres[nNombres++], n);
    // bufRetenida es más chico que el tope duro, y lo que lo limita es el HEAP,
    // no el tamaño de las fotos: igualarlo al tope se midió y deja mayorBloque en
    // 16372 tras WiFi, la mitad. Un archivo mayor sube bien —el contrato es 200—
    // pero no se retiene y /photo dará 404 para él. Eso NO es corrupción y la
    // página lo reporta distinto; ver el comentario de BANCO_RET en el .ini.
    if (bufRetenida && acum <= BANCO_RET) {
      memcpy(bufRetenida, modoHeap ? heapBuf : bufSubida, acum);
      retLen = acum;
      strcpy(retNombre, n);
    } else {
      Serial.printf("[subida] %u B > BANCO_RET (%u): no se retiene\n", acum, (unsigned)BANCO_RET);
    }
    snprintf(json, sizeof json, "{\"ok\":true,\"n\":\"%s\"}", n);
    code = 200;
  }

  liberarHeap();
  dueno = nullptr;
  Serial.printf("[subida] -> %d\n", code);

  if (code == 200) req->send(200, "application/json", json);
  else req->send(code, "text/plain", "");
}

// ──────────────────────────────────────────────────────────────────────
//  POST /delete
// ──────────────────────────────────────────────────────────────────────

// El cuerpo es JSON, no form-encoded, así que llega por onBody y no por params.
// Estático porque el sketch no reserva heap; basta con que /delete no se solape
// consigo mismo, que es disciplina del banco y no del contrato.
static char borrarN[13];
static bool borrarOk;

static void borrarCuerpo(AsyncWebServerRequest *, uint8_t *data, size_t len, size_t index, size_t total) {
  borrarOk = false;
  // onBody puede llamarse varias veces; con un JSON de 30 B nunca lo hará, pero
  // el código no debe depender de eso. Si llegara partido, se rechaza con 400.
  if (index != 0 || index + len != total) return;

  char cuerpo[128];
  size_t n = total < sizeof(cuerpo) - 1 ? total : sizeof(cuerpo) - 1;
  memcpy(cuerpo, data, n);
  cuerpo[n] = 0;

  const char *c = strstr(cuerpo, "\"n\"");
  if (!c || !(c = strchr(c + 3, ':')) || !(c = strchr(c, '"'))) return;
  c++;
  const char *fin = strchr(c, '"');
  if (!fin || fin - c != 12) return;
  memcpy(borrarN, c, 12);
  borrarN[12] = 0;
  borrarOk = true;
}

static void borrarFin(AsyncWebServerRequest *req) {
  if (!borrarOk || !nombreValido(borrarN)) {
    req->send(400, "text/plain", "n");
    return;
  }
  for (uint8_t i = 0; i < nNombres; i++)
    if (strcmp(nombres[i], borrarN) == 0) {
      memmove(nombres[i], nombres[i + 1], (nNombres - i - 1) * sizeof(nombres[0]));
      nNombres--;
      break;
    }
  if (strcmp(retNombre, borrarN) == 0) {
    retNombre[0] = 0;
    retLen = 0;
  }
  // El contador NO retrocede (§3): reutilizar números daría colisiones con
  // fotos que aún estén en el manifiesto. El reinicio es /dbg?contador=1.
  req->send(200, "application/json", "{\"ok\":true}");
}

// ──────────────────────────────────────────────────────────────────────
//  setup
// ──────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== BANCO DE RED — no es el firmware del marco ===");
  autocomprobar();

  // Fase 0, primera parte: los buffers se piden ANTES de WiFi, que es cuando el
  // heap está más entero. Una sola vez y sin free: no pueden fragmentar.
  medir("fase0-arranque");
  bufSubida = (uint8_t *)malloc(BANCO_TOPE);
  bufRetenida = (uint8_t *)malloc(BANCO_RET);
  Serial.printf("[fase0-buffers] subida=%u %s  retenida=%u %s\n", (unsigned)BANCO_TOPE,
                bufSubida ? "ok" : "FALLO", (unsigned)BANCO_RET, bufRetenida ? "ok" : "FALLO");
  medir("fase0-conBuffers");

  hayFS = LittleFS.begin();
  if (!hayFS) Serial.println("LittleFS no monta: falta 'pio run -t uploadfs'");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0w = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0w < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi SIN CONEXION: status=%d ssid=\"%s\" — revisa pass y 2.4 GHz\n",
                  WiFi.status(), WIFI_SSID);
  } else {
    Serial.printf("WiFi %s  IP %s  RSSI %d\n",
                  WIFI_SSID, WiFi.localIP().toString().c_str(), WiFi.RSSI());
    if (MDNS.begin("marco")) Serial.println("mDNS: http://marco.local/");
  }

#ifdef BANCO_CORS
  // ╔══════════════════════════════════════════════════════════════════╗
  // ║  CABECERA DE BANCO. NO PUEDE SOBREVIVIR AL FIRMWARE FINAL.       ║
  // ║  Allá la página y el endpoint son el MISMO ORIGEN y esto sobra.  ║
  // ║  DefaultHeaders es GLOBAL: sale también en el 404 de onNotFound. ║
  // ║  La ruta preferente para verificar cada hito es LittleFS, que es ║
  // ║  la configuración de producción; esto es solo para iterar rápido ║
  // ║  con la página servida desde la Mac.                             ║
  // ╚══════════════════════════════════════════════════════════════════╝
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
#endif

  // ── Las cinco rutas del contrato (§4) ──────────────────────────────

  // serveStatic registraría un handler por prefijo, o sea el catch-all que el
  // contrato prohíbe. Ruta explícita.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!hayFS || !LittleFS.exists("/index.html")) {
      req->send(200, "text/plain", "banco activo, sin pagina: falta 'pio run -t uploadfs'");
      return;
    }
    // no-store DE BANCO, no del contrato. Aquí la página cambia varias veces por
    // sesión y Safari se queda con la vieja: se mide una versión creyendo medir
    // otra, y el síntoma es que «el cambio no aparece». En el marco pasa lo
    // contrario —la página vive en PROGMEM y solo cambia al reflashear—, así que
    // allá cachearla es correcto y esta línea no debe copiarse.
    AsyncWebServerResponse *r = req->beginResponse(LittleFS, "/index.html", "text/html");
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
  });

  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *req) {
    size_t n = 0;
    for (uint8_t i = 0; i < nNombres; i++) n += snprintf(lista + n, sizeof(lista) - n, "%s\n", nombres[i]);
    req->send(req->beginResponse(200, "text/plain", (const uint8_t *)lista, n));
  });

  server.on("/photo", HTTP_GET, [](AsyncWebServerRequest *req) {
    const AsyncWebParameter *p = req->getParam("n");
    if (!p || !nombreValido(p->value().c_str())) {
      req->send(400, "text/plain", "n");
      return;
    }
    if (!retLen || strcmp(p->value().c_str(), retNombre) != 0) {
      // El banco retiene solo la última foto: /list anuncia nombres que aquí
      // dan 404. Es artefacto del banco, no del contrato — la página debe
      // tratarlo como error, no compensarlo.
      req->send(404, "text/plain", "no");
      return;
    }
    // La otra mitad del candado: con una subida en curso, el memcpy del final
    // pisaría este buffer a media descarga. Aquí pierde /photo.
    if (dueno) {
      req->send(503, "text/plain", "subiendo");
      return;
    }
    // La respuesta se sirve POR REFERENCIA y se drena a lo largo de varios ACK.
    // El contador lo marca ocupado hasta que termina; onDisconnect dispara
    // después de agotar la fuente, así que decrementa tarde — el lado correcto
    // del error.
    enUso++;
    req->onDisconnect([]() {
      if (enUso) enUso--;
    });
    AsyncWebServerResponse *r = req->beginResponse(200, "image/jpeg", bufRetenida, retLen);
    // Sin esto Safari puede servirla de caché, y la verificación de integridad
    // compararía el blob contra sí mismo: pasaría siempre, incluso con el
    // multipart corrompiendo bytes. Es la prueba que motiva el banco entero.
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
  });

  server.on("/upload", HTTP_POST, subidaFin, subidaChunk);
  server.on("/delete", HTTP_POST, borrarFin, nullptr, borrarCuerpo);

  // No es hipotético: el navegador pide /favicon.ico solo, sin que nadie se lo
  // mande. Cinco rutas y ningún catch-all significa que todo lo demás cae aquí.
  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "no"); });

  // ══════════════════ BANCO — depuración, no es del contrato ═════════
  // Una sola ruta con parámetros: cinco rutas serían cinco cosas que recordar.
  server.on("/dbg", HTTP_GET, [](AsyncWebServerRequest *req) {
    auto par = [req](const char *k, uint32_t &dst) {
      if (req->hasParam(k)) dst = req->getParam(k)->value().toInt();
    };
    // topeCfg se acota a BANCO_TOPE: es el tamaño real del buffer y pasarse
    // sería un desbordamiento, no una prueba.
    if (req->hasParam("tope")) {
      long v = req->getParam("tope")->value().toInt();
      topeCfg = v > 0 && (size_t)v < (size_t)BANCO_TOPE ? (size_t)v : (size_t)BANCO_TOPE;
    }
    par("fallar", fallarN);
    par("fallarCada", fallarCada);
    par("ocupado", ocupadoMs);
    par("ceder", cederMs);
    if (req->hasParam("507")) forzar507 = req->getParam("507")->value().toInt() != 0;
    if (req->hasParam("heap")) modoHeap = req->getParam("heap")->value().toInt() != 0;
    if (req->hasParam("contador")) contador = req->getParam("contador")->value().toInt();
    // Seguro: si algún camino matara la petición sin disparar onDisconnect, el
    // contador quedaría en 1 y el banco respondería 503 a TODA subida hasta el
    // siguiente reinicio, con una causa nada evidente.
    if (req->hasParam("enuso")) enUso = 0;

    char j[512];
    snprintf(j, sizeof j,
             "{\"heap\":%u,\"mayorBloque\":%u,\"minHist\":%u,"
             "\"tope\":%u,\"topeHw\":%u,\"retMax\":%u,"
             "\"ocupadoMs\":%u,\"cederMs\":%u,\"modoHeap\":%s,"
             "\"contador\":%u,\"intentos\":%u,\"nombres\":%u,\"nombresLlenos\":%s,"
             "\"enUso\":%u,\"subiendo\":%s,\"retLen\":%u,"
             "\"ultima\":{\"bytes\":%u,\"chunks\":%u,\"chunkMax\":%u}}",
             ESP.getFreeHeap(), mayorBloque(), ESP.getMinFreeHeap(), topeCfg, (unsigned)BANCO_TOPE,
             (unsigned)BANCO_RET, ocupadoMs, cederMs, modoHeap ? "true" : "false", contador, intentos,
             nNombres, nNombres >= MAX_NOMBRES ? "true" : "false", enUso, dueno ? "true" : "false", retLen,
             acum, chunks, tamChunk);
    req->send(200, "application/json", j);
  });
  // ═══════════════════════════════════════════════════════════════════

  server.begin();

  // Fase 0, cierre: con WiFi y servidor arriba. Es el número que decide si
  // BANCO_TOPE y BANCO_RET dejan holgura o hay que bajarlos por flag.
  medir("fase0-listo");
}

void loop() {}
