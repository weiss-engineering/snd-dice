#ifndef SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED
#define SOUND_FIREWIRE_DICEFIRMWARE_H_INCLUDED

#include "dice.h"
#include <linux/firmware.h>

/* debug only: */
#define DICEFW_NAME		"dice.bin"

struct dice_fl_vendor_img_info {
	unsigned int ui_vproduct_id;
	char ui_vendor_id[8];
	unsigned int ui_vmajor;
	unsigned int ui_vminor;
	unsigned int user1;
	unsigned int user2;
};

struct dice_fl_app_info {
	unsigned int ui_base_sdk_version;		// [31-29]:buildFlags,[28-24]:vMaj,[23-20]:vMin,[19-16]:vSub,[15-0]:vBuild
	unsigned int ui_application_version;	// [31-24]:vMaj,[23-20]:vMin,[19-16]:vSub,[15-0]:vBuild
	unsigned int ui_vendor_id;
	unsigned int ui_product_id;
	char build_time[64];
	char build_date[64];
	unsigned int ui_board_serial_number;
};

int dice_fl_get_cur_img(struct dice* dice, struct dice_fl_vendor_img_info* img_info);
int dice_fl_get_cur_app(struct dice* dice, struct dice_fl_app_info* app_info);
void dice_fl_firmware_async(const struct firmware *fw, void *context);

#endif
