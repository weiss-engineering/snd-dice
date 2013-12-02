#ifndef SOUND_FIREWIRE_DICEPCM_H_INCLUDED
#define SOUND_FIREWIRE_DICEPCM_H_INCLUDED

#include "dice.h"



int dice_pcm_create(struct dice *dice /*, enum cip_flags*/);

/** In case any PCM stream is open force it back to SNDRV_PCM_STATE_OPEN
 * such that the application is forced to re-initialize it.
 */
void dice_pcm_reset_substreams(struct dice *dice);

#endif
