
#include <linux/slab.h>
#include <linux/string.h>
#include <sound/control.h>

#include "../fcp.h"
#include "../avc.h"
#include "dice.h"
#include "avc.h"

struct __attribute__ ((__packed__)) avc_su_cmd {
	u8 ctype;
	unsigned subunit_type	: 5;
	unsigned subunit_id		: 3;
	u8 opcode;
};
struct __attribute__ ((__packed__)) avc_su_vendor_cmd {
	const struct avc_su_cmd cmd;
	unsigned vendor_id		: 24;
};
struct __attribute__ ((__packed__)) avc_su_tc_vendor_cmd {
	const struct avc_su_vendor_cmd cmd;
	u8 class_id;
	u8 seq_id;
	u16 cmd_id;
};

/**
 * dice_avc_vendor_spec_cmd - send vendor specific AV/C command
 * @dice
 * @cmd: command header
 * @operands: buffer containing command operands (will be appended to command header)
 * @op_size: operands buffer size (bytes)
 * @response: buffer to copy response data to
 * @resp_size: size of response data to be copied
 * @resp_match_bytes: a bitmap specifying the response bytes used to detect the correct response frame
 * 				(apart from the command header which is already checked (bits1-9)), this bitmap will be
 * 				shifted accordingly (i.e. use BIT(0...) to address the bytes within the response buffer).
 */
static int dice_avc_vendor_spec_cmd(struct dice* dice, struct avc_su_tc_vendor_cmd* cmd,
										void* operands, size_t op_size,
										void* response, size_t resp_size,
										unsigned int resp_match_bytes)
{
	int err;
	u8* buf;
	const size_t tx_size = sizeof(struct avc_su_tc_vendor_cmd)+op_size;
	const size_t rx_size = sizeof(struct avc_su_tc_vendor_cmd)+resp_size;
	size_t buf_size = max_t(size_t, tx_size, rx_size);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		return -ENOMEM;
	}
	buf[0] = cmd->cmd.cmd.ctype;
	buf[1] = (AVC_SU_TYPE_UNIT<<3) | AVC_SU_ID_IGNORE; //(cmd->cmd.cmd.subunit_type<<3) | cmd->cmd.cmd.subunit_id;
	buf[2] = AVC_CMD_VENDOR_DEPENDENT; //cmd->cmd.cmd.opcode
	buf[3] = 0xff & (cmd->cmd.vendor_id>>16);
	buf[4] = 0xff & (cmd->cmd.vendor_id>>8);
	buf[5] = 0xff & (cmd->cmd.vendor_id);
	buf[6] = cmd->class_id;
	buf[7] = 0xff; //cmd->seq_id
	buf[8] = 0xff & (cmd->cmd_id>>8);
	buf[9] = 0xff & (cmd->cmd_id);
	if (operands && op_size > 0) {
		memcpy(&buf[sizeof(struct avc_su_tc_vendor_cmd)], operands, op_size);
	}

	err = fcp_avc_transaction(dice->unit, buf, tx_size, buf, rx_size,
				BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(5)|BIT(6)|BIT(7)|BIT(8)|BIT(9)|
							(resp_match_bytes<<(sizeof(struct avc_su_tc_vendor_cmd)-1)));
	if (err < 0) {
		dev_err(&dice->unit->device, "AVC transaction failed (%i).\n",err);
		goto error;
	}
	if (err < rx_size) {
		dev_err(&dice->unit->device, "short FCP response (%#x != %#x)\n", err, rx_size);
		err = -EIO;
		goto error;
	}
	if (buf[0] != ((cmd->cmd.cmd.ctype == AVC_CTYPE_CONTROL) ? AVC_RESPONSE_ACCEPTED : AVC_RESPONSE_STABLE)) {
		dev_err(&dice->unit->device, "vendor read command failed (%#x)\n",buf[0]);
		err = -EIO;
	}
	if (response && resp_size > 0) {
		memcpy(response, &buf[rx_size-resp_size], resp_size);
	}

error:
	kfree(buf);
	return err;
}

/* TC ELECTRONIC VENDOR SPECIFIC CALLBACK CLASS ID DEFINITIONS */
#define TC_VSAVC_CLASS_COMMON			0	// firmware common commands
#define	TC_VSAVC_CLASS_GENERAL			1	// general command

/* TC_VSAVC_CLASS_COMMON command id definitions */
#define TC_VSAVC_CMD_SQUAWK				6	//
#define TC_VSAVC_CMD_SELF_IDENTIFY      7   // self identify command
#define TC_VSAVC_CMD_CODELOAD			14	// commands for uploading code into device

/* TC_VSAVC_CLASS_GENERAL command id definitions */
#define	TC_VSAVC_CMD_PGM_IDENTIFY		1	// firmware identify command
#define	TC_VSAVC_CMD_TUNER_FREQ			2	// tuner frequency command
#define	TC_VSAVC_CMD_TUNER_PRESET		3	// tuner preset command
#define	TC_VSAVC_CMD_TUNER_SCAN_MODE	4	// tuner scan mode command
#define	TC_VSAVC_CMD_TUNER_OUTPUT		5	// tuner output command
#define TC_VSAVC_CMD_RAW_SERIAL			10  // outputs raw serial out the host port.
/********
 * Vendor specific command ID interpretations (may be reinterpreted by others
 * as long as vendor ID is unique):
 */
/**
 * Weiss command ID interpretation:
 */
#define TC_VSAVC_CMD_WEISS_BASE			0x8000	// Weiss AV/C commands base ID
#define WEISS_AVC_NAMES_MAX_SIZE		64
/**
 * Device constitution info:
 */
# define WEISS_CMD_ID_DEV_CONST			TC_VSAVC_CMD_WEISS_BASE+0x01
struct weiss_cmd_dev_const {		// R
	u32 num_params;
	u32 num_attrs;
	u32 reserved[6];/*future use*/
};
/**
 * Parameter operation (read/write value)
 */
# define WEISS_CMD_ID_PARAM_OP			TC_VSAVC_CMD_WEISS_BASE+0x02
struct weiss_cmd_param_op {			// R/W
	u32 param_id;
	u32 value;
	u32 reserved[4];/*future use*/
};
/**
 * Parameter information query
 */
# define WEISS_CMD_ID_PARAM_INFO		TC_VSAVC_CMD_WEISS_BASE+0x03
struct weiss_cmd_param_info {		// R
	u32 param_id;
	u32 type;	//snd_ctl_elem_type_t
	u32 iface;	//snd_ctl_elem_iface_t
	union {
		struct {
			u32 min;
			u32 max;
			u32 step;
		} integer;
		struct {
			u32 items;
		} enumerated;
	};
	char name[WEISS_AVC_NAMES_MAX_SIZE];
	u32 reserved[3];/*future use*/
};
/**
 * Enumeration type parameter item information query
 */
# define WEISS_CMD_ID_ENUM_ITEM_INFO	TC_VSAVC_CMD_WEISS_BASE+0x04
struct weiss_cmd_enum_item_info {	// R
	u32 param_id;
	u32 item_id;
	char name[WEISS_AVC_NAMES_MAX_SIZE];
};
/**
 * Attribute information query
 */
# define WEISS_CMD_ID_ATTR_INFO		TC_VSAVC_CMD_WEISS_BASE+0x05
#  define WEISS_ATTR_TYPE_STR	0
#  define WEISS_ATTR_TYPE_INT	1
struct weiss_cmd_attr_info {	// R
	u32 attr_id;
	char name[WEISS_AVC_NAMES_MAX_SIZE];
	u32 type;
	union {
		char string[WEISS_AVC_NAMES_MAX_SIZE];
		u32 integer;
	};
};

static int weiss_dice_write_param(struct dice* dice, struct weiss_cmd_param_op* param)
{
	int err;
	u8 i;
	// command:
	struct avc_su_tc_vendor_cmd cmd = {
		.cmd = {
			.cmd = {
				.ctype = AVC_CTYPE_CONTROL,
				.subunit_type = AVC_SU_TYPE_UNIT,
				.subunit_id = AVC_SU_ID_IGNORE,
				.opcode = AVC_CMD_VENDOR_DEPENDENT,
			},
			.vendor_id = dice->vendor,
		},
		.class_id = TC_VSAVC_CLASS_GENERAL,
		.seq_id = 0xff,
		.cmd_id = WEISS_CMD_ID_PARAM_OP,
	};
	// operand/response vessel:
	for (i = 0; i < sizeof(struct weiss_cmd_param_op)/4; ++i) {
		cpu_to_be32s(&((u32 *)param)[i]);
	}
	err = dice_avc_vendor_spec_cmd(dice, &cmd,
				param, sizeof(struct weiss_cmd_param_op),
				NULL, 0, BIT(0)|BIT(1)|BIT(2)|BIT(3)); /*param ID: first 4 bytes*/
	if (err<0) {
		dev_err(&dice->unit->device, "AVC param write failed (%i).\n", err);
		return err;
	}
	return err;
}

static int weiss_dice_read_param(struct dice* dice, struct weiss_cmd_param_op* param)
{
	int err;
	u8 i;
	// command:
	struct avc_su_tc_vendor_cmd cmd = {
		.cmd = {
			.cmd = {
				.ctype = AVC_CTYPE_STATUS,
				.subunit_type = AVC_SU_TYPE_UNIT,
				.subunit_id = AVC_SU_ID_IGNORE,
				.opcode = AVC_CMD_VENDOR_DEPENDENT,
			},
			.vendor_id = dice->vendor,
		},
		.class_id = TC_VSAVC_CLASS_GENERAL,
		.seq_id = 0xff,
		.cmd_id = WEISS_CMD_ID_PARAM_OP,
	};
	// operand/response vessel:
	param->value = 0xffffffff;
	for (i = 0; i < sizeof(struct weiss_cmd_param_op)/4; ++i) {
		cpu_to_be32s(&((u32 *)param)[i]);
	}
	err = dice_avc_vendor_spec_cmd(dice, &cmd,
				param, sizeof(struct weiss_cmd_param_op),
				param, sizeof(struct weiss_cmd_param_op),
				BIT(0)|BIT(1)|BIT(2)|BIT(3)); /*param ID: first 4 bytes*/
	if (err<0) {
		dev_err(&dice->unit->device, "AVC param read failed (%i).\n", err);
		return err;
	}
	for (i = 0; i < sizeof(struct weiss_cmd_param_op)/4; ++i) {
		be32_to_cpus(&((u32 *)param)[i]);
	}
	return err;
}
static int weiss_dice_dev_const(struct dice* dice, struct weiss_cmd_dev_const* dev_const)
{
	int err;
	u8 i;
	// command:
	struct avc_su_tc_vendor_cmd cmd = {
		.cmd = {
			.cmd = {
				.ctype = AVC_CTYPE_STATUS,
				.subunit_type = AVC_SU_TYPE_UNIT,
				.subunit_id = AVC_SU_ID_IGNORE,
				.opcode = AVC_CMD_VENDOR_DEPENDENT,
			},
			.vendor_id = dice->vendor,
		},
		.class_id = TC_VSAVC_CLASS_GENERAL,
		.seq_id = 0xff,
		.cmd_id = WEISS_CMD_ID_DEV_CONST,
	};
	// operand/response vessel:
	for (i = 0; i < sizeof(struct weiss_cmd_dev_const)/4; ++i) {
		cpu_to_be32s(&((u32 *)dev_const)[i]);
	}
	err = dice_avc_vendor_spec_cmd(dice, &cmd,
				dev_const, sizeof(struct weiss_cmd_dev_const),
				dev_const, sizeof(struct weiss_cmd_dev_const), 0);
	if (err<0) {
		dev_err(&dice->unit->device, "AVC dev_const read failed (%i).\n", err);
		return err;
	}
	for (i = 0; i < sizeof(struct weiss_cmd_dev_const)/4; ++i) {
		be32_to_cpus(&((u32 *)dev_const)[i]);
	}
	_dev_info(&dice->unit->device, "Weiss device constitution: params:%#x,attrs:%#x\n", dev_const->num_params, dev_const->num_attrs);
	return err;
}
static int weiss_dice_param_info(struct dice* dice, struct weiss_cmd_param_info* param_info)
{
	int err;
	u8 i;
	// command:
	struct avc_su_tc_vendor_cmd cmd = {
		.cmd = {
			.cmd = {
				.ctype = AVC_CTYPE_STATUS,
				.subunit_type = AVC_SU_TYPE_UNIT,
				.subunit_id = AVC_SU_ID_IGNORE,
				.opcode = AVC_CMD_VENDOR_DEPENDENT,
			},
			.vendor_id = dice->vendor,
		},
		.class_id = TC_VSAVC_CLASS_GENERAL,
		.seq_id = 0xff,
		.cmd_id = WEISS_CMD_ID_PARAM_INFO,
	};
	// operand/response vessel:
	for (i = 0; i < sizeof(struct weiss_cmd_param_info)/4; ++i) {
		cpu_to_be32s(&((u32 *)param_info)[i]);
	}
	err = dice_avc_vendor_spec_cmd(dice, &cmd,
			param_info, sizeof(struct weiss_cmd_param_info),
			param_info, sizeof(struct weiss_cmd_param_info),
			BIT(0)|BIT(1)|BIT(2)|BIT(3)); /*param ID: first 4 bytes*/
	if (err<0) {
		dev_err(&dice->unit->device, "AVC param_info read failed (%i).\n", err);
		return err;
	}
	for (i = 0; i < sizeof(struct weiss_cmd_param_info)/4; ++i) {
		be32_to_cpus(&((u32 *)param_info)[i]);
	}
//	_dev_info(&dice->unit->device, "read param info (ID:%#x,name:'%s',iface:%#x,type:%#x,\n",
//			param_info->param_id, param_info->name, param_info->iface, param_info->type);
//	switch(param_info->type) {
//	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
//		_dev_info(&dice->unit->device, "  items:%#x)\n",
//				param_info->enumerated.items);
//		break;
//	case SNDRV_CTL_ELEM_TYPE_INTEGER:
//		_dev_info(&dice->unit->device, "  min:%#x,max:%#x)\n",
//				param_info->integer.min, param_info->integer.max);
//		break;
//	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
//		break;
//	default:
//		dev_err(&dice->unit->device, "unsupported parameter type (%#x)\n", param_info->type);
//		return -EINVAL;
//	}
	return err;
}
static int weiss_dice_enum_item_info(struct dice* dice, struct weiss_cmd_enum_item_info* item_info)
{
	int err;
	u8 i;
	// command:
	struct avc_su_tc_vendor_cmd cmd = {
		.cmd = {
			.cmd = {
				.ctype = AVC_CTYPE_STATUS,
				.subunit_type = AVC_SU_TYPE_UNIT,
				.subunit_id = AVC_SU_ID_IGNORE,
				.opcode = AVC_CMD_VENDOR_DEPENDENT,
			},
			.vendor_id = dice->vendor,
		},
		.class_id = TC_VSAVC_CLASS_GENERAL,
		.seq_id = 0xff,
		.cmd_id = WEISS_CMD_ID_ENUM_ITEM_INFO,
	};
	// operand/response vessel:
	for (i = 0; i < sizeof(struct weiss_cmd_enum_item_info)/4; ++i) {
		cpu_to_be32s(&((u32 *)item_info)[i]);
	}
	err = dice_avc_vendor_spec_cmd(dice, &cmd,
			item_info, sizeof(struct weiss_cmd_enum_item_info),
			item_info, sizeof(struct weiss_cmd_enum_item_info),
			BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(5)|BIT(6)|BIT(7)); /*param&item ID: first 8 bytes*/
	if (err<0) {
		dev_err(&dice->unit->device, "AVC item_info read failed (%i).\n", err);
		return err;
	}
	for (i = 0; i < sizeof(struct weiss_cmd_enum_item_info)/4; ++i) {
		be32_to_cpus(&((u32 *)item_info)[i]);
	}
//	_dev_info(&dice->unit->device, "read item info (paramID:%#x,itemID:%#x,name:'%s')\n",
//			item_info->param_id, item_info->item_id, item_info->name);
	return err;
}

static int dice_sync_src_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	static char *texts[13] = {
		"AES1","AES2","AES3","AES4","AES","ADAT","TDIF","Wordclock","ARX1","ARX2","ARX3","ARX4","Internal"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 13;
	if (uinfo->value.enumerated.item > 12)
		uinfo->value.enumerated.item = 12;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int dice_sync_src_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	u32 value;
	err = dice_ctrl_get_global_clock_select(dice, &value);
	if (err < 0) {
		return err;
	}
	value &= CLOCK_SOURCE_MASK;
	ucontrol->value.enumerated.item[0] = value;
	return 0;
}
static int dice_sync_src_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err = 0;
	u32 value = ucontrol->value.enumerated.item[0] & CLOCK_SOURCE_MASK;
	err = dice_ctrl_set_clock_source(dice, value, false);
	if (err < 0) {
		return err;
	}
	return 1;
}

static int dice_weiss_param_enum_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	struct weiss_cmd_param_info param_info = {
		.param_id = (u32)kcontrol->private_value,
	};
	struct weiss_cmd_enum_item_info item_info = {
		.param_id = (u32)kcontrol->private_value,
		.item_id = uinfo->value.enumerated.item,
	};
	err = weiss_dice_param_info(dice, &param_info);
	if (err < 0) {
		return err;
	}
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = param_info.enumerated.items;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items) {
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	}
	err = weiss_dice_enum_item_info(dice, &item_info);
	if (err < 0) {
		return err;
	}
	strcpy(uinfo->value.enumerated.name, item_info.name);
	return 0;
}
static int dice_weiss_param_enum_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	struct weiss_cmd_param_op param;
	param.param_id = (u32)kcontrol->private_value;
	err = weiss_dice_read_param(dice, &param);
	if (err < 0)
		return err;
	ucontrol->value.enumerated.item[0] = param.value;
	return 0;
}
static int dice_weiss_param_enum_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	struct weiss_cmd_param_op param;
	param.param_id = (u32)kcontrol->private_value;
	param.value = (u32)(ucontrol->value.enumerated.item[0]);
//	_dev_info(&dice->unit->device,"put param (E,pID:%#x): %#x...\n", (u32)kcontrol->private_value, param.value);
	err = weiss_dice_write_param(dice, &param);
	if (err < 0)
		return err;
	return 1;
}

static int dice_weiss_param_int_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	struct weiss_cmd_param_info param_info;
	param_info.param_id = (u32)kcontrol->private_value;
	err = weiss_dice_param_info(dice, &param_info);
	if (err < 0) {
		dev_err(&dice->unit->device,"fail to get param info (%i)", err);
		return err;
	}
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = param_info.integer.min;
	uinfo->value.integer.max = param_info.integer.max;
	uinfo->value.integer.step = param_info.integer.step;
	return 0;
}
static int dice_weiss_param_int_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	struct weiss_cmd_param_op param;
	param.param_id = (u32)kcontrol->private_value;
	err = weiss_dice_read_param(dice, &param);
	if (err < 0) {
		return err;
	}
	ucontrol->value.integer.value[0] = (long)param.value;
	return 0;
}
static int dice_weiss_param_int_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	struct weiss_cmd_param_op param;
	param.param_id = (u32)kcontrol->private_value;
	param.value = (u32)(ucontrol->value.integer.value[0]);
//	_dev_info(&dice->unit->device,"put param (I,pID:%#x): %#x...\n", (u32)kcontrol->private_value, param.value);
	err = weiss_dice_write_param(dice, &param);
	if (err < 0) {
		return err;
	}
	return 1;
}
/**
 * Weiss specific method for populating its products'
 * snd_ctl's (via custom vendor dependent AV/C commands).
 */
static int dice_weiss_snd_ctl_construct(struct dice* dice)
{
	int err;
	struct weiss_cmd_dev_const dev_const;
	struct weiss_cmd_param_info param_info;
	u32 i;
	err = weiss_dice_dev_const(dice, &dev_const);
	if (err < 0)
		return err;

	for (i=0; i<dev_const.num_params; ++i) {
		param_info.param_id = i;
		err = weiss_dice_param_info(dice, &param_info);
		if (err < 0)
			continue;
		if (param_info.iface > (u32)SNDRV_CTL_ELEM_IFACE_LAST) {
			dev_err(&dice->unit->device,"invalid iface (%#x)\n", param_info.iface);
			continue;
		}
		switch (param_info.type) {
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED: {
				struct snd_kcontrol_new control = {
					.iface = (snd_ctl_elem_iface_t) param_info.iface,
					.name = param_info.name,
					.info = dice_weiss_param_enum_info,
					.get = dice_weiss_param_enum_get,
					.put = dice_weiss_param_enum_put,
					.private_value = i,
				};
				err = snd_ctl_add(dice->card, snd_ctl_new1(&control, dice));
				if (err < 0) {
					return err;
				}
			}
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER: {
				struct snd_kcontrol_new control = {
					.iface = (snd_ctl_elem_iface_t) param_info.iface,
					.name = param_info.name,
					.info = dice_weiss_param_int_info,
					.get = dice_weiss_param_int_get,
					.put = dice_weiss_param_int_put,
					.private_value = i,
				};
				err = snd_ctl_add(dice->card, snd_ctl_new1(&control, dice));
				if (err < 0) {
					return err;
				}
			}
			break;
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN: {
				struct snd_kcontrol_new control = {
					.iface = (snd_ctl_elem_iface_t) param_info.iface,
					.name = param_info.name,
					.info = snd_ctl_boolean_mono_info,
					.get = dice_weiss_param_int_get,
					.put = dice_weiss_param_int_put,
					.private_value = i,
				};
				err = snd_ctl_add(dice->card, snd_ctl_new1(&control, dice));
				if (err < 0) {
					return err;
				}
			}
			break;
		default:
			dev_err(&dice->unit->device,"unsupported param type (%#x)\n", param_info.type);
			break;
		}
	}
	return 0;
}

#define OUI_WEISS		0x001c6a

int dice_snd_ctl_construct(struct dice* dice)
{
	int err = 0;
	u32 i;
	// Intrinsic controls (for all DICE cards)
	static const struct snd_kcontrol_new controls[] = {
		{
			.iface = SNDRV_CTL_ELEM_IFACE_CARD,
			.name = "Sync source",
			.info = dice_sync_src_info,
			.get = dice_sync_src_get,
			.put = dice_sync_src_put,
		}
	};
	for (i = 0; i < ARRAY_SIZE(controls); ++i) {
		err = snd_ctl_add(dice->card, snd_ctl_new1(&controls[i], dice));
		if (err < 0)
			return err;
	}
	switch (dice->vendor) {
	case OUI_WEISS:
		dice_weiss_snd_ctl_construct(dice);
		break;
	}
	return err;
}
