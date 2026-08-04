/* Casos de la página, ejecutados DENTRO de ella por web/test/correr.js.

   Contrato: este archivo es una expresión que evalúa a una promesa de
   { lineas: [...], fallos: n }. Tiene acceso a todo el ámbito del script de la
   página —cola, ed, abrirEditor, quitarFoto…— porque corre en su mismo contexto.

   Las fotos son sintéticas: sin EXIF, pero con las proporciones que deciden el
   comportamiento. La orientación EXIF no se puede probar así, y se mide a mano en
   el teléfono con el panel de diagnóstico — ver §4 de la especificación. */
(async () => {
  const lineas = [];
  let fallos = 0;
  const ok = (c, m) => { if (!c) fallos++; lineas.push((c ? "PASA  " : "FALLA ") + m); };
  const esperar = ms => new Promise(x => setTimeout(x, ms));
  const hasta = async (f, ms = 8000) => {
    const t = Date.now();
    while (Date.now() - t < ms) { if (f()) return true; await esperar(50); }
    return false;
  };

  const jpeg = (w, h, nombre) => new Promise(res => {
    const c = document.createElement("canvas");
    c.width = w; c.height = h;
    const x = c.getContext("2d");
    for (let i = 0; i < 400; i++) {                    // ruido: que no comprima a nada
      x.fillStyle = `hsl(${(i * 37) % 360},70%,${30 + (i % 50)}%)`;
      x.fillRect((i * 97) % w, (i * 53) % h, w / 12, h / 12);
    }
    c.toBlob(b => res(new File([b], nombre, { type: "image/jpeg" })), "image/jpeg", 0.9);
  });

  const meter = async files => {
    const dt = new DataTransfer();
    for (const f of files) dt.items.add(f);
    const inp = document.getElementById("file");
    inp.files = dt.files;
    inp.dispatchEvent(new Event("change"));
  };
  const $$ = id => document.getElementById(id);

  const vertical   = await jpeg(900, 1200, "vertical34.jpg");   // 3:4
  const horizontal = await jpeg(1200, 900, "horizontal43.jpg"); // 4:3
  const exacta     = await jpeg(800, 1200, "exacta23.jpg");     // 2:3

  // ── El pipeline base, que la etapa 4 no debe haber movido ──────────────────
  await meter([vertical, horizontal, exacta]);
  ok(await hasta(() => cola.items.length === 3 && cola.items.every(i => i.estado === "lista")),
     "las tres fotos se prepararon");

  const [v, h, e] = cola.items;
  ok(v.dstW === 320 && v.dstH === 427, `3:4 completa → 320×427 (dio ${v.dstW}×${v.dstH})`);
  ok(h.dstW === 320 && h.dstH === 240, `4:3 completa → 320×240 (dio ${h.dstW}×${h.dstH})`);
  ok(e.dstW === 320 && e.dstH === 480, `2:3 completa → 320×480 (dio ${e.dstW}×${e.dstH})`);
  ok(v.aviso === null && h.aviso !== null, "el aviso de franjas solo salta en la 4:3");
  ok(v.recorte === null && h.recorte === null, "nadie entra recortado");

  // ── Editor sobre la 4:3, que es el caso que §6 dice que el recorte compensa ──
  abrirDetalle(h.id);
  await abrirEditor(h.id);
  ok(ed !== null && $$("vistaEditor").hidden === false, "el editor abrió");
  ok(Math.abs(ed.w * ed.nw / (ed.h * ed.nh) - 320 / 480) < 1e-9, "el recuadro es 2:3 exacto");
  ok(Math.abs(ed.w - 0.5) < 1e-9 && Math.abs(ed.h - 1) < 1e-9,
     `4:3 → caja de medio ancho (dio ${ed.w.toFixed(4)} × ${ed.h.toFixed(4)})`);
  ok($$("caja").style.width === "50%", "la caja se pinta en porcentajes");
  ok(ed.canvas.width === 640 && ed.canvas.height === 480,
     `previa con contain propio: 640×480 (dio ${ed.canvas.width}×${ed.canvas.height})`);

  mover(-5, 0); ok(ed.x === 0, "arrastrar a la izquierda se acota en 0");
  mover(5, 0);  ok(Math.abs(ed.x - 0.5) < 1e-9, "arrastrar a la derecha se acota en 1-w");

  /* El tope de zoom lo pone la resolución: 1200 px de ancho con caja de medio
     ancho son 600 px de fuente, y el suelo son los 320 de la pantalla. */
  ok(Math.abs(ed.zMax - 1.875) < 1e-9, `zMax por resolución = 1.875 (dio ${ed.zMax})`);
  aplicarZoom(1.5);
  ok(Math.abs(ed.w - 0.5 / 1.5) < 1e-9, "zoom 1.5× encoge la caja");
  ok(Math.abs(ed.w * ed.nw / (ed.h * ed.nh) - 320 / 480) < 1e-9, "sigue 2:3 tras el zoom");
  aplicarZoom(99);
  ok(Math.abs(ed.zoom - ed.zMax) < 1e-9, "pasarse de zoom se acota en zMax, nunca amplía");
  ok(Math.round(ed.w * ed.nw) === 320, "y en el tope el recorte mide 320 px de fuente");
  aplicarZoom(1);

  const antesDeCancelar = h.blob;
  $$("btnCancelarRecorte").click();
  await hasta(() => ed === null);
  ok(h.recorte === null && h.blob === antesDeCancelar, "cancelar no mutó el item");

  // ── Confirmar ──────────────────────────────────────────────────────────────
  await abrirEditor(h.id);
  const urlViejo = h.url;
  $$("btnAplicarRecorte").click();
  ok(await hasta(() => h.estado === "lista" && h.recorte !== null), "el recorte se aplicó");
  ok(h.dstW === 320 && h.dstH === 480, `recortada → 320×480 exactos (dio ${h.dstW}×${h.dstH})`);
  ok(h.maxBytes === 32768, `presupuesto completo: 32768 B (dio ${h.maxBytes})`);
  ok(h.aviso === null, "el aviso de franjas desapareció por construcción");
  ok(!$$("m" + h.id).innerHTML.includes("franjas"), "y el chip del mosaico también");
  ok(h.url !== urlViejo, "se creó un object URL nuevo");
  ok($$("avisoFranjas").hidden, "el aviso agregado de la rejilla se recalculó");
  ok($$("btnQuitarRecorte").hidden === false, "aparece «Quitar recorte»");

  // ── Reversible ─────────────────────────────────────────────────────────────
  $$("btnQuitarRecorte").click();
  ok(await hasta(() => h.recorte === null && h.estado === "lista"), "«Quitar recorte» revirtió");
  ok(h.dstW === 320 && h.dstH === 240, `y volvió a 320×240 (dio ${h.dstW}×${h.dstH})`);
  ok(h.aviso !== null, "el aviso de franjas volvió");

  // ── Una 2:3: el editor abre, y confirmar sin mover NO inventa un recorte ────
  cerrarDetalle();
  abrirDetalle(e.id);
  await abrirEditor(e.id);
  ok(Math.abs(ed.w - 1) < 1e-6 && Math.abs(ed.h - 1) < 1e-6, "en una 2:3 la caja es la foto entera");
  $$("btnAplicarRecorte").click();
  await esperar(500);
  ok(e.recorte === null, "confirmar sin mover una 2:3 NO guarda recorte (el caso del flotante)");
  ok(e.estado === "lista", "y la deja lista");

  // ── Historial de dos niveles ───────────────────────────────────────────────
  cerrarDetalle();
  abrirDetalle(h.id);
  await abrirEditor(h.id);
  ok(editorEmpujado, "el editor empujó su propia entrada");
  history.back();
  ok(await hasta(() => ed === null), "atrás cerró el editor");
  ok(detalleId === h.id && !$$("vistaDetalle").hidden,
     "y dejó el detalle abierto, sin saltarse dos niveles");
  history.back();
  ok(await hasta(() => detalleId === null), "el siguiente atrás cierra el detalle");
  ok(!$$("vistaRejilla").hidden, "y vuelve a la rejilla");

  // ── Sin margen para acercar, el slider no se pinta ─────────────────────────
  limpiarTanda();
  await meter([await jpeg(320, 480, "justa.jpg")]);
  await hasta(() => cola.items.length === 1 && cola.items[0].estado === "lista");
  await abrirEditor(cola.items[0].id);
  ok(ed.zMax === 1 && $$("labelZoom").hidden,
     "una 2:3 del tamaño del marco esconde el slider en vez de dejarlo muerto");
  $$("btnCancelarRecorte").click();
  await hasta(() => ed === null);
  cerrarDetalle();

  // ── Quitar foto: los números de las posteriores ────────────────────────────
  limpiarTanda();
  await meter([vertical, horizontal, exacta]);
  await hasta(() => cola.items.length === 3 && cola.items.every(i => i.estado === "lista"));
  const [v2, h2, e2] = cola.items;
  window.confirm = () => true;
  quitarFoto(v2.id);                                // la primera de tres
  ok(cola.items.length === 2, "la foto salió de la cola");
  ok($$("m" + v2.id) === null, "y su mosaico del DOM");
  ok($$("m" + h2.id).querySelector(".num").textContent === "1", "la que era #2 se repintó como #1");
  ok($$("m" + e2.id).querySelector(".num").textContent === "2", "y la que era #3 como #2");

  // ── El bucle de la cola sobrevive a un splice a media tanda ────────────────
  limpiarTanda();
  await meter([await jpeg(600, 800, "a.jpg"), await jpeg(600, 800, "b.jpg"),
               await jpeg(600, 800, "c.jpg"), await jpeg(600, 800, "d.jpg")]);
  await hasta(() => cola.items.length === 4 && cola.items[0].estado === "lista");
  quitarFoto(cola.items[0].id);                     // splice mientras corre
  ok(await hasta(() => !cola.corriendo && cola.items.every(i => i.estado === "lista")),
     "ninguna quedó en pendiente tras quitar una a media tanda");
  ok(cola.items.length === 3, "y quedaron las otras tres");

  // ── Los tres botones del detalle envuelven a 414 pt ────────────────────────
  abrirDetalle(cola.items[0].id);
  await esperar(100);
  $$("btnUpload").hidden = false;   // aquí CFG.BASE todavía es null (file://)
  await esperar(50);
  const fila = id => Math.round($$(id).getBoundingClientRect().top);
  ok(fila("btnDownload") === fila("btnQuitarFoto") && fila("btnUpload") > fila("btnDownload"),
     "con «Subir al marco» los botones envuelven en vez de encogerse");
  $$("btnUpload").hidden = true;
  cerrarDetalle();

  /* ══════════════════════════════════════════════════════════════════════════
     ETAPA 5 — LA CAPA DE RED

     Sin ESP32 y sin servidor: se sustituye window.fetch, que es más determinista
     que una placa y funciona igual sobre file://. Lo que NO se prueba aquí es el
     comportamiento de Safari con la pestaña en segundo plano — eso es teléfono.

     Sobre file:// la página se cae a CFG.BASE = null, que es lo que deja intactas
     las comprobaciones de arriba. Aquí se fija a "" —el valor de PRODUCCIÓN— y se
     restaura al final.
     ══════════════════════════════════════════════════════════════════════════ */
  const fetchReal = window.fetch;
  let enVuelo = 0, maxEnVuelo = 0, peticiones = [], ultimaFoto = null, contador = 0;
  let responder = null;

  const jsonOk = () => new Response(
    JSON.stringify({ ok: true, n: String(++contador).padStart(8, "0") + ".JPG" }),
    { status: 200, headers: { "Content-Type": "application/json" } });

  // El banco real devuelve la foto retenida; aquí se devuelve la que acaba de
  // llegar por el multipart, así la verificación de integridad se ejercita entera.
  const responderOk = async (url, init) => {
    if (url.includes("/upload")) { ultimaFoto = init.body.get("foto"); return jsonOk(); }
    if (url.includes("/photo"))  return new Response(ultimaFoto, { status: 200 });
    if (url.includes("/list"))   return new Response("", { status: 200 });
    return new Response("no", { status: 404 });
  };

  window.fetch = async (url, init = {}) => {
    peticiones.push({ url: String(url), init });
    enVuelo++; maxEnVuelo = Math.max(maxEnVuelo, enVuelo);
    try { return await responder(String(url), init); } finally { enVuelo--; }
  };

  const subirYEsperar = async () => {
    $$("btnSubirTanda").click();
    return hasta(() => !cola.subiendo && !cola.corriendo, 15000);
  };

  // ── La compuerta. El valor de producción es "" y "" es FALSY ────────────────
  limpiarTanda();
  CFG.BASE = ""; aplicarModoRed();
  ok(!$$("btnUpload").hidden, "con BASE = \"\" los botones de subir se pintan (la compuerta no es por veracidad)");
  CFG.BASE = null; aplicarModoRed();
  ok($$("btnUpload").hidden, "y con null desaparecen");
  CFG.BASE = ""; aplicarModoRed();

  // ── Tanda completa ─────────────────────────────────────────────────────────
  responder = responderOk;
  await meter([vertical, horizontal, exacta]);
  await hasta(() => cola.items.length === 3 && cola.items.every(i => i.estado === "lista"));
  peticiones = []; maxEnVuelo = 0;
  await subirYEsperar();

  ok(cola.items.every(i => i.estado === "subida"), "las tres fotos subieron");
  ok(cola.items.map(i => i.nombreRemoto).join(",") === "00000001.JPG,00000002.JPG,00000003.JPG",
     "nombreRemoto guardado, y en el orden de la rejilla");
  ok(maxEnVuelo === 1, "NUNCA dos peticiones en vuelo: concurrencia de subida = 1");
  ok(cola.items.every(i => (i.verif || "").startsWith("ok")), "la verificación de integridad pasó en las tres");
  ok($$("hintTanda").textContent === "Ya están en el marco.", "y la barra lo dice en lenguaje llano");

  const subs = peticiones.filter(p => p.url.endsWith("/upload"));
  ok(subs.length === 3, `una petición de subida por foto (dio ${subs.length})`);
  ok(subs[0].init.method === "POST", "método POST");
  ok(subs[0].init.headers === undefined,
     "SIN cabeceras propias: una sola dispararía un OPTIONS que cae en onNotFound → 404");
  ok(subs[0].init.body.get("foto") instanceof Blob, "el campo del multipart se llama «foto»");

  // ── 413: no reintentable, y no tumba la tanda ──────────────────────────────
  limpiarTanda();
  await meter([vertical, horizontal, exacta]);
  await hasta(() => cola.items.length === 3 && cola.items.every(i => i.estado === "lista"));
  let k = 0;
  responder = async (url, init) =>
    (url.includes("/upload") && ++k === 2) ? new Response("", { status: 413 }) : responderOk(url, init);
  await subirYEsperar();

  ok(cola.items.filter(i => i.estado === "subida").length === 2, "un 413 en la foto 2 no tumba la tanda");
  ok(cola.items[1].estado === "fallo" && cola.items[1].reintentable === false,
     "la foto del 413 queda en FALLO y NO reintentable");
  ok(!/\d/.test(cola.items[1].error), "su mensaje al usuario no lleva un solo número");
  ok($$("btnSubirTanda").hidden, "y sin nada reintentable el botón de subir desaparece");
  ok(!$$("avisoFallos").hidden && /Tócala/.test($$("avisoFallos").textContent),
     "la rejilla avisa del fallo y manda al detalle, que es donde está el motivo");

  // ── Red caída: corta tras dos seguidos, el resto sigue en LISTA ─────────────
  limpiarTanda();
  await meter([await jpeg(600, 800, "r1.jpg"), await jpeg(600, 800, "r2.jpg"),
               await jpeg(600, 800, "r3.jpg"), await jpeg(600, 800, "r4.jpg")]);
  await hasta(() => cola.items.length === 4 && cola.items.every(i => i.estado === "lista"));
  responder = async () => { throw new TypeError("Failed to fetch"); };
  await subirYEsperar();

  ok(cola.items.filter(i => i.estado === "fallo").length === 2, "corta tras dos fallos de red seguidos");
  ok(cola.items.filter(i => i.estado === "lista").length === 2,
     "y las que no se intentaron siguen en LISTA, no en FALLO");
  ok(/Revisa el WiFi/.test($$("avisoFallos").textContent),
     "la tanda dice que se cortó por la red y qué hacer, no solo «tócalas»");
  responder = responderOk;
  await subirYEsperar();
  ok(cola.items.every(i => i.estado === "subida"), "volver a tocar «Subir» termina la tanda");

  // ── 503: reintentable, y grita en el diagnóstico ───────────────────────────
  limpiarTanda();
  await meter([vertical, horizontal]);
  await hasta(() => cola.items.length === 2 && cola.items.every(i => i.estado === "lista"));
  responder = async (url, init) =>
    url.includes("/upload") ? new Response("", { status: 503 }) : responderOk(url, init);
  await subirYEsperar();
  const f503 = cola.items[0];
  ok(f503.estado === "fallo" && f503.reintentable, "el 503 sí es reintentable");
  ok(/503/.test(f503.motivo) && /CLIENTE/.test(f503.motivo), "y el motivo técnico dice de quién es el bug");
  ok($$("hintTanda").textContent.startsWith("BUG DE LA PÁGINA"), "la barra lo grita bajo CFG.DIAG");
  ok(!/503/.test(f503.error), "pero el mensaje al usuario no menciona el código");

  // ── 200 con cuerpo que no es el del contrato ───────────────────────────────
  limpiarTanda();
  await meter([vertical, horizontal]);
  await hasta(() => cola.items.every(i => i.estado === "lista"));
  responder = async (url, init) =>
    url.includes("/upload") ? new Response("<html>", { status: 200 }) : responderOk(url, init);
  await subirYEsperar();
  ok(cola.items[0].estado === "fallo" && /200/.test(cola.items[0].motivo),
     "un 200 con cuerpo inválido es un fallo, no un éxito");

  // ── Timeout ────────────────────────────────────────────────────────────────
  limpiarTanda();
  CFG.TIMEOUT_MS = 200;
  await meter([vertical, horizontal]);
  await hasta(() => cola.items.every(i => i.estado === "lista"));
  responder = (url, init) => new Promise((_, rechaza) =>
    init.signal.addEventListener("abort", () => rechaza(new DOMException("Aborted", "AbortError"))));
  await subirYEsperar();
  ok(cola.items[0].estado === "fallo" && cola.items[0].reintentable, "el timeout deja la foto reintentable");
  ok(/sin respuesta/.test(cola.items[0].motivo), "y el motivo dice que venció el reloj, no que abortó nadie");
  CFG.TIMEOUT_MS = 10000;

  // ── Con una subida EN VUELO: quitar, recortar, y elegir más fotos ──────────
  limpiarTanda();
  await meter([vertical, horizontal]);
  await hasta(() => cola.items.every(i => i.estado === "lista"));
  let soltar = null;
  responder = (url, init) => {
    if (!url.includes("/upload")) return responderOk(url, init);
    ultimaFoto = init.body.get("foto");
    return new Promise(res => { soltar = () => res(jsonOk()); });
  };
  $$("btnSubirTanda").click();
  ok(await hasta(() => cola.items[0].estado === "subiendo"), "la primera foto entró en SUBIENDO");

  window.confirm = () => true;
  quitarFoto(cola.items[0].id);
  ok(cola.items.length === 2, "no se puede quitar una foto con su multipart en vuelo");

  abrirDetalle(cola.items[1].id);
  await abrirEditor(cola.items[1].id);
  ok(ed === null, "y tampoco se abre el editor de recuadro con el bucle de subida corriendo");
  cerrarDetalle();

  await meter([await jpeg(600, 800, "tardia.jpg")]);
  await esperar(60);
  ok(!cola.corriendo, "elegir fotos a media subida NO arranca la preparación en paralelo");
  ok(cola.items.some(i => i.estado === "pendiente"), "la nueva se queda en PENDIENTE, esperando");

  responder = responderOk;      // que la siguiente no se quede colgada también
  soltar();
  ok(await hasta(() => !cola.subiendo && !cola.corriendo, 15000), "la tanda terminó");
  ok(!cola.items.some(i => i.estado === "pendiente"),
     "y el finally del bucle de subida reanudó la preparación de lo elegido a destiempo");

  // ── Elegir más fotos descarta las que ya están en el marco ─────────────────
  responder = responderOk;
  await subirYEsperar();
  const subidasAntes = cola.items.filter(i => i.estado === "subida").length;
  ok(subidasAntes === 3, `las tres quedaron subidas (dio ${subidasAntes})`);
  await meter([await jpeg(600, 800, "nueva.jpg")]);
  await hasta(() => cola.items.length === 1 && cola.items[0].estado === "lista");
  ok(cola.items.length === 1, "elegir más fotos saca de la tanda las que ya están en el marco");

  /* ── Nada técnico con CFG.DIAG = false ─────────────────────────────────────
     El 503 es el peor caso: bajo diagnóstico grita el código y de quién es el bug,
     y en producción no puede quedar ni rastro de eso en ningún texto visible. */
  limpiarTanda();
  await meter([vertical, horizontal]);
  await hasta(() => cola.items.every(i => i.estado === "lista"));
  responder = async (url, init) =>
    url.includes("/upload") ? new Response("", { status: 503 }) : responderOk(url, init);
  await subirYEsperar();

  CFG.DIAG = false;
  abrirDetalle(cola.items[0].id);
  pintarBarra();
  const visible = [$$("caption").textContent, $$("aviso").textContent,
                   $$("estadoTanda").textContent, $$("hintTanda").textContent].join(" | ");
  ok($$("diagCard").hidden, "con CFG.DIAG = false el panel de diagnóstico no se pinta");
  ok(!/\d{3}|HTTP|BUG|byte|JPG/.test(visible), "y ningún texto visible deja rastro técnico — " + visible);
  cerrarDetalle();
  CFG.DIAG = true;

  window.fetch = fetchReal;
  CFG.BASE = null; aplicarModoRed();
  limpiarTanda();

  return { lineas, fallos };
})()
