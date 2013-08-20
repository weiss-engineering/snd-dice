
#include <linux/slab.h>
#include <linux/string.h>

#include "../fcp.h"
#include "../avc.h"
#include "dice.h"
#include "dice-avc.h"

union avc_su_cmd {
	struct __attribute__ ((__packed__))  {
		u8 ctype;
		unsigned subunit_type	: 5;
		unsigned subunit_id		: 3;
		u8 opcode;
	};
	struct __attribute__ ((__packed__)) {
		u8 bytes[3];
	};
};

union avc_su_vendor_cmd {
	struct __attribute__ ((__packed__)) {
		union avc_su_cmd cmd;
		unsigned vendor_id		: 24;
	};
	struct __attribute__ ((__packed__)) {
		u8 bytes[sizeof(union avc_su_cmd) + 3];
	};
};

union avc_su_tc_vendor_cmd {
	struct __attribute__ ((__packed__)) {
		const union avc_su_vendor_cmd cmd;
		u8 class_id;
		u8 seq_id;
		u16 cmd_id;
	};
	struct __attribute__ ((__packed__)) {
		u8 bytes[sizeof(union avc_su_vendor_cmd) + 4];
	};
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
int dice_avc_read_vendor(struct dice* dice)
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

static int dice_avc_read_vendor_obsolete(struct dice* dice)
{
	int err = 0;
	u8 *operands;
	u8 *response;
	u8 i;
	union avc_su_tc_vendor_cmd cmd = {
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
	operands = kmalloc(4*8, GFP_KERNEL);
	if (!operands) {
		return -ENOMEM;
	}
	response = kmalloc(DEBUG_RESP_SIZE, GFP_KERNEL);
	if (!response) {
		kfree(operands);
		return -ENOMEM;
	}
	for (i=0;i<4*8;i++) {
		operands[i] = 0xff;
	}

//	err = dice_avc_tc_vendor_cmd(dice, cmd, operands, 4*8, response, DEBUG_RESP_SIZE);
	if (err<0) {
		goto error;
	}

	for (i=0; i<DEBUG_RESP_SIZE; i+=4) {
		_dev_info(&dice->unit->device, "vendor res: %2i+ 0:%#04x,1:%#04x,2:%#04x,3:%#04x\n",i,
			response[i+0],
			response[i+1],
			response[i+2],
			response[i+3]
		);
	}
error:
	kfree(operands);
	kfree(response);
	return err;
}
