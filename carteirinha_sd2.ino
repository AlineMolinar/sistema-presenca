#include <Arduino.h>
#include <Adafruit_PN532.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <SD.h>

#include "data.hpp"
#include "rfid.hpp"
#include "ui.hpp"

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    40,41,39,42,45,48,47,21,14,5,6,7,15,16,4,8,3,46,9,1,0,
    210,30,16,0,22,13,10,1,12000000
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, bus);

void setup(){
    Serial.begin(115200);
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);

    Serial.println();
    Serial.println("Sistema Presenca");

    if (!gfx->begin()) {
        Serial.println("[ERRO] Display nao iniciou!");
        while (true) delay(1000);
    }
    gfx->fillScreen(0x0000);
    Serial.println("[OK] Display 800x480");

    // Inicializa o barramento SPI uma unica vez (SCK=12, MISO=13, MOSI=11)
    SPI.begin(12, 13, 11);
    Serial.println("[OK] SPI iniciado (SCK=12 MISO=13 MOSI=11)");

    // RFID usa CS=17, SD usa CS=14 — cada um com seu proprio CS
    rfid_iniciar();
    Serial.println(SD.begin(10) ? "[OK] SD CS=14" : "[ERRO] SD CS=14 FALHOU");

    if (!data_iniciar()) {
        gfx->fillScreen(0xF800);
        gfx->setTextColor(0xFFFF); gfx->setTextSize(2);
        gfx->setCursor(20, 20);
        gfx->println("ERRO: data.json nao encontrado!");
        while (true) delay(1000);
    }

    WiFi.begin("Verlab", "Verlab$Router");
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(500);

    if (WiFi.status() == WL_CONNECTED) {
    configTime(-3 * 3600, 0, "pool.ntp.org");

    // Aguarda o NTP sincronizar (até 5 segundos)
    struct tm info;
    uint32_t t_ntp = millis();
    while (!getLocalTime(&info) && millis() - t_ntp < 5000) delay(200);

    if (getLocalTime(&info)) {
        Serial.printf("[WiFi] NTP OK: %02d/%02d/%04d\n",
            info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);
    } else {
        Serial.println("[WiFi] NTP nao respondeu a tempo");
    }
} else {
    Serial.println("[WiFi] Sem conexao - data/hora nao sincronizada");
}
WiFi.disconnect(true);

    ui_iniciar(gfx);
    Serial.println("[OK] Sistema pronto.");
}

void loop(){
    ui_loop();
    static uint32_t ultimo_rfid = 0;

    if (millis() - ultimo_rfid >= 150) {
        ultimo_rfid = millis();
        if (!ui_teclado_aberto() && rfid_disponivel()) {
            String uid = rfid_leruid();
            ui_processar_rfid(uid);
        }
    }

    delay(5);
}

