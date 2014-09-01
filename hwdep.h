#ifndef SOUND_FIREWIRE_DICEHWDEP_H_INCLUDED
#define SOUND_FIREWIRE_DICEHWDEP_H_INCLUDED

#include "dice.h"
#include <linux/bitops.h>

int dice_create_hwdep(struct dice *dice);

/* @WARNING: check with include/sound/firewire.h to avoid conflicts */
#define SNDRV_DICE_IOCTL_GET_GLOB_SETTINGS		_IOR('H', 0xfb, struct dice_global_settings)
#define SNDRV_DICE_IOCTL_GET_EXT_SYNC_STATUS	_IOR('H', 0xfc, struct dice_ext_sync_info)

#endif
