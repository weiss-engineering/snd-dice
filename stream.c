
#include "stream.h"
#include "../lib.h"

static const char* stream_name(struct amdtp_stream* s)
{
	return s->direction == AMDTP_IN_STREAM ? "capture" : "playback";
}

static int
dice_get_stream_roles(struct dice *dice,
                      enum amdtp_stream_sync_mode *sync_mode,
                      struct dice_stream **master, struct dice_stream **slave)
{
	int err;
	u32 global_clock_sel;
#if 1
	/* Fetching from the global settings isn't safe as the global clock
	 * select register notification ("interface changed") is not synchronized
	 * with the "clock accepted" notification. */
	err = dice_ctrl_get_global_clock_select(dice, &global_clock_sel);
	if (err < 0) {
		return err;
	}
#else
	err = 0;
	global_clock_sel = dice->global_settings.clock_select;
#endif
	if (dice_driver_is_clock_master(global_clock_sel)) {
		dev_notice(&dice->unit->device, "AMDTP_STREAM_SYNC_MODE_MASTER\n");
		*master = &dice->playback;
		*slave = &dice->capture;
		*sync_mode = AMDTP_STREAM_SYNC_MODE_MASTER;
	} else {
		dev_notice(&dice->unit->device, "AMDTP_STREAM_SYNC_MODE_SLAVE\n");
		*master = &dice->capture;
		*slave = &dice->playback;
		*sync_mode = AMDTP_STREAM_SYNC_MODE_SLAVE;
	}
	return err;
}

/** Once streams are launched use this function to figure out the stream
 * roles as the global clock register is volatile. Shutting down the streams
 * based on the register can cause a wrong shutdown sequence which again can
 * result in resource leaks.
 */
/*static */void
dice_get_stream_roles_from_streams(struct dice *dice,
                      enum amdtp_stream_sync_mode *sync_mode,
                      struct dice_stream **master,
                      struct dice_stream **slave)
{

	if (dice->playback.stream.sync_mode == AMDTP_STREAM_SYNC_MODE_MASTER) {
		*master = &dice->playback;
		*slave = &dice->capture;
		*sync_mode = AMDTP_STREAM_SYNC_MODE_MASTER;
	} else {
		*master = &dice->capture;
		*slave = &dice->playback;
		*sync_mode = AMDTP_STREAM_SYNC_MODE_SLAVE;
	}
}

int
dice_stream_update_config(struct dice *dice, struct dice_stream *stream)
{
		__be32 values[2];
		int err;
		unsigned int i;
		const char* rtx;
		bool is_rx = stream->stream.direction == AMDTP_OUT_STREAM;

		struct dice_stream_config *c = &stream->config;

		c->valid = false;

		if (is_rx) {
			rtx = "r";
			err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
						 dice_rx_address(dice, 0, RX_NUMBER),
						 values, 4, 0);
		} else {
			rtx = "t";
			err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
						 dice_tx_address(dice, 0, TX_NUMBER),
						 values, 4, 0);
		}
		if (err < 0)
			return err;
		c->num_isoc_ch = be32_to_cpu(values[0]);
		if (c->num_isoc_ch > DICE_MAX_FW_ISOC_CH) {
			dev_err(&dice->unit->device, "#%sx = %u: too large\n",
			        rtx, c->num_isoc_ch);
			return -ENXIO;
		}
		/* <
		 *  TODO: Remove when support of multiple TX streams is implemented */
		if (!is_rx && c->num_isoc_ch > 1) {
			dev_notice(&dice->unit->device,
			           "Number of isochronous capture streams is currently "
			           "limited to one. Using first channel of %i only.", c->num_isoc_ch);
			c->num_isoc_ch = 1;
		}
		/* > */

		c->num_pcm_ch = 0;
		c->num_midi_ch = 0;
		for (i = 0; i < c->num_isoc_ch; ++i) {
			if (is_rx) {
				err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
							 dice_rx_address(dice, i, RX_NUMBER_AUDIO),
							 values, 2 * 4, 0);
			} else {
				err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
										 dice_tx_address(dice, i, TX_NUMBER_AUDIO),
										 values, 2 * 4, 0);
			}
			if (err < 0)
				return err;
			c->isoc_layout[i].pcm_channels = be32_to_cpu(values[0]);
			c->isoc_layout[i].midi_ports   = be32_to_cpu(values[1]);
#if 0
			if (c->isoc_layout[i].pcm_channels[mode] > (mode < 2 ? 16 : 8) &&
			    (dice->vendor != OUI_MAUDIO || i > 0)) {
				dev_err(&dice->unit->device,
					"%sx%u(%u): #PCM = %u: too large\n",
					rtx, i, mode, layout->isoc_layout[i].pcm_channels[mode]);
				return -ENXIO;
			}
			if (c->isoc_layout[i].midi_ports[mode] > 8) {
				dev_err(&dice->unit->device,
					"%sx%u(%u): #MIDI = %u: too large\n",
					rtx, i, mode, c->isoc_layout[i].midi_ports[mode]);
				return -ENXIO;
			}
#endif

			c->num_pcm_ch += c->isoc_layout[i].pcm_channels;
			c->num_midi_ch += c->isoc_layout[i].midi_ports > 0 ? 1 : 0;
		}
#if 0
		if (dice->vendor == OUI_MAUDIO && c->num_isoc_ch > 1) {
			if (c->isoc_layout[0].pcm_channels[mode] <= (mode < 2 ? 16 : 8))
				c->isoc_layout[0].pcm_channels[mode] =
							c->num_pcm_ch[mode];
			c->num_isoc_ch[mode] = 1;
		}
#endif

		c->valid = true;

		return 0;
}

static void
dice_free_resources(struct dice *dice, struct dice_stream* stream)
{
	unsigned int i;
	__be32 channel;

	channel = cpu_to_be32((u32)-1);
	for (i = 0; i < stream->config.num_isoc_ch; ++i) {
		if (stream->stream.direction == AMDTP_OUT_STREAM) {
			snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
					   dice_rx_address(dice, i, RX_ISOCHRONOUS),
					   &channel, 4, 0);
		} else {
			snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
					   dice_tx_address(dice, i, TX_ISOCHRONOUS),
					   &channel, 4, 0);
		}
	}
	fw_iso_resources_free(&stream->resources);
}

static int
dice_allocate_resources(struct dice *dice, struct dice_stream* stream)
{
	unsigned int seq_start, i;
	__be32 values[2];
	int err;

	if (stream->resources.allocated)
		return 0;

	if (!stream->config.valid) {
		dev_err(&dice->unit->device, "Can not allocate stream resources when stream configuration is unknown/invalid.\n");
		return -ENXIO; /* Correct error? "No such device or address" - Copied from Clemens' stream config read routine...*/
	}

	err = fw_iso_resources_allocate(&stream->resources,
			amdtp_stream_get_max_payload(&stream->stream),
			fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		return err;

	values[0] = cpu_to_be32(stream->resources.channel);
	seq_start = 0;
	for (i = 0; i < stream->config.num_isoc_ch; ++i) {
		/* Note: Playback streams are merged into a single channel by setting
		 * the sequence start offsets. */
		values[1] = cpu_to_be32(seq_start);
		if (stream->stream.direction == AMDTP_OUT_STREAM) {
			err = snd_fw_transaction(dice->unit,
						 TCODE_WRITE_BLOCK_REQUEST,
						 dice_rx_address(dice, i, RX_ISOCHRONOUS),
						 values, 2 * 4, 0);
		} else {
			/* No sequence start - so how do we manage multiple iso
			 * streams?
			 */
			err = snd_fw_transaction(dice->unit,
						 TCODE_WRITE_BLOCK_REQUEST,
						 dice_tx_address(dice, i, TX_ISOCHRONOUS),
						 values, 1 * 4, 0);
		}
		if (err < 0) {
			dice_free_resources(dice, stream);
			return err;
		}
		seq_start += stream->config.isoc_layout[i].pcm_channels;
		if (stream->stream.dual_wire)
			seq_start += stream->config.isoc_layout[i].pcm_channels;
		seq_start += stream->config.isoc_layout[i].midi_ports > 0;
	}

	return 0;
}

int
dice_stream_init(struct dice* dice, enum cip_flags cip_flags)
{
	int err;

	dice->capture.pcm_substream = NULL;
	dice->playback.pcm_substream = NULL;

	err = fw_iso_resources_init(&dice->playback.resources, dice->unit);
	if (err < 0) {
		goto error;
	}
	dice->playback.resources.channels_mask = 0x00000000ffffffffuLL;

	err = fw_iso_resources_init(&dice->capture.resources, dice->unit);
	if (err < 0) {
		goto err_clean_pb_res;
	}
	dice->capture.resources.channels_mask = 0x00000000ffffffffuLL;


	err = amdtp_stream_init(&dice->playback.stream, dice->unit, AMDTP_OUT_STREAM, cip_flags);
	if (err < 0) {
		goto err_clean_cp_res;
	}
	err = amdtp_stream_init(&dice->capture.stream, dice->unit, AMDTP_IN_STREAM, cip_flags);
	if (err < 0) {
		goto err_clean_pb_stream;
	}

	return 0;

err_clean_pb_stream:
	amdtp_stream_destroy(&dice->playback.stream);
err_clean_cp_res:
	fw_iso_resources_destroy(&dice->capture.resources);
err_clean_pb_res:
	fw_iso_resources_destroy(&dice->playback.resources);
error:
	return err;
}

static inline struct dice_stream*
dice_stream_for_direction(struct dice* dice, enum dice_direction dir)
{
	return dir == DICE_PLAYBACK ? &dice->playback : &dice->capture;
}

static void
dice_stream_configure(struct dice *dice, struct dice_stream* dstream,
	                                     unsigned int sample_rate)
{
	unsigned int q, ch, m, x /* <- rx/tx */, i;

	amdtp_stream_set_parameters(&dstream->stream,
								sample_rate,
								dstream->config.num_pcm_ch, /* TODO: In case of capture receiving on a single iso ch can lead to problems... Perhaps we have to handle the streams separately. */
								dstream->config.num_midi_ch);
	/*
	 * When using multiple receivers with MIDI or dual-wire, the packets in
	 * a data block are not in the default order.
	 */
	q = 0;
	ch = 0;
	m = 0;
	if (!dstream->stream.dual_wire) {
		for (x = 0; x < dstream->config.num_isoc_ch; ++x) {
			for (i = 0; i < dstream->config.isoc_layout[x].pcm_channels; ++i)
				dstream->stream.pcm_quadlets[ch++] = q++;
			if (dstream->config.isoc_layout[x].midi_ports > 0)
				dstream->stream.midi_quadlets[m++] = q++;
		}
	} else {
		for (x = 0; x < dstream->config.num_isoc_ch; ++x) {
			for (i = 0; i < dstream->config.isoc_layout[x].pcm_channels; ++i) {
				dstream->stream.pcm_quadlets[ch++] = q;
				q += 2;
			}
			if (dstream->config.isoc_layout[x].midi_ports > 0)
				dstream->stream.midi_quadlets[m++] = q++;
		}
		q = 1;
		for (x = 0; x < dstream->config.num_isoc_ch; ++x) {
			for (i = 0; i < dstream->config.isoc_layout[x].pcm_channels; ++i) {
				dstream->stream.pcm_quadlets[ch++] = q;
				q += 2;
			}
			q += dstream->config.isoc_layout[x].midi_ports > 0;
		}
	}

}

static int
dice_stream_start_instance(struct dice* dice, struct dice_stream* stream, unsigned int sample_rate)
{
	int err;

	err = dice_allocate_resources(dice, stream);
	if (err < 0) {
		return err;
	}

	if (amdtp_stream_running(&stream->stream)) {
		return 0;
	}

	dev_notice(&dice->unit->device, "dice_stream_start_instance %s.\n", stream_name(&stream->stream));

	dice_stream_configure(dice, stream, sample_rate);
	err = amdtp_stream_start(&stream->stream,
	                         stream->resources.channel,
	                         fw_parent_device(dice->unit)->max_speed);
	if (err < 0) {
		return err;
	}

	return err;
}

static int
dice_stream_stop_instance(struct dice* dice, struct dice_stream* stream)
{
	amdtp_stream_stop(&stream->stream);

	if (stream->resources.allocated) {
		dice_free_resources(dice, stream);
	}
	return 0;
}

/* SYNC MASTER
 * capture stream (slave stream) can not run without playback stream
 *
 * SYNC SLAVE
 * playback stream (slave stream) can not run without capture stream
 */

int dice_stream_start (struct dice* dice,
                       enum dice_direction dir,
                       unsigned int sample_rate)
{
	int err;
	bool master_started = false, slave_started = false;
	enum amdtp_stream_sync_mode sync_mode;
	struct dice_stream *master, *slave, *stream;

	stream = dice_stream_for_direction (dice, dir);
	if (amdtp_stream_running(&stream->stream)) {
		goto out;
	}

	err = dice_get_stream_roles (dice, &sync_mode, &master, &slave);
	if (err < 0)
		goto error;

	/* Make sure master stream is running. */
	if (!amdtp_stream_running(&master->stream)) {
		amdtp_stream_set_sync_mode(sync_mode, &master->stream, &slave->stream);
		err = dice_stream_start_instance(dice, master, sample_rate);
		if (err < 0) {
			goto error;
		}
		master_started = true;
	}

	/* Start requested stream (if not master it isn't running yet) */
	if (stream != master) {
		/* We stop streaming for slave stream setup */
		dice_ctrl_enable_clear(dice);

		err = dice_stream_start_instance(dice, stream, sample_rate);
		if (err < 0) {
			goto error;
		}
		slave_started = true;
	}

	/* (Re-)enable dice streaming. */
	if (master_started | slave_started) {
		err = dice_ctrl_enable_set(dice);
		if (err < 0) {
			goto error;
		}
	}

	/* Now we wait for the first isochronous packet callbacks */
	if (master_started && !amdtp_stream_wait_run(&master->stream)) {
			dev_err(&dice->unit->device, "Master stream didn't start streaming.\n");
			err = -EIO;
			goto error;
	}
	if (slave_started && !amdtp_stream_wait_run(&slave->stream)) {
			dev_err(&dice->unit->device, "Slave stream didn't start streaming.\n");
			err = -EIO;
			goto error;
	}

out:
	return 0;

error:
	dice_ctrl_enable_clear(dice);

	if (master_started) {
		dice_stream_stop_instance (dice, master);
	}
	if (slave_started) {
		dice_stream_stop_instance (dice, slave);
	}
	return err;
}

int dice_stream_stop (struct dice* dice, struct dice_stream *stream)
{
	enum amdtp_stream_sync_mode sync_mode;
	struct dice_stream *master, *slave;
	int err;

	if (!amdtp_stream_running(&stream->stream)) {
		goto out;
	}

	dice_get_stream_roles_from_streams (dice, &sync_mode, &master, &slave);

	/* When reconfiguring streaming on the DICE we have to disable streaming
	 * in any case. */
	dice_ctrl_enable_clear(dice);

	if (stream == master) {
		dice_stream_stop_instance(dice, slave);
	}
	dice_stream_stop_instance(dice, stream);

	if (stream == slave) {
		/* Re-enable streaming when master should continue to run. */
		err = dice_ctrl_enable_set(dice);
		/* TODO: Handle error */
	}

out:
	return 0;

}

int dice_stream_stop_dir (struct dice* dice, enum dice_direction dir)
{
	return dice_stream_stop(dice, dice_stream_for_direction (dice, dir));
}

int dice_stream_stop_all (struct dice* dice)
{
	enum amdtp_stream_sync_mode sync_mode;
	struct dice_stream *master, *slave;

	dice_get_stream_roles_from_streams (dice, &sync_mode, &master, &slave);

	/* No streaming if master's not running */
	if (amdtp_stream_running(&master->stream)) {
		dice_ctrl_enable_clear(dice);
		dice_stream_stop_instance(dice, slave);
		dice_stream_stop_instance(dice, master);
	}

	return 0;
}

bool dice_stream_is_any_running(struct dice *dice)
{
	return amdtp_stream_running (&dice->capture.stream) ||
			amdtp_stream_running (&dice->capture.stream);
}

void dice_stream_pcm_abort(struct dice *dice)
{
	amdtp_stream_pcm_abort(&dice->playback.stream);
	amdtp_stream_pcm_abort(&dice->capture.stream);
}

void dice_stream_destroy(struct dice *dice)
{
	amdtp_stream_destroy(&dice->playback.stream);
	amdtp_stream_destroy(&dice->capture.stream);
}

void dice_stream_stop_on_bus_reset(struct dice* dice)
{
	enum amdtp_stream_sync_mode sync_mode;
	struct dice_stream *master, *slave;

	dice_get_stream_roles_from_streams (dice, &sync_mode, &master, &slave);

	amdtp_stream_stop(&slave->stream);
	amdtp_stream_stop(&master->stream);
}

void dice_stream_update_on_bus_reset(struct dice* dice)
{
	fw_iso_resources_update(&dice->playback.resources);
	fw_iso_resources_update(&dice->capture.resources);
}
