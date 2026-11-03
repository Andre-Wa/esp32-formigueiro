/*
 * sfx.h — Ganchos de áudio e vibração (DESATIVADOS por enquanto).
 *
 * O hardware de som/vibração entra depois, em porta própria (ver
 * SFX_PIN_BUZZER / HAPTIC_PIN_MOTOR em app_config.h). Todos os pontos do
 * jogo que futuramente farão barulho já chamam sfx_play()/haptic_pulse(),
 * então basta implementar este arquivo e virar SFX_ENABLE/HAPTIC_ENABLE.
 */
#pragma once

typedef enum {
    SFX_TAP = 0,      /* toque na tela */
    SFX_PET,          /* carinho na rainha */
    SFX_EAT,          /* comida */
    SFX_EGG,          /* ovo posto / nascimento */
    SFX_BUILD,        /* obra concluída */
    SFX_ALARM,        /* alerta (fome/predador) */
    SFX_RAIN,         /* começou a chover */
    SFX_COUNT,
} sfx_id_t;

static inline void sfx_init(void) { }
static inline void sfx_play(sfx_id_t id) { (void)id; }
static inline void haptic_pulse(int ms) { (void)ms; }
