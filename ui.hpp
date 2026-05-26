#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// telas do sistema
typedef enum {
    TELA_LOGIN = 0,
    TELA_CADASTRO_PROF,
    TELA_TURMAS,
    TELA_TURMA,
    TELA_CONTA,
    N_TELAS
} TelaID;

// abas (em cada turma)
typedef enum {
    ABA_ATUAL = 0,
    ABA_PASSADAS,
    ABA_CADASTRO
} AbaID;

uint16_t hex_to_565(const char* hex);

void ui_iniciar(Arduino_RGB_Display* display);
// navegacao
void ui_navegar(TelaID id);
void ui_navegar_turma(const char* turmaCod);
void ui_set_aba(AbaID aba);

void ui_loop();
bool ui_teclado_aberto();

//processar e comparar uids
void ui_processar_rfid(const String& uid);
void ui_atualizar_contadores_chamada();


#endif
