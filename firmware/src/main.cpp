// src/main.cpp — solo para validar que las dependencias resuelven y compilan
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <BH1750.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <qrcode.h>

TFT_eSPI tft = TFT_eSPI();
SPIClass hspi(HSPI);
BH1750 lux;
AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);
  hspi.begin(14, 27, 13, 26);
  Wire.begin(21, 22);
}
void loop() {}