
#include "dice-hwdep.h"
#include "dice-firmware.h"

#include <sound/core.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>
#include <linux/firmware.h>

static long dice_hwdep_read(struct snd_hwdep *hwdep, char __user *buf,
			    long count, loff_t *offset)
{
	struct dice *dice = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&dice->lock);

	while (!dice->dev_lock_changed && dice->notification_bits == 0) {
		prepare_to_wait(&dice->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&dice->lock);
		schedule();
		finish_wait(&dice->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&dice->lock);
	}

	memset(&event, 0, sizeof(event));
	if (dice->dev_lock_changed) {
		event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
		event.lock_status.status = dice->dev_lock_count > 0;
		dice->dev_lock_changed = false;

		count = min(count, (long)sizeof(event.lock_status));
	} else {
		event.dice_notification.type = SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION;
		event.dice_notification.notification = dice->notification_bits;
		dice->notification_bits = 0;

		count = min(count, (long)sizeof(event.dice_notification));
	}

	spin_unlock_irq(&dice->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static unsigned int dice_hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
				    poll_table *wait)
{
	struct dice *dice = hwdep->private_data;
	unsigned int events;

	poll_wait(file, &dice->hwdep_wait, wait);

	spin_lock_irq(&dice->lock);
	if (dice->dev_lock_changed || dice->notification_bits != 0)
		events = POLLIN | POLLRDNORM;
	else
		events = 0;
	spin_unlock_irq(&dice->lock);

	return events;
}

static int dice_hwdep_get_info(struct dice *dice, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(dice->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_DICE;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strlcpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int dice_hwdep_lock(struct dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count == 0) {
		dice->dev_lock_count = -1;
		err = 0;
	} else {
		err = -EBUSY;
	}

	spin_unlock_irq(&dice->lock);

	return err;
}

static int dice_hwdep_unlock(struct dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count == -1) {
		dice->dev_lock_count = 0;
		err = 0;
	} else {
		err = -EBADFD;
	}

	spin_unlock_irq(&dice->lock);

	return err;
}

static int dice_hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct dice *dice = hwdep->private_data;

	spin_lock_irq(&dice->lock);
	if (dice->dev_lock_count == -1)
		dice->dev_lock_count = 0;
	spin_unlock_irq(&dice->lock);

	return 0;
}

static int dice_hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct dice *dice = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return dice_hwdep_get_info(dice, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return dice_hwdep_lock(dice);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return dice_hwdep_unlock(dice);
	default:
		return -ENOIOCTLCMD;
	}
}

static int dice_hwdep_dsp_status(struct snd_hwdep *hwdep, struct snd_hwdep_dsp_status *dsp_status)
{
	struct dice *dice = hwdep->private_data;
	unsigned int i;
	dsp_status->num_dsps = 1;
	dsp_status->chip_ready = 1;
	snprintf(dsp_status->id, sizeof(dsp_status->id),"dice-%08x-%08x", dice->app_info.ui_vendor_id, dice->app_info.ui_product_id);
	for (i=0; i<dsp_status->num_dsps && i<sizeof(dsp_status->dsp_loaded); i++) {
		dsp_status->dsp_loaded |= BIT(i);
	}
	return 0;
}

static int dice_hwdep_dsp_load(struct snd_hwdep *hwdep, struct snd_hwdep_dsp_image *dsp_image)
{
	struct dice *dice = hwdep->private_data;
	int err;
	struct firmware fw = {
			.size = dsp_image->length,
			.data = vmalloc(fw.size),
	};
	if (!fw.data) {
		dev_warn(&dice->unit->device, "can't allocate firmware image (%u bytes)", fw.size);
		return -ENOMEM;
	}
	if (copy_from_user((void *)fw.data, dsp_image->image, dsp_image->length)) {
		err = -EFAULT;
		goto fw_load_done;
	}
	err = dice_firmware_load(dice, &fw, (dsp_image->driver_data & DICE_HWDEP_LOADDSP_DRV_FLAG_FORCE)!=0);

fw_load_done:
	vfree(fw.data);
	return err;
}

#ifdef CONFIG_COMPAT
static int dice_hwdep_compat_ioctl(struct snd_hwdep *hwdep, struct file *file,
				   unsigned int cmd, unsigned long arg)
{
	return dice_hwdep_ioctl(hwdep, file, cmd,
				(unsigned long)compat_ptr(arg));
}
#else
#define dice_hwdep_compat_ioctl NULL
#endif

int dice_create_hwdep(struct dice *dice)
{
	static const struct snd_hwdep_ops ops = {
		.read         = dice_hwdep_read,
		.release      = dice_hwdep_release,
		.poll         = dice_hwdep_poll,
		.ioctl        = dice_hwdep_ioctl,
		.ioctl_compat = dice_hwdep_compat_ioctl,
		.dsp_status   = dice_hwdep_dsp_status,
		.dsp_load     = dice_hwdep_dsp_load,
	};
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(dice->card, "DICE", 0, &hwdep);
	if (err < 0)
		return err;
	strcpy(hwdep->name, "DICE");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_DICE;
	hwdep->ops = ops;
	hwdep->private_data = dice;
	hwdep->exclusive = true;

	return 0;
}
