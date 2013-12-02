
# As long we are hacking locally we override the config...
CONFIG_SND_DICE := m

snd-dice-objs := dice.o firmware.o pcm.o stream.o hwdep.o avc.o notif.o
                 

obj-$(CONFIG_SND_DICE) += snd-dice.o
