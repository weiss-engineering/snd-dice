
#include "pcm.h"
#include "stream.h"

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "../lib.h"

#if 1
#define dbg_log(MSG, ...)	{ \
	struct dice *_d = (struct dice *)substream->private_data; \
	dev_notice(&_d->unit->device, MSG, ##__VA_ARGS__); \
}
#define dbg_log_func()		{ \
	struct dice *_d = (struct dice *)substream->private_data; \
	dev_notice(&_d->unit->device, "%s %s\n", __FUNCTION__, ss_name(substream)); \
}
#else
#define dbg_log(MSG, ...)	do { } while (0)
#define dbg_log_func()		do { } while (0)
#endif

static inline bool substream_is_playback(struct snd_pcm_substream *substream)
{
	return substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
}

static const char* ss_name(struct snd_pcm_substream *ss)
{
	return substream_is_playback(ss) ? "playback" : "capture";
}

static inline enum dice_direction
dice_dir_from_substream (struct snd_pcm_substream *substream)
{
	return substream_is_playback(substream) ? DICE_PLAYBACK : DICE_CAPTURE;
}

static inline struct dice_stream*
dice_stream_from_pcm_substream(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	if (substream_is_playback(substream)) {
		return &dice->playback;
	} else {
		return &dice->capture;
	}
}

static inline struct amdtp_stream*
dice_amdtp_from_pcm_substream(struct snd_pcm_substream *substream)
{
	return &dice_stream_from_pcm_substream(substream)->stream;
}

#if 0	/* TODO !!!! */

static int dice_rate_constraint(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct dice *dice = rule->private;
	const struct snd_interval *channels =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *rate_index =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval allowed_rates = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i) {
		mode = dice_rate_index_to_mode(i);
		if ((dice->clock_caps & (1 << i)) &&
		    snd_interval_test(channels, dice->rx.channels[mode])) {
			allowed_rates.min = min(allowed_rates.min,
						dice_rates[i]);
			allowed_rates.max = max(allowed_rates.max,
						dice_rates[i]);
		}
	}

	return snd_interval_refine(rate_index, &allowed_rates);
}

static int dice_channels_constraint(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct dice *dice = rule->private;
	const struct snd_interval *rate_index =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval allowed_channels = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(dice_rates); ++i)
		if ((dice->clock_caps & (1 << i)) &&
		    snd_interval_test(rate_index, dice_rates[i])) {
			mode = dice_rate_index_to_mode(i);
			allowed_channels.min = min(allowed_channels.min,
						   dice->rx.channels[mode]);
			allowed_channels.max = max(allowed_channels.max,
						   dice->rx.channels[mode]);
		}

	return snd_interval_refine(channels, &allowed_channels);
}
#endif


static int dice_pcm_open(struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_BLOCK_TRANSFER,
		.formats = AMDTP_PCM_FORMAT_BIT,
		.channels_min = 0,
		.channels_max = UINT_MAX,
		.buffer_bytes_max = 16 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
		.rates = 0,
	};
	struct dice *dice = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dice_stream* stream;
	unsigned int i, clock_select;
	struct dice_ext_sync_info sync_info;
	int err;

	dbg_log_func();

	stream = dice_stream_from_pcm_substream(substream);

	err = dice_try_lock(dice);
	if (err < 0) {
		goto error;
	}

	runtime->hw = hardware;

	if (dice_stream_is_any_running(dice)) {
		/* Already started streaming (other substream launched it). Therefore
		 * the sample rate and the stream layouts are known and fixed.
		 */
		mutex_lock(&dice->mutex);
		sync_info = dice->extended_sync_info;
		mutex_unlock(&dice->mutex);

		dbg_log("Streaming already active. Limiting sample rate to %i, channels to %i.",
		        dice_rates[sync_info.rate_index],
		        stream->config.num_pcm_ch);

		runtime->hw.rates |=
			snd_pcm_rate_to_rate_bit(dice_rates[sync_info.rate_index]);

		runtime->hw.channels_min = stream->config.num_pcm_ch;
		runtime->hw.channels_max = stream->config.num_pcm_ch;
	} else {
		mutex_lock(&dice->mutex);
		clock_select = dice->global_settings.clock_select;
		mutex_unlock(&dice->mutex);


		for (i = 0; i < ARRAY_SIZE(dice_rates); ++i)
			if (dice->global_settings.clock_caps & (1 << i))
				runtime->hw.rates |=
					snd_pcm_rate_to_rate_bit(dice_rates[i]);

		/* Channel count is not known yet as it can change when the sample
		 * rate is set. Or we perform a "Ladisch-Scan" here... */
	}
	err = snd_pcm_limit_hw_rates(runtime);
	if (err < 0)
		goto err_lock;

#if 0
	for (i = 0; i < DICE_NUM_MODES; ++i)
		if (layout->channels[i]) {
			runtime->hw.channels_min = min(runtime->hw.channels_min,
			                               layout->channels[i]);
			runtime->hw.channels_max = max(runtime->hw.channels_max,
			                               layout->channels[i]);
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
#endif

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
	struct dice_stream* stream = dice_stream_from_pcm_substream(substream);

	dbg_log_func();

	mutex_lock(&dice->mutex);
	stream->pcm_substream = NULL;

	/* TODO: Check if any of the streams is still running when all pcms are NULL! */

	mutex_unlock(&dice->mutex);

	dice_unlock(dice);

	return 0;
}

static int dice_pcm_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *hw_params)
{
	struct dice *dice = substream->private_data;
	unsigned int rate, req_rate_index, device_rate_index, channels;
	u32 clock_select;
	int err;
	struct dice_stream* stream;

	dbg_log_func();

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	stream = dice_stream_from_pcm_substream(substream);
	if (amdtp_stream_running(&stream->stream)) {
		dbg_log(".hw_params called on running/already configured stream.\n");
		return 0;
	}

	rate = params_rate(hw_params);
	req_rate_index = dice_rate_to_index(rate);

	err = dice_ctrl_get_global_clock_select(dice, &clock_select);
	if (err < 0) {
		return err;
	}

	device_rate_index = (clock_select & CLOCK_RATE_MASK) >> CLOCK_RATE_SHIFT;
	if (device_rate_index != req_rate_index) {
		if (dice_stream_is_any_running(dice)) {
			dev_err(&dice->unit->device, "Sample rate can not be changed when "
					"another stream with different rate is already running. Current rate: "
					"%i, requested rate: %i\n", device_rate_index, req_rate_index);
			return -EINVAL;
		}
		err = dice_ctrl_change_rate(dice, req_rate_index << CLOCK_RATE_SHIFT, false);
		if (err < 0)
			return err;
	}

	/* At this point the stream configuration should be updated and the
	 * number of channels should be known. */

	channels = params_channels(hw_params);
	if (stream->config.num_pcm_ch != channels) {
		dev_err(&dice->unit->device, "Number of PCM channels (%i) not matching stream channels (%i)",
		        channels, stream->config.num_pcm_ch);
		return -EINVAL;
	}

	/* TODO: For capture we currently only support the channels of the first
	 * stream.
	 */

	mutex_lock(&dice->mutex);
	err = dice_stream_start(dice, dice_dir_from_substream(substream), rate);
	if (err < 0) {
		mutex_unlock(&dice->mutex);
		return err;
	}
	stream->pcm_substream = substream;
	mutex_unlock(&dice->mutex);

	return 0;
}

static int
dice_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);
#if 1
#	if 0
	/* TODO: We should stop just the stream belonging to this PCM substream
	 * and the master stream if the PCM substream associated with it is not
	 * open. */
	dice_stream_stop_all(dice);
#	else
	/*
	 * if stream master:
	 * 		if slave pcm open:
	 * 			do nothing
	 * 		else:
	 * 			stop master
	 * else (if stream slave):
	 * 		stop slave stream
	 */
	{
		/* TODO: Verify this shutdown sequence! */
		enum amdtp_stream_sync_mode sync_mode;
		struct dice_stream *master;
		struct dice_stream *slave;
		struct dice_stream *stream = dice_stream_from_pcm_substream(substream);
		dice_get_stream_roles_from_streams(dice,
							  &sync_mode,
							  &master,
							  &slave);
		if (sync_mode == AMDTP_STREAM_SYNC_MODE_MASTER) {
			dice_stream_stop(dice, stream);
		} else {
			if (stream == master) {
				if (!slave->pcm_substream ) {
					/* slave needs master to run: stop only if slave is not running. */
					dice_stream_stop(dice, stream);
				}
			} else {
				if ( master->pcm_substream /* master pcm is open */) {
					/* Master PCM is open - we just stop the slave. */
					dice_stream_stop(dice, stream);
				} else {
					dice_stream_stop_all(dice);
				}
			}
		}
	}
#	endif
#else
	dice_stream_stop(dice, dice_dir_from_substream(substream));
#endif
	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int
dice_pcm_prepare(struct snd_pcm_substream *substream)
{
	int err = 0;
	struct amdtp_stream *amdtp = dice_amdtp_from_pcm_substream(substream);

	dbg_log_func();

	amdtp_stream_pcm_prepare(amdtp);

	return err;
}

static int
dice_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_substream *pcm;

	dbg_log_func();

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
	amdtp_stream_pcm_trigger(dice_amdtp_from_pcm_substream(substream), pcm);

	return 0;
}

static snd_pcm_uframes_t
dice_pcm_pointer(struct snd_pcm_substream *substream)
{
//	dbg_log_func();
	return amdtp_stream_pcm_pointer(dice_amdtp_from_pcm_substream(substream));
}

static struct snd_pcm_ops ops_playback = {
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

static struct snd_pcm_ops ops_capture = {
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

	err = snd_pcm_new(dice->card, "DICE", 0, 1, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = dice;
	strcpy(pcm->name, dice->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &ops_playback);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &ops_capture);

	dice->pcm = pcm;

	return 0;
}

static struct snd_pcm_substream * snd_pcm_get_substream(struct snd_pcm *pcm, int direction)
{
	struct snd_pcm_str *stream;
	struct snd_pcm_substream *substream;

	if (pcm != NULL) {
		stream = &pcm->streams[direction];
		substream = stream->substream;
		if (substream != NULL)
			return substream;
	}
	return NULL;
}

static void dice_pcm_reset_substream(struct dice *dice, int direction)
{
	struct snd_pcm_substream *ss = snd_pcm_get_substream(dice->pcm, direction);
	if (ss) {
		snd_pcm_stream_lock_irq(ss);
		if (ss->runtime)
			snd_pcm_stop(ss, SNDRV_PCM_STATE_OPEN);
		snd_pcm_stream_unlock_irq(ss);
	}
}

void dice_pcm_reset_substreams(struct dice *dice)
{
	dice_pcm_reset_substream(dice, SNDRV_PCM_STREAM_PLAYBACK);
	dice_pcm_reset_substream(dice, SNDRV_PCM_STREAM_CAPTURE);

}
