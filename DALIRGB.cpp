#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "WS_DALI.h"
#include "task.h"
#include "utils.h"
#include "hardware/pio.h"
#include "ws2812/generated/ws2812.pio.h"
//#include "ws2812/generated/ws2801.pio.h"

#define IS_RGBW false
#define LED_CLK1 21
#define LED_DATA1 20
#define LED_CLK0 19
#define LED_DATA0 18

static LedState g_led_state = {PATTERN_OFF, 0, 0, 0, 0};
static volatile uint32_t g_dali_rx_count = 0;
uint32_t output_array[16]={};
uint8_t currentSequenceLength = 16;
uint8_t brightness = 30;
//-------------------------------------------------------------------------------------------
static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}
//-------------------------------------------------------------------------------------------
/// @brief transfer array of colours to physical light string
void show()
{
    int k = 0;
    int n = currentSequenceLength;

    for (int i = 0; i < n; i++)
    {
        put_pixel(output_array[i]);
    }
}
//-------------------------------------------------------------------------------------------
static void apply_dali_frame(const DALI_ForwardFrame& frame)
{
    

    if (!frame.is_command) {
        // Direct arc power command maps to global intensity.
        g_led_state.level = frame.data_byte;
        g_led_state.pattern = (frame.data_byte == 0) ? PATTERN_OFF : PATTERN_FILL;
        return;
    }

    switch (frame.data_byte) {
        case DALI_OFF:
            printf("OFF: %02X ",frame.data_byte);
            g_led_state.color = 16711799;
            g_led_state.pattern = PATTERN_OFF;
            g_led_state.level = 0;
            break;
        case DALI_RECALL_MAX_LEVEL:
            printf("MAX: %02X ",frame.data_byte);
            g_led_state.pattern = PATTERN_FILL;
            g_led_state.speed = 100;
            g_led_state.level = 254;
            break;
        case DALI_GO_TO_SCENE0:
            printf("SC0: %02X ",frame.data_byte);
            g_led_state.color = 16711799;
            g_led_state.speed = 100;
            g_led_state.pattern = PATTERN_BREATH;
            break;
        case DALI_GO_TO_SCENE1:
            printf("SC1: %02X ",frame.data_byte);
            g_led_state.speed = 100;
            g_led_state.color = 16711799;
            g_led_state.pattern = PATTERN_RAINBOW;
            break;
        case DALI_GO_TO_SCENE2:
            printf("SC2: %02X ",frame.data_byte);
            g_led_state.color = 16711799;
            g_led_state.speed = 100;
            g_led_state.pattern = PATTERN_CHASE;
            break;
        case DALI_GO_TO_SCENE3:
            printf("SC3: %02X ",frame.data_byte);
            g_led_state.color = 16711799;
            g_led_state.speed = 100;
            g_led_state.pattern = PATTERN_STROBE;
            break;
        case DALI_GO_TO_SCENE4:
            printf("SC5: %02X ",frame.data_byte);
            g_led_state.pattern = PATTERN_SPARKLE;
            break;
        default:
            break;
    }
}
//-------------------------------------------------------------------------------------------
static bool PollDaliMessageNonBlocking()
{
    DALI_ForwardFrame frame;
    if (!DALI_GetNextForwardFrame(&frame, 0)) {
        return false;
    }

    g_dali_rx_count++;
    apply_dali_frame(frame);
    printf("DALI RX: addr=0%02X (0x%02X) data=0x%02X cmd=%u grp=%u (0x%02X)  brd=%u\r\n", frame.short_address, frame.address_byte, frame.data_byte, frame.is_command ? 1u : 0u,frame.is_group ? 1u : 0u, frame.group_address, frame.is_broadcast ? 1u : 0u);
    return true;
}

//-------------------------------------------------------------------------------------------
extern "C" void vAssertCalled(const char* file, int line) {
     // Print to USB-CDC if enabled; then halt
     printf("ASSERT: %s:%d\n", file, line);
     taskDISABLE_INTERRUPTS();
     for(;;);
 }
//-------------------------------------------------------------------------------------------
/// @brief initialise input output pins
void initGPIO()
{
    gpio_init(LED_DATA0);
    gpio_set_dir(LED_DATA0, GPIO_OUT);
    gpio_init(LED_CLK0);
    gpio_set_dir(LED_CLK0, GPIO_OUT);
    gpio_init(LED_DATA1);
    gpio_set_dir(LED_DATA1, GPIO_OUT);
    gpio_init(LED_CLK1);
    gpio_set_dir(LED_CLK1, GPIO_OUT);
}
//-------------------------------------------------------------------------------------------
/// @brief initialise PIO & Load LED driver code
void initPIO()
{
    PIO pio = pio0;
    //PIO pioa = pio1;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, 0, offset, LED_CLK1, 800000, IS_RGBW);
    //uint offset1 = pio_add_program(pioa, &ws2812_program);
    //ws2812_program_init(pioa, 0, offset1, LED_DATA1, 800000, IS_RGBW);
}
//-------------------------------------------------------------------------------------------
void clear_pattern(bool doShow)
{
    uint8_t n = currentSequenceLength;
    for (int i = 0; i < n; i++)
    {
        output_array[i] = 0;
    }
    if (doShow)
    {
        show();
    }
}
//-------------------------------------------------------------------------------------------
static void startup_led_test()
{
    const uint32_t test_colors[] = {
        urgb_u32(255, 0, 0),
        urgb_u32(0, 255, 0),
        urgb_u32(0, 0, 255),
        urgb_u32(255, 255, 255)
    };
    const int hold_ms = 250;

    printf("LED startup test\n");

    for (size_t c = 0; c < (sizeof(test_colors) / sizeof(test_colors[0])); c++) {
        for (int i = 0; i < currentSequenceLength; i++) {
            output_array[i] = test_colors[c];
        }
        show();
        sleep_ms(hold_ms);
    }

    clear_pattern(true);
    sleep_ms(100);
}
//-------------------------------------------------------------------------------------------
void cycleRainbow(int delay)
{
    // Set saturation and value to full brightness
    uint8_t saturation = 255;
    uint8_t value = ReScale(brightness, 0, 100, 0, 255);
    uint8_t n = currentSequenceLength;
    // Cycle through hues from 0 to 360 degrees in steps of 5 degrees
    
        for (float hue = 0.0; hue < 360.0; hue += 5) //(360/n)*2)
        {
            for (int i = 0; i < n; i++)
            {
                output_array[i] = hsl2rgb360(hue, saturation, value);
                show();
                sleep_ms(delay);
                if (PollDaliMessageNonBlocking()) { return; }
            }
        }
}
//-------------------------------------------------------------------------------------------
//-[8]- All LEDs pulse brightness up and down
void breath(uint32_t color, int delay) {
        for (int b = 1; b <= 100; b++) {
            uint32_t c = rgbBrightness(color, b);
            for (int i = 0; i < currentSequenceLength; i++)
                output_array[i] = c;
            show();
            sleep_ms(delay);
            if (PollDaliMessageNonBlocking()) { return; }
        }
        sleep_ms(delay * 10);
        if (PollDaliMessageNonBlocking()) { return; }
        for (int b = 100; b >= 0; b--) {
            uint32_t c = rgbBrightness(color, b);
            for (int i = 0; i < currentSequenceLength; i++)
                output_array[i] = c;
            show();
            sleep_ms(delay);
            if (PollDaliMessageNonBlocking()) { return; }
        }
        sleep_ms(delay * 5);
        if (PollDaliMessageNonBlocking()) { return; }
}
//-------------------------------------------------------------------------------------------
//-[9]- Rapid strobe flash
void strobe(uint32_t color, int delay) {
        for (int i = 0; i < currentSequenceLength; i++)
            output_array[i] = color;
        show();
        sleep_ms(delay);
        if (PollDaliMessageNonBlocking()) { return; }
        clear_pattern(true);
        sleep_ms(delay * 4);
        if (PollDaliMessageNonBlocking()) { return; }
}
//-------------------------------------------------------------------------------------------
//-[10]- Single pixel bouncing back and forth
void chase(uint32_t color, int delay) {
    uint8_t n = currentSequenceLength;
        for (int i = 0; i < n; i++) {
            clear_pattern(false);
            output_array[i] = color;
            show();
            sleep_ms(delay);
            if (PollDaliMessageNonBlocking()) { return; }
        }
        for (int i = n - 2; i > 0; i--) {
            clear_pattern(false);
            output_array[i] = color;
            show();
            sleep_ms(delay);
            if (PollDaliMessageNonBlocking()) { return; }
        }
}
//-------------------------------------------------------------------------------------------
//-[11]- Random LEDs twinkle each frame
void sparkle(uint32_t color, int delay) {
    uint8_t n = currentSequenceLength;
        clear_pattern(false);
        for (int i = 0; i < n; i++) {
            if (rand() % 4 == 0)
                output_array[i] = color;
        }
        show();
        sleep_ms(delay);
        if (PollDaliMessageNonBlocking()) { return; }
}
//-------------------------------------------------------------------------------------------
//-[12]- Fill all LEDs one-by-one then drain one-by-one
void fill_drain(uint32_t color, int delay) {
    uint8_t n = currentSequenceLength;
        for (int i = 0; i < n; i++) {
            output_array[i] = color;
            show();
            sleep_ms(delay);
            if (PollDaliMessageNonBlocking()) { return; }
        }
        sleep_ms(delay * 5);
        if (PollDaliMessageNonBlocking()) { return; }
        for (int i = 0; i < n; i++) {
            output_array[i] = 0;
            show();
            sleep_ms(delay);
            if (PollDaliMessageNonBlocking()) { return; }
        }
        sleep_ms(delay * 5);
        if (PollDaliMessageNonBlocking()) { return; }
}
//-------------------------------------------------------------------------------------------
static bool RunActivePatternStep()
{
    uint8_t n = currentSequenceLength;
   // printf("step\n");
    switch (g_led_state.pattern) {
        case PATTERN_OFF:
            printf("clear\n");
            clear_pattern(true);
            break;
        case PATTERN_BREATH:
            printf("breath\n");
            breath(g_led_state.color,g_led_state.speed);
            break;
        case PATTERN_RAINBOW:
            printf("rainbow\n");
            cycleRainbow(g_led_state.speed);
            break;
        case PATTERN_STROBE:
            printf("strobe\n");
            strobe(g_led_state.color,g_led_state.speed);
            break;
        case PATTERN_CHASE:
            printf("chase\n");
            chase(g_led_state.color,g_led_state.speed);
            break;
        case PATTERN_SPARKLE:
            printf("sparkle\n");
            sparkle(g_led_state.color,g_led_state.speed);
            break;
        case PATTERN_FILL:
            printf("fill\n");
            fill_drain(g_led_state.color,g_led_state.speed);
            break;
    }
    return false;
}
//-------------------------------------------------------------------------------------------
 static void vLEDTask(void *pv) {
     (void)pv;
     TickType_t last_wake = xTaskGetTickCount();
     uint32_t last_rx_handled = g_dali_rx_count;
     bool off_cleared = false;

     while (1) {
         (void)PollDaliMessageNonBlocking();

         if (g_dali_rx_count != last_rx_handled) {
             last_rx_handled = g_dali_rx_count;

             if (g_led_state.pattern == PATTERN_OFF) {
                 clear_pattern(true);
                 off_cleared = true;
             } else {
                 off_cleared = false;
                 (void)RunActivePatternStep();
             }
         } else if (g_led_state.pattern == PATTERN_OFF && !off_cleared) {
             // Ensure LEDs are cleared once after boot even before first message.
             clear_pattern(true);
             off_cleared = true;
         }

         vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(25));
     }
 }
//-------------------------------------------------------------------------------------------

 int main(void) {
    const UBaseType_t core0_mask = 1 << 0; // core 0
    const UBaseType_t core1_mask = 1 << 1; // core

    stdio_init_all();
    sleep_ms(1500);
    printf("Boot: DALIRGB starting\r\n");
    
    // Force DALI pins to a passive state immediately on boot.
    gpio_init(TX_PIN);
    gpio_set_dir(TX_PIN, GPIO_IN);
    gpio_disable_pulls(TX_PIN);
    gpio_init(RX_PIN);
    gpio_set_dir(RX_PIN, GPIO_IN);
    gpio_disable_pulls(RX_PIN);

    initGPIO();
    sleep_ms(20);
    initPIO();    
    startup_led_test();
    
    

    DALI_SetDeviceShortAddress(5);
    DALI_SetDeviceGroupMask(0);
    DALI_SetPromiscuousMode(false);
    DALI_SetListenOnly(true);

    hard_assert(DALI_StartService(core0_mask, tskIDLE_PRIORITY + 2, 1024));

    xTaskCreateAffinitySet(vLEDTask, "led", 512, NULL, tskIDLE_PRIORITY + 1, core1_mask, NULL);
    vTaskStartScheduler();  // hands control to FreeRTOS (never returns)
    for(;;);                // safety
 }

