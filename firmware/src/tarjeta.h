// src/tarjeta.h — La SD montada por el driver sdspi de ESP-IDF, no por SD.h
//
// ══ POR QUÉ NO SE USA LA LIBRERÍA `SD` DE ARDUINO ═════════════════════
//
// Porque no monta las tarjetas de este proyecto, y está medido en placa el
// 7-ago-2026 desde firmware/banco/, env:display. El fallo:
//
//   [W] ff_sd_initialize(): READ_OCR failed: 0
//   [E] sdcard_mount(): f_mount failed: (3) The physical drive cannot work
//
// Mecanismo, con los tres pasos medidos y no deducidos. `sd_diskio.cpp` manda
// CMD59 (CRC_ON_OFF) con argumento 1 de forma incondicional (:542) y después
// exige un OCR con el bit 20 puesto de un CMD58 hecho **todavía en estado idle**
// (:565). Rehaciendo la secuencia a mano sobre los mismos pines:
//
//   CMD59      OCR del primer CMD58 (en idle)
//   ───────    ──────────────────────────────
//   ninguno    0x40FF8000   ✓
//   arg 0      0x40FF8000   ✓   ← el control: MISMO número de comandos
//   arg 1      0x00000000   ✗
//
// El control es lo que lo cierra: con el CRC apagado explícitamente se manda un
// comando igual y el OCR sale bien, así que la variable es el CRC y no el tiempo
// ni un comando de más. Y los cuatro ceros no son basura — en SPI una tarjeta
// ocupada mantiene DO en bajo—, o sea que la tarjeta contesta «ocupada» a ese
// CMD58 concreto. El mismo CMD58 después de ACMD41 devuelve 0xC0FF8000 correcto,
// con el CRC igual de encendido.
//
// No se puede esquivar desde el código del proyecto: CMD59 es incondicional y
// `supports_crc` arranca en true (:776). Probado con DOS tarjetas distintas, y
// el master de arduino-esp32 tiene el mismo código, así que tampoco hay versión
// a la que subir.
//
// ══ QUÉ SE HACE EN SU LUGAR ══════════════════════════════════════════
//
// El driver sdspi del IDF, que ya viene en la plataforma —no es una dependencia
// nueva— y que hace ese paso CONDICIONAL: `sdmmc_init_spi_crc` solo corre si el
// host no lleva `SDMMC_HOST_FLAG_SPI_IGNORE_DATA_CRC`. El header lo dice literal:
// «SPI mode only: Do not enable CRC verification (skip CMD59)».
//
// El montaje se envuelve en un `fs::FS` sobre `VFSImpl`, que es exactamente lo
// que hace la librería `SD` por dentro (`SD.cpp`: `SDFS SD = SDFS(FSImplPtr(new
// VFSImpl()))`). Con eso `File`, `open()`, `exists()` y sobre todo el
// `request->send(fs, "/manifest.txt", "text/plain")` de ESPAsyncWebServer —el
// streaming sin heap de §3— siguen siendo el mismo código de antes.
//
// ══ LA PEGA, Y ES REAL ═══════════════════════════════════════════════
//
// El `SPIClass` de Arduino y el driver del IDF NO pueden tener el mismo
// periférico a la vez: Arduino usa `spiStartBus` de esp32-hal-spi y el IDF usa
// `spi_bus_initialize`, y son dos formas incompatibles de manejar SPI2. El banco
// necesita las dos —la sonda de comandos crudos es de Arduino— así que allá se
// turnan explícitamente. El marco solo usa ésta.
//
// El display no entra en el reparto: TFT_eSPI va en VSPI (SPI3) porque
// USE_HSPI_PORT no se define nunca. Aquí se toma SPI2, que es HSPI.

#pragma once

#include <FS.h>
#include <vfs_api.h>

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#define TARJETA_BASE "/sd"

// El impl se guarda aparte del `fs::FS` porque el punto de montaje se fija
// DESPUÉS de montar, y `fs::FS` solo expone el getter (FS.h:109). Es el mismo
// truco que usa SDFS::begin() con su `_impl->mountpoint(...)`.
static std::shared_ptr<VFSImpl> _tarjetaImpl = std::make_shared<VFSImpl>();
static fs::FS TARJETA(_tarjetaImpl);

static sdmmc_card_t *_tarjetaCard = nullptr;

static inline sdmmc_card_t *tarjetaCard() { return _tarjetaCard; }
static inline bool tarjetaMontada() { return _tarjetaCard != nullptr; }

static inline void desmontarTarjeta() {
  if (!_tarjetaCard) return;
  esp_vfs_fat_sdcard_unmount(TARJETA_BASE, _tarjetaCard);
  _tarjetaCard = nullptr;
  _tarjetaImpl->mountpoint(nullptr);
  spi_bus_free(SDSPI_DEFAULT_HOST);
}

// `hz` es la frecuencia de trabajo. NO afecta al handshake de init, que corre a
// 400 kHz por especificación pase lo que pase, así que remontar a otra
// frecuencia sí reproduce lo que hace producción al encender — al revés que
// SPI_FREQUENCY de TFT_eSPI, que es macro de compilación y obliga a reflashear.
static inline bool montarTarjeta(int sck, int miso, int mosi, int cs, uint32_t hz) {
  if (_tarjetaCard) return true;

  spi_bus_config_t bus = {};
  bus.mosi_io_num     = mosi;
  bus.miso_io_num     = miso;
  bus.sclk_io_num     = sck;
  bus.quadwp_io_num   = -1;
  bus.quadhd_io_num   = -1;
  bus.max_transfer_sz = 4092;

  const esp_err_t eBus = spi_bus_initialize(SDSPI_DEFAULT_HOST, &bus, SPI_DMA_CH_AUTO);
  // INVALID_STATE = ya estaba inicializado, que es lo normal al remontar. No es
  // un fallo y tratarlo como tal dejaría la tarjeta sin montar tras un `m`.
  if (eBus != ESP_OK && eBus != ESP_ERR_INVALID_STATE) return false;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot         = SDSPI_DEFAULT_HOST;
  host.max_freq_khz = (int)(hz / 1000);
  host.flags |= SDMMC_HOST_FLAG_SPI_IGNORE_DATA_CRC;   // ← salta el CMD59. Es TODO el arreglo.

  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.host_id = SDSPI_DEFAULT_HOST;
  dev.gpio_cs = (gpio_num_t)cs;

  esp_vfs_fat_mount_config_t cfg = VFS_FAT_MOUNT_DEFAULT_CONFIG();
  // max_files se queda en el default de 5, y SUBIRLO ES UNA TRAMPA CARA que ya se
  // pagó una vez: cada descriptor de FatFs arrastra su buffer de sector, así que
  // pasar de 5 a 12 costó ~30 KB de heap medidos en placa (el libre antes de
  // autoConnect cayó de 110,324 a 80,644). Y como el fallo de la galería es por
  // HEAP y no por descriptores —el abort() sale de un `new` que no puede ni
  // asignar la excepción—, subirlo empeora exactamente lo que se intentaba
  // arreglar: el marco pasó de caerse con ~30 conexiones a caerse con 4.
  // El tope de verdad es el de peticiones concurrentes: PHOTO_MAX_VUELO en main.cpp.
  // format_if_mount_failed se queda en false A PROPÓSITO: formatear sola la
  // tarjeta ante un montaje fallido borraría las fotos del marco, que es el
  // único sitio donde viven. Un fallo de montaje se reporta, no se "arregla".

  if (esp_vfs_fat_sdspi_mount(TARJETA_BASE, &host, &dev, &cfg, &_tarjetaCard) != ESP_OK) {
    _tarjetaCard = nullptr;
    spi_bus_free(SDSPI_DEFAULT_HOST);
    return false;
  }
  _tarjetaImpl->mountpoint(TARJETA_BASE);
  return true;
}

// Bytes totales y usados. `fs::FS` a secas no los tiene —eran de SDFS— y el
// equivalente del IDF es éste. Hacen falta para medir el cluster por consumo.
static inline bool tarjetaEspacio(uint64_t &total, uint64_t &usado) {
  uint64_t libre = 0;
  if (!_tarjetaCard || esp_vfs_fat_info(TARJETA_BASE, &total, &libre) != ESP_OK) return false;
  usado = total - libre;
  return true;
}
