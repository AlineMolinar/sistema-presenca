#ifndef RFID_H
#define RFID_H

#include <Arduino.h>

#define PN532_SCK   12 
#define PN532_MISO  13  
#define PN532_MOSI  11  
#define PN532_SS    17
#define RFID_COOLDOWN_MS 800
#define RFID_POLL_INTERVAL_MS 400
#define PN532_READ_TIMEOUT_MS 35

void rfid_iniciar();
bool rfid_disponivel();
String rfid_leruid();

#endif 


