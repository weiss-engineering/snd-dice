/*
 * dice-notif.h
 *
 *  Created on: Aug 22, 2013
 *      Author: uli
 */

#ifndef SOUND_FIREWIRE_DICE_NOTIF_H_INCLUDED
#define SOUND_FIREWIRE_DICE_NOTIF_H_INCLUDED

#include "dice.h"

void dice_fw_notification_callback(struct fw_card *card, struct fw_request *request,
			      int tcode, int destination, int source,
			      int generation, unsigned long long offset,
			      void *data, size_t length, void *callback_data);

#endif /* SOUND_FIREWIRE_DICE_NOTIF_H_INCLUDED */
