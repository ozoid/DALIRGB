#include "WS_DALI.h"

#include <cstdio>

#include "FreeRTOS.h"
#include "queue.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "semphr.h"
#include "task.h"

Dali dali;
uint8_t DALI_Addr[64] = {0};
uint8_t DALI_NUM = 0;

static repeating_timer_t dali_timer;
static bool dali_timer_started = false;
static QueueHandle_t dali_rx_frame_queue = NULL;
static uint8_t dali_device_short_address = 0xFF;
static uint16_t dali_device_group_mask = 0;
static volatile bool dali_promiscuous_mode = false;
static volatile bool dali_listen_only = true;
static volatile bool dali_rx_inverted = false;
static volatile DALI_DebugStats dali_stats = {};

enum DaliOp : uint8_t {
  DALI_OP_CMD,
  DALI_OP_SET_LEVEL,
  DALI_OP_COMMISSION
};

struct DaliRequest {
  DaliOp op;
  uint16_t cmd;
  uint8_t arg0;
  uint8_t arg1;
  int16_t result;
  SemaphoreHandle_t done;
};

static QueueHandle_t dali_queue = NULL;
static TaskHandle_t dali_task_handle = NULL;
static bool dali_probe_init = false;
static uint8_t dali_probe_last_level = 1;
static uint16_t dali_probe_run_len = 0;

static void dali_probe_commit_run(uint16_t run_len)
{
  if (run_len <= 2) {
    dali_stats.rx_run_1_2++;
  } else if (run_len <= 5) {
    dali_stats.rx_run_3_5++;
  } else if (run_len <= 12) {
    dali_stats.rx_run_6_12++;
  } else {
    dali_stats.rx_run_13_plus++;
  }
}

static uint8_t dali_guess_signal_kind()
{
  if (dali_stats.rx_raw_low_samples == 0 && dali_stats.rx_raw_high_samples > 1000)
    return 1; // stuck high
  if (dali_stats.rx_raw_high_samples == 0 && dali_stats.rx_raw_low_samples > 1000)
    return 2; // stuck low

  uint32_t transitions = dali_stats.rx_raw_transitions;
  if (transitions < 20)
    return 0; // unknown / not enough data

  uint32_t dali_like = dali_stats.rx_run_3_5;
  uint32_t serial_like = dali_stats.rx_run_1_2;
  uint32_t filtered_like = dali_stats.rx_run_13_plus;

  if (dali_like > (serial_like + filtered_like))
    return 3; // likely Manchester DALI-shaped

  if (serial_like >= dali_like || filtered_like > dali_like)
    return 4; // likely serial/filtered/non-DALI waveform

  return 0;
}

static void dali_print_raw_dump(const char* tag, uint8_t rx_len)
{
  (void)tag;
  (void)rx_len;
  return;

  uint8_t raw[8] = {0};
  uint8_t bits = dali.debug_get_last_rx_sample_bits();
  uint8_t bytes = dali.debug_copy_last_rx_samples(raw, sizeof(raw));
  if (bits == 0 || bytes == 0) {
    return;
  }

  char sample_bits[65];
  uint8_t bit_count = bits > 64 ? 64 : bits;
  for (uint8_t i = 0; i < bit_count; i++) {
    uint8_t b = raw[i >> 3];
    sample_bits[i] = ((b >> (7 - (i & 0x07))) & 0x01) ? '1' : '0';
  }
  sample_bits[bit_count] = '\0';

  dali_stats.rx_raw_dump_count++;
  printf("DALI raw[%s] len=%u bits=%u inv=%u: %s\r\n",
         tag,
         (unsigned int)rx_len,
         (unsigned int)bit_count,
         (unsigned int)(dali_rx_inverted ? 1 : 0),
         sample_bits);
}

static DALI_ForwardFrame dali_parse_forward_frame(uint8_t address_byte, uint8_t data_byte)
{
  DALI_ForwardFrame frame = {};
  frame.address_byte = address_byte;
  frame.data_byte = data_byte;
  frame.is_command = (address_byte & 0x01) != 0;
  frame.is_broadcast = (address_byte == 0xFF) || (address_byte == 0xFE);
  frame.is_group = ((address_byte & 0x80) != 0) && !frame.is_broadcast;
  frame.short_address = (address_byte >> 1) & 0x3F;
  frame.group_address = (address_byte >> 1) & 0x0F;
  return frame;
}

bool DALI_FrameMatchesDevice(const DALI_ForwardFrame* frame)
{
  if (frame == NULL) {
    return false;
  }

  if (dali_promiscuous_mode) {
    return true;
  }
  
  if (frame->is_broadcast) {
    return true;
  }

  if (frame->is_group) {
    if (frame->group_address < 16) {
      return (dali_device_group_mask & (1u << frame->group_address)) != 0;
    }
    return false;
  }

  return frame->short_address == dali_device_short_address;
}

void DALI_SetPromiscuousMode(bool enabled)
{
  dali_promiscuous_mode = enabled;
}

bool DALI_GetPromiscuousMode()
{
  return dali_promiscuous_mode;
}

void DALI_SetListenOnly(bool enabled)
{
  dali_listen_only = enabled;
  if (enabled) {
    gpio_set_dir(TX_PIN, GPIO_IN);
    gpio_disable_pulls(TX_PIN);
  } else {
    gpio_set_dir(TX_PIN, GPIO_OUT);
    gpio_put(TX_PIN, 1);
  }
}

bool DALI_GetListenOnly()
{
  return dali_listen_only;
}

void DALI_SetRxInverted(bool enabled)
{
  dali_rx_inverted = enabled;
}

bool DALI_GetRxInverted()
{
  return dali_rx_inverted;
}

void DALI_SetDeviceShortAddress(uint8_t short_addr)
{
  dali_device_short_address = short_addr & 0x3F;
}

void DALI_SetDeviceGroupMask(uint16_t group_mask)
{
  dali_device_group_mask = group_mask;
}

bool DALI_GetNextForwardFrame(DALI_ForwardFrame* frame, uint32_t timeout_ms)
{
  if (frame == NULL || dali_rx_frame_queue == NULL) {
    return false;
  }

  TickType_t wait_ticks = pdMS_TO_TICKS(timeout_ms);
  return xQueueReceive(dali_rx_frame_queue, frame, wait_ticks) == pdTRUE;
}

void DALI_GetDebugStats(DALI_DebugStats* stats)
{
  if (stats == NULL) {
    return;
  }
  stats->rx_frames_total = dali_stats.rx_frames_total;
  stats->rx_frames_matched = dali_stats.rx_frames_matched;
  stats->rx_frames_filtered = dali_stats.rx_frames_filtered;
  stats->rx_queue_drops = dali_stats.rx_queue_drops;
  stats->rx_decode_errors = dali_stats.rx_decode_errors;
  stats->rx_busy_count = dali_stats.rx_busy_count;
  stats->rx_other_len_count = dali_stats.rx_other_len_count;
  stats->rx_raw_high_samples = dali_stats.rx_raw_high_samples;
  stats->rx_raw_low_samples = dali_stats.rx_raw_low_samples;
  stats->rx_raw_transitions = dali_stats.rx_raw_transitions;
  stats->rx_run_1_2 = dali_stats.rx_run_1_2;
  stats->rx_run_3_5 = dali_stats.rx_run_3_5;
  stats->rx_run_6_12 = dali_stats.rx_run_6_12;
  stats->rx_run_13_plus = dali_stats.rx_run_13_plus;
  stats->tx_drive_low_calls = dali_stats.tx_drive_low_calls;
  stats->tx_release_calls = dali_stats.tx_release_calls;
  stats->rx_raw_dump_count = dali_stats.rx_raw_dump_count;
  stats->rx_inverted = (uint8_t)(dali_rx_inverted ? 1 : 0);
  stats->listen_only = (uint8_t)(dali_listen_only ? 1 : 0);
  stats->signal_guess = dali_guess_signal_kind();
}

static void dali_delay_ms(uint32_t delay_ms)
{
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  } else {
    sleep_ms(delay_ms);
  }
}

static int16_t dali_submit_request(DaliOp op, uint16_t cmd, uint8_t arg0, uint8_t arg1, uint32_t timeout_ms)
{
  if (dali_queue == NULL) {
    return -DALI_RESULT_TIMEOUT;
  }

  if (xTaskGetCurrentTaskHandle() == dali_task_handle) {
    switch (op) {
      case DALI_OP_CMD:
        return dali.cmd(cmd, arg0);
      case DALI_OP_SET_LEVEL:
        dali.set_level(arg0, arg1);
        return DALI_OK;
      case DALI_OP_COMMISSION:
        return dali.commission(arg0);
      default:
        return -DALI_RESULT_INVALID_CMD;
    }
  }

  DaliRequest req = {};
  req.op = op;
  req.cmd = cmd;
  req.arg0 = arg0;
  req.arg1 = arg1;
  req.result = -DALI_RESULT_TIMEOUT;
  req.done = xSemaphoreCreateBinary();
  if (req.done == NULL) {
    return -DALI_RESULT_TIMEOUT;
  }

  TickType_t wait_ticks = pdMS_TO_TICKS(timeout_ms);
  if (wait_ticks == 0) {
    wait_ticks = 1;
  }

  if (xQueueSend(dali_queue, &req, wait_ticks) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return -DALI_RESULT_TIMEOUT;
  }

  if (xSemaphoreTake(req.done, wait_ticks) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return -DALI_RESULT_TIMEOUT;
  }

  int16_t result = req.result;
  vSemaphoreDelete(req.done);
  return result;
}

uint8_t bus_is_high() {
  bool raw = gpio_get(RX_PIN); //slow version
  if (!dali_probe_init) {
    dali_probe_init = true;
    dali_probe_last_level = raw ? 1 : 0;
    dali_probe_run_len = 1;
  } else {
    uint8_t lv = raw ? 1 : 0;
    if (lv == dali_probe_last_level) {
      if (dali_probe_run_len < 0xFFFF)
        dali_probe_run_len++;
    } else {
      dali_stats.rx_raw_transitions++;
      dali_probe_commit_run(dali_probe_run_len);
      dali_probe_last_level = lv;
      dali_probe_run_len = 1;
    }
  }

  if (raw) {
    dali_stats.rx_raw_high_samples++;
  } else {
    dali_stats.rx_raw_low_samples++;
  }
  return dali_rx_inverted ? !raw : raw;
}

//use bus
void bus_set_low() {
  dali_stats.tx_drive_low_calls++;
  if (dali_listen_only) {
    return;
  }
  gpio_set_dir(TX_PIN, GPIO_OUT);
  gpio_put(TX_PIN, 0); //opto slow version
}

//release bus
void bus_set_high() {
  dali_stats.tx_release_calls++;
  if (dali_listen_only) {
    return;
  }
  gpio_set_dir(TX_PIN, GPIO_OUT);
  gpio_put(TX_PIN, 1); //opto slow version
}

static bool onTimer(repeating_timer_t *rt) {
  (void)rt;
  dali.timer();
  return true;
}

void DALI_Init() {
  printf("This is Wavshare's DALI board \r\n");
  //setup RX/TX pin
  gpio_init(RX_PIN);
  gpio_set_dir(RX_PIN, GPIO_IN);
  gpio_disable_pulls(RX_PIN);

  gpio_init(TX_PIN);
  DALI_SetListenOnly(dali_listen_only);
  
  if (!dali_timer_started) {
    // 104 us is the closest integer period to the required 104.167 us at 9600 Hz.
    dali_timer_started = add_repeating_timer_us(-104, onTimer, NULL, &dali_timer);
  }

  dali.begin(bus_is_high, bus_set_low, bus_set_high);
}

static void dali_execute_request(DaliRequest *req)
{
  switch (req->op) {
    case DALI_OP_CMD:
      req->result = dali.cmd(req->cmd, req->arg0);
      break;
    case DALI_OP_SET_LEVEL:
      dali.set_level(req->arg0, req->arg1);
      req->result = DALI_OK;
      break;
    case DALI_OP_COMMISSION:
      req->result = dali.commission(req->arg0);
      break;
    default:
      req->result = -DALI_RESULT_INVALID_CMD;
      break;
  }

  xSemaphoreGive(req->done);
}

static void DALI_ServiceTask(void *pv)
{
  (void)pv;
  DALI_Init();

  DaliRequest req;
  uint8_t rx_data[4];
  while (1) {
    if (xQueueReceive(dali_queue, &req, 0) == pdTRUE) {
      dali_execute_request(&req);
    }

    uint8_t rx_len = dali.rx(rx_data);
    if (rx_len == 16 && dali_rx_frame_queue != NULL) {
      dali_stats.rx_frames_total++;
      dali_print_raw_dump("frame", rx_len);
      DALI_ForwardFrame frame = dali_parse_forward_frame(rx_data[0], rx_data[1]);
      if (DALI_FrameMatchesDevice(&frame)) {
        dali_stats.rx_frames_matched++;
        if (xQueueSend(dali_rx_frame_queue, &frame, 0) != pdTRUE) {
          dali_stats.rx_queue_drops++;
        }
      } else {
        dali_stats.rx_frames_filtered++;
      }
    } else if (rx_len == 1) {
      dali_stats.rx_busy_count++;
    } else if (rx_len == 2) {
      dali_stats.rx_decode_errors++;
      dali_print_raw_dump("decodeErr", rx_len);
    } else if (rx_len != 0) {
      dali_stats.rx_other_len_count++;
      dali_print_raw_dump("other", rx_len);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool DALI_StartService(uint32_t core_affinity_mask, uint32_t task_priority, uint16_t task_stack_words)
{
  if (dali_task_handle != NULL) {
    return true;
  }

  if (dali_queue == NULL) {
    dali_queue = xQueueCreate(16, sizeof(DaliRequest));
    if (dali_queue == NULL) {
      return false;
    }
  }

  if (dali_rx_frame_queue == NULL) {
    dali_rx_frame_queue = xQueueCreate(16, sizeof(DALI_ForwardFrame));
    if (dali_rx_frame_queue == NULL) {
      return false;
    }
  }

  BaseType_t rc = xTaskCreateAffinitySet(
      DALI_ServiceTask,
      "dali-svc",
      task_stack_words,
      NULL,
      task_priority,
      core_affinity_mask,
      &dali_task_handle);

  return rc == pdPASS;
}

int16_t DALI_Cmd(uint16_t cmd, uint8_t arg, uint32_t timeout_ms)
{
  return dali_submit_request(DALI_OP_CMD, cmd, arg, 0, timeout_ms);
}

uint8_t DALI_SetLevel(uint8_t level, uint8_t adr, uint32_t timeout_ms)
{
  int16_t rv = dali_submit_request(DALI_OP_SET_LEVEL, 0, level, adr, timeout_ms);
  return (rv < 0) ? (uint8_t)(-rv) : DALI_OK;
}

uint8_t DALI_Commission(uint8_t init_arg, uint32_t timeout_ms)
{
  int16_t rv = dali_submit_request(DALI_OP_COMMISSION, 0, init_arg, 0, timeout_ms);
  if (rv < 0) {
    return (uint8_t)(-rv);
  }
  return (uint8_t)rv;
}

bool DALI_RunStartupScanAndCommission()
{
  DALI_NUM = 0;
  Scan_DALI_addr_ALL();
  if (DALI_NUM == 0) {
    Assign_new_address_ALL();
  }
  return true;
}

void Blinking_ALL() {                                                        
  printf("Running: Blinking all lamps\r\n");
  DALI_SetLevel(254);
  dali_delay_ms(500);
  DALI_SetLevel(0);
  dali_delay_ms(500);
}
void Luminaire_Brightness(uint8_t Light, uint8_t addr) {                     
  printf("Running: Set the brightness of the fixture at address %d to %d %%\r\n", addr, Light);
  uint8_t Light_practical = (uint8_t)(2.55*Light);
  DALI_SetLevel(Light_practical, addr);
}

void Lighten_ALL() {                                                          
  printf("Running: Turn on all lights on the DALI\r\n");
  DALI_SetLevel(200);
}
void Extinguish_ALL() {                                                      
  printf("Running: Turn off all lights on the DALI\r\n");
  DALI_SetLevel(0);
}

void Scan_DALI_addr_ALL() {                                                  
  printf("Running: Scan all addresses\r\n");
  DALI_NUM = 0;
  uint8_t addr;
  for(addr = 0; addr<64; addr++) {
    int16_t rv = DALI_Cmd(DALI_QUERY_STATUS, addr, 1000);
    if(rv>=0) {
      DALI_Addr[DALI_NUM] = addr;
      DALI_NUM ++;
      int16_t min_level = DALI_Cmd(DALI_QUERY_MIN_LEVEL, addr, 1000);
      printf("Address %d  status=0x%x  minLevel= %d \r\n", addr, rv, min_level);
      DALI_SetLevel(254, addr);
      dali_delay_ms(500);
      DALI_SetLevel(0, addr);
    }
    else if (-rv != DALI_RESULT_NO_REPLY) {
      printf("short address= %d ERROR= %d  \r\n", addr,-rv);
    }
  }  
  printf("End scan,%d devices were scanned\r\n",DALI_NUM);
}
void Delete_DALI_addr_ALL() {                                                           
  printf("Running: Delete all short addresses\r\n");
  //remove all short addresses
  DALI_Cmd(DALI_DATA_TRANSFER_REGISTER0, 0xFF, 1000);
  DALI_Cmd(DALI_SET_SHORT_ADDRESS, 0xFF, 1000);
  printf("DONE delete \r\n");
}

void Assign_new_address_ALL(){                                                  
  printf("Running: Assign new addresses to all devices\r\n");   
  printf("Might need a couple of runs to find all lamps ...\r\n");
  printf("Be patient, this takes a while ...\r\n");
  uint8_t cnt = DALI_Commission(0xff, 60000); //init_arg=0b11111111 : all without short address  
  printf("DONE, assigned %d new short addresses\r\n",cnt);
  Scan_DALI_addr_ALL();
}
