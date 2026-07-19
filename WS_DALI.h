#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "DALI_Lib.h"

#define TX_PIN 14
#define RX_PIN 5

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


void DALI_Init();                                                         // Example Initialize the DALI bus
bool DALI_StartService(uint32_t core_affinity_mask, uint32_t task_priority, uint16_t task_stack_words);
bool DALI_RunStartupScanAndCommission();
int16_t DALI_Cmd(uint16_t cmd, uint8_t arg, uint32_t timeout_ms = 1000);
uint8_t DALI_SetLevel(uint8_t level, uint8_t adr = 0xFF, uint32_t timeout_ms = 1000);
uint8_t DALI_Commission(uint8_t init_arg = 0xFF, uint32_t timeout_ms = 60000);
bool DALI_GetNextForwardFrame(DALI_ForwardFrame* frame, uint32_t timeout_ms = 0);
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
