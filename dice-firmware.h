#ifndef SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED
#define SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED

#include "dice.h"

struct dice_fl_vendor_img_desc {
	unsigned int ui_vproduct_id;
	char ui_vendor_id[8];
	unsigned int ui_vmajor;
	unsigned int ui_vminor;
	unsigned int user1;
	unsigned int user2;
};

int dice_fl_get_cur_img(struct dice* dice, struct dice_fl_vendor_img_desc* img_desc);

#endif
