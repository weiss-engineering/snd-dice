#ifndef SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED
#define SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED

#include <linux/firmware.h>
#include <sound/info.h>
#include "dice.h"

/* debug only: */
//#define DEBUG_DICE_FW_BIN_NAME		"dice.bin"

int dice_firmware_info_read(struct dice* dice);
int dice_firmware_load(struct dice* dice, const struct firmware *fw, bool force);
void dice_firmware_load_async(const struct firmware *fw, void *context);
void dice_firmware_proc_read(const struct dice *dice, struct snd_info_buffer *buffer);

#endif
