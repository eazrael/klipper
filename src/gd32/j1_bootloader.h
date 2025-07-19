#ifndef __GD32_J1_BOOTLOADER_H
#define __GD32_J1_BOOTLOADER_H


#if CONFIG_HAVE_BOOTLOADER_REQUEST
void bootloader_handle_confirm_request(void);
#endif

#endif