#ifndef PC_SIM_H
#define PC_SIM_H
#include "current_pi_sim.h"
#ifndef PC_SIMULATION
#define PC_SIMULATION 1
#endif
#define PC_SIM_FEEDBACK_SIZE 68U
#define PC_SIM_COMMAND_SIZE 30U
typedef struct {
  CurrentPiState pi;
  uint32_t session, sequence, last_rx_ms, last_tx_ms, errors, retries;
  uint64_t time_us, next_pi_us;
  int32_t encoder;
  uint8_t active, pending, stopped, status, hall, sector;
  uint8_t command[PC_SIM_COMMAND_SIZE];
  uint8_t buffer[PC_SIM_FEEDBACK_SIZE];
  uint16_t used;
} PCSimState;
extern PCSimState g_pc_sim;
extern volatile uint32_t g_pc_reverse, g_pc_switched;
void PCSim_Reset(PCSimState *s);
int PCSim_Feed(PCSimState *s, uint8_t byte, uint32_t now,
               const CurrentPiInputs *inputs, uint8_t reverse, uint8_t switched);
int PCSim_Retry(PCSimState *s, uint32_t now);
/* HAL adapter is separate so protocol/controller can be tested without hardware. */
#ifdef USE_HAL_DRIVER
#include "stm32g4xx_hal.h"
void PCSim_Init(UART_HandleTypeDef *uart);
void PCSim_Poll(const CurrentPiInputs *inputs);
#endif
#endif
