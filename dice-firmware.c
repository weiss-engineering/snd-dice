/*
 * TC Applied Technologies Digital Interface Communications Engine driver firmware loader
 *
 * Copyright (c) Rolf Anderegg <rolf.anderegg@weiss.ch>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include "dice-interface.h"
#include "../lib.h"

struct dice_fl_img_desc {
	char name[16];
	unsigned int flash_base;
	unsigned int mem_base;
	unsigned int size;
	unsigned int entry_point;
	unsigned int length;
	unsigned int chk_sum;
	unsigned int ui_board_serial_number;
	unsigned int ui_version_high;
	unsigned int ui_version_low;
	unsigned int ui_configuration_flags;
	char build_time[64];
	char build_date[64];
};

struct dice_fl_app_info {
	unsigned int ui_base_sdk_version;		//The full version/revision of the SDK this build was based on
	unsigned int ui_application_version;	//The full version/revision of the Application
	unsigned int ui_vendor_id;				//The Vendor ID
	unsigned int ui_product_id;				//The product ID
	char build_time[64];					//Build time
	char build_date[64];					//Build date
	unsigned int ui_board_serial_number;	//The serial number of the board as optained from persist. storage
};

struct dice_fl_vendor_img_desc {
	unsigned int ui_vproduct_id;
	char ui_vendor_id[8];
	unsigned int ui_vmajor;
	unsigned int ui_vminor;
	unsigned int user1;
	unsigned int user2;
};

static int dice_fl_exec(struct fw_unit* unit, unsigned int opcode)
{
	int err;
	__be32 value;

	value = cpu_to_be32(opcode | OPCODE_EXECUTE);
	err = snd_fw_transaction(unit, TCODE_WRITE_QUADLET_REQUEST,
					DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,
					&value, 4, 0);
	if (err < 0)
		return err;
	while(1) {
		msleep(20);
		err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
							DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,
							&value, 4, 0);
		if (err < 0)
			return err;
		if ((value & cpu_to_be32(OPCODE_EXECUTE)) == 0)
			break;
		return -EIO;
	}

	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
					DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_RETURN_STATUS,
					&value, 4, 0);
	if (err < 0)
		return err;

	return be32_to_cpu(value);
}

static int dice_fl_get_cur_img(struct fw_unit* unit, struct dice_fl_vendor_img_desc* img_desc)
{
	int err;
	__be32 values[4];
	unsigned int i;

	err = dice_fl_exec(unit, OPCODE_GET_RUNNING_IMAGE_VINFO);
	if (err != 0)
		return -EIO;

	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
						&values, 4, 0);
	if (err < 0)
		return err;
	img_desc->ui_vproduct_id = be32_to_cpu(values[0]);

	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 0x4,
						img_desc->ui_vendor_id, sizeof(img_desc->ui_vendor_id), 0);
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(img_desc->ui_vendor_id) % 4 != 0);
		for (i = 0; i < sizeof(img_desc->ui_vendor_id); i += 4)
			swab32s((u32 *)&img_desc->ui_vendor_id[i]);
		img_desc->ui_vendor_id[sizeof(img_desc->ui_vendor_id) - 1] = '\0';
	}

	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 0xC,
						&values, 4*4, 0);
	if (err < 0)
		return err;
	img_desc->ui_vmajor = be32_to_cpu(values[0]);
	img_desc->ui_vminor = be32_to_cpu(values[1]);
	img_desc->user1 = be32_to_cpu(values[2]);
	img_desc->user2 = be32_to_cpu(values[3]);

	return 0;
}
