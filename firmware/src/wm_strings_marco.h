/**
 * wm_strings_marco.h — cadenas del portal de WiFiManager, en español.
 *
 * Se activa con un build flag, NUNCA editando la librería:
 *
 *     build_flags = -D WM_STRINGS_FILE='"wm_strings_marco.h"' -I src
 *
 * `WiFiManager.h` hace `#include WM_STRINGS_FILE` y por defecto trae
 * `wm_strings_en.h`. Es el mismo patrón que TFT_eSPI con `USER_SETUP_LOADED`:
 * editar el archivo de la librería pierde la configuración en cada
 * actualización y al clonar el repo.
 *
 * NO se partió de `wm_strings_es.h`, que sí viene en la librería: está SIN
 * TRADUCIR. Su contenido es el inglés con otras mayúsculas
 * («Credentials Saved» contra «Credentials saved»), su encabezado dice
 * «SAMPLE SAMPLE SAMPLE» y su versión es 0.0.0. Comprobado cadena por cadena.
 *
 * ── Qué se tradujo y qué no ────────────────────────────────────────────
 *
 * El portal del marco corre con `setMenu({"wifi"})`, así que lo ÚNICO que se
 * ve es: página raíz → «Conectar a tu WiFi» → lista de redes → contraseña →
 * «Guardar». Eso es lo que está traducido, más los mensajes de error de
 * conexión, que es donde alguien se atora de verdad.
 *
 * Se dejan en inglés a propósito:
 *
 *   - `HTTP_INFO_*` (~40 filas: chip id, heap, IDF, MAC…). No hay enlace que
 *     lleve ahí con el menú recortado, y la regla 3 dice que nada técnico es
 *     visible. Traducir diagnósticos que nadie ve es trabajo que envejece.
 *   - `HTTP_UPDATE*`. La página de OTA no puede funcionar: `huge_app.csv`
 *     tiene una sola partición de app (`app0`, sin `ota_1`), así que
 *     `Update.begin()` falla. Es un botón muerto por construcción.
 *   - `HTTP_HELP` se vacía: era una tabla con la lista de rutas del portal.
 *
 * Y NUNCA se traducen, porque no son texto visible sino protocolo:
 * `S_debugPrefix` (prefijo del log serie), `S_GET`/`S_POST` (métodos HTTP),
 * `S_options`, `S_parampre` (prefijo de campos de formulario), `S_ssidpre`,
 * y todo `HTTP_SCRIPT`/`HTTP_STYLE`/`HTTP_ITEM*`/`HTTP_FORM_START`, que son
 * marcado, CSS y JavaScript.
 *
 * Al actualizar WiFiManager: comparar contra `wm_strings_en.h` del nuevo tag.
 * Si aparece un símbolo nuevo, el build falla con «undeclared identifier», que
 * es exactamente el aviso que se quiere — no un texto que se queda en inglés
 * en silencio.
 */

#ifndef _WM_STRINGS_MARCO_H_
#define _WM_STRINGS_MARCO_H_

#ifndef WIFI_MANAGER_OVERRIDE_STRINGS

// Los tokens y las rutas viven aquí y se quedan en inglés: son URLs, no texto.
#include "wm_consts_en.h"

const char WM_LANGUAGE[] PROGMEM = "es-MX";

const char HTTP_HEAD_START[]       PROGMEM = "<!DOCTYPE html>"
"<html lang='es'><head>"
"<meta name='format-detection' content='telephone=no'>"
"<meta charset='UTF-8'>"
"<meta  name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>"
"<title>{v}</title>";

// JavaScript: no se toca.
const char HTTP_SCRIPT[]           PROGMEM = "<script>function c(l){"
"document.getElementById('s').value=l.getAttribute('data-ssid')||l.innerText||l.textContent;"
"p = l.nextElementSibling.classList.contains('l');"
"document.getElementById('p').disabled = !p;"
"if(p)document.getElementById('p').focus();};"
"function f() {var x = document.getElementById('p');x.type==='password'?x.type='text':x.type='password';}"
"</script>";

const char HTTP_HEAD_END[]         PROGMEM = "</head><body class='{c}'><div class='wrap'>";
const char HTTP_ROOT_MAIN[]        PROGMEM = "<h1>{t}</h1><h3>{v}</h3>";

// Con setMenu({"wifi"}) solo se pinta la primera. Las demás se traducen igual
// porque cuestan nada y evitan una página a medio idioma si algún día se
// vuelve a abrir el menú.
const char * const HTTP_PORTAL_MENU[] PROGMEM = {
"<form action='/wifi'    method='get'><button>Conectar a tu WiFi</button></form><br/>\n", // MENU_WIFI
"<form action='/0wifi'   method='get'><button>Conectar a tu WiFi (sin buscar)</button></form><br/>\n", // MENU_WIFINOSCAN
"<form action='/info'    method='get'><button>Información</button></form><br/>\n", // MENU_INFO
"<form action='/param'   method='get'><button>Ajustes</button></form><br/>\n",//MENU_PARAM
"<form action='/close'   method='get'><button>Cerrar</button></form><br/>\n", // MENU_CLOSE
"<form action='/restart' method='get'><button>Reiniciar</button></form><br/>\n",// MENU_RESTART
"<form action='/exit'    method='get'><button>Salir</button></form><br/>\n",  // MENU_EXIT
"<form action='/erase'   method='get'><button class='D'>Borrar</button></form><br/>\n", // MENU_ERASE
"<form action='/update'  method='get'><button>Actualizar</button></form><br/>\n",// MENU_UPDATE
"<hr><br/>" // MENU_SEP
};

const char HTTP_PORTAL_OPTIONS[]   PROGMEM = "";
const char HTTP_ITEM_QI[]          PROGMEM = "<div role='img' aria-label='{r}%' title='{r}%' class='q q-{q} {i} {h}'></div>";
const char HTTP_ITEM_QP[]          PROGMEM = "<div class='q {h}'>{r}%</div>";
const char HTTP_ITEM[]             PROGMEM = "<div><a href='#p' onclick='c(this)' data-ssid='{V}'>{v}</a>{qi}{qp}</div>";

const char HTTP_FORM_START[]       PROGMEM = "<form method='POST' action='{v}'>";
// «Red» y no «SSID»: es la etiqueta del campo que se rellena solo al tocar una
// red de la lista, y nadie tiene por qué saber qué es un SSID (regla 1).
const char HTTP_FORM_WIFI[]        PROGMEM = "<label for='s'>Red</label><input id='s' name='s' maxlength='32' autocorrect='off' autocapitalize='none' placeholder='{v}'><br/><label for='p'>Contraseña</label><input id='p' name='p' maxlength='64' type='password' placeholder='{p}'><input type='checkbox' id='showpass' onclick='f()'> <label for='showpass'>Ver contraseña</label><br/>";
const char HTTP_FORM_WIFI_END[]    PROGMEM = "";
const char HTTP_FORM_STATIC_HEAD[] PROGMEM = "<hr><br/>";
const char HTTP_FORM_END[]         PROGMEM = "<br/><br/><button type='submit'>Guardar</button></form>";
const char HTTP_FORM_LABEL[]       PROGMEM = "<label for='{i}'>{t}</label>";
const char HTTP_FORM_PARAM_HEAD[]  PROGMEM = "<hr><br/>";
const char HTTP_FORM_PARAM[]       PROGMEM = "<br/><input id='{i}' name='{n}' maxlength='{l}' value='{v}' {c}>\n"; // no quitar el salto de línea

const char HTTP_SCAN_LINK[]        PROGMEM = "<br/><form action='/wifi?refresh=1' method='POST'><button name='refresh' value='1'>Buscar otra vez</button></form>";
// La pantalla que se ve justo después de tocar «Guardar». Sin «ESP» ni «AP»:
// se nombra el marco y se nombra la red por la que hay que volver a entrar.
const char HTTP_SAVED[]            PROGMEM = "<div class='msg'>Guardando<br/>El marco está conectándose a tu red.<br />Si no lo logra, vuelve a conectarte a «Marco-Fotos» e inténtalo otra vez</div>";
const char HTTP_PARAMSAVED[]       PROGMEM = "<div class='msg S'>Guardado<br/></div>";
const char HTTP_END[]              PROGMEM = "</div></body></html>";
const char HTTP_ERASEBTN[]         PROGMEM = "<br/><form action='/erase' method='get'><button class='D'>Borrar la configuración de WiFi</button></form>";
const char HTTP_UPDATEBTN[]        PROGMEM = "<br/><form action='/update' method='get'><button>Actualizar</button></form>";
const char HTTP_BACKBTN[]          PROGMEM = "<hr><br/><form action='/' method='get'><button>Volver</button></form>";

// Los cuatro mensajes de estado son donde alguien se atora de verdad: hay que
// distinguir «escribiste mal la contraseña» de «esa red no aparece».
const char HTTP_STATUS_ON[]        PROGMEM = "<div class='msg S'><strong>Conectado</strong> a {v}<br/><em><small>con IP {i}</small></em></div>";
const char HTTP_STATUS_OFF[]       PROGMEM = "<div class='msg {c}'><strong>Sin conexión</strong> a {v}{r}</div>";
const char HTTP_STATUS_OFFPW[]     PROGMEM = "<br/>La contraseña no es correcta";
const char HTTP_STATUS_OFFNOAP[]   PROGMEM = "<br/>No se encontró esa red";
const char HTTP_STATUS_OFFFAIL[]   PROGMEM = "<br/>No se pudo conectar";
const char HTTP_STATUS_NONE[]      PROGMEM = "<div class='msg'>Todavía sin red configurada</div>";
const char HTTP_BR[]               PROGMEM = "<br/>";

// CSS: no se toca.
const char HTTP_STYLE[]            PROGMEM = "<style>"
".c,body{text-align:center;font-family:verdana}div,input,select{padding:5px;font-size:1em;margin:5px 0;box-sizing:border-box}"
"input,button,select,.msg{border-radius:.3rem;width: 100%}input[type=radio],input[type=checkbox]{width:auto}"
"button,input[type='button'],input[type='submit']{cursor:pointer;border:0;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%}"
"input[type='file']{border:1px solid #1fa3ec}"
".wrap {text-align:left;display:inline-block;min-width:260px;max-width:500px}"
"a{color:#000;font-weight:700;text-decoration:none}a:hover{color:#1fa3ec;text-decoration:underline}"
".q{height:16px;margin:0;padding:0 5px;text-align:right;min-width:38px;float:right}.q.q-0:after{background-position-x:0}.q.q-1:after{background-position-x:-16px}.q.q-2:after{background-position-x:-32px}.q.q-3:after{background-position-x:-48px}.q.q-4:after{background-position-x:-64px}.q.l:before{background-position-x:-80px;padding-right:5px}.ql .q{float:left}.q:after,.q:before{content:'';width:16px;height:16px;display:inline-block;background-repeat:no-repeat;background-position: 16px 0;"
"background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAAAQCAMAAADeZIrLAAAAJFBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADHJj5lAAAAC3RSTlMAIjN3iJmqu8zd7vF8pzcAAABsSURBVHja7Y1BCsAwCASNSVo3/v+/BUEiXnIoXkoX5jAQMxTHzK9cVSnvDxwD8bFx8PhZ9q8FmghXBhqA1faxk92PsxvRc2CCCFdhQCbRkLoAQ3q/wWUBqG35ZxtVzW4Ed6LngPyBU2CobdIDQ5oPWI5nCUwAAAAASUVORK5CYII=');}"
"@media (-webkit-min-device-pixel-ratio: 2),(min-resolution: 192dpi){.q:before,.q:after {"
"background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAALwAAAAgCAMAAACfM+KhAAAALVBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAOrOgAAAADnRSTlMAESIzRGZ3iJmqu8zd7gKjCLQAAACmSURBVHgB7dDBCoMwEEXRmKlVY3L//3NLhyzqIqSUggy8uxnhCR5Mo8xLt+14aZ7wwgsvvPA/ofv9+44334UXXngvb6XsFhO/VoC2RsSv9J7x8BnYLW+AjT56ud/uePMdb7IP8Bsc/e7h8Cfk912ghsNXWPpDC4hvN+D1560A1QPORyh84VKLjjdvfPFm++i9EWq0348XXnjhhT+4dIbCW+WjZim9AKk4UZMnnCEuAAAAAElFTkSuQmCC');"
"background-size: 95px 16px;}}"
".msg{padding:20px;margin:20px 0;border:1px solid #eee;border-left-width:5px;border-left-color:#777}.msg h4{margin-top:0;margin-bottom:5px}.msg.P{border-left-color:#1fa3ec}.msg.P h4{color:#1fa3ec}.msg.D{border-left-color:#dc3630}.msg.D h4{color:#dc3630}.msg.S{border-left-color: #5cb85c}.msg.S h4{color: #5cb85c}"
"dt{font-weight:bold}dd{margin:0;padding:0 0 0.5em 0;min-height:12px}"
"td{vertical-align: top;}"
".h{display:none}"
"button{transition: 0s opacity;transition-delay: 3s;transition-duration: 0s;cursor: pointer}"
"button.D{background-color:#dc3630}"
"button:active{opacity:50% !important;cursor:wait;transition-delay: 0s}"
"body.invert,body.invert a,body.invert h1 {background-color:#060606;color:#fff;}"
"body.invert .msg{color:#fff;background-color:#282828;border-top:1px solid #555;border-right:1px solid #555;border-bottom:1px solid #555;}"
"body.invert .q[role=img]{-webkit-filter:invert(1);filter:invert(1);}"
":disabled {opacity: 0.5;}"
"</style>";

// Era una tabla con la lista de rutas del portal, en inglés. Dentro de un
// regalo no pinta nada (regla 3), y vaciarla ahorra su sitio en flash.
const char HTTP_HELP[]             PROGMEM = "";

// Sin traducir a propósito: la partición `huge_app.csv` tiene una sola app
// (`app0`, sin `ota_1`), así que `Update.begin()` no puede funcionar. Traducir
// una página que nunca va a completar su acción sería peor que dejarla.
const char HTTP_UPDATE[] PROGMEM = "Upload new firmware<br/><form method='POST' action='u' enctype='multipart/form-data' onchange=\"(function(el){document.getElementById('uploadbin').style.display = el.value=='' ? 'none' : 'initial';})(this)\"><input type='file' name='update' accept='.bin,application/octet-stream'><button id='uploadbin' type='submit' class='h D'>Update</button></form><small><a href='http://192.168.4.1/update' target='_blank'>* May not function inside captive portal, open in browser http://192.168.4.1</a><small>";
const char HTTP_UPDATE_FAIL[] PROGMEM = "<div class='msg D'><strong>Update failed!</strong><Br/>Reboot device and try again</div>";
const char HTTP_UPDATE_SUCCESS[] PROGMEM = "<div class='msg S'><strong>Update successful.  </strong> <br/> Device rebooting now...</div>";

// ── Página /info: se queda en inglés ──────────────────────────────────────
// Con setMenu({"wifi"}) no hay enlace que lleve ahí. Son diagnósticos —chip id,
// heap, IDF, MAC—, o sea justo lo que la regla 3 dice que no se ve. Quien
// escriba /info a mano está depurando y lee inglés.
#ifdef ESP32
	const char HTTP_INFO_esphead[]    PROGMEM = "<h3>esp32</h3><hr><dl>";
	const char HTTP_INFO_chiprev[]    PROGMEM = "<dt>Chip rev</dt><dd>{1}</dd>";
  	const char HTTP_INFO_lastreset[]  PROGMEM = "<dt>Last reset reason</dt><dd>CPU0: {1}<br/>CPU1: {2}</dd>";
  	const char HTTP_INFO_aphost[]     PROGMEM = "<dt>Access point hostname</dt><dd>{1}</dd>";
    const char HTTP_INFO_psrsize[]    PROGMEM = "<dt>PSRAM Size</dt><dd>{1} bytes</dd>";
	const char HTTP_INFO_temp[]       PROGMEM = "<dt>Temperature</dt><dd>{1} C&deg; / {2} F&deg;</dd>";
    const char HTTP_INFO_hall[]       PROGMEM = "<dt>Hall</dt><dd>{1}</dd>";
#else
	const char HTTP_INFO_esphead[]    PROGMEM = "<h3>esp8266</h3><hr><dl>";
	const char HTTP_INFO_fchipid[]    PROGMEM = "<dt>Flash chip ID</dt><dd>{1}</dd>";
	const char HTTP_INFO_corever[]    PROGMEM = "<dt>Core version</dt><dd>{1}</dd>";
	const char HTTP_INFO_bootver[]    PROGMEM = "<dt>Boot version</dt><dd>{1}</dd>";
	const char HTTP_INFO_lastreset[]  PROGMEM = "<dt>Last reset reason</dt><dd>{1}</dd>";
	const char HTTP_INFO_flashsize[]  PROGMEM = "<dt>Real flash size</dt><dd>{1} bytes</dd>";
#endif

const char HTTP_INFO_memsmeter[]  PROGMEM = "<br/><progress value='{1}' max='{2}'></progress></dd>";
const char HTTP_INFO_memsketch[]  PROGMEM = "<dt>Memory - Sketch size</dt><dd>Used / Total bytes<br/>{1} / {2}";
const char HTTP_INFO_freeheap[]   PROGMEM = "<dt>Memory - Free heap</dt><dd>{1} bytes available</dd>";
const char HTTP_INFO_wifihead[]   PROGMEM = "<br/><h3>WiFi</h3><hr>";
const char HTTP_INFO_uptime[]     PROGMEM = "<dt>Uptime</dt><dd>{1} mins {2} secs</dd>";
const char HTTP_INFO_chipid[]     PROGMEM = "<dt>Chip ID</dt><dd>{1}</dd>";
const char HTTP_INFO_idesize[]    PROGMEM = "<dt>Flash size</dt><dd>{1} bytes</dd>";
const char HTTP_INFO_sdkver[]     PROGMEM = "<dt>SDK version</dt><dd>{1}</dd>";
const char HTTP_INFO_cpufreq[]    PROGMEM = "<dt>CPU frequency</dt><dd>{1}MHz</dd>";
const char HTTP_INFO_apip[]       PROGMEM = "<dt>Access point IP</dt><dd>{1}</dd>";
const char HTTP_INFO_apmac[]      PROGMEM = "<dt>Access point MAC</dt><dd>{1}</dd>";
const char HTTP_INFO_apssid[]     PROGMEM = "<dt>Access point SSID</dt><dd>{1}</dd>";
const char HTTP_INFO_apbssid[]    PROGMEM = "<dt>BSSID</dt><dd>{1}</dd>";
const char HTTP_INFO_stassid[]    PROGMEM = "<dt>Station SSID</dt><dd>{1}</dd>";
const char HTTP_INFO_staip[]      PROGMEM = "<dt>Station IP</dt><dd>{1}</dd>";
const char HTTP_INFO_stagw[]      PROGMEM = "<dt>Station gateway</dt><dd>{1}</dd>";
const char HTTP_INFO_stasub[]     PROGMEM = "<dt>Station subnet</dt><dd>{1}</dd>";
const char HTTP_INFO_dnss[]       PROGMEM = "<dt>DNS Server</dt><dd>{1}</dd>";
const char HTTP_INFO_host[]       PROGMEM = "<dt>Hostname</dt><dd>{1}</dd>";
const char HTTP_INFO_stamac[]     PROGMEM = "<dt>Station MAC</dt><dd>{1}</dd>";
const char HTTP_INFO_conx[]       PROGMEM = "<dt>Connected</dt><dd>{1}</dd>";
const char HTTP_INFO_autoconx[]   PROGMEM = "<dt>Autoconnect</dt><dd>{1}</dd>";

const char HTTP_INFO_aboutver[]     PROGMEM = "<dt>WiFiManager</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutarduino[] PROGMEM = "<dt>Arduino</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutsdk[]     PROGMEM = "<dt>ESP-SDK/IDF</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutdate[]    PROGMEM = "<dt>Build date</dt><dd>{1}</dd>";

const char S_brand[]              PROGMEM = "Marco de fotos";
// NO traducir: prefijo del log serie. Los mensajes «*wm:…» del monitor se
// buscan por esta marca.
const char S_debugPrefix[]        PROGMEM = "*wm:";
const char S_y[]                  PROGMEM = "Sí";
const char S_n[]                  PROGMEM = "No";
const char S_enable[]             PROGMEM = "Activado";
const char S_disable[]            PROGMEM = "Desactivado";
// NO traducir: son métodos HTTP.
const char S_GET[]                PROGMEM = "GET";
const char S_POST[]               PROGMEM = "POST";
const char S_NA[]                 PROGMEM = "Desconocido";
const char S_passph[]             PROGMEM = "********";
const char S_titlewifisaved[]     PROGMEM = "Listo";
const char S_titlewifisettings[]  PROGMEM = "Ajustes guardados";
const char S_titlewifi[]          PROGMEM = "Conectar a tu WiFi";
const char S_titleinfo[]          PROGMEM = "Información";
const char S_titleparam[]         PROGMEM = "Ajustes";
const char S_titleparamsaved[]    PROGMEM = "Ajustes guardados";
const char S_titleexit[]          PROGMEM = "Salir";
const char S_titlereset[]         PROGMEM = "Reinicio";
const char S_titleerase[]         PROGMEM = "Borrado";
const char S_titleclose[]         PROGMEM = "Cerrar";
// NO traducir: es un token de sustitución, no texto.
const char S_options[]            PROGMEM = "options";
const char S_nonetworks[]         PROGMEM = "No se encontró ninguna red. Toca «Buscar otra vez».";
const char S_staticip[]           PROGMEM = "IP fija";
const char S_staticgw[]           PROGMEM = "Puerta de enlace";
const char S_staticdns[]          PROGMEM = "DNS";
const char S_subnet[]             PROGMEM = "Máscara de subred";
const char S_exiting[]            PROGMEM = "Saliendo";
const char S_resetting[]          PROGMEM = "El marco se va a reiniciar en unos segundos.";
const char S_closing[]            PROGMEM = "Ya puedes cerrar esta página; el portal sigue abierto";
const char S_error[]              PROGMEM = "Ocurrió un error";
const char S_notfound[]           PROGMEM = "Página no encontrada\n\n";
const char S_uri[]                PROGMEM = "URI: ";
const char S_method[]             PROGMEM = "\nMétodo: ";
const char S_args[]               PROGMEM = "\nArgumentos: ";
// NO traducir: prefijo de los nombres de campo del formulario.
const char S_parampre[]           PROGMEM = "param_";

const char D_HR[]                 PROGMEM = "--------------------";

// NO traducir: prefijo del SSID por defecto del AP. Aquí se pasa «Marco-Fotos»
// explícito a autoConnect(), así que no llega a usarse.
#ifdef ESP8266
    const char S_ssidpre[]        PROGMEM = "ESP";
#elif defined(ESP32)
    const char S_ssidpre[]        PROGMEM = "ESP32";
#else
    const char S_ssidpre[]        PROGMEM = "WM";
#endif

#endif // WIFI_MANAGER_OVERRIDE_STRINGS

#endif // _WM_STRINGS_MARCO_H_
