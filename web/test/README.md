# Arnés de la página

Ejecuta `web/index.html` en Chrome sin cabeza y comprueba su comportamiento desde
dentro. **No forma parte de la página**: el empaquetado de la etapa 6 toma
únicamente `web/index.html`, y nada de esta carpeta puede acabar en PROGMEM.

## Correrlo

```bash
node web/test/correr.js
```

Sin `npm install`, sin dependencias. Solo hacen falta:

- **node ≥ 22**, por el `WebSocket` global — es lo único que se necesita para
  hablar el protocolo de depuración de Chrome sin instalar nada.
- **Chrome**. Se busca en la ruta de macOS; en otro sistema, o con otra
  instalación, se indica con la variable `CHROME`.

```bash
CHROME=/ruta/a/chrome node web/test/correr.js     # otra instalación
PUERTO=9444 node web/test/correr.js               # si 9333 está ocupado
node web/test/correr.js web/test/otro-archivo.js  # otro juego de casos
```

Sale con código **1** si algo falla, así que sirve tal cual en un hook o en CI.

## Qué comprueba

`casos.js` cubre el pipeline de preparación (las proporciones de §4 y §6, el
presupuesto por densidad, el aviso de franjas), el editor de recuadro completo
—relación fija, acotado del arrastre, tope de zoom por resolución, cancelar sin
mutar, confirmar, reversibilidad—, el historial de dos niveles, quitar una foto y
la renumeración de las posteriores, y que el bucle de la cola sobreviva a que se
quite una foto a media tanda.

El conductor además recoge los `console.assert` de la propia página —los de la
geometría del recorte, que corren tras `CFG.DIAG`— y los cuenta como fallos.

## Qué NO comprueba, y no puede

**La orientación EXIF.** Las fotos de los casos se generan con Canvas y no llevan
metadatos. Eso se mide a mano, en el teléfono, con el panel de diagnóstico — que
es justamente por lo que ese panel existe. Ver §4 de la especificación funcional.

**Nada de WebKit.** Esto es Chromium. Las conclusiones sobre Safari de iOS salen
de medir en el teléfono, no de aquí. Lo que este arnés atrapa son las regresiones:
que tocar `escalar()` o el bucle de la cola no rompa lo que ya funcionaba.

**El gesto real.** `touch-action`, `setPointerCapture` y la fluidez del arrastre
con un dedo se prueban en el teléfono. Los casos llaman a `mover()` y
`aplicarZoom()` directamente, o sea comprueban la geometría, no la entrada.

## Escribir casos nuevos

`casos.js` es **una expresión** que evalúa a una promesa de
`{ lineas: [...], fallos: n }`. Corre en el mismo contexto que el script de la
página, así que ve todo su ámbito: `cola`, `ed`, `abrirEditor`, `quitarFoto`.

Para la **etapa 5** no hace falta servidor: los fallos de red y los reintentos
parciales se provocan sustituyendo `window.fetch` dentro del archivo de casos,
que es más determinista que un servidor de verdad y funciona igual sobre `file://`.
