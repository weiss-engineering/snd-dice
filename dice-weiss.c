// SPDX-License-Identifier: GPL-2.0
/*
 * dice-weiss.c - a part of driver for DICE based devices
 *
 * Copyright (c) 2023 Rolf Anderegg and Michele Perrone
 */

#include "dice.h"

struct dice_weiss_spec {
	unsigned int tx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
	unsigned int rx_pcm_chs[MAX_STREAMS][SND_DICE_RATE_MODE_COUNT];
	bool has_midi;
};

static const struct dice_weiss_spec dac202 = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.has_midi   = false
};

static const struct dice_weiss_spec man301 = {
	.tx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.rx_pcm_chs = {{2, 2, 2}, {0, 0, 0} },
	.has_midi   = false
};


int snd_dice_detect_weiss_formats(struct snd_dice *dice)
{
	static const struct {
		u32 model_id;
		const struct dice_weiss_spec *spec;
	} *entry, entries[] = {
		{0x000007, &dac202},
		{0x00000b, &man301}
	};
	struct fw_csr_iterator it;
	int key, val, model_id;
	int i;

	model_id = 0;
	fw_csr_iterator_init(&it, dice->unit->directory);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		if (key == CSR_MODEL) {
			model_id = val;
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(entries); ++i) {
		entry = entries + i;
		if (entry->model_id == model_id)
			break;
	}
	if (i == ARRAY_SIZE(entries))
		return -ENODEV;

	memcpy(dice->tx_pcm_chs, entry->spec->tx_pcm_chs,
	       MAX_STREAMS * SND_DICE_RATE_MODE_COUNT * sizeof(unsigned int));
	memcpy(dice->rx_pcm_chs, entry->spec->rx_pcm_chs,
	       MAX_STREAMS * SND_DICE_RATE_MODE_COUNT * sizeof(unsigned int));

	if (entry->spec->has_midi) {
		dice->tx_midi_ports[0] = 1;
		dice->rx_midi_ports[0] = 1;
	}

	return 0;
}
