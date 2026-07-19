#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "DALI_Lib.h"

// CHx-DALI is treated as a spare RX-related pin, not a channel-enable/control pin.
#define DALI_PIN 2
#define RX_PIN 3
#define TX_PIN 6

extern uint8_t DALI_Addr[64];
extern uint8_t DALI_NUM;

struct DALI_ForwardFrame {
	uint8_t address_byte;
	uint8_t data_byte;
	bool is_command;
	bool is_broadcast;
	bool is_group;
	uint8_t short_address;
	uint8_t group_address;
};

struct DALI_DebugStats {
	uint32_t rx_frames_total;
	uint32_t rx_frames_matched;
	uint32_t rx_frames_filtered;
	uint32_t rx_queue_drops;
	uint32_t rx_decode_errors;
	uint32_t rx_busy_count;
	uint32_t rx_other_len_count;
	uint32_t rx_raw_high_samples;
	uint32_t rx_raw_low_samples;
	uint32_t rx_raw_transitions;
	uint32_t rx_run_1_2;
	uint32_t rx_run_3_5;
	uint32_t rx_run_6_12;
	uint32_t rx_run_13_plus;
	uint32_t tx_drive_low_calls;
	uint32_t tx_release_calls;
	uint32_t rx_raw_dump_count;
	uint8_t rx_inverted;
	uint8_t listen_only;
	uint8_t signal_guess;
};


void DALI_Init();                                                         // Example Initialize the DALI bus
bool DALI_StartService(uint32_t core_affinity_mask, uint32_t task_priority, uint16_t task_stack_words);
bool DALI_RunStartupScanAndCommission();
int16_t DALI_Cmd(uint16_t cmd, uint8_t arg, uint32_t timeout_ms = 1000);
uint8_t DALI_SetLevel(uint8_t level, uint8_t adr = 0xFF, uint32_t timeout_ms = 1000);
uint8_t DALI_Commission(uint8_t init_arg = 0xFF, uint32_t timeout_ms = 60000);
bool DALI_GetNextForwardFrame(DALI_ForwardFrame* frame, uint32_t timeout_ms = 0);
void DALI_GetDebugStats(DALI_DebugStats* stats);
void DALI_SetPromiscuousMode(bool enabled);
bool DALI_GetPromiscuousMode();
void DALI_SetListenOnly(bool enabled);
bool DALI_GetListenOnly();
void DALI_SetRxInverted(bool enabled);
bool DALI_GetRxInverted();
void DALI_SetDeviceShortAddress(uint8_t short_addr);
void DALI_SetDeviceGroupMask(uint16_t group_mask);
bool DALI_FrameMatchesDevice(const DALI_ForwardFrame* frame);

void Blinking_ALL();                                                      // All lights on the bus flash
void Lighten_ALL();                                                       // Light all lamps on the bus
void Extinguish_ALL();                                                    // Turn off all lights on the bus
void Luminaire_Brightness(uint8_t Light, uint8_t addr);                   // Luminaires with addresses addr (0 to 63) on the bus are set to Light(0 to 100)%
void Scan_DALI_addr_ALL();                                                // Scan all devices on the bus
void Scan_DALI_addr_ALL_DT6();
void Delete_DALI_addr_ALL();                                              // Delete the addresses of all devices on the bus   
void Assign_new_address_ALL();                                            // Reassign addresses to all devices on the bus
