#include "motor_model.h"
#include "config.h"
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(MODEL_PERIOD_US == 1000U, "This model requires a fixed 1 ms period");
_Static_assert(MODEL_COMMAND_TIMEOUT_MS > 0U, "Timeout must be positive");
motor_params_t motor_default_params(void)
{
    return (motor_params_t){MODEL_VBUS_V, MODEL_R_EQ_OHM, MODEL_L_EQ_H,
        MODEL_KE_V_PER_RAD_S, MODEL_MAX_CURRENT_A, MODEL_CURRENT_OFFSET_A,
        MODEL_CURRENT_NOISE_A, MODEL_SENSOR_FILTER_TAU_S};
}
bool motor_init(motor_model_t *m, const motor_params_t *p)
{
    memset(m, 0, sizeof(*m));
    if (!isfinite(p->vbus) || !isfinite(p->r) || !isfinite(p->l) || !isfinite(p->ke) ||
        !isfinite(p->max_current) || !isfinite(p->offset) || !isfinite(p->noise) ||
        !isfinite(p->sensor_tau) || p->vbus < 0 || p->r <= 0 || p->l <= 0 ||
        p->ke < 0 || p->max_current <= 0 || p->max_current > 32.767f ||
        p->noise < 0 || p->sensor_tau < 0) return false;
    m->params = *p; m->noise_state = 0x9e3779b9U; m->initialized = true;
    return true;
}
uint16_t motor_step(motor_model_t *m, const stm_command_t *c, bool has,
                    uint64_t received_us, uint64_t now_us)
{
    bool timeout = !has || now_us < received_us ||
                   now_us - received_us >= (uint64_t)MODEL_COMMAND_TIMEOUT_MS * 1000U;
    bool hall = has && (c->flags & STM_HALL_VALID) && c->sector >= 1 && c->sector <= 6;
    bool enabled = has && (c->flags & STM_ENABLED);
    uint16_t status = (has && !timeout ? HIL_HAS_COMMAND : 0) |
        (hall ? HIL_VALID_HALL : 0) | (enabled ? HIL_ENABLED : 0) | (timeout ? HIL_TIMEOUT : 0);
    if (!m->initialized) return HIL_TIMEOUT;
    const motor_params_t *p = &m->params;
    const double dt = (double)MODEL_PERIOD_US / 1000000.0;
    /* Double intermediates avoid overflow even for extreme finite float parameters. */
    double emf = has ? (double)p->ke * fabs((double)c->rpm_x10 / 10.0) * 0.10471975511965977 : 0;
    double duty = c->duty_x100 > 10000U ? 10000.0 : (double)c->duty_x100;
    double applied = enabled && hall && !timeout ? (double)p->vbus * duty / 10000.0 : 0;
    double dl = dt / (double)p->l;
    double next = ((double)m->current + dl * (applied - emf)) / (1.0 + dl * p->r);
    m->current = (float)fmin((double)p->max_current, fmax(0.0, next));
    /* Diagnostics saturate at the wire format's range. */
    m->back_emf = (float)fmin(emf, 2147483.0);
    m->applied = (float)fmin(applied, 2147483.0);
    uint32_t x = m->noise_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5; m->noise_state = x;
    double noise = ((double)x / UINT32_MAX * 2.0 - 1.0) * p->noise;
    double target = (double)m->current + p->offset + noise;
    double filtered = (double)m->measured + dt / ((double)p->sensor_tau + dt) * (target - m->measured);
    m->measured = (float)fmin(32.767, fmax(-32.768, filtered));
    return status;
}
int16_t motor_amps_to_ma(float v)
{
    if (!isfinite(v)) return 0;
    double n = round((double)v * 1000.0);
    return (int16_t)fmin(INT16_MAX, fmax(INT16_MIN, n));
}
int32_t motor_volts_to_mv(float v)
{
    if (!isfinite(v)) return 0;
    double n = round((double)v * 1000.0);
    return (int32_t)fmin(INT32_MAX, fmax(INT32_MIN, n));
}
