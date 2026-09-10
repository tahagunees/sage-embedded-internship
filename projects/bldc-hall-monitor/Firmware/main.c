/* NUCLEO-G491RE: manual Hall-sector monitor. No motor-drive outputs. */
#include "stm32g4xx.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>

static volatile uint32_t ticks;
static volatile unsigned rx_head, rx_tail;
static volatile uint32_t rx_errors;
static volatile char rx_buffer[128];
static const uint8_t sequence[6] = {1, 5, 4, 6, 2, 3};
static uint8_t hall = 1, step = 1, hardware_mode;
static int direction;
static int32_t relative_steps;
static uint32_t faults, buttons, packet, last_move, last_report;
static uint8_t dirty = 1;

/* No operating system or static constructors; ST startup calls libc init. */
void _init(void) { }
void _fini(void) { }

void SysTick_Handler(void) { ++ticks; }

void LPUART1_IRQHandler(void)
{
    uint32_t status = LPUART1->ISR;
    if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        ++rx_errors;
    }
    if (status & USART_ISR_RXNE_RXFNE) {
        char c = (char)LPUART1->RDR;
        unsigned next = (rx_head + 1U) % sizeof(rx_buffer);
        if (next != rx_tail) { rx_buffer[rx_head] = c; rx_head = next; }
        else ++rx_errors;
    }
}

static uint8_t hall_to_step(uint8_t value)
{
    for (unsigned i = 0; i < 6; ++i) if (sequence[i] == value) return i + 1;
    return 0;
}

static void accept_hall(uint8_t value, int establish_reference)
{
    uint8_t next = hall_to_step(value);
    if (!establish_reference && value == hall) return;
    direction = 0;
    if (!next) ++faults;
    else if (!establish_reference && step) {
        int delta = (next - step + 6) % 6;
        if (delta == 1) direction = 1;
        else if (delta == 5) direction = -1;
        else if (delta) ++faults;
        if (direction > 0 && relative_steps < INT32_MAX) ++relative_steps;
        if (direction < 0 && relative_steps > INT32_MIN) --relative_steps;
    }
    hall = value; step = next; last_move = ticks; dirty = 1;
}

static uint8_t read_hall(void)
{
    /* One atomic port read: H1=PA0, H2=PA1, H3=PA4. */
    uint32_t pins = GPIOA->IDR;
    return (uint8_t)(((pins & 1U) << 2) | (pins & 2U) | ((pins >> 4) & 1U));
}

static char *append_unsigned(char *dest, uint32_t value)
{
    char digits[10]; unsigned length = 0;
    do { digits[length++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (length) *dest++ = digits[--length];
    return dest;
}

static char *append_signed(char *dest, int32_t value)
{
    uint32_t magnitude = (uint32_t)value;
    if (value < 0) { *dest++ = '-'; magnitude = 0U - magnitude; }
    return append_unsigned(dest, magnitude);
}

static void send_report(void)
{
    char line[128];
    char *p = line;
    *p++ = 'F'; *p++ = '1'; *p++ = ',';
    p = append_unsigned(p, packet++); *p++ = ',';
    p = append_unsigned(p, ticks); *p++ = ',';
    const char *mode_name = hardware_mode ? "HALL" : "SIM";
    while (*mode_name) *p++ = *mode_name++;
    *p++ = ','; p = append_unsigned(p, hall);
    *p++ = ','; p = append_unsigned(p, step);
    *p++ = ','; p = append_signed(p, direction);
    *p++ = ','; p = append_signed(p, relative_steps);
    *p++ = ','; p = append_unsigned(p, faults + rx_errors);
    *p++ = ','; p = append_unsigned(p, buttons);
    *p++ = '\r'; *p++ = '\n';
    for (char *c = line; c < p; ++c) {
        while (!(LPUART1->ISR & USART_ISR_TXE_TXFNF)) { }
        LPUART1->TDR = *c;
    }
    last_report = ticks; dirty = 0;
}

static void command(const char *text)
{
    if (!strcmp(text, "?")) { dirty = 1; return; }
    if (!strcmp(text, "R")) {
        relative_steps = 0; faults = 0; rx_errors = 0; buttons = 0;
        direction = 0; dirty = 1; return;
    }
    if (!strcmp(text, "M0") || !strcmp(text, "M1")) {
        hardware_mode = text[1] == '1'; relative_steps = 0; direction = 0;
        accept_hall(hardware_mode ? read_hall() : sequence[0], 1); return;
    }
    if (hardware_mode) return;
    if (!strcmp(text, "N")) accept_hall(sequence[step % 6], 0);
    else if (!strcmp(text, "P")) accept_hall(sequence[(step + 4) % 6], 0);
    else if (text[0] == 'S' && text[1] >= '1' && text[1] <= '6' && text[2] == 0)
        accept_hall(sequence[text[1] - '1'], 0);
}

static void init_hardware(void)
{
    /* Reset clock: HSI16, AHB/APB prescalers = 1. */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOCEN;
    RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;
    (void)RCC->AHB2ENR;
    GPIOA->MODER &= ~((3U << 0) | (3U << 2) | (3U << 8));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3U << 0) | (3U << 2) | (3U << 8)))
                    | (2U << 0) | (2U << 2) | (2U << 8);
    /* PA2/PA3 AF12 -> onboard ST-LINK VCP; 115200, 8N1. */
    GPIOA->MODER = (GPIOA->MODER & ~((3U << 4) | (3U << 6))) | (2U << 4) | (2U << 6);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~((15U << 8) | (15U << 12))) | (12U << 8) | (12U << 12);
    GPIOA->OSPEEDR |= (3U << 4) | (3U << 6);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3U << 6)) | (1U << 6);
    GPIOC->MODER &= ~(3U << 26);
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3U << 26)) | (2U << 26);
    LPUART1->BRR = (256UL * 16000000UL + 57600UL) / 115200UL;
    LPUART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
    NVIC_SetPriority(LPUART1_IRQn, 2);
    NVIC_EnableIRQ(LPUART1_IRQn);
    SysTick_Config(16000U);
}

int main(void)
{
    init_hardware();
    char command_buffer[16]; unsigned command_length = 0; int discard = 0;
    uint8_t raw_button = !!(GPIOC->IDR & (1U << 13)), stable_button = raw_button;
    uint8_t candidate = read_hall(); uint32_t button_time = ticks, hall_time = ticks;
    uint32_t observed_rx_errors = 0;
    for (;;) {
        /* Discard partial commands after reception loss; recover at newline. */
        if (observed_rx_errors != rx_errors) {
            observed_rx_errors = rx_errors; discard = 1; command_length = 0;
        }
        while (rx_tail != rx_head) {
            char c = rx_buffer[rx_tail]; rx_tail = (rx_tail + 1U) % sizeof(rx_buffer);
            if (c == '\r') continue;
            if (c == '\n') {
                if (!discard) { command_buffer[command_length] = 0; command(command_buffer); }
                command_length = 0; discard = 0;
            } else if (!discard && command_length < sizeof(command_buffer) - 1) {
                command_buffer[command_length++] = c;
            } else { discard = 1; command_length = 0; }
        }
        uint32_t now = ticks;
        uint8_t b = !!(GPIOC->IDR & (1U << 13));
        if (b != raw_button) { raw_button = b; button_time = now; }
        if (b != stable_button && (uint32_t)(now - button_time) >= 30) {
            stable_button = b;
            if (b) {
                ++buttons; dirty = 1;
                if (!hardware_mode) accept_hall(sequence[step % 6], 0);
            }
        }
        if (hardware_mode) {
            uint8_t value = read_hall();
            if (value != candidate) { candidate = value; hall_time = now; }
            if ((uint32_t)(now - hall_time) >= 2 && value != hall) accept_hall(value, 0);
        }
        if (direction && (uint32_t)(now - last_move) >= 800) { direction = 0; dirty = 1; }
        if (dirty || (uint32_t)(now - last_report) >= 100) send_report();
    }
}
