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
#include "dice-firmware.h"
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

/*
 * Firmware loading is handled by the "DICE firmwareloader" interface.  Its registers are
 * located in this address space (1M into the private space).
 */
#define DICE_FIRMWARE_LOAD_SPACE	(DICE_PRIVATE_SPACE + 0x00100000)

/* offset from FIRMWARE_LOAD_SPACE */
#define FIRMWARE_VERSION		0x000
#define FIRMWARE_OPCODE			0x004
#define  OPCODE_MASK			0x00000fff
#define  OPCODE_GET_IMAGE_DESC	0x00000000
#define  OPCODE_DELETE_IMAGE	0x00000001
#define  OPCODE_CREATE_IMAGE	0x00000002
#define  OPCODE_UPLOAD			0x00000003
#define  OPCODE_UPLOAD_STAT		0x00000004
#define  OPCODE_RESET_IMAGE		0x00000005
#define  OPCODE_TEST_ACTION		0x00000006
#define  OPCODE_GET_RUNNING_IMAGE_VINFO	0x0000000a
#define  OPCODE_EXECUTE			0x80000000
#define FIRMWARE_RETURN_STATUS		0x008
#define FIRMWARE_PROGRESS		0x00c
#define  PROGRESS_CURR_MASK		0x00000fff
#define  PROGRESS_MAX_MASK		0x00fff000
#define  PROGRESS_TOUT_MASK		0x0f000000
#define  PROGRESS_FLAG			0x80000000
#define FIRMWARE_CAPABILITIES		0x010
#define  FL_CAP_AUTOERASE		0x00000001
#define  FL_CAP_PROGRESS		0x00000002
#define FIRMWARE_DATA			0x02c
#define  TEST_CMD_POKE			0x00000001
#define  TEST_CMD_PEEK			0x00000002
#define  CMD_GET_AVS_CNT		0x00000003
#define  CMD_CLR_AVS_CNT		0x00000004
#define  CMD_SET_MODE			0x00000005
#define  CMD_SET_MIDIBP			0x00000006
#define  CMD_GET_AVSPHASE		0x00000007
#define  CMD_ENABLE_BNC_SYNC		0x00000008
#define  CMD_PULSE_BNC_SYNC		0x00000009
#define  CMD_EMUL_SLOW_CMD		0x0000000a
#define FIRMWARE_TEST_DELAY		0xfd8
#define FIRMWARE_TEST_BUF		0xfdc

static int dice_fl_exec(struct dice* dice, unsigned int opcode)
{
	int err;
	__be32 value;

	value = cpu_to_be32(opcode | OPCODE_EXECUTE);
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
					DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,
					&value, 4, 0);
	if (err < 0) {
		dev_warn(&dice->unit->device, "FL opcode (%#x->%#llx) exec failed: %i.",opcode,DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,err);
		return err;
	}
	while(1) {
		msleep(20);
		err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
							DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,
							&value, 4, 0);
		if (err < 0)
			return err;
		if ((value & cpu_to_be32(OPCODE_EXECUTE)) == 0)
			break;
		dev_warn(&dice->unit->device, "FL opcode exec timeout.");
		return -EIO;
	}

	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
					DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_RETURN_STATUS,
					&value, 4, 0);
	if (err < 0)
		return err;

	return be32_to_cpu(value);
}

int dice_fl_get_cur_img(struct dice* dice, struct dice_fl_vendor_img_desc* img_desc)
{
	int err;
	__be32 values[4];
	unsigned int i;

	_dev_info(&dice->unit->device, "Get running image vinfo...");
	err = dice_fl_exec(dice, OPCODE_GET_RUNNING_IMAGE_VINFO);
	if (err != 0)
		return -EIO;

	_dev_info(&dice->unit->device, "Read running image vinfo...");
	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
						&values, 4, 0);
	if (err < 0)
		return err;
	_dev_info(&dice->unit->device, "Current FW:");
	img_desc->ui_vproduct_id = be32_to_cpu(values[0]);

	_dev_info(&dice->unit->device, "  product ID: %i", img_desc->ui_vproduct_id);
	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 0x4,
						img_desc->ui_vendor_id, sizeof(img_desc->ui_vendor_id), 0);
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(img_desc->ui_vendor_id) % 4 != 0);
		for (i = 0; i < sizeof(img_desc->ui_vendor_id); i += 4)
			swab32s((u32 *)&img_desc->ui_vendor_id[i]);
		img_desc->ui_vendor_id[sizeof(img_desc->ui_vendor_id) - 1] = '\0';
	}
	_dev_info(&dice->unit->device, "  vendor ID: %s", img_desc->ui_vendor_id);

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 0xC,
						&values, 4*4, 0);
	if (err < 0)
		return err;
	img_desc->ui_vmajor = be32_to_cpu(values[0]);
	img_desc->ui_vminor = be32_to_cpu(values[1]);
	img_desc->user1 = be32_to_cpu(values[2]);
	img_desc->user2 = be32_to_cpu(values[3]);

	_dev_info(&dice->unit->device, "  UI version: maj:%i, min:%i", img_desc->ui_vmajor, img_desc->ui_vminor);
	_dev_info(&dice->unit->device, "  user: 1:%i, 2:%i", img_desc->user1, img_desc->user2);

	return 0;
}

