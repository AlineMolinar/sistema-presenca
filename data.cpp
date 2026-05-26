#include "data.hpp"
#include <time.h>
#include <SD.h>
#include <SPI.h>

#define SD_CS_PIN 10

DynamicJsonDocument data_doc(JSON_DOC_SIZE);
bool data_lixo = false;

bool data_iniciar() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[DB] ERRO: SD nao montou");
        return false;
    }

    if (!SD.exists("/data.json")) {
        Serial.println("[DB] ERRO: /data.json nao encontrado no SD");
        Serial.println("[DB]       Copie o arquivo data.json para o cartao SD");
        return false;
    }

    File f = SD.open("/data.json", FILE_READ);
    if (!f) {
        Serial.println("[DB] ERRO: nao abriu o arquivo");
        return false;
    }

    DeserializationError err = deserializeJson(data_doc, f);
    f.close();
    if (err) {
        Serial.printf("[DB] ERRO: JSON invalido: %s\n", err.c_str());
        return false;
    }

    Serial.println("[DB] data.json carregado com sucesso");
    Serial.printf("[DB] %d professores | %d turmas | %d aulas\n",
                  (int)data_doc["professores"].size(),
                  (int)data_doc["turmas"].size(),
                  (int)data_doc["aulas"].size());
    return true;
}

bool salvar_data() {
    File f = SD.open("/data.json", FILE_WRITE);
    if (!f) {
        Serial.println("[DB] ERRO: nao salvou");
        return false;
    }

    serializeJson(data_doc, f);
    f.close();
    data_lixo = false;
    Serial.println("[DB] Salvo");
    return true;
}

JsonObject prof_por_mat(const char* mat) {
    for (JsonObject p : data_doc["professores"].as<JsonArray>()) {
        if (strcmp(p["matricula"] | "", mat) == 0) {
            return p;
        }
    }
    return JsonObject();
}

JsonObject prof_por_rfid(const char* uid) {
    if (!uid || strlen(uid) == 0) {
        return JsonObject();
    }

    for (JsonObject p : data_doc["professores"].as<JsonArray>()) {
        const char* rfid = p["rfid_uid"] | "";
        if (strlen(rfid) > 0 && strcmp(rfid, uid) == 0) {
            return p;
        }
    }
    return JsonObject();
}

bool rfid_prof(const char* mat, const char* uid) {
    JsonObject p = prof_por_mat(mat);
    if (p.isNull()) {
        return false;
    }

    p["rfid_uid"] = uid;
    data_lixo = true;
    return salvar_data();
}

JsonObject turma_por_codigo(const char* cod) {
    for (JsonObject t : data_doc["turmas"].as<JsonArray>()) {
        if (strcmp(t["codigo"] | "", cod) == 0) {
            return t;
        }
    }
    return JsonObject();
}

JsonObject aluno_por_mat(const char* turma_codigo, const char* mat) {
    JsonObject turma = turma_por_codigo(turma_codigo);
    if (turma.isNull()) {
        return JsonObject();
    }

    for (JsonObject a : turma["alunos"].as<JsonArray>()) {
        if (strcmp(a["matricula"] | "", mat) == 0) {
            return a;
        }
    }
    return JsonObject();
}

JsonObject aluno_por_rfid(const char* uid, char* turma_codigo) {
    if (!uid || strlen(uid) == 0) {
        return JsonObject();
    }

    for (JsonObject t : data_doc["turmas"].as<JsonArray>()) {
        for (JsonObject a : t["alunos"].as<JsonArray>()) {
            const char* auid = a["rfid_uid"] | "";
            if (strlen(auid) > 0 && strcmp(auid, uid) == 0) {
                if (turma_codigo) {
                    strncpy(turma_codigo, t["codigo"] | "", 15);
                    turma_codigo[15] = '\0';
                }
                return a;
            }
        }
    }
    return JsonObject();
}

bool rfid_aluno(const char* turma_codigo, const char* matricula, const char* nome, const char* uid) {
    JsonObject turma = turma_por_codigo(turma_codigo);
    if (turma.isNull()) {
        return false;
    }

    for (JsonObject a : turma["alunos"].as<JsonArray>()) {
        if (strcmp(a["matricula"] | "", matricula) == 0) {
            a["rfid_uid"] = uid;
            if (strlen(a["nome"] | "") == 0) {
                a["nome"] = nome;
            }
            data_lixo = true;
            return salvar_data();
        }
    }
    return false;
}

bool add_aluno(const char* turma_codigo, const char* mat, const char* nome) {
    JsonObject turma = turma_por_codigo(turma_codigo);
    if (turma.isNull()) {
        return false;
    }

    for (JsonObject a : turma["alunos"].as<JsonArray>()) {
        if (strcmp(a["matricula"] | "", mat) == 0) {
            return false;
        }
    }

    JsonArray alunos = turma["alunos"].as<JsonArray>();
    JsonObject novo = alunos.createNestedObject();
    novo["matricula"] = mat;
    novo["nome"] = nome;
    novo["rfid_uid"] = "";
    data_lixo = true;
    return salvar_data();
}

void data_hoje(char* buf, size_t tamanho) {
    time_t agora = time(nullptr);
    struct tm info_tempo;
    localtime_r(&agora, &info_tempo);
    strftime(buf, tamanho, "%Y-%m-%d", &info_tempo);
}

JsonObject aula_hoje(const char* turma_codigo) {
    char hoje[16];
    data_hoje(hoje, sizeof(hoje));
    return aula_por_data(turma_codigo, hoje);
}

JsonObject aula_por_data(const char* turma_codigo, const char* data) {
    // Rejeita datas invalidas (ano < 2000)
    if (!data || strlen(data) < 4 || strncmp(data, "20", 2) != 0) {
        Serial.printf("[DB] AVISO: data invalida ignorada: %s\n", data ? data : "null");
        return JsonObject();
    }
    char id[32];
    snprintf(id, sizeof(id), "%s_%s", turma_codigo, data);
 
    for (JsonObject au : data_doc["aulas"].as<JsonArray>()) {
        if (strcmp(au["id"] | "", id) == 0) {
            return au;
        }
    }
 
    JsonArray aulas = data_doc["aulas"].as<JsonArray>();
    JsonObject nova = aulas.createNestedObject();
    nova["id"] = id;
    nova["turma"] = turma_codigo;
    nova["data"] = data;
    JsonObject presencas = nova.createNestedObject("presencas");
 
    JsonObject turma = turma_por_codigo(turma_codigo);
    if (!turma.isNull()) {
        for (JsonObject a : turma["alunos"].as<JsonArray>()) {
            presencas[a["matricula"].as<const char*>()] = false;
        }
    }
 
    data_lixo = true;
    salvar_data();
    return nova;
}

bool presenca(const char* turma_codigo, const char* data, const char* mat, bool presente) {
    JsonObject aula = aula_por_data(turma_codigo, data);
    if (aula.isNull()) {
        return false;
    }

    aula["presencas"][mat] = presente;
    data_lixo = true;
    return salvar_data();
}

InfoTurma info_turma(const char* turma_codigo) {
    InfoTurma e = {0, 0, 0.0f};
    JsonObject turma = turma_por_codigo(turma_codigo);
    if (turma.isNull()) {
        return e;
    }

    e.total_alunos = turma["alunos"].size();

    uint16_t soma = 0;
    uint16_t total_slots = 0;
    for (JsonObject aula : data_doc["aulas"].as<JsonArray>()) {
        if (strcmp(aula["turma"] | "", turma_codigo) != 0) {
            continue;
        }

        e.total_aulas++;
        for (JsonPair kv : aula["presencas"].as<JsonObject>()) {
            total_slots++;
            if (kv.value().as<bool>()) {
                soma++;
            }
        }
    }

    e.media_presenca = total_slots > 0 ? (float)soma / total_slots : 0.0f;
    return e;
}