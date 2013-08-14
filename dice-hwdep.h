#ifndef SOUND_FIREWIRE_DICEHWDEP_H_INCLUDED
#define SOUND_FIREWIRE_DICEHWDEP_H_INCLUDED

#include "dice.h"
#include <linux/bitops.h>

#define DICE_HWDEP_LOADDSP_DRV_FLAG_FORCE	BIT(0)

int dice_create_hwdep(struct dice *dice);

#endif
