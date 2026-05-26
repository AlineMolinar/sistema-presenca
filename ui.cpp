#include "ui.hpp"
#include "data.hpp"
#include "rfid.hpp"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <TAMC_GT911.h>


#define SCR_W 480
#define SCR_H 272

#define TOUCH_GT911
#define TOUCH_GT911_SDA 19
#define TOUCH_GT911_SCL 20
#define TOUCH_GT911_INT -1
#define TOUCH_GT911_RST 38
#define TOUCH_GT911_ROTATION ROTATION_NORMAL
#define TOUCH_MAP_X1 480
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 272
#define TOUCH_MAP_Y2 0

#ifndef ROTATION_NORMAL
#define ROTATION_NORMAL 0
#endif

// criando e inicializando o touchscreen
static TAMC_GT911 ts = TAMC_GT911(
    TOUCH_GT911_SDA, TOUCH_GT911_SCL,
    TOUCH_GT911_INT, TOUCH_GT911_RST,
    800, 480
);

static void touch_init() {
    Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
    Wire.setClock(400000);
    ts.begin();
    ts.setRotation(TOUCH_GT911_ROTATION);
}

static bool touch_get(int16_t *x, int16_t *y) {
    static bool was_touched = false;
    static uint32_t release_time = 0;
    static int16_t last_x = 0, last_y = 0;

    ts.read();

    if (ts.isTouched) {
        was_touched = true;
        last_x = (int16_t)constrain(map(ts.points[0].x, 795, 339, 0, SCR_W - 1), 0, SCR_W - 1);
        last_y = (int16_t)constrain(map(ts.points[0].y, 471, 209, 0, SCR_H - 1), 0, SCR_H - 1);
        *x = last_x;
        *y = last_y;
        return true;
    }

    if (was_touched) {
        was_touched = false;
        release_time = millis();
    }

    return false;
}

// criacao variaveis a serem usadas pelo lvgl

static Arduino_RGB_Display *gfx = nullptr;
static TelaID tela_atual = TELA_LOGIN;
static AbaID aba_atual = ABA_ATUAL;
static String prof_logado_mat = "";
static String turmaatual_cod = "";
static String rfid_pendente = "";
static String edit_mat = "";

static char cad_prof_mat_buf[16] = "";
static char cad_aluno_mat_buf[16] = "";
static char cad_aluno_nome_buf[48] = "";

static bool conta_aguardando_rfid = false;
static bool modal_edit_aberto = false;
static uint32_t ultimo_tick_lvgl = 0;

static lv_color_t *lv_buf_1 = nullptr;
static lv_color_t *lv_buf_2 = nullptr;
static lv_display_t *lv_display = nullptr;
static lv_indev_t *touch_indev = nullptr;

static lv_style_t style_screen;
static lv_style_t style_card;
static lv_style_t style_header;
static lv_style_t style_btn_primary;
static lv_style_t style_btn_outline;
static lv_style_t style_btn_warn;
static lv_style_t style_input;
static lv_style_t style_chip;
static lv_style_t style_tab_active;
static lv_style_t style_tab_idle;

static lv_obj_t *screen_root = nullptr;
static lv_obj_t *screen_header = nullptr;
static lv_obj_t *screen_body = nullptr;
static lv_obj_t *screen_footer = nullptr;
static lv_obj_t *kbd = nullptr;
static lv_obj_t *active_textarea = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *previous_screen = nullptr;

static const uint32_t COLOR_BG = 0xEAF1F4;
static const uint32_t COLOR_SURFACE = 0xFFFFFF;
static const uint32_t COLOR_PRIMARY = 0x80300A;
static const uint32_t COLOR_PRIMARY_DARK = 0x502005;
static const uint32_t COLOR_ACCENT = 0x129CF3;
static const uint32_t COLOR_TEXT = 0x231F1B;
static const uint32_t COLOR_MUTED = 0x80726B;
static const uint32_t COLOR_BORDER = 0xDDD8D4;
static const uint32_t COLOR_SUCCESS = 0x559D1F;
static const uint32_t COLOR_DANGER = 0x4545D6;
static const uint32_t COLOR_SOFT = 0xF5EEE9;

static uint32_t lvgl_tick_cb() {
    return millis();
}

// acoes a serem feitas
enum ActionID {
    ACT_NONE = 0,
    ACT_GO_TURMAS,
    ACT_GO_CONTA,
    ACT_BACK_LOGIN,
    ACT_CONFIRM_CAD_PROF,
    ACT_CAD_ALUNO,
    ACT_CONTA_RFID,
    ACT_CLOSE_MODAL,
    ACT_LOGOUT,
    ACT_TAB_ATUAL,
    ACT_TAB_PASSADAS,
    ACT_TAB_CADASTRO
};

// troca r e g pois no display eh usado bgr
uint16_t hex_to_565(const char *hex) {
    if (!hex || hex[0] != '#') return 0x781F;

    unsigned long v = strtoul(hex + 1, nullptr, 16);
    uint8_t r = (v >> 16) & 0xFF;
    uint8_t g = (v >> 8) & 0xFF;
    uint8_t b = v & 0xFF;
    return ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
}
static lv_color_t lv_color_from_hex(const char *hex) {
    if (!hex || hex[0] != '#') return lv_color_hex(0x7C4DFF);
    return lv_color_hex(strtoul(hex + 1, nullptr, 16));
}

// area em que sera a tela em si
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    uint32_t n = w * h;
    uint16_t *buf = (uint16_t *)px_map;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = buf[i];
        uint16_t r = (c >> 11) & 0x1F;
        uint16_t g = (c >> 5)  & 0x3F;
        uint16_t b =  c        & 0x1F;
        buf[i] = (b << 11) | (g << 5) | r;
    }
    gfx->draw16bitRGBBitmap(area->x1, area->y1, buf, w, h);
    lv_display_flush_ready(disp);

}// leitura do touch
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void) indev;
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    int16_t x = 0;
    int16_t y = 0;
    if (touch_get(&x, &y)) {
        x = constrain(x, 0, SCR_W - 1);
        y = constrain(y, 0, SCR_H - 1);
        last_x = x;
        last_y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        static uint32_t last_touch_log = 0;
        if (millis() - last_touch_log > 250) {
            Serial.printf("[TOUCH] x=%d y=%d\n", x, y);
            last_touch_log = millis();
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

static void init_styles() {
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(COLOR_BG));
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&style_screen, lv_color_hex(COLOR_TEXT));
    lv_style_set_pad_all(&style_screen, 0);

    lv_style_init(&style_card);
    lv_style_set_radius(&style_card, 18);
    lv_style_set_bg_color(&style_card, lv_color_hex(COLOR_SURFACE));
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_border_color(&style_card, lv_color_hex(COLOR_BORDER));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_shadow_width(&style_card, 20);
    lv_style_set_shadow_opa(&style_card, LV_OPA_20);
    lv_style_set_shadow_color(&style_card, lv_color_hex(0x000000));
    lv_style_set_pad_all(&style_card, 12);

    lv_style_init(&style_header);
    lv_style_set_bg_color(&style_header, lv_color_hex(COLOR_PRIMARY));
    lv_style_set_bg_grad_color(&style_header, lv_color_hex(COLOR_PRIMARY_DARK));
    lv_style_set_bg_grad_dir(&style_header, LV_GRAD_DIR_HOR);
    lv_style_set_border_width(&style_header, 0);
    lv_style_set_pad_all(&style_header, 10);
    lv_style_set_text_color(&style_header, lv_color_hex(0xFFFFFF));

    lv_style_init(&style_btn_primary);
    lv_style_set_radius(&style_btn_primary, 14);
    lv_style_set_bg_color(&style_btn_primary, lv_color_hex(COLOR_PRIMARY));
    lv_style_set_bg_opa(&style_btn_primary, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_primary, 0);
    lv_style_set_text_color(&style_btn_primary, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_ver(&style_btn_primary, 8);
    lv_style_set_pad_hor(&style_btn_primary, 16);

    lv_style_init(&style_btn_outline);
    lv_style_set_radius(&style_btn_outline, 14);
    lv_style_set_bg_color(&style_btn_outline, lv_color_hex(COLOR_SURFACE));
    lv_style_set_bg_opa(&style_btn_outline, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_outline, lv_color_hex(COLOR_PRIMARY));
    lv_style_set_border_width(&style_btn_outline, 2);
    lv_style_set_text_color(&style_btn_outline, lv_color_hex(COLOR_PRIMARY));
    lv_style_set_pad_ver(&style_btn_outline, 8);
    lv_style_set_pad_hor(&style_btn_outline, 16);

    lv_style_init(&style_btn_warn);
    lv_style_set_radius(&style_btn_warn, 14);
    lv_style_set_bg_color(&style_btn_warn, lv_color_hex(COLOR_ACCENT));
    lv_style_set_bg_opa(&style_btn_warn, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_warn, 0);
    lv_style_set_text_color(&style_btn_warn, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_ver(&style_btn_warn, 8);
    lv_style_set_pad_hor(&style_btn_warn, 16);

    lv_style_init(&style_input);
    lv_style_set_radius(&style_input, 12);
    lv_style_set_bg_color(&style_input, lv_color_hex(COLOR_SOFT));
    lv_style_set_bg_opa(&style_input, LV_OPA_COVER);
    lv_style_set_border_color(&style_input, lv_color_hex(COLOR_BORDER));
    lv_style_set_border_width(&style_input, 1);
    lv_style_set_pad_all(&style_input, 8);
    lv_style_set_text_color(&style_input, lv_color_hex(COLOR_TEXT));

    lv_style_init(&style_chip);
    lv_style_set_radius(&style_chip, 999);
    lv_style_set_bg_color(&style_chip, lv_color_hex(COLOR_SOFT));
    lv_style_set_bg_opa(&style_chip, LV_OPA_COVER);
    lv_style_set_pad_ver(&style_chip, 6);
    lv_style_set_pad_hor(&style_chip, 12);

    lv_style_init(&style_tab_active);
    lv_style_set_radius(&style_tab_active, 12);
    lv_style_set_bg_color(&style_tab_active, lv_color_hex(COLOR_PRIMARY));
    lv_style_set_text_color(&style_tab_active, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&style_tab_active, 0);

    lv_style_init(&style_tab_idle);
    lv_style_set_radius(&style_tab_idle, 12);
    lv_style_set_bg_color(&style_tab_idle, lv_color_hex(0xFFFFFF));
    lv_style_set_text_color(&style_tab_idle, lv_color_hex(COLOR_MUTED));
    lv_style_set_border_color(&style_tab_idle, lv_color_hex(COLOR_BORDER));
    lv_style_set_border_width(&style_tab_idle, 1);
}

static void clear_status() {
    if (status_label) lv_label_set_text(status_label, "");
}
static void hide_keyboard();
static void show_status(const char *text, uint32_t color);

static void activate_screen() {
    lv_scr_load_anim(screen_root, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    if (previous_screen && previous_screen != screen_root) {
        lv_obj_del_async(previous_screen);
    }
    previous_screen = screen_root;
}

//criar um texto
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    if (font) lv_obj_set_style_text_font(label, font, 0);
    return label;
}

// criar botao
static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_style_t *style, ActionID action) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, style, 0);
    lv_obj_set_height(btn, 38);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        ActionID action_id = (ActionID)(uintptr_t)lv_event_get_user_data(e);
        switch (action_id) {
            case ACT_GO_TURMAS:
                ui_navegar(TELA_TURMAS);
                break;
            case ACT_GO_CONTA:
                ui_navegar(TELA_CONTA);
                break;
            case ACT_BACK_LOGIN:
                rfid_pendente = "";
                ui_navegar(TELA_LOGIN);
                break;
            case ACT_CONFIRM_CAD_PROF:
                hide_keyboard();
                if (strlen(cad_prof_mat_buf) == 0) {
                    show_status("Digite a matricula do professor.", COLOR_DANGER);
                } else if (rfid_pendente.length() == 0) {
                    show_status("Passe a carteirinha antes de confirmar.", COLOR_DANGER);
                } else if (rfid_prof(cad_prof_mat_buf, rfid_pendente.c_str())) {
                    prof_logado_mat = cad_prof_mat_buf;
                    rfid_pendente = "";
                    ui_navegar(TELA_TURMAS);
                } else {
               show_status("Matricula nao encontrada no sistema.", COLOR_DANGER);
                }
                break;
            case ACT_CAD_ALUNO:
                if (strlen(cad_aluno_mat_buf) > 0 && strlen(cad_aluno_nome_buf) > 0 && rfid_pendente.length() > 0) {
                    // bool ok = rfid_aluno(
                    //     turmaatual_cod.c_str(),
                    //     cad_aluno_mat_buf,
                    //     cad_aluno_nome_buf,
                    //     rfid_pendente.c_str()
                    // );
                    // if (!ok) {
                    //     add_aluno(turmaatual_cod.c_str(), cad_aluno_mat_buf, cad_aluno_nome_buf);
                    //     rfid_aluno(
                    //         turmaatual_cod.c_str(),
                    //         cad_aluno_mat_buf,
                    //         cad_aluno_nome_buf,
                    //         rfid_pendente.c_str()
                    //     );
                    // }
                    JsonObject aluno = aluno_por_mat(turmaatual_cod.c_str(), cad_aluno_mat_buf);
                    if (aluno.isNull()) {
                        show_status("Matricula nao encontrada nesta turma.", COLOR_DANGER);
                        rfid_pendente = "";
                        memset(cad_aluno_mat_buf, 0, sizeof(cad_aluno_mat_buf));
                        break;
                    }
                     bool ok = rfid_aluno(
                        turmaatual_cod.c_str(),
                        cad_aluno_mat_buf,
                        aluno["nome"] | "",
                        rfid_pendente.c_str()
                    );
                    if (ok) {
                        show_status("Carteirinha vinculada com sucesso!", COLOR_SUCCESS);
                        Serial.println("[CAD] Aluno cadastrado!");
                    } else {
                        show_status("Erro ao vincular carteirinha.", COLOR_DANGER);
                    }


                    rfid_pendente = "";
                    memset(cad_aluno_mat_buf, 0, sizeof(cad_aluno_mat_buf));
                    memset(cad_aluno_nome_buf, 0, sizeof(cad_aluno_nome_buf));
                    Serial.println("[CAD] Aluno cadastrado!");
                    ui_navegar(TELA_TURMA);
                }
                break;
            case ACT_CONTA_RFID:
                conta_aguardando_rfid = true;
                ui_navegar(TELA_CONTA);
                break;
            case ACT_CLOSE_MODAL:
                modal_edit_aberto = false;
                edit_mat = "";
                ui_navegar(TELA_TURMA);
                break;
            case ACT_LOGOUT:
                prof_logado_mat = "";
                rfid_pendente = "";
                ui_navegar(TELA_LOGIN);
                break;
            case ACT_TAB_ATUAL:
                ui_set_aba(ABA_ATUAL);
                break;
            case ACT_TAB_PASSADAS:
                ui_set_aba(ABA_PASSADAS);
                break;
            case ACT_TAB_CADASTRO:
                ui_set_aba(ABA_CADASTRO);
                break;
            default:
                break;
        }
    }, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void hide_keyboard() {
    if (kbd) lv_obj_add_flag(kbd, LV_OBJ_FLAG_HIDDEN);
    active_textarea = nullptr;
}

static void sync_textarea_to_buffer(lv_event_t *e) {
    char *buf = (char *)lv_event_get_user_data(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    const char *txt = lv_textarea_get_text(ta);
    if (buf) {
        strncpy(buf, txt, lv_textarea_get_max_length(ta));
        buf[lv_textarea_get_max_length(ta)] = '\0';
    }
}

static void textarea_focus_cb(lv_event_t *e) {
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    if (!kbd) return;

    active_textarea = ta;
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(kbd, ta);
    lv_keyboard_set_mode(kbd, (lv_keyboard_mode_t)(uintptr_t)lv_event_get_user_data(e));
    lv_obj_clear_flag(kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kbd);
}

static lv_obj_t *make_textarea(
    lv_obj_t *parent,
    const char *placeholder,
    char *buf,
    uint16_t max_len,
    lv_keyboard_mode_t mode
) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_remove_style_all(ta);
    lv_obj_add_style(ta, &style_input, 0);
    lv_obj_set_width(ta, lv_pct(100));
    lv_obj_set_height(ta, 36);
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len - 1);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_text(ta, buf);
    lv_obj_add_event_cb(ta, textarea_focus_cb, LV_EVENT_FOCUSED, (void *)(uintptr_t)mode);
    lv_obj_add_event_cb(ta, textarea_focus_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);
    lv_obj_add_event_cb(ta, sync_textarea_to_buffer, LV_EVENT_VALUE_CHANGED, buf);
    return ta;
}

static void keyboard_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        hide_keyboard();
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// criacao da tela com header e footer 
static void create_shell(bool show_footer) {
    screen_root = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen_root);
    lv_obj_add_style(screen_root, &style_screen, 0);
    lv_obj_set_size(screen_root, SCR_W, SCR_H);

    screen_header = lv_obj_create(screen_root);
    lv_obj_remove_style_all(screen_header);
    lv_obj_add_style(screen_header, &style_header, 0);
    lv_obj_set_size(screen_header, SCR_W, 54);
    lv_obj_align(screen_header, LV_ALIGN_TOP_MID, 0, 0);

    screen_body = lv_obj_create(screen_root);
    lv_obj_remove_style_all(screen_body);
    lv_obj_set_size(screen_body, SCR_W, show_footer ? 172 : 212);
    lv_obj_align(screen_body, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(screen_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen_body, 0, 0);
    lv_obj_set_style_pad_all(screen_body, 0, 0);
    lv_obj_set_scroll_dir(screen_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(screen_body, LV_SCROLLBAR_MODE_AUTO);

    if (show_footer) {
        screen_footer = lv_obj_create(screen_root);
        lv_obj_remove_style_all(screen_footer);
        lv_obj_set_size(screen_footer, SCR_W, 40);
        lv_obj_align(screen_footer, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(screen_footer, lv_color_hex(COLOR_PRIMARY_DARK), 0);
        lv_obj_set_style_bg_opa(screen_footer, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen_footer, 5, 0);
        lv_obj_clear_flag(screen_footer, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *left = make_button(screen_footer, "Turmas", &style_btn_outline, ACT_GO_TURMAS);
        lv_obj_set_size(left, 130, 30);
        lv_obj_align(left, LV_ALIGN_LEFT_MID, 54, 0);
        lv_obj_set_style_bg_color(left, lv_color_hex(COLOR_PRIMARY_DARK), 0);
        lv_obj_set_style_text_color(left, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(left, lv_color_hex(0xFFFFFF), 0);

        lv_obj_t *right = make_button(screen_footer, "Conta", &style_btn_outline, ACT_GO_CONTA);
        lv_obj_set_size(right, 130, 30);
        lv_obj_align(right, LV_ALIGN_RIGHT_MID, -54, 0);
        lv_obj_set_style_bg_color(right, lv_color_hex(COLOR_PRIMARY_DARK), 0);
        lv_obj_set_style_text_color(right, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(right, lv_color_hex(0xFFFFFF), 0);
    } else {
        screen_footer = nullptr;
    }

    kbd = lv_keyboard_create(screen_root);
    lv_obj_set_size(kbd, SCR_W, 120);
    lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kbd, keyboard_event_cb, LV_EVENT_ALL, NULL);
}

// criar header
static void set_header(const char *title, const char *subtitle) {
    make_label(screen_header, title, lv_color_hex(0x000000), &lv_font_montserrat_20);
    if (subtitle && subtitle[0]) {
        lv_obj_t *sub = make_label(screen_header, subtitle, lv_color_hex(0x1565C0), &lv_font_montserrat_14);
        lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 0, 26);
    }
}

static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &style_card, 0);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void show_status(const char *text, uint32_t color) {
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
}

static void open_edit_modal() {
    lv_obj_t *overlay = lv_obj_create(screen_root);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *modal = make_card(overlay, 20, 22, 440, 228);
    make_label(modal, "Editar Presencas", lv_color_hex(COLOR_PRIMARY), &lv_font_montserrat_20);

    JsonObject al = aluno_por_mat(turmaatual_cod.c_str(), edit_mat.c_str());
    if (!al.isNull()) {
        char info[96];
        snprintf(info, sizeof(info), "%s (%s)", al["nome"] | "", edit_mat.c_str());
        lv_obj_t *label = make_label(modal, info, lv_color_hex(COLOR_TEXT), LV_FONT_DEFAULT);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 30);
    }

    lv_obj_t *list = lv_obj_create(modal);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 408, 126);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(list, 10, 0);

    // Garante que aula de hoje exista
    char hoje_modal[16];
    data_hoje(hoje_modal, sizeof(hoje_modal));
    if (strncmp(hoje_modal, "20", 2) == 0) {
        aula_hoje(turmaatual_cod.c_str());
    }

    // Buffers estáticos para os ponteiros passados ao lambda
    static char datas_modal[3][16];
    int count = 0;
    for (JsonObject au : data_doc["aulas"].as<JsonArray>()) {
        if (strcmp(au["turma"] | "", turmaatual_cod.c_str()) != 0) continue;
        const char* d = au["data"] | "";
        if (strncmp(d, "20", 2) != 0) continue;  // ignora datas invalidas
        if (count >= 3) break;

        // Copia a data para buffer estático — o ponteiro do JSON pode ser invalidado
        strncpy(datas_modal[count], d, 15);
        datas_modal[count][15] = '\0';

        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 408, 34);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_SOFT), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        make_label(row, datas_modal[count], lv_color_hex(COLOR_TEXT), &lv_font_montserrat_14);

        bool ok = au["presencas"][edit_mat.c_str()].as<bool>();
        lv_obj_t *toggle = lv_btn_create(row);
        lv_obj_set_size(toggle, 120, 28);
        lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(toggle, 10, 0);
        lv_obj_set_style_bg_color(toggle, lv_color_hex(ok ? COLOR_SUCCESS : COLOR_DANGER), 0);

        // Passa o ponteiro estático — seguro para o lambda
        lv_obj_add_event_cb(toggle, [](lv_event_t *e) {
            const char *data = (const char *)lv_event_get_user_data(e);
            JsonObject aula = aula_por_data(turmaatual_cod.c_str(), data);
            if (aula.isNull()) return;
            bool presente_atual = aula["presencas"][edit_mat.c_str()].as<bool>();
            presenca(turmaatual_cod.c_str(), data, edit_mat.c_str(), !presente_atual);
            ui_navegar(TELA_TURMA);
        }, LV_EVENT_CLICKED, (void *)datas_modal[count]);

        lv_obj_t *txt = lv_label_create(toggle);
        lv_label_set_text(txt, ok ? "PRESENTE" : "FALTA");
        lv_obj_center(txt);
        count++;
    }

    lv_obj_t *close_btn = make_button(modal, "Fechar", &style_btn_primary, ACT_CLOSE_MODAL);
    lv_obj_set_size(close_btn, 110, 34);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

// TELA LOGIN
static void render_login() {
    create_shell(false);
    lv_obj_set_style_bg_color(screen_root, lv_color_hex(COLOR_PRIMARY_DARK), 0);

    //card
    lv_obj_t *hero = make_card(screen_root, 0, 0, 400, 240); 
    lv_obj_center(hero);
    lv_obj_set_style_radius(hero, 22, 0);
    
    // titulo
    lv_obj_t *title = make_label(hero, "LOGIN", lv_color_hex(COLOR_TEXT), &lv_font_montserrat_28);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40); // Alinhado ao topo do hero

    lv_obj_t *headline = make_label(hero, "Aproxime sua carteirinha", lv_color_hex(COLOR_PRIMARY), &lv_font_montserrat_28);
    lv_obj_align(headline, LV_ALIGN_OUT_RIGHT_TOP, 15, 90);

    activate_screen();
}

// TELA CADASTRO PROFESSOR 
static void render_cadastro_prof() {
    create_shell(false);

    //titulo
    lv_obj_t *title = make_label(screen_root, "Cadastro de Professor", lv_color_hex(COLOR_TEXT), &lv_font_montserrat_20);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    //card
    lv_obj_t *card = make_card(screen_body, 0, 0, 430, 204);
    lv_obj_center(card);
    lv_obj_t *left = lv_obj_create(card);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, 390, 180);
    lv_obj_align(left, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, 6, 0);

    //campo da matricula (com teclado)
    make_label(left, "Matricula", lv_color_hex(COLOR_TEXT), LV_FONT_DEFAULT);
    make_textarea(left, "Ex.: 123456", cad_prof_mat_buf, sizeof(cad_prof_mat_buf), LV_KEYBOARD_MODE_NUMBER);

    //aparece o uid 
    make_label(left, "Carteirinha", lv_color_hex(COLOR_TEXT), &lv_font_montserrat_16);
    lv_obj_t *rfid_box = lv_obj_create(left);
    lv_obj_remove_style_all(rfid_box);
    lv_obj_add_style(rfid_box, &style_input, 0);
    lv_obj_set_width(rfid_box, lv_pct(100));
    lv_obj_set_height(rfid_box, 40);
    lv_obj_clear_flag(rfid_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(rfid_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *rfid_label = lv_label_create(rfid_box);
    lv_label_set_text_fmt(rfid_label, "%s", rfid_pendente.length() > 0 ? rfid_pendente.c_str() : "Aguardando carteirinha...");
    lv_obj_set_style_text_color(rfid_label, lv_color_hex(rfid_pendente.length() > 0 ? COLOR_SUCCESS : COLOR_MUTED), 0);
    lv_obj_center(rfid_label);

    //confirmar 
    lv_obj_t *confirm = make_button(left, "Confirmar Cadastro", &style_btn_primary, ACT_CONFIRM_CAD_PROF);
    lv_obj_set_size(confirm, lv_pct(100), 38);

    activate_screen();
}

static void open_turma_event_cb(lv_event_t *e) {
    const char *cod = (const char *)lv_event_get_user_data(e);
    ui_navegar_turma(cod);
}

// TELA TURMAS
static void render_turmas() {
    create_shell(true);

    JsonObject prof = prof_por_mat(prof_logado_mat.c_str());
    char subtitle[64] = "";
    if (!prof.isNull()) snprintf(subtitle, sizeof(subtitle), "Prof. %s", prof["nome"] | "");
    set_header("Minhas Turmas", subtitle);

    if (prof.isNull()) {
        activate_screen();
        return;
    }

    JsonArray turmas_prof = prof["turmas"].as<JsonArray>();
    int i = 0;
    for (JsonVariant v : turmas_prof) {
        const char *cod = v.as<const char *>();
        JsonObject t = turma_por_codigo(cod);
        if (t.isNull()) continue;

        int col = i % 2;
        int row = i / 2;
        lv_obj_t *card = make_card(screen_body, 12 + col * 232, row * 84, 224, 76);
        lv_obj_add_event_cb(card, open_turma_event_cb, LV_EVENT_CLICKED, (void *)cod);

        lv_obj_t *band = lv_obj_create(card);
        lv_obj_remove_style_all(band);
        lv_obj_set_size(band, 224, 6);
        lv_obj_align(band, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(band, lv_color_from_hex(t["cor"] | "#7C4DFF"), 0);
        lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);

        // icone do lado do nome da turma
        lv_obj_t *icon = lv_obj_create(card);
        lv_obj_remove_style_all(icon);
        lv_obj_set_size(icon, 34, 34);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 14);
        lv_obj_set_style_bg_color(icon, lv_color_from_hex(t["cor"] | "#7C4DFF"), 0);
        lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(icon, 12, 0);
        char ini[2] = {(t["nome"] | "")[0] ? (t["nome"] | "")[0] : '?', '\0'};
        lv_obj_t *ini_lbl = make_label(icon, ini, lv_color_hex(0xFFFFFF), &lv_font_montserrat_20);
        lv_obj_center(ini_lbl);

        //nome da turma
        lv_obj_t *name = make_label(card, t["nome"] | "", lv_color_hex(COLOR_TEXT), &lv_font_montserrat_14);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 44, 12);

        lv_obj_t *sched = make_label(card, t["horario"] | "", lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
        lv_obj_align(sched, LV_ALIGN_TOP_LEFT, 44, 36);

        // InfoTurma stats = info_turma(cod);
        // char info[96];
        // snprintf(info, sizeof(info), "%d alunos | %.0f%% media", (int)stats.total_alunos, stats.media_presenca * 100.0f);
        // lv_obj_t *meta = make_label(card, info, lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
        // lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        i++;
    }

    activate_screen();
}

// TELA ABA ATUAL
static void render_aba_atual() {
    JsonObject turma = turma_por_codigo(turmaatual_cod.c_str());
    if (turma.isNull()) return;

    char hoje[16];
    data_hoje(hoje, sizeof(hoje));
    JsonObject aula = aula_hoje(turmaatual_cod.c_str());

    int total = turma["alunos"].size();
    int pres = 0;
    for (JsonObject al : turma["alunos"].as<JsonArray>()) {
        const char *mat = al["matricula"] | "";
        if (aula["presencas"][mat].as<bool>()) pres++;
    }

    lv_obj_t *summary = make_card(screen_body, 12, 38, 456, 58);
    make_label(summary, "Chamada", lv_color_hex(COLOR_PRIMARY), &lv_font_montserrat_20);
    lv_obj_t *date = make_label(summary, hoje, lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
    lv_obj_align(date, LV_ALIGN_TOP_LEFT, 0, 26);

    char presence[32];
    snprintf(presence, sizeof(presence), "Presentes %d/%d", pres, total);
    lv_obj_t *presence_label = make_label(summary, presence, lv_color_hex(COLOR_SUCCESS), &lv_font_montserrat_20);
    lv_obj_align(presence_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *bar = lv_bar_create(summary);
    lv_obj_set_size(bar, 170, 10);
    lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, 0, 30);
    lv_bar_set_range(bar, 0, total > 0 ? total : 1);
    lv_bar_set_value(bar, pres, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_SUCCESS), LV_PART_INDICATOR);

    lv_obj_t *rfid_card = make_card(screen_body, 12, 102, 456, 42);
    make_label(rfid_card, "Aproxime a carteirinha para confirmar presenca", lv_color_hex(0xFFFFFF), LV_FONT_DEFAULT);
    lv_obj_set_style_bg_color(rfid_card, lv_color_hex(COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(rfid_card, 0, 0);

    int i = 0;
    for (JsonObject al : turma["alunos"].as<JsonArray>()) {
        if (i >= 4) break;
        const char *mat = al["matricula"] | "";
        const char *nome = al["nome"] | "";
        bool ok = aula["presencas"][mat].as<bool>();

        int col = i % 2;
        int row = i / 2;
        lv_obj_t *chip = make_card(screen_body, 12 + col * 232, 150 + row * 90, 224, 80);
        lv_obj_set_style_bg_color(chip, lv_color_hex(ok ? 0xDFF6E7 : 0xFFFFFF), 0);
        lv_obj_set_style_border_color(chip, lv_color_hex(ok ? COLOR_SUCCESS : COLOR_BORDER), 0);
        make_label(chip, nome, lv_color_hex(COLOR_TEXT), LV_FONT_DEFAULT);
        lv_obj_t *mat_label = make_label(chip, mat, lv_color_hex(ok ? COLOR_SUCCESS : COLOR_MUTED), LV_FONT_DEFAULT);
        lv_obj_align(mat_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        i++;
    }
}

static void edit_row_event_cb(lv_event_t *e) {
    const char *mat = (const char *)lv_event_get_user_data(e);
    edit_mat = mat;
    modal_edit_aberto = true;
    ui_navegar(TELA_TURMA);
}

// TELA ABAS PASSADAS
static void render_aba_passadas() {
    JsonObject turma = turma_por_codigo(turmaatual_cod.c_str());
    if (turma.isNull()) return;

    String datas[10];
    int ndatas = 0;
    for (JsonObject au : data_doc["aulas"].as<JsonArray>()) {
        if (strcmp(au["turma"] | "", turmaatual_cod.c_str()) != 0) continue;
        if (ndatas < 10) datas[ndatas++] = au["data"].as<String>();
    }

    if (ndatas == 0) {
        lv_obj_t *empty = make_card(screen_body, 12, 38, 456, 72);
        make_label(empty, "Nenhuma aula registrada ainda.", lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
        return;
    }

    for (int row = 0; row < 5; row++) {
        JsonVariant aluno_var = turma["alunos"][row];
        if (aluno_var.isNull()) break;
        JsonObject al = aluno_var.as<JsonObject>();

        lv_obj_t *card = make_card(screen_body, 12, 38 + row * 70, 456, 60);
        make_label(card, al["nome"] | "", lv_color_hex(COLOR_TEXT), LV_FONT_DEFAULT);

        lv_obj_t *mat = make_label(card, al["matricula"] | "", lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
        lv_obj_align(mat, LV_ALIGN_TOP_LEFT, 0, 20);

        for (int i = 0; i < ndatas && i < 3; i++) {
            JsonObject aula = aula_por_data(turmaatual_cod.c_str(), datas[i].c_str());
            bool ok = !aula.isNull() && aula["presencas"][al["matricula"] | ""].as<bool>();

            lv_obj_t *pill = lv_obj_create(card);
            lv_obj_remove_style_all(pill);
            lv_obj_set_size(pill, 54, 26);
            lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -70 - i * 60, 0);
            lv_obj_set_style_radius(pill, 10, 0);
            lv_obj_set_style_bg_color(pill, lv_color_hex(ok ? COLOR_SUCCESS : COLOR_DANGER), 0);
            lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);

            char pill_text[24];
            snprintf(pill_text, sizeof(pill_text), "%s %s", datas[i].substring(5).c_str(), ok ? "P" : "F");
            lv_obj_t *txt = make_label(pill, pill_text, lv_color_hex(0xFFFFFF), LV_FONT_DEFAULT);
            lv_obj_center(txt);
        }

        lv_obj_t *edit = make_button(card, "Editar", &style_btn_warn, ACT_NONE);
        lv_obj_set_size(edit, 62, 26);
        lv_obj_align(edit, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(edit, edit_row_event_cb, LV_EVENT_CLICKED, (void *)(al["matricula"] | ""));
    }
}

// TELA CADASTRO DO ALUNO
static void render_aba_cadastro() {
    lv_obj_t *card = make_card(screen_body, 12, 38, 456, 286);

    lv_obj_t *left = lv_obj_create(card);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, 424, 254);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, 6, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    make_label(left, "Matricula do aluno", lv_color_hex(COLOR_TEXT), LV_FONT_DEFAULT);
    make_textarea(left, "Matricula", cad_aluno_mat_buf, sizeof(cad_aluno_mat_buf), LV_KEYBOARD_MODE_NUMBER);

    make_label(left, "Nome do aluno", lv_color_hex(COLOR_TEXT), LV_FONT_DEFAULT);
    make_textarea(left, "Nome completo", cad_aluno_nome_buf, sizeof(cad_aluno_nome_buf), LV_KEYBOARD_MODE_TEXT_LOWER);

    make_label(left, "Carteirinha", lv_color_hex(COLOR_TEXT), LV_FONT_DEFAULT);
    lv_obj_t *rfid_box = lv_obj_create(left);
    lv_obj_remove_style_all(rfid_box);
    lv_obj_add_style(rfid_box, &style_input, 0);
    lv_obj_set_width(rfid_box, lv_pct(100));
    lv_obj_set_height(rfid_box, 38);
    lv_obj_clear_flag(rfid_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(rfid_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *rfid_label = lv_label_create(rfid_box);
    lv_label_set_text_fmt(rfid_label, "%s", rfid_pendente.length() > 0 ? rfid_pendente.c_str() : "Aguardando carteirinha...");
    lv_obj_set_style_text_color(rfid_label, lv_color_hex(rfid_pendente.length() > 0 ? COLOR_SUCCESS : COLOR_MUTED), 0);
    lv_obj_center(rfid_label);

    lv_obj_t *submit = make_button(left, "Cadastrar Aluno", &style_btn_primary, ACT_CAD_ALUNO);
    lv_obj_set_width(submit, lv_pct(100));

    status_label = make_label(card, "", lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// TELA TURMAS
static void render_turma() {
    create_shell(true);

    JsonObject turma = turma_por_codigo(turmaatual_cod.c_str());
    set_header(turma.isNull() ? turmaatual_cod.c_str() : (turma["nome"] | ""), "Presenca dos alunos");

    static const char *tabs[3] = {"Atual", "Passadas", "Cadastro"};
    static const ActionID actions[3] = {ACT_TAB_ATUAL, ACT_TAB_PASSADAS, ACT_TAB_CADASTRO};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = make_button(screen_body, tabs[i], i == aba_atual ? &style_tab_active : &style_tab_idle, actions[i]);
        lv_obj_set_size(btn, 142, 30);
        lv_obj_set_pos(btn, 12 + i * 154, 0);
    }

    switch (aba_atual) {
        case ABA_ATUAL:
            render_aba_atual();
            break;
        case ABA_PASSADAS:
            render_aba_passadas();
            break;
        case ABA_CADASTRO:
            render_aba_cadastro();
            break;
    }

    activate_screen();

    if (modal_edit_aberto && aba_atual == ABA_PASSADAS) {
        open_edit_modal();
    }
}

static void render_conta() {
    create_shell(true);
    set_header("Minha Conta", "Dados do professor");

    JsonObject prof = prof_por_mat(prof_logado_mat.c_str());
    if (prof.isNull()) {
        activate_screen();
        return;
    }

    lv_obj_t *profile = make_card(screen_body, 12, 0, 456, 180);
    make_label(profile, prof["nome"] | "", lv_color_hex(COLOR_TEXT), &lv_font_montserrat_20);

    char line1[64];
    char line2[96];
    snprintf(line1, sizeof(line1), "Matricula: %s", prof["matricula"] | "");
    snprintf(line2, sizeof(line2), "Email: %s", prof["email"] | "");
    lv_obj_t *mat = make_label(profile, line1, lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
    lv_obj_align(mat, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_t *mail = make_label(profile, line2, lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
    lv_obj_align(mail, LV_ALIGN_TOP_LEFT, 0, 56);

    const char *uid = prof["rfid_uid"] | "";
    char rfid_line[96];
    snprintf(rfid_line, sizeof(rfid_line), "RFID: %s", strlen(uid) > 0 ? uid : "Nao cadastrado");
    lv_obj_t *rfid = make_label(profile, rfid_line, lv_color_hex(strlen(uid) > 0 ? COLOR_SUCCESS : COLOR_DANGER), LV_FONT_DEFAULT);
    lv_obj_align(rfid, LV_ALIGN_TOP_LEFT, 0, 78);

    lv_obj_t *cta = make_button(
        profile,
        conta_aguardando_rfid ? "Aguardando nova carteirinha..." : "Cadastrar Nova Carteirinha",
        conta_aguardando_rfid ? &style_btn_warn : &style_btn_primary,
        ACT_CONTA_RFID
    );
    lv_obj_set_size(cta, 400, 36);
    lv_obj_align(cta, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *stats_col = lv_obj_create(screen_body);
    lv_obj_remove_style_all(stats_col);
    lv_obj_set_pos(stats_col, 12, 180);
    lv_obj_set_size(stats_col, 456, 420);
    lv_obj_set_style_bg_opa(stats_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_col, 0, 0);
    lv_obj_clear_flag(stats_col, LV_OBJ_FLAG_SCROLLABLE);

    make_label(stats_col, "Estatisticas", lv_color_hex(COLOR_PRIMARY), &lv_font_montserrat_16);

    JsonArray tlist = prof["turmas"].as<JsonArray>();
    int i = 0;
    for (JsonVariant v : tlist) {
        const char *cod = v.as<const char *>();
        JsonObject t = turma_por_codigo(cod);
        if (t.isNull()) continue;
        InfoTurma e = info_turma(cod);

        lv_obj_t *card = make_card(stats_col, 0, 30 + i * 120, 456, 100);
        // lv_obj_t *band = lv_obj_create(card);
        // lv_obj_remove_style_all(band);
        // lv_obj_set_size(band, 456, 6);
        // lv_obj_align(band, LV_ALIGN_TOP_MID, 0, 0);
        // lv_obj_set_style_bg_color(band, lv_color_from_hex(t["cor"] | "#7C4DFF"), 0);
        // lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);

        make_label(card, t["nome"] | "", lv_color_hex(COLOR_TEXT), &lv_font_montserrat_14);
        char info[96];
        snprintf(info, sizeof(info), "%d alunos | %d aulas | %.0f%% presenca", (int)e.total_alunos, (int)e.total_aulas, e.media_presenca * 100.0f);
        lv_obj_t *text = make_label(card, info, lv_color_hex(COLOR_MUTED), LV_FONT_DEFAULT);
        lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 24);

        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_size(bar, 410, 10);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, (int)(e.media_presenca * 100.0f), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_from_hex(t["cor"] | "#7C4DFF"), LV_PART_INDICATOR);
        i++;
    }

    lv_obj_t *logout_btn = make_button(stats_col, "Sair da Conta", &style_btn_warn, ACT_LOGOUT);
    lv_obj_set_size(logout_btn, 456, 40);
    lv_obj_set_pos(logout_btn, 0, 30 + i * 120 + 10);

    activate_screen();
}

// renderizar a tela atual
static void render_current_screen() {
    status_label = nullptr;
    switch (tela_atual) {
        case TELA_LOGIN:
            render_login();
            break;
        case TELA_CADASTRO_PROF:
            render_cadastro_prof();
            break;
        case TELA_TURMAS:
            render_turmas();
            break;
        case TELA_TURMA:
            render_turma();
            break;
        case TELA_CONTA:
            render_conta();
            break;
        default:
            break;
    }
}

// PROCESSAR O UID 
void ui_processar_rfid(const String &uid) {
    // de acordo com o uid, mudar para outra tela
    switch (tela_atual) {
        case TELA_LOGIN: {
            JsonObject prof = prof_por_rfid(uid.c_str());
            if (!prof.isNull()) {
                prof_logado_mat = prof["matricula"].as<String>();
                Serial.printf("[UI] Login: %s\n", prof_logado_mat.c_str());
                ui_navegar(TELA_TURMAS);
            } else {
                rfid_pendente = uid;
                memset(cad_prof_mat_buf, 0, sizeof(cad_prof_mat_buf));
                ui_navegar(TELA_CADASTRO_PROF);
            }
            break;
        }
        case TELA_CADASTRO_PROF: {
            // rfid_pendente = uid;
            // ui_navegar(TELA_CADASTRO_PROF);
            // break;
              JsonObject prof = prof_por_rfid(uid.c_str());
            if (!prof.isNull()) {
                prof_logado_mat = prof["matricula"].as<String>();
                rfid_pendente = "";
                ui_navegar(TELA_TURMAS);
            } else {
                rfid_pendente = uid;
                ui_navegar(TELA_CADASTRO_PROF);
            }
            break;
        }
        case TELA_TURMA:
            if (aba_atual == ABA_ATUAL) {
                char tCod[16] = "";
                JsonObject al = aluno_por_rfid(uid.c_str(), tCod);
                if (!al.isNull() && strcmp(tCod, turmaatual_cod.c_str()) == 0) {
                    char hoje[16];
                    data_hoje(hoje, sizeof(hoje));
                    const char *mat = al["matricula"] | "";
                    presenca(turmaatual_cod.c_str(), hoje, mat, true);
                    Serial.printf("[PRES] %s marcado presente\n", mat);
                    ui_navegar(TELA_TURMA);
                } else {
                    Serial.println("[RFID] Carteirinha nao reconhecida nesta turma");
                }
            } else if (aba_atual == ABA_CADASTRO) {
                rfid_pendente = uid;
                ui_navegar(TELA_TURMA);
            }
            break;
        case TELA_CONTA:
            if (conta_aguardando_rfid) {
                rfid_prof(prof_logado_mat.c_str(), uid.c_str());
                conta_aguardando_rfid = false;
                Serial.println("[CONTA] Nova carteirinha cadastrada");
                ui_navegar(TELA_CONTA);
            }
            break;
        default:
            break;
    }
}

void ui_atualizar_contadores_chamada() {
    if (tela_atual == TELA_TURMA && aba_atual == ABA_ATUAL) {
        ui_navegar(TELA_TURMA);
    }
}

// INICIALIZAR
void ui_iniciar(Arduino_RGB_Display *display) {
    gfx = display;

    touch_init();

    lv_init();
    previous_screen = lv_scr_act();

#if LV_VERSION_CHECK(9, 0, 0)
    lv_tick_set_cb(lvgl_tick_cb);
#endif
    size_t buf_pixels = SCR_W * 40;
    lv_buf_1 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_buf_2 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    if (!lv_buf_1 || !lv_buf_2) {
        Serial.println("[UI] ERRO: buffers LVGL nao alocados");
        while (true) delay(1000);
    }

    lv_display = lv_display_create(SCR_W, SCR_H);
    lv_display_set_color_format(lv_display, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(
        lv_display,
        lv_buf_1,
        lv_buf_2,
        buf_pixels * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );
    lv_display_set_flush_cb(lv_display, lvgl_flush_cb);
    lv_display_set_default(lv_display);

    touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, lvgl_touch_read_cb);
    lv_indev_set_display(touch_indev, lv_display);
    lv_indev_enable(touch_indev, true);
    (void)touch_indev;

    Serial.printf("[UI] touch_indev=%p\n", touch_indev);
    Serial.printf("[UI] lv_display=%p\n", lv_display);

    init_styles();
    ultimo_tick_lvgl = millis();
    render_current_screen();
}

// navegacao
void ui_navegar(TelaID id) {
    tela_atual = id;
    if (id != TELA_CONTA) conta_aguardando_rfid = false;
    if (id != TELA_TURMA) modal_edit_aberto = false;
    clear_status();
    render_current_screen();
}

// navegar na turma
void ui_navegar_turma(const char *cod) {
    turmaatual_cod = cod;
    aba_atual = ABA_ATUAL;
    rfid_pendente = "";
    modal_edit_aberto = false;
    memset(cad_aluno_mat_buf, 0, sizeof(cad_aluno_mat_buf));
    memset(cad_aluno_nome_buf, 0, sizeof(cad_aluno_nome_buf));
    ui_navegar(TELA_TURMA);
}

// qual aba 
void ui_set_aba(AbaID aba) {
    aba_atual = aba;
    rfid_pendente = "";
    modal_edit_aberto = false;
    ui_navegar(TELA_TURMA);
}

void ui_loop() {
#if !LV_VERSION_CHECK(9, 0, 0)
    uint32_t agora = millis();
    lv_tick_inc(agora - ultimo_tick_lvgl);
    ultimo_tick_lvgl = agora;
#endif
    lv_timer_handler();
}

bool ui_teclado_aberto() {
    return kbd && !lv_obj_has_flag(kbd, LV_OBJ_FLAG_HIDDEN);
}
