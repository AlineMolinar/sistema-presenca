#include "rfid.hpp"
#include <SPI.h>
#include <Adafruit_PN532.h>

static Adafruit_PN532 nfc(PN532_SS, &SPI);
static unsigned long t_ultima = 0;
static unsigned long t_ultima_tentativa = 0;
static String uid_pendente = "";

static String uid_para_string(const uint8_t *uid, uint8_t uidLength) {
    String uidStr = "";
    for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) uidStr += "0";
        uidStr += String(uid[i], HEX);
    }
    uidStr.toUpperCase();
    return uidStr;
}

void rfid_iniciar() {
    // SPI ja foi inicializado no setup() do .ino
    nfc.begin();

    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("[RFID] AVISO: PN532 nao detectado!");
    } else {
        Serial.printf("[RFID] PN532 OK (firmware 0x%02X)\n", (versiondata >> 24) & 0xFF);
        nfc.SAMConfig();
    }
}

bool rfid_disponivel() {
    if (uid_pendente.length() > 0) {
        return true;
    }

    unsigned long agora = millis();
    if (agora - t_ultima < RFID_COOLDOWN_MS || agora - t_ultima_tentativa < RFID_POLL_INTERVAL_MS) {
        return false;
    }
    t_ultima_tentativa = agora;

    uint8_t success;
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;

    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, PN532_READ_TIMEOUT_MS);
    if (success) {
        uid_pendente = uid_para_string(uid, uidLength);
        t_ultima = millis();
        Serial.printf("[RFID] UID lido: %s\n", uid_pendente.c_str());
    }
    return success;
}

String rfid_leruid() {
    if (uid_pendente.length() > 0) {
        String uidStr = uid_pendente;
        uid_pendente = "";
        return uidStr;
    }

    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, PN532_READ_TIMEOUT_MS)) {
        String uidStr = uid_para_string(uid, uidLength);
        t_ultima = millis();
        Serial.printf("[RFID] UID lido: %s\n", uidStr.c_str());
        return uidStr;
    }
    return "";
}
