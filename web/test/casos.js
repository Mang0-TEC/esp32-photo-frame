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
  $$("btnUpload").hidden = false;                   // etapa 5 simulada
  await esperar(50);
  const fila = id => Math.round($$(id).getBoundingClientRect().top);
  ok(fila("btnDownload") === fila("btnQuitarFoto") && fila("btnUpload") > fila("btnDownload"),
     "con «Subir al marco» los botones envuelven en vez de encogerse");
  $$("btnUpload").hidden = true;
  cerrarDetalle();

  return { lineas, fallos };
})()
