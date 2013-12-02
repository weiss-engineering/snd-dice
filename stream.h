/*
 * dice-stream.h
 *
 *  Created on: Aug 10, 2013
 *      Author: uli
 */

#ifndef DICE_STREAM_H_
#define DICE_STREAM_H_

#include "dice.h"

int dice_stream_init(struct dice* dice, enum cip_flags cip_flags);
void dice_stream_destroy(struct dice *dice);

int dice_stream_start (struct dice* dice, enum dice_direction dir, unsigned int sample_rate);
int dice_stream_stop (struct dice* dice, struct dice_stream *stream);
int dice_stream_stop_dir (struct dice* dice, enum dice_direction dir);
int dice_stream_stop_all (struct dice* dice);

/** Update the device stream configuration.
 *
 * Note that the streams must be stopped when updating the config. If not, the
 * previous configuration is overridden and the stream resources can not be
 * deallocated correctly.
 */
int dice_stream_update_config(struct dice *dice, struct dice_stream *stream);

/** Check if any streaming (playback or capture) is running). */
bool dice_stream_is_any_running(struct dice *dice);

/** Disables amdtp <-> pcmsubstream transfers and notifies user space application on pcm status (XRUN). */
void dice_stream_pcm_abort(struct dice *dice);

/** Stops the streams but does not release the firewire resources. */
void dice_stream_stop_on_bus_reset(struct dice* dice);
/** Updates the firewire resources. */
void dice_stream_update_on_bus_reset(struct dice* dice);

void dice_get_stream_roles_from_streams(struct dice *dice,
                      enum amdtp_stream_sync_mode *sync_mode,
                      struct dice_stream **master,
                      struct dice_stream **slave);
#endif /* DICE_STREAM_H_ */
