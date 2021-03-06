#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/can.h>
#include <stddef.h>
#include <bootloader.h>
#include <boot_arg.h>
#include <platform/mcu/armv7-m/timeout_timer.h>
#include "platform.h"

// page buffer used by config commands.
uint8_t config_page_buffer[CONFIG_PAGE_SIZE];

void can_interface_init(void)
{
    rcc_periph_clock_enable(RCC_CAN);
    rcc_periph_clock_enable(RCC_GPIOA);

    gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO8);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO8);
    gpio_clear(GPIOA, GPIO8);

    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO11 | GPIO12);
    gpio_set_af(GPIOA, GPIO_AF9, GPIO11 | GPIO12);

    /*
    STM32F3 CAN on 36MHz configured APB1 peripheral clock
    36MHz / 2 -> 18MHz
    18MHz / (1tq + 10tq + 7tq) = 1MHz => 1Mbit
    */
    can_init(CAN, // Interface
             false, // Time triggered communication mode.
             true, // Automatic bus-off management.
             false, // Automatic wakeup mode.
             false, // No automatic retransmission.
             false, // Receive FIFO locked mode.
             true, // Transmit FIFO priority.
             CAN_BTR_SJW_1TQ, // Resynchronization time quanta jump width
             CAN_BTR_TS1_10TQ, // Time segment 1 time quanta width
             CAN_BTR_TS2_7TQ, // Time segment 2 time quanta width
             2, // Prescaler
             false, // Loopback
             false); // Silent

    // filter to match any standard id
    // mask bits: 0 = Don't care, 1 = mute match corresponding id bit
    can_filter_id_mask_32bit_init(
        CAN,
        0, // filter nr
        0, // id: only std id, no rtr
        6, // mask: macth any std id
        0, // assign to fifo0
        true // enable
    );
}

void fault_handler(void)
{
    gpio_set(GPIOC, GPIO15);

    while (1)
        ; // debug

    reboot_system(BOOT_ARG_START_BOOTLOADER_NO_TIMEOUT);
}

typedef struct {
    uint8_t pllpre;
    uint8_t pll;
    uint8_t pllsrc;
    uint32_t flash_config;
    uint8_t hpre;
    uint8_t ppre1;
    uint8_t ppre2;
    uint8_t power_save;
    uint32_t apb1_frequency;
    uint32_t apb2_frequency;
} my_clock_scale_t;

// clock config for external HSE 16MHz cristal
static const my_clock_scale_t clock_72mhz = {
    .pllpre = RCC_CFGR2_PREDIV_HSE_IN_PLL_DIV_2,
    .pll = RCC_CFGR_PLLMUL_PLL_IN_CLK_X9,
    .pllsrc = RCC_CFGR_PLLSRC_HSE_PREDIV,
    .hpre = RCC_CFGR_HPRE_DIV_NONE,
    .ppre1 = RCC_CFGR_PPRE1_DIV_2,
    .ppre2 = RCC_CFGR_PPRE2_DIV_NONE,
    .power_save = 1,
    .flash_config = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2WS,
    .apb1_frequency = 36000000,
    .apb2_frequency = 72000000,
};

static inline void rcc_set_main_pll_hse(uint32_t pll)
{
    RCC_CFGR = (~RCC_CFGR_PLLMUL_MASK & RCC_CFGR) | (pll << RCC_CFGR_PLLMUL_SHIFT) | RCC_CFGR_PLLSRC;
}

static inline void rcc_clock_setup_hse(const my_clock_scale_t* clock)
{
    /* Enable internal high-speed oscillator. */
    rcc_osc_on(HSE);
    rcc_wait_for_osc_ready(HSE);
    /* Select HSE as SYSCLK source. */
    rcc_set_sysclk_source(RCC_CFGR_SW_HSE);
    rcc_wait_for_sysclk_status(HSE);

    rcc_osc_off(PLL);
    rcc_wait_for_osc_not_ready(PLL);
    rcc_set_pll_source(clock->pllsrc);
    rcc_set_main_pll_hse(clock->pll);
    RCC_CFGR2 = (clock->pllpre << RCC_CFGR2_PREDIV_SHIFT);
    /* Enable PLL oscillator and wait for it to stabilize. */
    rcc_osc_on(PLL);
    rcc_wait_for_osc_ready(PLL);

    rcc_set_hpre(clock->hpre);
    rcc_set_ppre2(clock->ppre2);
    rcc_set_ppre1(clock->ppre1);
    /* Configure flash settings. */
    flash_set_ws(clock->flash_config);
    /* Select PLL as SYSCLK source. */
    rcc_set_sysclk_source(RCC_CFGR_SW_PLL);
    /* Wait for PLL clock to be selected. */
    rcc_wait_for_sysclk_status(PLL);

    /* Set the peripheral clock frequencies used. */
    rcc_apb1_frequency = clock->apb1_frequency;
    rcc_apb2_frequency = clock->apb2_frequency;
}

void platform_main(int arg)
{
    rcc_clock_setup_hse(&clock_72mhz);

    // LEDs
    rcc_periph_clock_enable(RCC_GPIOC);
    gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13 | GPIO14 | GPIO15);
    gpio_set_output_options(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO13 | GPIO14 | GPIO15);

    // white led on
    gpio_set(GPIOC, GPIO13);

    // configure timeout of 3000 milliseconds
    timeout_timer_init(72000000, 3000);

    can_interface_init();

    bootloader_main(arg);

    reboot_system(BOOT_ARG_START_BOOTLOADER);
}
