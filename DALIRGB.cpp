#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "WS_DALI.h"
#include "task.h"
#include "utils.h"
#include "hardware/pio.h"
#include "ws2812/generated/ws2812.pio.h"
//#include "ws2812/generated/ws2801.pio.h"

#define IS_RGBW false
#define LED_CLK1 21 // not happy on this pin
#define LED_DATA1 20
#define LED_CLK0 19
#define LED_DATA0 18

static const uint8_t kDaliAddressCount = 64;
static const uint8_t kDaliGroupCount = 16;
static const uint8_t kDaliGroupLedBase = 80;
static const uint8_t kDaliBroadcastLedIndex = 96;
static const uint8_t kMonitorLedCount = kDaliBroadcastLedIndex + 1;
static const uint8_t kDaliMinVisualLevel = 1;
static const uint8_t kDaliMaxVisualLevel = 254;
static const uint8_t kDaliStepVisualLevel = 25;

enum ConsoleLedMode {
    CONSOLE_LED_MODE_NORMAL = 0,
    CONSOLE_LED_MODE_MONITOR
};

struct DaliLampVisualState {
    uint8_t level;
    uint8_t last_active_level;
};

static LedState g_led_state = {PATTERN_OFF, 0, 0, 0, 0};
static volatile uint32_t g_dali_rx_count = 0;
static volatile ConsoleLedMode g_console_led_mode = CONSOLE_LED_MODE_NORMAL;
static bool g_manual_promiscuous_mode = false;
static uint8_t g_dali_short_address = 5;
static uint16_t g_dali_group_mask = 0;
static DaliLampVisualState g_address_visuals[kDaliAddressCount] = {};
static DaliLampVisualState g_group_visuals[kDaliGroupCount] = {};
static DaliLampVisualState g_broadcast_visual = {};
uint32_t output_array[kMonitorLedCount]={};
uint8_t currentSequenceLength = kMonitorLedCount;
uint8_t brightness = 30;

static void apply_dali_visual_frame(const DALI_ForwardFrame& frame);
static void set_console_led_mode(ConsoleLedMode mode);
static void sync_promiscuous_mode();
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
    if (g_console_led_mode == CONSOLE_LED_MODE_MONITOR) {
        apply_dali_visual_frame(frame);
    } else {
        apply_dali_frame(frame);
    }
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
    ws2812_program_init(pio, 0, offset, LED_DATA0, 800000, IS_RGBW);
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
static const char* console_led_mode_name()
{
    return (g_console_led_mode == CONSOLE_LED_MODE_MONITOR) ? "monitor" : "normal";
}

//-------------------------------------------------------------------------------------------
static uint32_t dali_level_to_visual_color(uint8_t level)
{
    uint8_t green = (uint8_t)(((uint16_t)level * 255u) / kDaliMaxVisualLevel);
    uint8_t red = (uint8_t)(255u - green);
    return urgb_u32(red, green, 0);
}

//-------------------------------------------------------------------------------------------
static void sync_promiscuous_mode()
{
    DALI_SetPromiscuousMode(g_manual_promiscuous_mode || (g_console_led_mode == CONSOLE_LED_MODE_MONITOR));
}

//-------------------------------------------------------------------------------------------
static void reset_dali_visuals()
{
    for (int i = 0; i < kDaliAddressCount; i++) {
        g_address_visuals[i].level = 0;
        g_address_visuals[i].last_active_level = kDaliMaxVisualLevel;
    }

    for (int i = 0; i < kDaliGroupCount; i++) {
        g_group_visuals[i].level = 0;
        g_group_visuals[i].last_active_level = kDaliMaxVisualLevel;
    }

    g_broadcast_visual.level = 0;
    g_broadcast_visual.last_active_level = kDaliMaxVisualLevel;
}

//-------------------------------------------------------------------------------------------
static void render_dali_visuals()
{
    for (int i = 0; i < kDaliAddressCount; i++) {
        output_array[i] = dali_level_to_visual_color(g_address_visuals[i].level);
    }

    for (int i = kDaliAddressCount; i < kDaliGroupLedBase; i++) {
        output_array[i] = 0;
    }

    for (int i = 0; i < kDaliGroupCount; i++) {
        output_array[kDaliGroupLedBase + i] = dali_level_to_visual_color(g_group_visuals[i].level);
    }

    output_array[kDaliBroadcastLedIndex] = dali_level_to_visual_color(g_broadcast_visual.level);
    show();
}

//-------------------------------------------------------------------------------------------
static void set_console_led_mode(ConsoleLedMode mode)
{
    g_console_led_mode = mode;
    sync_promiscuous_mode();

    if (mode == CONSOLE_LED_MODE_MONITOR) {
        reset_dali_visuals();
        render_dali_visuals();
    } else {
        g_led_state.pattern = PATTERN_OFF;
        clear_pattern(true);
    }
}

//-------------------------------------------------------------------------------------------
static void dali_visual_apply_level(DaliLampVisualState* state, uint8_t level)
{
    if (state == NULL) {
        return;
    }

    state->level = level;
    if (level > 0) {
        state->last_active_level = level;
    }
}

//-------------------------------------------------------------------------------------------
static void dali_visual_step_level(DaliLampVisualState* state, int delta)
{
    if (state == NULL) {
        return;
    }

    int next = (int)state->level + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > kDaliMaxVisualLevel) {
        next = kDaliMaxVisualLevel;
    }

    dali_visual_apply_level(state, (uint8_t)next);
}

//-------------------------------------------------------------------------------------------
static void dali_visual_apply_command(DaliLampVisualState* state, const DALI_ForwardFrame& frame)
{
    if (state == NULL) {
        return;
    }

    if (!frame.is_command) {
        if (frame.data_byte <= kDaliMaxVisualLevel) {
            dali_visual_apply_level(state, frame.data_byte);
        }
        return;
    }

    switch (frame.data_byte) {
        case DALI_OFF:
            dali_visual_apply_level(state, 0);
            break;
        case DALI_RECALL_MAX_LEVEL:
            dali_visual_apply_level(state, kDaliMaxVisualLevel);
            break;
        case DALI_RECALL_MIN_LEVEL:
            dali_visual_apply_level(state, kDaliMinVisualLevel);
            break;
        case DALI_UP:
        case DALI_STEP_UP:
            dali_visual_step_level(state, kDaliStepVisualLevel);
            break;
        case DALI_DOWN:
        case DALI_STEP_DOWN:
            dali_visual_step_level(state, -kDaliStepVisualLevel);
            break;
        case DALI_STEP_DOWN_AND_OFF:
            if (state->level <= kDaliStepVisualLevel) {
                dali_visual_apply_level(state, 0);
            } else {
                dali_visual_step_level(state, -kDaliStepVisualLevel);
            }
            break;
        case DALI_ON_AND_STEP_UP:
            if (state->level == 0) {
                dali_visual_apply_level(state, state->last_active_level > 0 ? state->last_active_level : kDaliMaxVisualLevel);
            } else {
                dali_visual_step_level(state, kDaliStepVisualLevel);
            }
            break;
        case DALI_GO_TO_LAST_ACTIVE_LEVEL:
            dali_visual_apply_level(state, state->last_active_level > 0 ? state->last_active_level : kDaliMaxVisualLevel);
            break;
        default:
            break;
    }
}

//-------------------------------------------------------------------------------------------
static void apply_dali_visual_frame(const DALI_ForwardFrame& frame)
{
    if (frame.is_broadcast) {
        for (int i = 0; i < kDaliAddressCount; i++) {
            dali_visual_apply_command(&g_address_visuals[i], frame);
        }
        dali_visual_apply_command(&g_broadcast_visual, frame);
        render_dali_visuals();
        return;
    }

    if (frame.is_group) {
        if (frame.group_address < kDaliGroupCount) {
            dali_visual_apply_command(&g_group_visuals[frame.group_address], frame);
            render_dali_visuals();
        }
        return;
    }

    if (frame.short_address < kDaliAddressCount) {
        dali_visual_apply_command(&g_address_visuals[frame.short_address], frame);
        render_dali_visuals();
    }
}

//-------------------------------------------------------------------------------------------
static void print_console_help()
{
    printf("\r\nUSB console commands:\r\n");
    printf("  help                 Show this menu\r\n");
    printf("  status               Show current settings\r\n");
    printf("  mode <normal|monitor> Set LED behavior mode\r\n");
    printf("  prom <0|1>           Set promiscuous mode\r\n");
    printf("  listen <0|1>         Set listen-only mode\r\n");
    printf("  invert <0|1>         Set RX inversion\r\n");
    printf("  addr <0-63>          Set device short address\r\n");
    printf("  group <0-65535>      Set device group mask\r\n");
    printf("  defaults             Restore startup defaults\r\n");
    printf("\r\n");
}

//-------------------------------------------------------------------------------------------
static void print_console_status()
{
    printf("Settings: mode=%s prom=%u manualProm=%u listen=%u invert=%u addr=%u group=0x%04X rx=%lu\r\n",
           console_led_mode_name(),
           DALI_GetPromiscuousMode() ? 1u : 0u,
           g_manual_promiscuous_mode ? 1u : 0u,
           DALI_GetListenOnly() ? 1u : 0u,
           DALI_GetRxInverted() ? 1u : 0u,
           g_dali_short_address,
           g_dali_group_mask,
           (unsigned long)g_dali_rx_count);
}

//-------------------------------------------------------------------------------------------
static bool parse_bool_arg(const char* value, bool* out)
{
    if (value == NULL || out == NULL) {
        return false;
    }

    if (strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "true") == 0) {
        *out = true;
        return true;
    }

    if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0 || strcmp(value, "false") == 0) {
        *out = false;
        return true;
    }

    return false;
}

//-------------------------------------------------------------------------------------------
static void apply_default_dali_settings()
{
    g_manual_promiscuous_mode = false;
    g_dali_short_address = 5;
    g_dali_group_mask = 0;

    DALI_SetDeviceShortAddress(g_dali_short_address);
    DALI_SetDeviceGroupMask(g_dali_group_mask);
    set_console_led_mode(CONSOLE_LED_MODE_NORMAL);
    DALI_SetListenOnly(true);
    DALI_SetRxInverted(false);
}

//-------------------------------------------------------------------------------------------
static void handle_console_command(char* line)
{
    char* command = strtok(line, " \t");
    if (command == NULL) {
        return;
    }

    if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
        print_console_help();
        return;
    }

    if (strcmp(command, "status") == 0) {
        print_console_status();
        return;
    }

    if (strcmp(command, "mode") == 0) {
        char* value = strtok(NULL, " \t");
        if (value == NULL) {
            printf("Missing mode value. Use normal or monitor.\r\n");
            return;
        }
        if (strcmp(value, "normal") == 0) {
            set_console_led_mode(CONSOLE_LED_MODE_NORMAL);
            print_console_status();
            return;
        }
        if (strcmp(value, "monitor") == 0) {
            set_console_led_mode(CONSOLE_LED_MODE_MONITOR);
            print_console_status();
            return;
        }
        printf("Unknown mode: %s\r\n", value);
        return;
    }

    if (strcmp(command, "defaults") == 0) {
        apply_default_dali_settings();
        print_console_status();
        return;
    }

    char* value = strtok(NULL, " \t");
    if (value == NULL) {
        printf("Missing value. Type 'help' for commands.\r\n");
        return;
    }

    if (strcmp(command, "prom") == 0) {
        bool enabled = false;
        if (!parse_bool_arg(value, &enabled)) {
            printf("Invalid prom value: %s\r\n", value);
            return;
        }
        g_manual_promiscuous_mode = enabled;
        sync_promiscuous_mode();
        print_console_status();
        return;
    }

    if (strcmp(command, "listen") == 0) {
        bool enabled = false;
        if (!parse_bool_arg(value, &enabled)) {
            printf("Invalid listen value: %s\r\n", value);
            return;
        }
        DALI_SetListenOnly(enabled);
        print_console_status();
        return;
    }

    if (strcmp(command, "invert") == 0) {
        bool enabled = false;
        if (!parse_bool_arg(value, &enabled)) {
            printf("Invalid invert value: %s\r\n", value);
            return;
        }
        DALI_SetRxInverted(enabled);
        print_console_status();
        return;
    }

    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 0);
    if (end == value || *end != '\0') {
        printf("Invalid numeric value: %s\r\n", value);
        return;
    }

    if (strcmp(command, "addr") == 0) {
        if (parsed > 63) {
            printf("Address out of range: %lu\r\n", parsed);
            return;
        }
        g_dali_short_address = (uint8_t)parsed;
        DALI_SetDeviceShortAddress(g_dali_short_address);
        print_console_status();
        return;
    }

    if (strcmp(command, "group") == 0) {
        if (parsed > 0xFFFFu) {
            printf("Group mask out of range: %lu\r\n", parsed);
            return;
        }
        g_dali_group_mask = (uint16_t)parsed;
        DALI_SetDeviceGroupMask(g_dali_group_mask);
        print_console_status();
        return;
    }

    printf("Unknown command: %s\r\n", command);
}

//-------------------------------------------------------------------------------------------
static void vConsoleTask(void *pv)
{
    (void)pv;
    char line[64] = {0};
    size_t line_len = 0;
    bool last_was_cr = false;

    print_console_help();
    print_console_status();
    printf("> ");

    while (1) {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (ch == '\n' && last_was_cr) {
            last_was_cr = false;
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            last_was_cr = (ch == '\r');
            if (line_len > 0) {
                line[line_len] = '\0';
                printf("\r\n");
                handle_console_command(line);
                line_len = 0;
            }
            printf("> ");
            continue;
        }

        last_was_cr = false;

        if (ch == '\b' || ch == 127) {
            if (line_len > 0) {
                line_len--;
                printf("\b \b");
            }
            continue;
        }

        if (ch < 32 || ch > 126) {
            continue;
        }

        if (line_len < (sizeof(line) - 1)) {
            line[line_len++] = (char)ch;
            putchar(ch);
        }
    }
}

//-------------------------------------------------------------------------------------------
 static void vLEDTask(void *pv) {
     (void)pv;
     TickType_t last_wake = xTaskGetTickCount();
     uint32_t last_rx_handled = g_dali_rx_count;
     bool off_cleared = false;

     while (1) {
         if (g_console_led_mode == CONSOLE_LED_MODE_MONITOR) {
             (void)PollDaliMessageNonBlocking();
             vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(25));
             continue;
         }

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
    
    

    apply_default_dali_settings();

    hard_assert(DALI_StartService(core0_mask, tskIDLE_PRIORITY + 2, 1024));

    xTaskCreateAffinitySet(vConsoleTask, "console", 768, NULL, tskIDLE_PRIORITY + 1, core0_mask, NULL);
    xTaskCreateAffinitySet(vLEDTask, "led", 512, NULL, tskIDLE_PRIORITY + 1, core1_mask, NULL);
    vTaskStartScheduler();  // hands control to FreeRTOS (never returns)
    for(;;);                // safety
 }

