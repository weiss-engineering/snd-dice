
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
		goto error;
	}
	if (err < rx_size) {
		dev_err(&dice->unit->device, "short FCP response\n");
		err = -EIO;
		goto error;
	}
	if (buf[0] != AVC_RESPONSE_STABLE) {
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

int dice_avc_vendor_spec_cmd_fwinfo(struct dice* dice)
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
