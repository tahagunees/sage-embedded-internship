#include "pc_sim.h"
#include "main.h"
#if PC_SIMULATION
PCSimState g_pc_sim;
volatile uint32_t g_pc_reverse=0, g_pc_switched=0;
static UART_HandleTypeDef *link_uart;
static DMA_HandleTypeDef rx_dma;
static uint8_t rx_ring[512];
static uint16_t rx_read;

static void StartRx(void)
{
  rx_read=0;
  if(HAL_UART_Receive_DMA(link_uart,rx_ring,sizeof(rx_ring))!=HAL_OK) Error_Handler();
  /* Circular DMA is drained in the main loop; no byte/half/full interrupts. */
  __HAL_DMA_DISABLE_IT(&rx_dma,DMA_IT_HT|DMA_IT_TC|DMA_IT_TE);
}
void PCSim_Init(UART_HandleTypeDef *uart)
{
  link_uart=uart;PCSim_Reset(&g_pc_sim);
  __HAL_RCC_DMA1_CLK_ENABLE();__HAL_RCC_DMAMUX1_CLK_ENABLE();
  rx_dma.Instance=DMA1_Channel4;
  rx_dma.Init.Request=DMA_REQUEST_LPUART1_RX;
  rx_dma.Init.Direction=DMA_PERIPH_TO_MEMORY;
  rx_dma.Init.PeriphInc=DMA_PINC_DISABLE;rx_dma.Init.MemInc=DMA_MINC_ENABLE;
  rx_dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;
  rx_dma.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;
  rx_dma.Init.Mode=DMA_CIRCULAR;rx_dma.Init.Priority=DMA_PRIORITY_HIGH;
  if(HAL_DMA_Init(&rx_dma)!=HAL_OK) Error_Handler();
  __HAL_LINKDMA(uart,hdmarx,rx_dma);
  StartRx();
}
static void Send(void)
{
  if(HAL_UART_Transmit(link_uart,g_pc_sim.command,PC_SIM_COMMAND_SIZE,10)!=HAL_OK)
    ++g_pc_sim.errors;
}
void PCSim_Poll(const CurrentPiInputs *inputs)
{
  if(link_uart->ErrorCode!=HAL_UART_ERROR_NONE ||
     __HAL_DMA_GET_FLAG(&rx_dma,DMA_FLAG_TE4)) {
    ++g_pc_sim.errors;
    HAL_UART_AbortReceive(link_uart);
    __HAL_UART_CLEAR_FLAG(link_uart,UART_CLEAR_OREF|UART_CLEAR_NEF|UART_CLEAR_FEF);
    g_pc_sim.used=0;
    StartRx();
  }
  uint16_t write=(uint16_t)((sizeof(rx_ring)-__HAL_DMA_GET_COUNTER(&rx_dma))%sizeof(rx_ring));
  while(rx_read!=write) {
    uint8_t byte=rx_ring[rx_read];rx_read=(uint16_t)((rx_read+1)%sizeof(rx_ring));
    if(PCSim_Feed(&g_pc_sim,byte,HAL_GetTick(),inputs,g_pc_reverse!=0,g_pc_switched!=0)) Send();
  }
  if(PCSim_Retry(&g_pc_sim,HAL_GetTick())) Send();
}
#endif
