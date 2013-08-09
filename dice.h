#ifndef SOUND_FIREWIRE_DICE_H_INCLUDED
#define SOUND_FIREWIRE_DICE_H_INCLUDED

#include <linux/firewire.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include "../amdtp.h"
#include "../iso-resources.h"
#include "dice-interface.h"

/** Number of sample rate modes. */
#define DICE_NUM_MODES	3
/** Maximum number of isochronous channels per direction. */
#define DICE_MAX_FW_ISOC_CH		4

enum dice_direction {
	DICE_DIRECTION_RX = 0,
	DICE_DIRECTION_TX = 1
};

struct audio_layout {
	/** Number of isochronous fw channels for the individual modes. */
	unsigned int count[DICE_NUM_MODES];
	/** Total number of PCM channels for the individual modes (accumulated
	 * PCM channels over all isochronous fw channels. */
	unsigned int channels[DICE_NUM_MODES];
	struct {
		/** Number of PCM channels for the corresponding isochronous fw channel
		 * for each DICE mode. */
		u8 pcm_channels[DICE_NUM_MODES];
		/** Number of MIDI ports for the corresponding isochronous fw channel
		 * for each DICE mode. */
		u8 midi_ports[DICE_NUM_MODES];
	} isoc_layout[DICE_MAX_FW_ISOC_CH];
};

struct dice_stream {
	struct fw_iso_resources resources;
	struct amdtp_stream stream;
};

struct dice_pcm {
	struct dice_stream playback;
	struct dice_stream capture;
};

struct dice {
	struct snd_card *card;
	struct fw_unit *unit;
	spinlock_t lock;
	struct mutex mutex;
	unsigned int vendor;
	unsigned int global_offset;
	unsigned int rx_offset;
	unsigned int rx_size;
	unsigned int tx_offset;
	unsigned int tx_size;
	unsigned int clock_caps;

	struct audio_layout rx;
	struct audio_layout tx;

	struct fw_address_handler fw_notification_handler;
	int owner_generation;
	int dev_lock_count; /* > 0 driver, < 0 userspace */
	bool dev_lock_changed;
	bool global_enabled;
	unsigned int current_mode;
	struct completion clock_accepted;
	wait_queue_head_t hwdep_wait;
	u32 notification_bits;

	struct dice_pcm pcm;
};

#define DICE_NUM_RATES	7
extern const unsigned int dice_rates[DICE_NUM_RATES];

unsigned int dice_rate_to_index(unsigned int rate);
unsigned int dice_rate_index_to_mode(unsigned int rate_index);

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

int dice_ctrl_change_rate(struct dice *dice, unsigned int clock_rate);
int dice_ctrl_get_clock(struct dice *dice, u32 *clock);

#endif
