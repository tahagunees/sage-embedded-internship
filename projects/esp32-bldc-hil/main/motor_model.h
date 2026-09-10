#pragma once
#include "stm_protocol.h"
typedef struct {
    float vbus, r, l, ke, max_current, offset, noise, sensor_tau;
} motor_params_t;
typedef struct {
    motor_params_t params;
    float current, measured, back_emf, applied;
    uint32_t noise_state;
    bool initialized;
} motor_model_t;
motor_params_t motor_default_params(void);
bool motor_init(motor_model_t *model, const motor_params_t *params);
uint16_t motor_step(motor_model_t *model, const stm_command_t *command,
                    bool has_command, uint64_t received_us, uint64_t now_us);
int16_t motor_amps_to_ma(float value);
int32_t motor_volts_to_mv(float value);
