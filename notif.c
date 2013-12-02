
#include "notif.h"

#include <linux/firewire-constants.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "stream.h"
#include "pcm.h"

#if 1
#define dbg_log(MSG, ...)	dev_notice(&dice->unit->device, MSG, ##__VA_ARGS__)
#else
#define dbg_log(MSG, ...)	do { } while (0)
#endif

#define DICE_NOTIF_OTHER_MASK                        \
	(~(NOTIFY_RX_CFG_CHG   | NOTIFY_TX_CFG_CHG |     \
	   NOTIFY_DUP_ISOC_BIT | NOTIFY_BW_ERR_BIT |     \
	   NOTIFY_LOCK_CHG     | NOTIFY_CLOCK_ACCEPTED | \
	   NOTIFY_INTERFACE_CHG))

typedef struct {
  struct work_struct dice_notif_work;
  struct dice* dice;
  u32 notif_bits;
} dice_notif_work_t;

static void dice_process_rx_tx_reconfig(struct dice* dice, bool rx, bool tx)
{
	int err;
	if (dice_stream_is_any_running(dice)) {

		dev_notice(&dice->unit->device,
				   "DICE reconfigured RX/TX streams. Stopping ALSA PCM and AMDTP streams.\n");

		/* We must revert to SNDRV_PCM_STATE_OPEN as we really don't know if
		 * the new stream configuration is compatible with the current pcm
		 * substream. From the documentation:
		 *
		 *     "SND_PCM_STATE_OPEN The PCM device is in the open state. After
		 *      the snd_pcm_open() open call, the device is in this state.
		 *      Also, when snd_pcm_hw_params() call fails, then this state is
		 *      entered to force application calling snd_pcm_hw_params()
		 *      function to set right communication parameters."
		 */
		dice_pcm_reset_substreams(dice);

		mutex_lock(&dice->mutex);
		dice_stream_stop_all(dice);
		mutex_unlock(&dice->mutex);
	}
	/* Note that the streams must be stopped when updating the configuration.
	 * If not, the previous configuration is overridden and the stream
	 * resources can not be deallocated correctly.
	 */
	if (rx) {
		mutex_lock(&dice->mutex);
		err = dice_stream_update_config(dice, &dice->playback);
		mutex_unlock(&dice->mutex);
		if (err < 0) {
			dev_err(&dice->unit->device,
					   "Failed to update isochronous RX stream configuration.\n");
		}
	}
	if (tx) {
		mutex_lock(&dice->mutex);
		err = dice_stream_update_config(dice, &dice->capture);
		mutex_unlock(&dice->mutex);
		if (err < 0) {
			dev_err(&dice->unit->device,
					   "Failed to update isochronous TX stream configuration.\n");
		}
	}

	/* Restarting streams wouldn't make much sense because if the stream layout
	 * changes usually the number of channels, sample rate, midi channels etc.
	 * change as well, rendering this task very complicated.
	 */

	return;
}

static void dice_process_lock_change(struct dice* dice)
{
	struct dice_ext_sync_info info;
	int err;
	bool lock_lost, lock_regained;

	err = dice_ctrl_get_ext_sync_info(dice, &info);
	if (err < 0) {
		dev_err(&dice->unit->device, "Failed to get extended sync info during lock change.\n");
		return;
	}

	lock_lost = !info.locked && dice->extended_sync_info.locked;
	lock_regained = info.locked && !dice->extended_sync_info.locked;

	if (info.clock_source != dice->extended_sync_info.clock_source)
		dbg_log("Extended sync clock source changed: 0x%x (was 0x%x)\n",
		        info.clock_source, dice->extended_sync_info.clock_source);

	if (info.locked != dice->extended_sync_info.locked)
		dbg_log("Extended sync lock changed: 0x%x (was 0x%x)\n",
		        info.locked, dice->extended_sync_info.locked);

	if (info.rate_index != dice->extended_sync_info.rate_index)
		dbg_log("Extended sync rate changed: 0x%x (was 0x%x)\n",
		        info.rate_index, dice->extended_sync_info.rate_index);

	if (info.adat_user_data != dice->extended_sync_info.adat_user_data)
		dbg_log("Extended sync ADAT user data changed: 0x%x (was 0x%x)\n",
		        info.adat_user_data, dice->extended_sync_info.adat_user_data);

	mutex_lock(&dice->mutex);
	dice->extended_sync_info = info;
	mutex_unlock(&dice->mutex);

	if (lock_lost) {
		dev_notice(&dice->unit->device, "Audio clock unlocked.\n");
	}
	if (lock_regained) {
		dev_notice(&dice->unit->device, "Audio clock locked.\n");
	}
}

static void dice_process_interface_change(struct dice* dice)
{
	struct dice_global_settings settings;
	int err;

	err = dice_ctrl_get_global_settings(dice, &settings);
	if (err < 0) {
		dev_err(&dice->unit->device,
				   "Failed to get global settings during interface change.\n");
		return;
	}

	if (settings.clock_select != dice->global_settings.clock_select)
		dbg_log("Global clock select changed: 0x%x (was 0x%x)\n",
		           settings.clock_select, dice->global_settings.clock_select);

    if (settings.enable != dice->global_settings.enable)
		dbg_log("Global enable changed: 0x%x (was 0x%x)\n",
		           settings.enable, dice->global_settings.enable);

    if (settings.status != dice->global_settings.status)
		dbg_log("Global status changed: 0x%x (was 0x%x)\n",
		           settings.status, dice->global_settings.status);

    if (settings.extended_status != dice->global_settings.extended_status)
    	dbg_log("Global extended status changed: 0x%x (was 0x%x)\n",
    	           settings.extended_status, dice->global_settings.extended_status);

    if (settings.measured_sample_rate != dice->global_settings.measured_sample_rate)
    	dbg_log("Measured sample rate changed: %i (was %i)\n",
    	settings.measured_sample_rate, dice->global_settings.measured_sample_rate);

    if (settings.clock_caps != dice->global_settings.clock_caps)
    	dbg_log("Clock capabilities changed: 0x%x (was 0x%x)\n",
    	           settings.clock_caps, dice->global_settings.clock_caps);

	mutex_lock(&dice->mutex);
	dice->global_settings = settings;
	mutex_unlock(&dice->mutex);
}

/** Deferred notification interrupt processing. Schedulable. */
static void dice_notif_work(struct work_struct *work)
{
	dice_notif_work_t *notif_work = (dice_notif_work_t *)work;
	struct dice* dice = notif_work->dice;
	bool rx = notif_work->notif_bits & NOTIFY_RX_CFG_CHG;
	bool tx = notif_work->notif_bits & NOTIFY_TX_CFG_CHG;
	static int count = 0;

	/* We process the RX/TX reconfiguration notifications first such that this
	 * data is ready when we signal the "clock accepted" below.
	 */
	if (rx || tx){
		if (rx)
			dbg_log("NOTIFY_RX_CFG_CHG[%i]\n", count);
		if (tx)
			dbg_log("NOTIFY_TX_CFG_CHG[%i]\n", count);
		dice_process_rx_tx_reconfig(dice, rx, tx);
	}

	if (notif_work->notif_bits & NOTIFY_LOCK_CHG) {
		dbg_log("NOTIFY_LOCK_CHG[%i]\n", count);
		dice_process_lock_change(dice);
	}

	if (notif_work->notif_bits & NOTIFY_DUP_ISOC_BIT)
		dbg_log("NOTIFY_DUP_ISOC_BIT[%i]\n", count);

	if (notif_work->notif_bits & NOTIFY_BW_ERR_BIT)
		dbg_log("NOTIFY_BW_ERR_BIT[%i]\n", count);

	if (notif_work->notif_bits & NOTIFY_INTERFACE_CHG) {
		dbg_log("NOTIFY_INTERFACE_CHANGE[%i]\n", count);
		dice_process_interface_change(dice);
	}

	/* The clock accepted and tx/rx reconfiguration notifications are sent
	 * together. Therefore we first process the reconfiguration notification
	 * and when done signal the clock accepted. This way the stream setup is
	 * not destroyed by the deferred reconfiguration notifications.
	 */
	if (notif_work->notif_bits & NOTIFY_CLOCK_ACCEPTED) {
		dbg_log("NOTIFY_CLOCK_ACCEPTED[%i]\n", count);
		complete(&dice->clock_accepted);
	}

	if (notif_work->notif_bits & DICE_NOTIF_OTHER_MASK)
		/* Insert your vendor/product specific notification handler here:
		 *
		 * if (dice->vendor == OUI_WEISS) {
		 *     dice_process_weiss_notification(dice, notif_work->notif_bits);
		 * }
		 */
		dev_notice(&dice->unit->device,
		           "NOTIFY_OTHER[%i] - unknown/vendor/model notification(s): %x\n",
		           count, notif_work->notif_bits & DICE_NOTIF_OTHER_MASK);

	count++;

	kfree(work);
}


/* This is atomic. */
static void
dice_schedule_notif_work(struct dice *dice, u32 notif_bits, work_func_t work_func)
{
	dice_notif_work_t* notif_work;
	int err;

	/* We must use atomic as we're scheduling work from atomic callback */
	notif_work = (dice_notif_work_t *) kmalloc(sizeof(dice_notif_work_t),
	                                           GFP_ATOMIC);
	if (notif_work) {
		INIT_WORK((struct work_struct *)notif_work, work_func);
		notif_work->dice = dice;
		notif_work->notif_bits = notif_bits;

		err = queue_work(dice->notif_queue, (struct work_struct *) notif_work);
		if (err < 0) {
			dev_err(&dice->unit->device, "Failed to schedule work for notification.\n");
			kfree(notif_work);
		}
	} else {
		dev_err(&dice->unit->device, "Out of memory when allocating work struct for notification.\n");

		/* Good out of memory strategy comes here! */
	}
}

void dice_fw_notification_callback(struct fw_card *card, struct fw_request *request,
			      int tcode, int destination, int source,
			      int generation, unsigned long long offset,
			      void *data, size_t length, void *callback_data)
{
	struct dice *dice = callback_data;
	u32 bits;
	unsigned long flags;

	if (tcode != TCODE_WRITE_QUADLET_REQUEST) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);
		return;
	}
	if ((offset & 3) != 0) {
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);
		return;
	}

	bits = be32_to_cpup(data);

	spin_lock_irqsave(&dice->lock, flags);
	dice->notification_bits |= bits;
	spin_unlock_irqrestore(&dice->lock, flags);

	fw_send_response(card, request, RCODE_COMPLETE);

	dice_schedule_notif_work(dice, bits, dice_notif_work);

	wake_up(&dice->hwdep_wait);
}
