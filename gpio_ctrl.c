/****************************************************************************
*
* Comments:
*   This file contains the functions which write and read the SPI memories
*   using the ECSPI driver in interrupt mode.
*
****************************************************************************/

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "gpio_pins.h"
#include "board.h"
#include "gpio_ctrl.h"
#include "gpio_imx.h"
#include "rdc_semaphore.h"
#include "debug_console_imx.h"

static SemaphoreHandle_t xSemaphore;

static void GPIO_Ctrl_InitLedPin()
{
#ifdef BOARD_GPIO_LED_CONFIG
    gpio_init_config_t ledInit = {
        .pin           = BOARD_GPIO_LED_CONFIG->pin,
        .direction     = gpioDigitalOutput,
        .interruptMode = gpioNoIntmode
    };

    /* Acquire RDC semaphore before access GPIO to avoid conflict, it's
     * necessary when GPIO RDC is configured as Semaphore Required */
    RDC_SEMAPHORE_Lock(BOARD_GPIO_LED_RDC_PDAP);

    GPIO_Init(BOARD_GPIO_LED_CONFIG->base, &ledInit);

    RDC_SEMAPHORE_Unlock(BOARD_GPIO_LED_RDC_PDAP);
#endif
}

static void GPIO_Ctrl_InitKeyPin()
{
#ifdef BOARD_GPIO_KEY_CONFIG
    gpio_init_config_t keyInit = {
        .pin           = BOARD_GPIO_KEY_CONFIG->pin,
        .direction     = gpioDigitalInput,
        .interruptMode = gpioIntFallingEdge
    };

    /* Acquire RDC semaphore before access GPIO to avoid conflict, it's
     * necessary when GPIO RDC is configured as Semaphore Required */
    RDC_SEMAPHORE_Lock(BOARD_GPIO_KEY_RDC_PDAP);

    GPIO_Init(BOARD_GPIO_KEY_CONFIG->base, &keyInit);

    RDC_SEMAPHORE_Unlock(BOARD_GPIO_KEY_RDC_PDAP);

    /* Set GPT interrupt priority 3 */
    NVIC_SetPriority(BOARD_GPIO_KEY_IRQ_NUM, 3);
#endif
}

void GPIO_Ctrl_Init()
{
    xSemaphore = xSemaphoreCreateBinary();

    GPIO_Ctrl_InitLedPin();
    GPIO_Ctrl_InitKeyPin();
}

void GPIO_Ctrl_ToggleLed()
{
    static bool on = false;

#ifdef BOARD_GPIO_LED_CONFIG
    RDC_SEMAPHORE_Lock(BOARD_GPIO_LED_RDC_PDAP);

    GPIO_WritePinOutput(BOARD_GPIO_LED_CONFIG->base,
            BOARD_GPIO_LED_CONFIG->pin, on ? gpioPinSet : gpioPinClear);

    RDC_SEMAPHORE_Unlock(BOARD_GPIO_LED_RDC_PDAP);
#else
    PRINTF("%c ", on ? '+' : '-');
#endif

    on = !on;
}

void GPIO_Ctrl_WaitKeyPressed()
{
#ifdef BOARD_GPIO_KEY_CONFIG
    uint32_t i, debounce;

    do
    {
        debounce = 0;

        RDC_SEMAPHORE_Lock(BOARD_GPIO_KEY_RDC_PDAP);

        /* Clear the interrupt state */
        GPIO_ClearStatusFlag(BOARD_GPIO_KEY_CONFIG->base, BOARD_GPIO_KEY_CONFIG->pin);
        /* Enable GPIO pin interrupt */
        GPIO_SetPinIntMode(BOARD_GPIO_KEY_CONFIG->base, BOARD_GPIO_KEY_CONFIG->pin, true);

        RDC_SEMAPHORE_Unlock(BOARD_GPIO_KEY_RDC_PDAP);

        /* Enable the IRQ. */
        NVIC_EnableIRQ(BOARD_GPIO_KEY_IRQ_NUM);

        PRINTF("\n\rPress the (%s) key to switch the blinking frequency:\n\r", BOARD_GPIO_KEY_CONFIG->name);
        xSemaphoreTake(xSemaphore, portMAX_DELAY);

        for (i = 0; i < 3; i++)
        {
            /* Susupend Task to wait Key stable. */
            vTaskDelay(5);

            /* Check key value. */
            if (0 == GPIO_ReadPinInput(BOARD_GPIO_KEY_CONFIG->base, BOARD_GPIO_KEY_CONFIG->pin))
            {
                /* Increase debounce counter. */
                debounce ++;
            }
        }

        if (debounce >= 2)
        {
            break;
        }
    } while (1);
#else
    /* Without key on board, we return every 5 seconds */
    PRINTF("\n\rWait 5 seconds to switch blinking frequency:\n\r");
    xSemaphoreTake(xSemaphore, configTICK_RATE_HZ * 5);
#endif
}

#ifdef BOARD_GPIO_KEY_CONFIG
void BOARD_GPIO_KEY_HANDLER()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* When user input captured, we disable GPIO interrupt */
    NVIC_DisableIRQ(BOARD_GPIO_KEY_IRQ_NUM);

    RDC_SEMAPHORE_Lock(BOARD_GPIO_KEY_RDC_PDAP);

    /* Disable GPIO pin interrupt */
    GPIO_SetPinIntMode(BOARD_GPIO_KEY_CONFIG->base, BOARD_GPIO_KEY_CONFIG->pin, false);
    /* Clear the interrupt state */
    GPIO_ClearStatusFlag(BOARD_GPIO_KEY_CONFIG->base, BOARD_GPIO_KEY_CONFIG->pin);

    RDC_SEMAPHORE_Unlock(BOARD_GPIO_KEY_RDC_PDAP);

    /* Unlock the task to process the event. */
    xSemaphoreGiveFromISR(xSemaphore, &xHigherPriorityTaskWoken);

    /* Perform a context switch to wake the higher priority task. */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
#endif
