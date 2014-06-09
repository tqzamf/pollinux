/*
 *   Sound driver for PNX8550 Audio Out 1 module.
 *
 *   Copyright 2003 Vivien Chappelier <vivien.chappelier@linux-mips.org>
 *   Copyright 2008 Thomas Bogendoerfer <tsbogend@alpha.franken.de>
 *   Mxier part taken from mace_audio.c:
 *   Copyright 2007 Thorben Jändling <tj.trevelyan@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include <asm/mach-pnx8550/audio.h>
#include <asm/mach-pnx8550/framebuffer.h>
#include <asm/div64.h>

MODULE_AUTHOR("Matthias <tqzamf@gmail.com>");
MODULE_DESCRIPTION("PNX8550 AO1 audio");
MODULE_LICENSE("GPL");

struct snd_pnx8550ao1 {
	struct snd_card *card;
	unsigned int control;
	void *buffer;
	dma_addr_t buf1_dma;
	dma_addr_t buf2_dma;
	snd_pcm_uframes_t ptr;
	struct snd_pcm_substream *substream;
	int volume;
};

static int pnx8550ao1_gain_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = AK4705_VOL_MAX;
	return 0;
}

static int pnx8550ao1_gain_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pnx8550ao1 *chip = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = chip->volume;

	return 0;
}

static int pnx8550ao1_gain_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pnx8550ao1 *chip = snd_kcontrol_chip(kcontrol);
	int newvol, oldvol;

	oldvol = chip->volume;
	newvol = ucontrol->value.integer.value[0];
	if (newvol < 0)
		newvol = 0;
	if (newvol > AK4705_VOL_MAX)
		newvol = AK4705_VOL_MAX;
	chip->volume = newvol;
	pnx8550fb_set_volume(newvol);

	return newvol != oldvol;
}

static struct snd_kcontrol_new pnx8550ao1_ctrl_volume __devinitdata = {
	.iface          = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name           = "Main Playback Volume",
	.index          = 0,
	.access         = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info           = pnx8550ao1_gain_info,
	.get            = pnx8550ao1_gain_get,
	.put            = pnx8550ao1_gain_put,
};

static int __devinit snd_pnx8550ao1_new_mixer(struct snd_pnx8550ao1 *chip)
{
	int err;
	
	err = snd_ctl_add(chip->card,
			  snd_ctl_new1(&pnx8550ao1_ctrl_volume, chip));
	if (!err) {
		chip->volume = AK4705_VOL_DEFAULT;
		pnx8550fb_set_volume(chip->volume);
	}
	
	return err;
}

static irqreturn_t snd_pnx8550ao1_isr(int irq, void *dev_id)
{
	struct snd_pnx8550ao1 *chip = dev_id;
	unsigned int control, status;

	status = PNX8550_AO_STATUS;
	control = chip->control;
	if (status & PNX8550_AO_BUF1) {
		// buffer 1 has run empty, so we need to fill it now. tell the
		// hardware that we already did, because that clears the interrupt.
		chip->ptr = PNX8550_AO_SAMPLES;
		control |= PNX8550_AO_BUF1;
	} else if (status & PNX8550_AO_BUF2) {
		// same for buffer 2
		chip->ptr = 0;
		control |= PNX8550_AO_BUF2;
	}
	
	// clear UDR and HBE errors, just in case
	if (status & (PNX8550_AO_UDR | PNX8550_AO_HBE)) {
		printk(KERN_ERR "pnx8550ao1: %s%s error\n",
				(status & PNX8550_AO_HBE) ? "bandwidth" : "",
				(status & PNX8550_AO_UDR) ? "underrun" : "");
		control |= PNX8550_AO_UDR | PNX8550_AO_HBE;
	}
	PNX8550_AO_CTL = control;
	
	// tell ALSA we're done
	if (chip->substream)
		snd_pcm_period_elapsed(chip->substream);

	return IRQ_HANDLED;
}

static struct snd_pcm_hardware snd_pnx8550ao1_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER
			| SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats =          SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE,
	.rates =            SNDRV_PCM_RATE_8000_96000 | SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min =         PNX8550_AO_RATE_MIN,
	.rate_max =         PNX8550_AO_RATE_MAX,
	.channels_min =     2,
	.channels_max =     2,
	.buffer_bytes_max = 2*PNX8550_AO_BUF_SIZE,
	.period_bytes_min = PNX8550_AO_BUF_SIZE,
	.period_bytes_max = PNX8550_AO_BUF_SIZE,
	.periods_min =      1,
	.periods_max =      2,
};

static int snd_pnx8550ao1_open(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	chip->ptr = 0;
	chip->substream = substream;
	runtime->hw = snd_pnx8550ao1_hw;
	runtime->private_data = chip;
	return 0;
}

static int snd_pnx8550ao1_close(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->private_data = NULL;
	chip->substream = NULL;
	return 0;
}

static int snd_pnx8550ao1_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

static int snd_pnx8550ao1_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int snd_pnx8550ao1_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long control, dds, rem;
	u64 rate;
	
	printk(KERN_DEBUG "pnx8550ao1: setting %dch %dHz, format=%d\n",
			runtime->channels, runtime->rate, runtime->format);

	control = PNX8550_AO_TRANS_MODE_16 | PNX8550_AO_TRANS_MODE_STEREO;
	// signed / unsigned conversion can be done in hardware.
	// endianness can't, unfortunately.
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		control |= PNX8550_AO_SIGN_CONVERT_SIGNED;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
		control |= PNX8550_AO_SIGN_CONVERT_UNSIGNED;
		break;
	default:
		return -EINVAL;
	}
	
	// the DDS can do any rate, with ridculously low jitter specs.
	// the DAC doesn't seem to choke on wildly out-of-spec rates either.
	// unfortunately, ALSA only supports rates in multiples of 1Hz ;)
	
	// fosck = 1.728GHz * N / 2^32; fs = fosck / 256
	// thus: N = 2^32 * (256 * fs) / 1.728GHz
	rate = (((u64) runtime->rate) << 32) << 8;
	rem = do_div(rate, PNX8550_AO_DDS_REF);
	dds = rate;
	if (rem > PNX8550_AO_DDS_REF / 2)
		dds++; // rounding
	
	// clamp to allowed values, for safety
	if (dds < PNX8550_AO_DDS_MIN) {
		printk(KERN_ERR "pnx8550ao1: clamping DDS %ld, must be >%d\n",
				dds, PNX8550_AO_DDS_MIN);
		dds = PNX8550_AO_DDS_MIN;
	}
	if (dds > PNX8550_AO_DDS_MAX) {
		printk(KERN_ERR "pnx8550ao1: clamping DDS %ld, must be <%d\n",
				dds, PNX8550_AO_DDS_MAX);
		dds = PNX8550_AO_DDS_MAX;
	}

	chip->control = control;
	PNX8550_AO_DDS_CTL = dds;
	return 0;
}

static int snd_pnx8550ao1_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		chip->control |= (PNX8550_AO_TRANS_ENABLE | PNX8550_AO_BUF_INTEN);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		chip->control &= ~(PNX8550_AO_TRANS_ENABLE | PNX8550_AO_BUF_INTEN);
		break;
	default:
		return -EINVAL;
	}
	PNX8550_AO_CTL = chip->control | PNX8550_AO_UDR | PNX8550_AO_HBE
				| PNX8550_AO_BUF1 | PNX8550_AO_BUF2;

	return 0;
}

static snd_pcm_uframes_t
snd_pnx8550ao1_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);

	/* get the current "hardware" pointer */
	return chip->ptr;
}

static int snd_pnx8550ao1_copy(struct snd_pcm_substream *substream, int channel,
               snd_pcm_uframes_t pos, void *src, snd_pcm_uframes_t count)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	
	memcpy(chip->buffer + frames_to_bytes(substream->runtime, pos),
			src, frames_to_bytes(substream->runtime, count));

	return 0;
}

static struct snd_pcm_ops snd_pnx8550ao1_playback_ops = {
	.open =        snd_pnx8550ao1_open,
	.close =       snd_pnx8550ao1_close,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_pnx8550ao1_hw_params,
	.hw_free =     snd_pnx8550ao1_hw_free,
	.prepare =     snd_pnx8550ao1_prepare,
	.trigger =     snd_pnx8550ao1_trigger,
	.pointer =     snd_pnx8550ao1_pointer,
	.copy =        snd_pnx8550ao1_copy,
	.page =        snd_pcm_lib_get_vmalloc_page,
	.mmap =        snd_pcm_lib_mmap_vmalloc,
};

static int __devinit snd_pnx8550ao1_new_pcm(struct snd_pnx8550ao1 *chip)
{
	struct snd_pcm *pcm;
	int err;

	/* create pcm device with one output and no input */
	err = snd_pcm_new(chip->card, "PNX8550 Audio Out 1", 0, 1, 0, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = chip;
	strcpy(pcm->name, "PNX8550 AO1");

	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_pnx8550ao1_playback_ops);

	return 0;
}

static int snd_pnx8550ao1_free(struct snd_pnx8550ao1 *chip)
{
	// mute the volume; there can be noise on the outputs otherwise
	pnx8550fb_set_volume(0);
	
	/* reset interface and disable transmission */
	PNX8550_AO_STATUS = PNX8550_AO_RESET;

	/* release IRQ */
	free_irq(PNX8550_AO_IRQ, chip);

	dma_free_coherent(NULL, PNX8550_AO_BUF_ALLOC, chip->buffer,
			chip->buf1_dma);

	/* release card data */
	kfree(chip);
	return 0;
}

static int snd_pnx8550ao1_dev_free(struct snd_device *device)
{
	struct snd_pnx8550ao1 *chip = device->device_data;

	return snd_pnx8550ao1_free(chip);
}

static struct snd_device_ops ops = {
	.dev_free = snd_pnx8550ao1_dev_free,
};

static int __devinit snd_pnx8550ao1_create(struct snd_card *card,
					   struct snd_pnx8550ao1 **rchip)
{
	struct snd_pnx8550ao1 *chip;
	int err;

	*rchip = NULL;

	chip = kzalloc(sizeof(struct snd_pnx8550ao1), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->card = card;

	/* allocate DMA-capable buffers */
	chip->buffer = dma_alloc_coherent(NULL, PNX8550_AO_BUF_ALLOC,
					     &chip->buf1_dma, GFP_USER);
	if (chip->buffer == NULL) {
		printk(KERN_ERR
		       "pnx8550ao1: could not allocate ring buffers\n");
		kfree(chip);
		return -ENOMEM;
	}
	chip->buf2_dma = chip->buf1_dma + PNX8550_AO_BUF_SIZE;

	/* reset interface and disable transmission for the time being */
	PNX8550_AO_STATUS = PNX8550_AO_RESET;

	/* allocate IRQ */
	if (request_irq(PNX8550_AO_IRQ, snd_pnx8550ao1_isr, 0,
			"pnx8550ao1", chip)) {
		snd_pnx8550ao1_free(chip);
		printk(KERN_ERR "pnx8550ao1: cannot allocate irq %d\n",
			   PNX8550_AO_IRQ);
		return -EBUSY;
	}

	/* configure and start the clocks */
	PNX8550_AO_DDS_CTL = PNX8550_AO_DDS_48;
	PNX8550_AO_OSCK_CTL = PNX8550_AO_CLK_ENABLE;
	PNX8550_AO_SCLK_CTL = PNX8550_AO_CLK_ENABLE;
	/* configure interface for I²S */
	PNX8550_AO_SERIAL = PNX8550_AO_SERIAL_I2S;
	PNX8550_AO_FRAMING = PNX8550_AO_FRAMING_I2S;
	PNX8550_AO_CFC = PNX8550_AO_CFC_I2S;
	/* keep the chip in reset for now */
	chip->control = PNX8550_AO_TRANS_MODE_16;
	
	/* setup buffer base addresses and sizes */
	PNX8550_AO_BUF1_BASE = chip->buf1_dma;
	PNX8550_AO_BUF2_BASE = chip->buf2_dma;
	PNX8550_AO_SIZE = PNX8550_AO_SAMPLES;
	
	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		snd_pnx8550ao1_free(chip);
		return err;
	}
	*rchip = chip;
	return 0;
}

static int __devinit snd_pnx8550ao1_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct snd_pnx8550ao1 *chip;
	int err;

	err = snd_card_create(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	err = snd_pnx8550ao1_create(card, &chip);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	snd_card_set_dev(card, &pdev->dev);

	err = snd_pnx8550ao1_new_pcm(chip);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	err = snd_pnx8550ao1_new_mixer(chip);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->shortname, "PNX8550 AO1");
	strcpy(card->driver, card->shortname);
	strcpy(card->longname, "PNX8550 Audio Out 1");
	printk(KERN_INFO "pnx8550ao1: %s driver buf1=%08x buf2=%08x\n",
			card->longname, chip->buf1_dma, chip->buf2_dma);
	printk(KERN_DEBUG "pnx8550ao1: buffer1=%08x buffer2=%08x\n",
			chip->buf1_dma, chip->buf2_dma);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	platform_set_drvdata(pdev, card);
	return 0;
}

static int __devexit snd_pnx8550ao1_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);

	snd_card_free(card);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver pnx8550ao1_driver = {
	.probe	= snd_pnx8550ao1_probe,
	.remove	= __devexit_p(snd_pnx8550ao1_remove),
	.driver = {
		.name	= "pnx8550ao1",
		.owner	= THIS_MODULE,
	}
};

module_platform_driver(pnx8550ao1_driver);
