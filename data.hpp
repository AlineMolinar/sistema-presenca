#ifndef DATA_H
#define DATA_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>

#define JSON_DOC_SIZE 32768

extern DynamicJsonDocument data_doc;
extern bool data_lixo;

bool data_iniciar();
bool salvar_data();


JsonObject prof_por_mat(const char* mat);
JsonObject prof_por_rfid(const char* uid);
bool rfid_prof(const char* mat, const char* uid);
JsonObject turma_por_codigo(const char* cod);
JsonObject aluno_por_mat(const char* turma_codigo, const char* mat);
JsonObject aluno_por_rfid(const char* uid, char* turma_codigo);
bool rfid_aluno(const char* turma_codigo, const char* matricula, const char* nome, const char* uid);
bool add_aluno(const char* turma_codigo, const char* mat, const char* nome);
void data_hoje(char* buf, size_t tamanho);
JsonObject aula_hoje(const char* turma_codigo);
JsonObject aula_por_data(const char* turma_codigo, const char* data);
bool presenca(const char* turma_codigo, const char* data, const char* mat, bool presente);

struct InfoTurma{
    uint16_t total_aulas;
    uint16_t total_alunos;
    float media_presenca;
};

InfoTurma info_turma(const char* turma_codigo);

#endif