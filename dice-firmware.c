/*
 * TC Applied Technologies Digital Interface Communications Engine driver firmware loader
 *
 * Copyright (c) Rolf Anderegg <rolf.anderegg@weiss.ch>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/slab.h>
#include <linux/delay.h>
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
#define  OPCODE_GET_FLASH_INFO	0x00000007
#define  OPCODE_READ_MEMORY		0x00000008
#define  OPCODE_GET_RUNNING_IMAGE_VINFO	0x0000000a
#define  OPCODE_CREATE_IMAGE2	0x0000000b
#define  OPCODE_GET_APP_INFO	0x0000000c
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

/**
 * DICE firmwareloader execute command with opcode.
 * Sets the executable bit & waits for it to be cleared. Then reads and returns
 * the return status.
 */
static int dice_fl_exec(struct dice* dice, unsigned int const opcode, unsigned int t_sleep)
{
	int err;
	__be32 value;

	value = cpu_to_be32((opcode & OPCODE_MASK) | OPCODE_EXECUTE);
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
					DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,
					&value, 4, 0);
	if (err < 0) {
		dev_warn(&dice->unit->device, "FL opcode (%#x->%#llx) exec failed: %i.",opcode,DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,err);
		return err;
	}
	/* TODO: use some sort of wait queue instead of sleeping */
	while(1) {
		msleep(t_sleep);
		err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
							DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_OPCODE,
							&value, 4, 0);
		if (err < 0) {
			return err;
		}
		if ((value & cpu_to_be32(OPCODE_EXECUTE)) == 0) {
			break;
		}
		dev_warn(&dice->unit->device, "FL opcode exec timeout.");
		return -EIO;
	}

	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
					DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_RETURN_STATUS,
					&value, 4, 0);
	if (err < 0) {
		return err;
	}
	return be32_to_cpu(value);
}

int dice_fl_get_cur_img(struct dice* dice, struct dice_fl_vendor_img_info* img_info)
{
	int err;
	__be32 values[4];
	unsigned int i;

	_dev_info(&dice->unit->device, "Get running image vinfo...");
	err = dice_fl_exec(dice, OPCODE_GET_RUNNING_IMAGE_VINFO, 20);
	if (err != 0) {
		return -EIO;
	}
	_dev_info(&dice->unit->device, "Read running image vinfo...");
	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
						values, 4, 0);
	if (err < 0) {
		return err;
	}
	_dev_info(&dice->unit->device, "Current FW:");
	img_info->ui_vproduct_id = be32_to_cpu(values[0]);

	_dev_info(&dice->unit->device, "  product ID: %i", img_info->ui_vproduct_id);
	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 0x4,
						img_info->ui_vendor_id, sizeof(img_info->ui_vendor_id), 0);
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(img_info->ui_vendor_id) % 4 != 0);
		for (i = 0; i < sizeof(img_info->ui_vendor_id); i += 4)
			swab32s((u32 *)&img_info->ui_vendor_id[i]);
		img_info->ui_vendor_id[sizeof(img_info->ui_vendor_id) - 1] = '\0';
	}
	_dev_info(&dice->unit->device, "  vendor ID: %s", img_info->ui_vendor_id);

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 0xC,
						values, 4*4, 0);
	if (err < 0) {
		return err;
	}
	img_info->ui_vmajor = be32_to_cpu(values[0]);
	img_info->ui_vminor = be32_to_cpu(values[1]);
	img_info->user1 = be32_to_cpu(values[2]);
	img_info->user2 = be32_to_cpu(values[3]);

	_dev_info(&dice->unit->device, "  UI version: %i.%i.%i.%i", img_info->ui_vmajor, img_info->ui_vminor, img_info->user1, img_info->user2);

	return 0;
}

#define SDK_VERSION_MASK_MAJOR			0x1f000000
#define SDK_VERSION_MASK_MINOR			0x00f00000
#define SDK_VERSION_MASK_SUB			0x000f0000
#define SDK_VERSION_MASK_BUILD			0x0000ffff
#define SDK_VERSION32_MAJOR(v32)		(((v32)&SDK_VERSION_MASK_MAJOR)>>24)
#define SDK_VERSION32_MINOR(v32)		(((v32)&SDK_VERSION_MASK_MINOR)>>20)
#define SDK_VERSION32_SUB(v32)			(((v32)&SDK_VERSION_MASK_SUB)>>16)
#define SDK_VERSION32_BUILD(v32)		( (v32)&SDK_VERSION_MASK_BUILD)

#define DICE_FW_VERSION_MASK_MAJOR		0xff000000
#define DICE_FW_VERSION_MASK_MINOR		0x00f00000
#define DICE_FW_VERSION_MASK_SUB		0x000f0000
#define DICE_FW_VERSION_MASK_BUILD		0x0000ffff
#define DICE_FW_VERSION32_MAJOR(v32)	(((v32)&DICE_FW_VERSION_MASK_MAJOR)>>24)
#define DICE_FW_VERSION32_MINOR(v32)	(((v32)&DICE_FW_VERSION_MASK_MINOR)>>20)
#define DICE_FW_VERSION32_SUB(v32)		(((v32)&DICE_FW_VERSION_MASK_SUB)>>16)
#define DICE_FW_VERSION32_BUILD(v32)	( (v32)&DICE_FW_VERSION_MASK_BUILD)

#define DICEFW_MAGIC_SIZE						36
#define TCAT_DICE_VERSION_MAGIC_STRING			"56448A3A-77AB-4631-A34D-5CD917EE4B24"
#define TCAT_DICE_VERSION_NEW_MAGIC_STRING		"B3F35591-997E-43dc-92BC-0904EFE8BC2B"
struct dice_fl_file_vinfo_new {
	char magic_num[DICEFW_MAGIC_SIZE];			//< == kTCAT_DICE_VERSION_NEW_MAGIC_STRING
	unsigned int ui_base_sdk_version;
	unsigned int ui_application_version;
	unsigned int ui_vendor_id;
	unsigned int ui_product_id;
	char build_time[64];
	char build_date[64];
};
struct dice_fl_file_vinfo {
	char magic_num[DICEFW_MAGIC_SIZE];			//< == kTCAT_DICE_VERSION_MAGIC_STRING
	char major[5];				//< 4 digits followed by a space
	char minor[5];				//< 4 digits followed by a space
	char vendor_id[7];			//< 6 digit 24-bit OUI (as defined in targetVendorDefs.h) followed by a space
	char v_product_id[4];		//< 3 digit (10 bits) Product ID (as defined in targetVendorDefs.h) followed by a space
	char v_major[5];			//< 4 digits followed by a space
	char v_minor[5];			//< 4 digits followed by a space
};

static struct dice_fl_file_vinfo_new* dice_fl_find_fw_desc(struct firmware const* fw)
{
	int scan = 0, found = 0;
	char const magic[DICEFW_MAGIC_SIZE] = { TCAT_DICE_VERSION_NEW_MAGIC_STRING };

	while (scan < fw->size - sizeof(struct dice_fl_file_vinfo_new)) {
		if (fw->data[scan++] == magic[found]) {
			found++;
		} else {
			found = 0;
		}
		if (scan >= fw->size) {
			break;
		}
		if (found == sizeof(magic)) {
			break;
		}
	}

	if (found != sizeof(magic)) {
		return NULL;
	}

	return (void *)&fw->data[scan - found];
}

static int dice_fl_get_file_vinfo(struct firmware const* fw, struct dice_fl_file_vinfo_new** file_vinfo)
{
	struct dice_fl_file_vinfo_new* vinfo;

	if (!fw || fw->size==0) {
		return -ENOENT;
	}
	/* extract supplied file's firmware version: */
	vinfo = dice_fl_find_fw_desc(fw);
	if (!vinfo) {
		return -ENOENT;
	}
	*file_vinfo = vinfo;
	return 0;
}

int dice_fl_get_cur_app(struct dice* dice, struct dice_fl_app_info* app_info)
{
	int err;
	__be32 values[4];
	unsigned int i;

	err = dice_fl_exec(dice, OPCODE_GET_APP_INFO, 20);
	if (err != 0) {
		return -EIO;
	}
	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
							DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
							values, 4*4, 0);
	if (err < 0) {
		return err;
	}
	app_info->ui_base_sdk_version = be32_to_cpu(values[0]);
	app_info->ui_application_version = be32_to_cpu(values[1]);
	app_info->ui_vendor_id = be32_to_cpu(values[2]);
	app_info->ui_product_id = be32_to_cpu(values[3]);

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 4*4,
						app_info->build_time, sizeof(app_info->build_time), 0);
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(app_info->build_time) % 4 != 0);
		for (i = 0; i < sizeof(app_info->build_time); i += 4) {
			swab32s((u32 *)&app_info->build_time[i]);
		}
		app_info->build_time[sizeof(app_info->build_time) - 1] = '\0';
	}
	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 4*4 + sizeof(app_info->build_time),
						app_info->build_date, sizeof(app_info->build_date), 0);
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(app_info->build_date) % 4 != 0);
		for (i = 0; i < sizeof(app_info->build_date); i += 4) {
			swab32s((u32 *)&app_info->build_date[i]);
		}
		app_info->build_date[sizeof(app_info->build_date) - 1] = '\0';
	}

	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
							DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 4*4 + sizeof(app_info->build_time) + sizeof(app_info->build_date),
							values, 4, 0);
	if (err < 0) {
		return err;
	}
	app_info->ui_board_serial_number = be32_to_cpu(values[0]);

	return 0;
}

#define DICE_FL_UPLOAD_BLOCKSIZE		1004
static int dice_fl_upload_blocks(struct dice* dice, struct firmware const* fw)
{
	int err = 0;
	size_t block_len;
	u8* upload_buffer;
	__be32 values[2];
	unsigned int index = 0;
	unsigned int checksum = 0;
	unsigned int i = 0;
//	u8 debug_progr = 0;

	if (fw->size % 4 != 0) {
		dev_err(&dice->unit->device, "firmware binary (%i) isn't zero padded", fw->size);
		return -EINVAL;
	}

	upload_buffer = kmalloc(DICE_FL_UPLOAD_BLOCKSIZE, GFP_ATOMIC);
	if (!upload_buffer) {
		dev_err(&dice->unit->device, "can't allocate memory chunk for firmware");
		return -ENOMEM;
	}
	while (index < fw->size) {
		block_len = min_t(size_t, DICE_FL_UPLOAD_BLOCKSIZE, fw->size - index);
		values[0] = cpu_to_be32(index);
		values[1] = cpu_to_be32(block_len);
		err = snd_fw_transaction(dice->unit, TCODE_WRITE_BLOCK_REQUEST,
				DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
				values, 4*2, 0);
		if (err < 0) {
			dev_err(&dice->unit->device, "firmware upload header error");
			goto upload_err;
		}
		memcpy(upload_buffer, fw->data + index, block_len);
		err = snd_fw_transaction(dice->unit, TCODE_WRITE_BLOCK_REQUEST,
				DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 4*2,
				upload_buffer, block_len, 0);
		if (err < 0) {
			dev_err(&dice->unit->device, "firmware upload block (index:%#x, block_len:%#x) error", index, block_len);
			goto upload_err;
		}
		err = dice_fl_exec(dice, OPCODE_UPLOAD, 1);
		if (err != 0) {
			dev_err(&dice->unit->device, "firmware upload error");
			err = -EIO;
			goto upload_err;
		}
		i=0;
		for (;i<block_len;i++) {
			checksum += upload_buffer[i];
		}
		index += block_len;
//		if (((100*index/fw->size) % 10 == 0) && ((100*index/fw->size) != debug_progr)) {
//			debug_progr = (100*index/fw->size);
//			_dev_info(&dice->unit->device, "  FW progress: %i%%", debug_progr);
//		}
	}
	kfree(upload_buffer);
	values[0] = cpu_to_be32(fw->size);
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
						values, 4, 0);
	if (err < 0) {
		dev_err(&dice->unit->device, "firmware upload stat data error");
		return err;
	}
	err = dice_fl_exec(dice, OPCODE_UPLOAD_STAT, 50);
	if (err != 0) {
		dev_err(&dice->unit->device, "firmware upload stat error");
		return -EIO;
	}
	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
						values, 4, 0);
	if (err < 0) {
		dev_err(&dice->unit->device, "firmware upload stat res error");
		return err;
	}
	if (be32_to_cpu(values[0]) != checksum) {
		dev_err(&dice->unit->device, "firmware upload checksum error (index:%#x, dChk: %#x, flChck: %#x)", index, be32_to_cpu(values[0]), checksum);
		return -EIO;
	}
	_dev_info(&dice->unit->device, "firmware upload successful (size:%i, check:%#x).", fw->size, checksum);
	return 0;

upload_err:
	kfree(upload_buffer);
	return err;
}

#define DICE_FIRMWARE_IMG_NAME			"dice\0"
#define DICE_FIRMWARE_IMG_EXEC_ADDR		0x30000
#define DICE_FIRMWARE_IMG_ENTRY_ADDR	0x30040

static int dice_fl_upload(struct dice* dice, struct firmware const* fw, bool force)
{
	int err;
	char img_name[16] = DICE_FIRMWARE_IMG_NAME;
	__be32 values[3];
	unsigned int i, length;

	struct dice_fl_app_info cur_firmware_info;
	struct dice_fl_file_vinfo_new* file_firmware_info;

	err = dice_fl_get_file_vinfo(fw, &file_firmware_info);
	if (err < 0) {
		return err;
	}
	if (!file_firmware_info) {
		return -ENOENT;
	}
	err = dice_fl_get_cur_app(dice, &cur_firmware_info);
	if (err < 0) {
		return err;
	}
	_dev_info(&dice->unit->device, " current firmware: vendor:%#x, product:%i, FW:%i.%i.%i.%i (%s, %s), SDK:%i.%i.%i.%i",
			cur_firmware_info.ui_vendor_id, cur_firmware_info.ui_product_id,
			DICE_FW_VERSION32_MAJOR(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_MINOR(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_SUB(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_BUILD(cur_firmware_info.ui_application_version),
			cur_firmware_info.build_date, cur_firmware_info.build_time,
			SDK_VERSION32_MAJOR(cur_firmware_info.ui_base_sdk_version),SDK_VERSION32_MINOR(cur_firmware_info.ui_base_sdk_version),SDK_VERSION32_SUB(cur_firmware_info.ui_base_sdk_version),SDK_VERSION32_BUILD(cur_firmware_info.ui_base_sdk_version));
	if (!force) {
		if ((file_firmware_info->ui_vendor_id != cur_firmware_info.ui_vendor_id) ||
				(file_firmware_info->ui_product_id != cur_firmware_info.ui_product_id)) {
			dev_warn(&dice->unit->device, "supplied firmware (vendor:%#x,prod:%i) is incompatible with this DICE product (vendor:%#x,prod:%i)",
					file_firmware_info->ui_vendor_id, file_firmware_info->ui_product_id,
					cur_firmware_info.ui_vendor_id, cur_firmware_info.ui_product_id);
			return -EPERM;
		}
		if (file_firmware_info->ui_application_version < cur_firmware_info.ui_application_version) {
			dev_warn(&dice->unit->device, "supplied firmware (%i.%i.%i.%i) is inferior to current DICE firmware (%i.%i.%i.%i)",
					DICE_FW_VERSION32_MAJOR(file_firmware_info->ui_application_version),DICE_FW_VERSION32_MINOR(file_firmware_info->ui_application_version),DICE_FW_VERSION32_SUB(file_firmware_info->ui_application_version),DICE_FW_VERSION32_BUILD(file_firmware_info->ui_application_version),
					DICE_FW_VERSION32_MAJOR(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_MINOR(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_SUB(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_BUILD(cur_firmware_info.ui_application_version));
			return -EPERM;
		}
		if ((file_firmware_info->ui_application_version == cur_firmware_info.ui_application_version) &&
				file_firmware_info->ui_base_sdk_version <= cur_firmware_info.ui_base_sdk_version) {
			dev_warn(&dice->unit->device, "supplied firmware (%i.%i.%i.%i, SDK:%i.%i.%i.%i) is inferior to current DICE firmware (%i.%i.%i.%i, SDK:%i.%i.%i.%i)",
					DICE_FW_VERSION32_MAJOR(file_firmware_info->ui_application_version),DICE_FW_VERSION32_MINOR(file_firmware_info->ui_application_version),DICE_FW_VERSION32_SUB(file_firmware_info->ui_application_version),DICE_FW_VERSION32_BUILD(file_firmware_info->ui_application_version),
					SDK_VERSION32_MAJOR(file_firmware_info->ui_base_sdk_version),SDK_VERSION32_MINOR(file_firmware_info->ui_base_sdk_version),SDK_VERSION32_SUB(file_firmware_info->ui_base_sdk_version),SDK_VERSION32_BUILD(file_firmware_info->ui_base_sdk_version),
					DICE_FW_VERSION32_MAJOR(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_MINOR(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_SUB(cur_firmware_info.ui_application_version),DICE_FW_VERSION32_BUILD(cur_firmware_info.ui_application_version),
					SDK_VERSION32_MAJOR(cur_firmware_info.ui_base_sdk_version),SDK_VERSION32_MINOR(cur_firmware_info.ui_base_sdk_version),SDK_VERSION32_SUB(cur_firmware_info.ui_base_sdk_version),SDK_VERSION32_BUILD(cur_firmware_info.ui_base_sdk_version));
			return -EPERM;
		}
	}
	_dev_info(&dice->unit->device, "load new firmware: vendor:%#x, product:%i, FW:%i.%i.%i.%i (%s, %s), SDK:%i.%i.%i.%i",
			file_firmware_info->ui_vendor_id, file_firmware_info->ui_product_id,
			DICE_FW_VERSION32_MAJOR(file_firmware_info->ui_application_version),DICE_FW_VERSION32_MINOR(file_firmware_info->ui_application_version),DICE_FW_VERSION32_SUB(file_firmware_info->ui_application_version),DICE_FW_VERSION32_BUILD(file_firmware_info->ui_application_version),
			file_firmware_info->build_date, file_firmware_info->build_time,
			SDK_VERSION32_MAJOR(file_firmware_info->ui_base_sdk_version),SDK_VERSION32_MINOR(file_firmware_info->ui_base_sdk_version),SDK_VERSION32_SUB(file_firmware_info->ui_base_sdk_version),SDK_VERSION32_BUILD(file_firmware_info->ui_base_sdk_version));
	/* Swap image name to correct endianness */
	BUILD_BUG_ON(sizeof(img_name) % 4 != 0);
	for (i = 0; i < sizeof(img_name); i += 4)
		swab32s((u32 *)&img_name[i]);
	img_name[sizeof(img_name) - 1] = '\0';

	/* TODO: Upload in blocks of 1KB, calculate & compare 32bit checksum after each block */
	err = dice_fl_upload_blocks(dice, fw);
	if (err < 0) {
		return err;
	}
	dev_warn(&dice->unit->device, "aborted load.");
	return 1;

	/* Delete old "dice" image */
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_BLOCK_REQUEST,
					DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
					img_name, sizeof(img_name), 0);
	if (err < 0) {
		return err;
	}
	err = dice_fl_exec(dice, OPCODE_DELETE_IMAGE, 300);
	if (err != 0) {
		return -EIO;
	}

	/* Create new "dice" image */
	values[0] = cpu_to_be32(length);
	values[1] = cpu_to_be32(DICE_FIRMWARE_IMG_EXEC_ADDR);
	values[2] = cpu_to_be32(DICE_FIRMWARE_IMG_ENTRY_ADDR);
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA,
						values, 4*3, 0);
	err = snd_fw_transaction(dice->unit, TCODE_WRITE_BLOCK_REQUEST,
						DICE_FIRMWARE_LOAD_SPACE + FIRMWARE_DATA + 4*3,
						img_name, sizeof(img_name), 0);
	err = dice_fl_exec(dice, OPCODE_CREATE_IMAGE, 300);
	if (err != 0) {
		return -EIO;
	}

	/* Reset device */
	err = dice_fl_exec(dice, OPCODE_RESET_IMAGE, 20);
	if (err != 0) {
		return -EIO;
	}

	return 0;
}

static void dice_fl_firmware_failed(struct dice* dice, const struct firmware *fw) {
	if (fw) {
		_dev_info(&dice->unit->device, "releasing firmware (size: %i).\n",fw->size);
		release_firmware(fw);
	}
}

void dice_fl_firmware_async(const struct firmware *fw, void *context)
{
	int err;
	struct dice* dice = context;

	if (!fw) {
		dev_err(&dice->unit->device, "firmware not found.\n");
		goto fw_done;
	}
	_dev_info(&dice->unit->device, "firmware found (size: %i).\n",fw->size);

	err = dice_fl_upload(dice, fw, false);
	if (err) {
		goto fw_done;
	}

fw_done:
	dice_fl_firmware_failed(dice, fw);
}

static int dice_fl_request_firmware(struct dice* dice)
{
	int err = 0;
	struct firmware const* fw;

#ifdef DEBUG_DICE_FW_BIN_NAME
	err = request_firmware(&fw, DEBUG_DICE_FW_BIN_NAME, &dice->unit->device);
	if (err < 0) {
		dice_fl_firmware_failed(dice, fw);
		return err;
	}
#endif

	dice_fl_firmware_async(fw, dice);
	return 0;
}
