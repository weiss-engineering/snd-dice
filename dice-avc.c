
#include <linux/slab.h>
#include <linux/string.h>

#include "../fcp.h"
#include "../avc.h"
#include "dice.h"
#include "dice-avc.h"

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
/* Weiss specific command ID definitions: */
#define TC_VSAVC_CMD_WEISS_CMDS			0x100  // Weiss wdicelib:avci commands

#define DEBUG_CMD_SIZE		0x2a
#define DEBUG_RESP_SIZE		0x4c
static int dice_avc_read_vendor(struct dice* dice)
{
	u8* buf_cmd;
	int err = 0;
	u8 i;

	buf_cmd = kmalloc(DEBUG_RESP_SIZE, GFP_KERNEL);
	if (!buf_cmd) {
		return -ENOMEM;
	}

	buf_cmd[0] = AVC_CTYPE_STATUS;
	buf_cmd[1] = AVC_SU_TYPE_UNIT<<3 | AVC_SU_ID_IGNORE;
	buf_cmd[2] = AVC_CMD_VENDOR_DEPENDENT;
	buf_cmd[3] = 0xff & (dice->vendor>>16);
	buf_cmd[4] = 0xff & (dice->vendor>>8);
	buf_cmd[5] = 0xff & (dice->vendor);
	buf_cmd[6] = TC_VSAVC_CLASS_GENERAL;
	buf_cmd[7] = 0xff;
	buf_cmd[8] = 0;
	buf_cmd[9] = TC_VSAVC_CMD_PGM_IDENTIFY;
	for (i=10; i<DEBUG_CMD_SIZE; i++)
		buf_cmd[i] = 0xff;

	err = fcp_avc_transaction(dice->unit, buf_cmd, DEBUG_CMD_SIZE, buf_cmd, DEBUG_RESP_SIZE, 0x3fe /*bytes 1-9*/);
	if (err < 0) {
		goto avc_done;
	}
	if (err < DEBUG_RESP_SIZE) {
		dev_err(&dice->unit->device, "short FCP response\n");
		err = -EIO;
		goto avc_done;
	}
	if (buf_cmd[0] != AVC_RESPONSE_STABLE) {
		dev_err(&dice->unit->device, "vendor read command failed (%#x)\n",buf_cmd[0]);
		err = -EIO;
//		goto avc_done;
	}
	for (i=0; i<DEBUG_RESP_SIZE; i+=4) {
		_dev_info(&dice->unit->device, "vendor res: %2i+ 0:%#04x,1:%#04x,2:%#04x,3:%#04x\n",i,
				buf_cmd[i+0],
				buf_cmd[i+1],
				buf_cmd[i+2],
				buf_cmd[i+3]
		);
	}
avc_done:
	kfree(buf_cmd);
	return err;
}

static int dice_avc_vendor_spec_cmd(struct dice* dice, struct avc_su_tc_vendor_cmd* cmd,
										void* operands, size_t op_size,
										void* response, size_t resp_size)
{
	int err;
	u8* buf;
	const size_t tx_size = sizeof(struct avc_su_tc_vendor_cmd)+op_size;
	const size_t rx_size = sizeof(struct avc_su_tc_vendor_cmd)+resp_size;
	size_t buf_size = min_t(size_t, tx_size, rx_size);

	_dev_info(&dice->unit->device, "transmitting AVC cmd...\n");
	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		return -ENOMEM;
	}
	buf[0] = cmd->cmd.cmd.ctype;
	buf[1] = (cmd->cmd.cmd.subunit_type<<3) | cmd->cmd.cmd.subunit_id;
	buf[2] = cmd->cmd.cmd.opcode;
	buf[3] = 0xff & (cmd->cmd.vendor_id>>16);
	buf[4] = 0xff & (cmd->cmd.vendor_id>>8);
	buf[5] = 0xff & (cmd->cmd.vendor_id);
	buf[6] = cmd->class_id;
	buf[7] = cmd->seq_id;
	buf[8] = 0xff & (cmd->cmd_id>>8);
	buf[9] = 0xff & (cmd->cmd_id);
	if (op_size > 0) {
		memcpy(buf+10, operands, op_size);
	}

	err = fcp_avc_transaction(dice->unit, buf, tx_size, buf, rx_size,
				BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(5)|BIT(6)|BIT(7)|BIT(8)|BIT(9));
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
	if (resp_size>0) {
		memcpy(response, buf+rx_size-resp_size, resp_size);
	}

error:
	kfree(buf);
	return err;
}

struct TC_PROGRAM_ATTRIBUTES
{
	u32 attributeVersion;
	u32 programType;
	u32 programVersion;
	u32 reserved[5];
};

static int dice_avc_vendor_spec_cmd_fwinfo(struct dice* dice)
{
	int err = 0;
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
		.cmd_id = TC_VSAVC_CMD_PGM_IDENTIFY,
	};
	// operand/response vessel:
	struct TC_PROGRAM_ATTRIBUTES tc_attrs = {
		.attributeVersion = 0xffffffff,
		.programType = 0xffffffff,
		.programVersion = 0xffffffff,
		.reserved = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
	};

	err = dice_avc_vendor_spec_cmd(dice, &cmd, &tc_attrs, sizeof(struct TC_PROGRAM_ATTRIBUTES), &tc_attrs, sizeof(struct TC_PROGRAM_ATTRIBUTES));
	if (err<0) {
		goto error;
	}
	for (i = 0; i < sizeof(struct TC_PROGRAM_ATTRIBUTES)/4; ++i) {
		be32_to_cpus(&((u32 *)&tc_attrs)[i]);
	}
	_dev_info(&dice->unit->device, "TC firmware info: attV:%#x,prT:%#x,prV:%#x,res:%#x/%#x/%#x/%#x/%#x\n",
			tc_attrs.attributeVersion,tc_attrs.programType,tc_attrs.programVersion,
			tc_attrs.reserved[0],tc_attrs.reserved[1],tc_attrs.reserved[2],tc_attrs.reserved[3],tc_attrs.reserved[4]);

error:
	return err;
}

static int weiss_dice_avc_write(struct dice* dice, u32* value)
{
	int err = 0;
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
		.cmd_id = TC_VSAVC_CMD_WEISS_CMDS,
	};
	// operand/response vessel:
	for (i = 0; i < sizeof(u32)/4; ++i) {
		cpu_to_be32s(&((u32 *)value)[i]);
	}

	err = dice_avc_vendor_spec_cmd(dice, &cmd, value, sizeof(u32), NULL, 0);
	if (err<0) {
		dev_err(&dice->unit->device, "AVC param write failed (%i).\n", err);
		goto error;
	}
	_dev_info(&dice->unit->device, "ctl put successful\n");

error:
	return err;
}

/*********************************************************
 * WEISS (vendor ID: 0x001C6A) vendor specific  commands:
 */
/**
 * Parameter operation (read/write value)
 */
#define WEISS_CMD_ID_PARAM_OP			0x00
struct weiss_cmd_param_op {			// R/W
	u32 param_id;
	u32 value;
};
/**
 * Parameter information query
 */
#define WEISS_CMD_ID_PARAM_INFO			0x01
# define WEISS_PTYPE_INT			0x00
# define WEISS_PTYPE_BOOL			0x01
# define WEISS_PTYPE_ENUM			0x02
struct weiss_cmd_param_info {		// R
	u32 param_id;
	u32 type;
	union {
		struct {
			u32 min;
			u32 max;
		} integer;
		struct {
			u32 items;
		} enumerated;
	};
};
/**
 * Enumeration type parameter item information query
 */
#define WEISS_CMD_ID_ENUM_ITEM_INFO		0x02
struct weiss_cmd_enum_item_info {	// R
	u32 param_id;
	u32 item_id;
	char name[64];
};

static int weiss_dice_avc_read(struct dice* dice, u32* value)
{
	int err = 0;
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
		.cmd_id = TC_VSAVC_CMD_WEISS_CMDS,
	};
	_dev_info(&dice->unit->device, "reading AVC param...\n");
	// operand/response vessel:
	*value = 0xffffffff;
	for (i = 0; i < sizeof(u32)/4; ++i) {
		cpu_to_be32s(&((u32 *)value)[i]);
	}

	err = dice_avc_vendor_spec_cmd(dice, &cmd, value, sizeof(u32), value, sizeof(u32));
	if (err<0) {
		dev_err(&dice->unit->device, "AVC param read failed (%i).\n", err);
		goto error;
	}
	for (i = 0; i < sizeof(u32)/4; ++i) {
		be32_to_cpus(&((u32 *)value)[i]);
	}
	_dev_info(&dice->unit->device, "ctl get successful (val:%#x)\n", *value);

error:
	return err;
}

#include <sound/control.h>

static int dice_sync_src_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
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
	err = dice_ctrl_get_clock(dice, &value);
	if (err < 0)
		return err;
	value &= CLOCK_SOURCE_MASK;
	ucontrol->value.enumerated.item[0] = value;
	return 0;
}
static int dice_sync_src_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	u32 value = ucontrol->value.enumerated.item[0] & CLOCK_SOURCE_MASK;
#if 0 /* use according method when implemented: */
	err = dice_set_clock_source(dice, &value);
#else
	err = 0;
#endif
	if (err < 0)
		return err;
	return 1;
}

static int dice_level_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	static char *texts[4] = {
		"0 dB", "-10 dB", "-20 dB", "-30 dB",
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	_dev_info(&dice->unit->device,"level info (num:%#x,id:%#x) done.\n", kcontrol->id.numid, kcontrol->id.index);
	return 0;
}
static int dice_level_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	u32 value;
	_dev_info(&dice->unit->device,"read level value...\n");
	err = weiss_dice_avc_read(dice, &value);
	if (err < 0)
		return err;
	ucontrol->value.enumerated.item[0] = value;
	return 0;
}
static int dice_level_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct dice *dice = snd_kcontrol_chip(kcontrol);
	int err;
	u32 value = ucontrol->value.enumerated.item[0];
	_dev_info(&dice->unit->device,"write level value (%#x)...\n", value);
	err = weiss_dice_avc_write(dice, &value);
	if (err < 0)
		return err;
	return 1;
}

int dice_snd_ctl_construct(struct dice* dice)
{
	int err = 0;
	u32 i;
	static const struct snd_kcontrol_new controls[] = {
		{
			.iface = SNDRV_CTL_ELEM_IFACE_CARD,
			.name = "Sync source",
			.info = dice_sync_src_info,
			.get = dice_sync_src_get,
			.put = dice_sync_src_put,
		},
		{
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.name = "Output level",
			.info = dice_level_info,
			.get = dice_level_get,
			.put = dice_level_put,
		},
	};
	_dev_info(&dice->unit->device,"construct snd ctls...\n");
	for (i = 0; i < ARRAY_SIZE(controls); ++i) {
		err = snd_ctl_add(dice->card, snd_ctl_new1(&controls[i], dice));
		if (err < 0)
			return err;
	}
	_dev_info(&dice->unit->device,"construct snd ctls done (%#x).\n", err);
	return err;
}
