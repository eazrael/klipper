// Bootloeader/Flash support on Snapmaker J1
//
// Copyright (C) 2024, 2025  Evil Azrael
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "gd32f30x_fmc.h" 
#include "generic/misc.h"
#include "internal.h"
//#include "command.h" // for output macro
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// update status location = flash end - 12kb = 0x08000000 + 0x100000 - 0x3000
#define APPLICATION_STATUS_ADDR 0x080fd000


// Bootloader management
// Snapmaker J1 printer have a bootloader flashed into the microcontroller. 
// The bootloader has it's own firmware upgrade protocol, but we can use 
// that also for klipper. 
// From a MCU point of view we have to take care of 3 states. Unfortunately 
// changing the state also requires flashing and recalculating checksum. 
// 
// status = APPLICATION_STATUS_COMPLETED_FLASH
// The bootloader completed flashing and starts the application, the 
// application has to confirm that is in good health by setting the 
// the status to APPLICATION_STATUS_APP_RUNNABLE. If not done the bootloader
// will automatically enter the flashing mode on the next restart. 
//
// status = APPLICATION_STATUS_APP_RUNNABLE
// Normal state, nothing to do 
//
// Any other state will make the bootloader go into flash mode after a reset.
// 
// If we want to flash new firmware, we indicate this by setting the state to 
// APPLICATION_STATUS_REQUEST_FLASH and reset. 

// reduced checking in comparison to the release snapmaker sources
// on read just check checksum and state



// taken from sources at https://github.com/Snapmaker/SnapmakerController-IDEX
// it's called update_packet_info_t there
#pragma pack(push, 1)
typedef struct {
  uint8_t  file_flag[21];
  uint8_t  pack_version;
  uint16_t type;
  uint8_t is_force_update;
  uint16_t start_id;
  uint16_t end_id;
  uint8_t  app_version[32];
  uint8_t  pack_time[20];
  uint16_t status_flag;
  uint32_t app_length;
  uint32_t app_checknum;
  uint32_t app_flash_start_addr;
  uint8_t usart_num;
  uint8_t receiver_id;
  uint32_t pack_head_checksum;
} application_info_t;
#pragma pack(pop)

typedef enum {
    APPLICATION_STATUS_FIRST_BURN       = 0xaa00, 
    APPLICATION_STATUS_REQUEST_FLASH    = 0xaa01,
    APPLICATION_STATUS_START_FLASH      = 0xaa02,
    APPLICATION_STATUS_PENDING_FLASH    = 0xaa03, 
    APPLICATION_STATUS_COMPLETED_FLASH  = 0xaa04, 
    APPLICATION_STATUS_APP_RUNNABLE     = 0xaa05
} update_status_state; 


static application_info_t *mcu_application_status = (application_info_t *) (APPLICATION_STATUS_ADDR); 

static update_status_state get_update_status(void)
{
    return mcu_application_status->status_flag; 
}

static uint32_t calc_cksum_application_status(application_info_t *header)
{
    uint32_t sum = 0; 
    uint32_t len = offsetof(application_info_t, pack_head_checksum); 
    uint8_t *data =  (uint8_t*) header; 
    
    for(unsigned i = 0; i < len -1; i += 2)
        sum += data[i] << 8 | data[i + 1]; 
    if(len % 2)
        sum += data[len - 1];
    return ~sum; 
}

static unsigned is_header_valid(application_info_t* header) 
{
    uint32_t chksum = calc_cksum_application_status(header);
    return chksum == header->pack_head_checksum; 
}

static void update_cksum(application_info_t *header) 
{
    header->pack_head_checksum = calc_cksum_application_status(header);
} 

static unsigned bootloader_needs_confirmation(void) 
{
    return is_header_valid(mcu_application_status) 
        && get_update_status() == APPLICATION_STATUS_COMPLETED_FLASH; 
}

static void write_update_status(application_info_t *app_info) 
{    
    fmc_wscnt_set(WS_WSCNT_2);
    fmc_unlock(); 

    fmc_flag_clear(FMC_FLAG_BANK1_PGERR);
    fmc_flag_clear(FMC_FLAG_BANK1_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK1_END);

    fmc_page_erase((uint32_t) mcu_application_status);

    //write in 32bit words
    unsigned word_count = sizeof(application_info_t) / 4;

    uint32_t *source_data = (uint32_t*) app_info; 
    uint32_t *flash_address = (uint32_t *)(mcu_application_status);

    while(word_count--)
        fmc_word_program((uint32_t) (flash_address++), *(source_data++)); 

    unsigned remainder = sizeof(application_info_t) % 4; 
    if (remainder)
    {
        uint32_t buffer = 0;
        memcpy(&buffer, source_data, remainder); 
        fmc_word_program((uint32_t) flash_address, buffer); 
    }

    fmc_lock(); 
}

static void bootloader_confirm_update(void)
{
    //output("Bootloader_confirm_update");
    application_info_t buf = *mcu_application_status;
    buf.status_flag = APPLICATION_STATUS_APP_RUNNABLE;
    update_cksum(&buf);

    write_update_status(&buf); 
}

static void bootloader_request_flash(void)
{
    // technically just erasing the 0x800fd000 page is sufficient to trigger the update
    application_info_t buf = {0};
    buf.status_flag = APPLICATION_STATUS_REQUEST_FLASH;
    update_cksum(&buf);
    write_update_status(&buf); 
}

void bootloader_handle_confirm_request(void)
{
    if (!bootloader_needs_confirmation())
        return;
    //output("Bootloader needs update confirmation");
    bootloader_confirm_update(); 
}

void bootloader_request(void) 
{
    //output("Requesting firmware update mode");
    bootloader_request_flash();
    command_reset((uint32_t*) 0); 
}
