#ifndef SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED
#define SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED

#include <linux/firmware.h>

/* debug only: */
//#define DEBUG_DICE_FW_BIN_NAME		"dice.bin"

void dice_fl_firmware_async(const struct firmware *fw, void *context);

#endif
