#pragma once
#include <stdint.h>

constexpr int PIN_PWM    = 18;
constexpr int PIN_SWITCH = 17;
constexpr int PIN_LDR    = 4;
constexpr int PIN_PIR1   = 15;
constexpr int PIN_RGB    = 48;

constexpr bool    PWM_INVERTED           = false;
constexpr bool    LDR_HIGH_IS_BRIGHT     = true;
constexpr bool    SWITCH_ACTIVE_LOW      = true;
constexpr bool    SWITCH_RETURNS_TO_AUTO = true;
constexpr uint8_t RGB_LEVEL              = 20;

constexpr bool  LDR_AUTO_CAL   = true;
constexpr float LDR_MIN_SPAN   = 400.0f;
constexpr int   LDR_DARK_RAW   = 300;
constexpr int   LDR_BRIGHT_RAW = 3500;

constexpr uint32_t PIR_WARMUP_MS     = 60000UL;
constexpr uint32_t PIR_FILTER_MS     = 200;
constexpr uint32_t PIR_MIN_LOW_MS    = 1500;
constexpr uint32_t PIR_STUCK_WARN_MS = 30000UL;
constexpr uint32_t OCCUPANCY_HOLD_MS = 60000UL;

constexpr uint8_t AUTO_MIN_DUTY    = 20;
constexpr uint8_t SWITCH_ON_DUTY   = 255;
constexpr int     AUTO_DEADBAND    = 3;
constexpr int     AUTO_SLEW_STEP   = 4;

constexpr float SUPPLY_VOLTS  = 12.0f;
constexpr float LED_MAX_WATTS = 1.5f;

constexpr uint32_t PWM_FREQ = 1000;
constexpr uint8_t  PWM_RES  = 8;

constexpr uint32_t SENSOR_MS     = 100;
constexpr uint32_t LOG_MS        = 1000;
constexpr uint32_t SAVE_MS       = 300000UL;
constexpr uint32_t DEBOUNCE_MS   = 40;
constexpr uint32_t WIFI_TRY_MS   = 12000UL;
constexpr uint32_t WIFI_RETRY_MS = 17000UL;
constexpr int      AVG_SAMPLES   = 100;

constexpr uint32_t NONCE_TTL_MS = 60000UL;
constexpr uint32_t TOKEN_TTL_MS = 12UL * 3600UL * 1000UL;
constexpr uint32_t LOCKOUT_MS   = 30000UL;
constexpr int      MAX_FAILS    = 5;
constexpr int      MAX_NONCES   = 4;
constexpr int      MAX_TOKENS   = 4;
