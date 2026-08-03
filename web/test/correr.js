#!/usr/bin/env node
/* Conduce web/index.html en Chrome sin cabeza y ejecuta un archivo de casos
   dentro de la página. Sin dependencias: habla el protocolo de Chrome por el
   WebSocket que node trae de fábrica desde la v22.

   Este archivo NO forma parte de la página. El empaquetado de la etapa 6 toma
   únicamente web/index.html — ver el README de esta carpeta.

   Uso:  node web/test/correr.js [casos.js]
   Sale con código 1 si algo falla, para que sirva en un hook o en CI. */
"use strict";

const { spawn } = require("child_process");
const fs   = require("fs");
const os   = require("os");
const path = require("path");

const CHROME = process.env.CHROME || (process.platform === "darwin"
  ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
  : "google-chrome");
const PUERTO = Number(process.env.PUERTO || 9333);

const raiz    = path.resolve(__dirname, "..", "..");
const pagina  = path.join(raiz, "web", "index.html");
const archivo = path.resolve(process.argv[2] || path.join(__dirname, "casos.js"));

for (const f of [pagina, archivo])
  if (!fs.existsSync(f)) { console.error("no existe: " + f); process.exit(1); }
if (!fs.existsSync(CHROME) && process.platform === "darwin") {
  console.error("no se encontró Chrome en:\n  " + CHROME +
                "\nindica la ruta con la variable CHROME");
  process.exit(1);
}

const esperar = ms => new Promise(r => setTimeout(r, ms));

/* El teléfono de referencia: iPhone 11, 414×896 pt a 2×. Importa porque la página
   decide envolturas y tamaños por ancho de viewport. */
const PANTALLA = { width: 414, height: 896, deviceScaleFactor: 2, mobile: true };

(async () => {
  const chrome = spawn(CHROME, [
    "--headless=new",
    "--remote-debugging-port=" + PUERTO,
    "--no-first-run", "--no-default-browser-check",
    "--user-data-dir=" + path.join(os.tmpdir(), "marco-fotos-arnes"),
    "file://" + pagina
  ], { stdio: "ignore" });

  const cerrar = () => { try { chrome.kill(); } catch {} };
  process.on("exit", cerrar);
  process.on("SIGINT", () => { cerrar(); process.exit(130); });

  // Chrome tarda en abrir el puerto; se sondea en vez de dormir a ciegas.
  let destino = null;
  for (let i = 0; i < 60 && !destino; i++) {
    await esperar(250);
    try {
      const lista = await (await fetch(`http://127.0.0.1:${PUERTO}/json/list`)).json();
      destino = lista.find(t => t.type === "page" && t.webSocketDebuggerUrl);
    } catch { /* todavía no levanta */ }
  }
  if (!destino) { console.error("Chrome no abrió el puerto de depuración"); process.exit(1); }

  const ws = new WebSocket(destino.webSocketDebuggerUrl);
  await new Promise((r, x) => { ws.addEventListener("open", r); ws.addEventListener("error", x); });

  let id = 0;
  const pendientes = new Map();
  const consola = [];
  ws.addEventListener("message", ev => {
    const m = JSON.parse(ev.data);
    if (m.id && pendientes.has(m.id)) { pendientes.get(m.id)(m); pendientes.delete(m.id); }
    // Los console.assert de la página (la geometría del recorte) son fallos también.
    if (m.method === "Runtime.consoleAPICalled" && m.params.type === "assert")
      consola.push("ASSERT EN LA PÁGINA: " +
        m.params.args.map(a => a.value !== undefined ? a.value : a.description).join(" "));
    if (m.method === "Runtime.exceptionThrown")
      consola.push("EXCEPCIÓN: " + m.params.exceptionDetails.text + " " +
        ((m.params.exceptionDetails.exception || {}).description || ""));
  });
  const enviar = (method, params) => new Promise(r => {
    const i = ++id; pendientes.set(i, r);
    ws.send(JSON.stringify({ id: i, method, params }));
  });

  await enviar("Runtime.enable");
  await enviar("Emulation.setDeviceMetricsOverride", PANTALLA);
  await esperar(500);                       // que termine de correr el script de la página

  const res = await enviar("Runtime.evaluate", {
    expression: fs.readFileSync(archivo, "utf8"),
    awaitPromise: true, returnByValue: true
  });

  if (res.result.exceptionDetails) {
    console.error("los casos lanzaron:\n" +
      JSON.stringify(res.result.exceptionDetails.exception ||
                     res.result.exceptionDetails, null, 2));
    process.exit(1);
  }

  const r = res.result.result.value;
  console.log(r.lineas.join("\n"));
  if (consola.length) console.log("\n" + consola.join("\n"));

  const total = r.fallos + consola.length;
  console.log(`\n${r.lineas.length} comprobaciones · ${total ? total + " FALLARON" : "todas pasaron"}`);
  ws.close(); cerrar();
  process.exit(total ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
