
#include "dice-pcm.h"

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "../lib.h"


static int dice_rate_constraint(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct dice *dice = rule->private;
	const struct snd_interval *channels =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *rate =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval allowed_rates = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i) {
		mode = dice_rate_index_to_mode(i);
		if ((dice->clock_caps & (1 << i)) &&
		    snd_interval_test(channels, dice->rx_channels[mode])) {
			allowed_rates.min = min(allowed_rates.min,
						dice_rates[i]);
			allowed_rates.max = max(allowed_rates.max,
						dice_rates[i]);
		}
	}

	return snd_interval_refine(rate, &allowed_rates);
}

static int dice_channels_constraint(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct dice *dice = rule->private;
	const struct snd_interval *rate =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval allowed_channels = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i)
		if ((dice->clock_caps & (1 << i)) &&
		    snd_interval_test(rate, dice_rates[i])) {
			mode = dice_rate_index_to_mode(i);
			allowed_channels.min = min(allowed_channels.min,
						   dice->rx_channels[mode]);
			allowed_channels.max = max(allowed_channels.max,
						   dice->rx_channels[mode]);
		}

	return snd_interval_refine(channels, &allowed_channels);
}

static int dice_pcm_open(struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_BLOCK_TRANSFER,
		.formats = AMDTP_PCM_FORMAT_BIT,
		.channels_min = UINT_MAX,
		.channels_max = 0,
		.buffer_bytes_max = 16 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
	};
	struct dice *dice = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int i;
	int err;

	err = dice_try_lock(dice);
	if (err < 0)
		goto error;

	runtime->hw = hardware;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i)
		if (dice->clock_caps & (1 << i))
			runtime->hw.rates |=
				snd_pcm_rate_to_rate_bit(dice_rates[i]);
	snd_pcm_limit_hw_rates(runtime);

	for (i = 0; i < DICE_NUM_MODES; ++i)
		if (dice->rx_channels[i]) {
			runtime->hw.channels_min = min(runtime->hw.channels_min,
						       dice->rx_channels[i]);
			runtime->hw.channels_max = max(runtime->hw.channels_max,
						       dice->rx_channels[i]);
		}

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  dice_rate_constraint, dice,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		goto err_lock;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  dice_channels_constraint, dice,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto err_lock;

	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 32);
	if (err < 0)
		goto err_lock;
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 32);
	if (err < 0)
		goto err_lock;

	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					   5000, UINT_MAX);
	if (err < 0)
		goto err_lock;

	err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (err < 0)
		goto err_lock;

	return 0;

err_lock:
	dice_unlock(dice);
error:
	return err;
}

static int dice_pcm_close(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	dice_unlock(dice);

	return 0;
}

static void dice_free_resources(struct dice *dice)
{
	unsigned int i;
	__be32 channel;

	channel = cpu_to_be32((u32)-1);
	for (i = 0; i < dice->rx_count[dice->current_mode]; ++i)
		snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
				   dice_rx_address(dice, i, RX_ISOCHRONOUS),
				   &channel, 4, 0);

	fw_iso_resources_free(&dice->rx_resources);
}

static int dice_allocate_resources(struct dice *dice)
{
	unsigned int seq_start, i;
	__be32 values[2];
	int err;

	if (dice->rx_resources.allocated)
		return 0;

	err = fw_iso_resources_allocate(&dice->rx_resources,
			amdtp_stream_get_max_payload(&dice->rx_stream),
			fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		return err;

	values[0] = cpu_to_be32(dice->rx_resources.channel);
	seq_start = 0;
	for (i = 0; i < dice->rx_count[dice->current_mode]; ++i) {
		values[1] = cpu_to_be32(seq_start);
		err = snd_fw_transaction(dice->unit,
					 TCODE_WRITE_BLOCK_REQUEST,
					 dice_rx_address(dice, i, RX_ISOCHRONOUS),
					 values, 2 * 4, 0);
		if (err < 0) {
			dice_free_resources(dice);
			return err;
		}
		seq_start += dice->rx[i].pcm_channels[dice->current_mode];
		if (dice->rx_stream.dual_wire)
			seq_start += dice->rx[i].pcm_channels[dice->current_mode];
		seq_start += dice->rx[i].midi_ports[dice->current_mode] > 0;
	}

	return 0;
}

static int dice_start_packet_streaming(struct dice *dice)
{
	int err;

	if (amdtp_stream_running(&dice->rx_stream))
		return 0;

	err = amdtp_stream_start(&dice->rx_stream,
				 dice->rx_resources.channel,
				 fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		return err;

	err = dice_ctrl_enable_set(dice);
	if (err < 0) {
		amdtp_stream_stop(&dice->rx_stream);
		return err;
	}

	return 0;
}

static int dice_start_streaming(struct dice *dice)
{
	int err;

	err = dice_allocate_resources(dice);
	if (err < 0)
		return err;

	err = dice_start_packet_streaming(dice);
	if (err < 0) {
		dice_free_resources(dice);
		return err;
	}

	return 0;
}

void dice_stop_packet_streaming(struct dice *dice)
{
	if (amdtp_stream_running(&dice->rx_stream)) {
		dice_ctrl_enable_clear(dice);
		amdtp_stream_stop(&dice->rx_stream);
	}
}

void dice_stop_streaming(struct dice *dice)
{
	dice_stop_packet_streaming(dice);

	if (dice->rx_resources.allocated)
		dice_free_resources(dice);
}

void dice_abort_streaming(struct dice *dice)
{
	amdtp_stream_pcm_abort(&dice->rx_stream);
}

void dice_destroy_streaming(struct dice *dice)
{
	amdtp_stream_destroy(&dice->rx_stream);
}

static int dice_pcm_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *hw_params)
{
	struct dice *dice = substream->private_data;
	unsigned int rate_index, mode, midi_data_channels;
	unsigned int q, ch, m, rx, i;
	int err;

	mutex_lock(&dice->mutex);
	dice_stop_streaming(dice);
	mutex_unlock(&dice->mutex);

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	rate_index = dice_rate_to_index(params_rate(hw_params));
	err = dice_ctrl_change_rate(dice, rate_index << CLOCK_RATE_SHIFT);
	if (err < 0)
		return err;

	mode = dice_rate_index_to_mode(rate_index);
	dice->current_mode = mode;

	midi_data_channels = 0;
	for (rx = 0; rx < dice->rx_count[mode]; ++rx)
		midi_data_channels += dice->rx[rx].midi_ports[mode] > 0;
	amdtp_stream_set_parameters(&dice->rx_stream,
				    params_rate(hw_params),
				    params_channels(hw_params),
				    midi_data_channels);

	/*
	 * When using multiple receivers with MIDI or dual-wire, the packets in
	 * a data block are not in the default order.
	 */
	q = 0;
	ch = 0;
	m = 0;
	if (!dice->rx_stream.dual_wire) {
		for (rx = 0; rx < dice->rx_count[mode]; ++rx) {
			for (i = 0; i < dice->rx[rx].pcm_channels[mode]; ++i)
				dice->rx_stream.pcm_quadlets[ch++] = q++;
			if (dice->rx[rx].midi_ports[mode] > 0)
				dice->rx_stream.midi_quadlets[m++] = q++;
		}
	} else {
		for (rx = 0; rx < dice->rx_count[mode]; ++rx) {
			for (i = 0; i < dice->rx[rx].pcm_channels[mode]; ++i) {
				dice->rx_stream.pcm_quadlets[ch++] = q;
				q += 2;
			}
			if (dice->rx[rx].midi_ports[mode] > 0)
				dice->rx_stream.midi_quadlets[m++] = q++;
		}
		q = 1;
		for (rx = 0; rx < dice->rx_count[mode]; ++rx) {
			for (i = 0; i < dice->rx[rx].pcm_channels[mode]; ++i) {
				dice->rx_stream.pcm_quadlets[ch++] = q;
				q += 2;
			}
			q += dice->rx[rx].midi_ports[mode] > 0;
		}
	}

	return 0;
}

static int dice_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);
	dice_stop_streaming(dice);
	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int dice_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;
	int err;

	mutex_lock(&dice->mutex);

	if (amdtp_streaming_error(&dice->rx_stream))
		dice_stop_packet_streaming(dice);

	err = dice_start_streaming(dice);
	if (err < 0) {
		mutex_unlock(&dice->mutex);
		return err;
	}

	mutex_unlock(&dice->mutex);

	amdtp_stream_pcm_prepare(&dice->rx_stream);

	return 0;
}

static int dice_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct dice *dice = substream->private_data;
	struct snd_pcm_substream *pcm;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pcm = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pcm = NULL;
		break;
	default:
		return -EINVAL;
	}
	amdtp_stream_pcm_trigger(&dice->rx_stream, pcm);

	return 0;
}

static snd_pcm_uframes_t dice_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	return amdtp_stream_pcm_pointer(&dice->rx_stream);
}

static struct snd_pcm_ops ops = {
	.open      = dice_pcm_open,
	.close     = dice_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = dice_pcm_hw_params,
	.hw_free   = dice_pcm_hw_free,
	.prepare   = dice_pcm_prepare,
	.trigger   = dice_pcm_trigger,
	.pointer   = dice_pcm_pointer,
	.page      = snd_pcm_lib_get_vmalloc_page,
	.mmap      = snd_pcm_lib_mmap_vmalloc,
};

int dice_pcm_create(struct dice *dice)
{

	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(dice->card, "DICE", 0, 1, 0, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = dice;
	strcpy(pcm->name, dice->card->shortname);
	pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->ops = &ops;

	return 0;
}
