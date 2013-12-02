#ifndef SOUND_FIREWIRE_DICE_H_INCLUDED
#define SOUND_FIREWIRE_DICE_H_INCLUDED

#include <linux/firewire.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "../amdtp.h"
#include "../iso-resources.h"
#include "interface.h"

#define OUI_MAUDIO		0x000d6c
#define OUI_WEISS		0x001c6a

#define DICE_CATEGORY_ID	0x04
#define WEISS_CATEGORY_ID	0x00

/** Maximum number of isochronous channels per direction. */
#define DICE_MAX_FW_ISOC_CH		4

/** Data directions as seen from DICE' perspective. */
enum dice_direction {
	DICE_RX = 0,
	DICE_TX = 1,
	DICE_PLAYBACK = DICE_RX,
	DICE_CAPTURE = DICE_TX
};

struct dice_stream_config {
	bool valid;
	/** Number of isochronous fw channels. */
	unsigned int num_isoc_ch;
	/** Total number of PCM channels (accumulated
	 * PCM channels over all isochronous fw channels). */
	unsigned int num_pcm_ch;
	/** Total number of MIDI channels (accumulated
	 * PCM channels over all isochronous fw channels). */
	unsigned int num_midi_ch;

	struct {
		/** Number of PCM channels for the corresponding isochronous fw channel. */
		u8 pcm_channels;
		/** Number of MIDI ports for the corresponding isochronous fw channel. */
		u8 midi_ports;
	} isoc_layout[DICE_MAX_FW_ISOC_CH];
};

struct dice_stream {
	struct dice_stream_config config;
	struct fw_iso_resources resources;
	struct amdtp_stream stream;
	struct snd_pcm_substream *pcm_substream;
};

struct dice_firmware_info {
	unsigned int ui_base_sdk_version;		// [31-29]:buildFlags,[28-24]:vMaj,[23-20]:vMin,[19-16]:vSub,[15-0]:vBuild
	unsigned int ui_application_version;	// [31-24]:vMaj,[23-20]:vMin,[19-16]:vSub,[15-0]:vBuild
	unsigned int ui_vendor_id;
	unsigned int ui_product_id;
	char build_time[64];
	char build_date[64];
	unsigned int ui_board_serial_number;
};

struct dice_global_settings {
	u32 owner_hi;
	u32 owner_lo;
	u32 notification;
	char nick_name[NICK_NAME_SIZE];
	u32 clock_select;
	u32 enable;
	u32 status;
	u32 extended_status;
	u32 measured_sample_rate;
	u32 version;
	/* Old firmware does not necessarily support those two */
	u32 clock_caps;
	char clock_source_names[CLOCK_SOURCE_NAMES_SIZE];
};

struct dice_ext_sync_info {
	u32 clock_source;
	u32 locked;
	u32 rate_index;
	u32 adat_user_data;
};

struct dice {
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct fw_unit *unit;
	spinlock_t lock;
	struct mutex mutex;
	unsigned int vendor;
	unsigned int global_offset;
	unsigned int global_size;
	unsigned int ext_sync_offset;
	unsigned int rx_offset;
	unsigned int rx_size;
	unsigned int tx_offset;
	unsigned int tx_size;

	struct dice_global_settings global_settings;
	struct dice_ext_sync_info extended_sync_info;

	struct fw_address_handler fw_notification_handler;
	int owner_generation;
	int dev_lock_count; /* > 0 driver, < 0 userspace */
	bool dev_lock_changed;
	bool global_enabled;
	struct completion clock_accepted;
	wait_queue_head_t hwdep_wait;
	u32 notification_bits;

	struct workqueue_struct *notif_queue;

	struct dice_stream playback;
	struct dice_stream capture;

	struct dice_firmware_info app_info;
};

#define DICE_NUM_RATES	7
extern const unsigned int dice_rates[DICE_NUM_RATES];

unsigned int dice_rate_to_index(unsigned int rate);


void dice_lock_changed(struct dice *dice);
int dice_try_lock(struct dice *dice);
void dice_unlock(struct dice *dice);

static inline u64 dice_global_address(struct dice *dice, unsigned int offset)
{
	return DICE_PRIVATE_SPACE + dice->global_offset + offset;
}

static inline u64 dice_rx_address(struct dice *dice,
			     unsigned int index, unsigned int offset)
{
	return DICE_PRIVATE_SPACE + dice->rx_offset +
			index * dice->rx_size + offset;
}

static inline u64 dice_tx_address(struct dice *dice,
			     unsigned int index, unsigned int offset)
{
	return DICE_PRIVATE_SPACE + dice->tx_offset +
			index * dice->tx_size + offset;
}

int dice_ctrl_enable_set(struct dice *dice);
void dice_ctrl_enable_clear(struct dice *dice);

/** clock_rate must be one of CLOCK_RATE_XX and already shifted by CLOCK_RATE_SHIFT.
 * TODO: We should do all this stuff within this function.
 */
int dice_ctrl_change_rate(struct dice *dice, unsigned int clock_rate, bool force);
int dice_ctrl_set_clock_source(struct dice *dice, u32 clock_source, bool force);
int dice_ctrl_get_global_clock_select(struct dice *dice, u32 *clock);

static inline bool is_clock_source(u32 global_clock_select, u32 clock_source)
{
	return (global_clock_select & CLOCK_SOURCE_MASK) == clock_source;
}

static inline bool dice_driver_is_clock_master(u32 global_clock_select)
{
	return (global_clock_select & CLOCK_SOURCE_MASK) == CLOCK_SOURCE_ARX1/*||
			(global_clock_select & CLOCK_SOURCE_MASK) == global_clock_select & CLOCK_SOURCE_ARX2 ||
			(global_clock_select & CLOCK_SOURCE_MASK) == global_clock_select & CLOCK_SOURCE_ARX3 ||
			(global_clock_select & CLOCK_SOURCE_MASK) == global_clock_select & CLOCK_SOURCE_ARX4*/;
}

static inline int dice_ctrl_get_sample_rate(struct dice *dice, unsigned int *sample_rate)
{
	int err;
	unsigned int sample_rate_index;
	u32 global_clock_select;

	err = dice_ctrl_get_global_clock_select(dice, &global_clock_select);
	if (err < 0)
		return err;

	sample_rate_index = (global_clock_select & CLOCK_RATE_MASK) >> CLOCK_RATE_SHIFT;
	*sample_rate = dice_rates[sample_rate_index];

	return 0;
}

int dice_ctrl_get_ext_sync_info(struct dice *dice, struct dice_ext_sync_info *sync_info);
int dice_ctrl_get_global_settings(struct dice *dice, struct dice_global_settings *settings);

#endif
