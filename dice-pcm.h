#ifndef SOUND_FIREWIRE_DICEPCM_H_INCLUDED
#define SOUND_FIREWIRE_DICEPCM_H_INCLUDED

#include "dice.h"



int dice_pcm_create(struct dice *dice /*, enum cip_flags*/);

void dice_stop_packet_streaming(struct dice *dice);
void dice_stop_streaming(struct dice *dice);
void dice_abort_streaming(struct dice *dice);
void dice_destroy_streaming(struct dice *dice);

#endif
