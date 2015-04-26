/*
 *   Sound driver for PNX8550 SPDIF Out module.
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
#include <sound/asoundef.h>

#include <asm/mach-pnx8550/spdif.h>
#include <asm/div64.h>

MODULE_AUTHOR("Matthias <tqzamf@gmail.com>");
MODULE_DESCRIPTION("PNX8550 SPDO audio");
MODULE_LICENSE("GPL");

static struct platform_device_id platform_device_ids[] = {
	{ .name = "pnx8550spdo" },
	{}
};
MODULE_DEVICE_TABLE(platform, platform_device_ids);

struct snd_pnx8550spdo {
	struct snd_card *card;
	void *buffer;
	dma_addr_t buf1_dma;
	dma_addr_t buf2_dma;
	snd_pcm_uframes_t samples;
	snd_pcm_uframes_t ptr;
	struct snd_pcm_substream *substream;
	struct device *dev;
	unsigned char cswl[6];
	unsigned char cswr[6];
};

static irqreturn_t snd_pnx8550spdo_isr(int irq, void *dev_id)
{
	struct snd_pnx8550spdo *chip = dev_id;
	unsigned int control, status;

	status = PNX8550_SPDO_STATUS;
	control = PNX8550_SPDO_BUF_INTEN | PNX8550_SPDO_TRANS_ENABLE;
	if (status & PNX8550_SPDO_BUF1) {
		// buffer 1 has run empty, so we need to fill it now. tell the
		// hardware that we already did, because that clears the interrupt.
		chip->ptr = chip->samples;
		control |= PNX8550_SPDO_BUF1;
	} else if (status & PNX8550_SPDO_BUF2) {
		// same for buffer 2
		chip->ptr = 0;
		control |= PNX8550_SPDO_BUF2;
	}
	
	// clear UDR and HBE errors, just in case
	if (status & (PNX8550_SPDO_UDR | PNX8550_SPDO_HBE)) {
		printk(KERN_ERR "pnx8550spdo: %s%s error\n",
				(status & PNX8550_SPDO_HBE) ? "bandwidth" : "",
				(status & PNX8550_SPDO_UDR) ? "underrun" : "");
		control |= PNX8550_SPDO_UDR | PNX8550_SPDO_HBE;
	}
	PNX8550_SPDO_CTL = control;
	
	// tell ALSA we're done
	if (chip->substream)
		snd_pcm_period_elapsed(chip->substream);

	return IRQ_HANDLED;
}

static struct snd_pcm_hardware snd_pnx8550spdo_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER
			| SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats =          SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates =            SNDRV_PCM_RATE_8000_96000 | SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min =         PNX8550_SPDO_RATE_MIN,
	.rate_max =         PNX8550_SPDO_RATE_MAX,
	.channels_min =     2,
	.channels_max =     2,
	.buffer_bytes_max = 2*PNX8550_SPDO_BUF_VIRTUAL,
	.period_bytes_min = PNX8550_SPDO_BUF_VIRTUAL,
	.period_bytes_max = PNX8550_SPDO_BUF_VIRTUAL,
	.periods_min =      1,
	.periods_max =      2,
};

static int snd_pnx8550spdo_open(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550spdo *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	chip->ptr = 0;
	chip->substream = substream;
	runtime->hw = snd_pnx8550spdo_hw;
	runtime->private_data = chip;
	return 0;
}

static int snd_pnx8550spdo_close(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550spdo *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->private_data = NULL;
	chip->substream = NULL;
	return 0;
}

static int snd_pnx8550spdo_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct snd_pnx8550spdo *chip = snd_pcm_substream_chip(substream);
	int size = params_buffer_bytes(hw_params);

	if (chip->buffer == NULL) {
		if (size != 2*PNX8550_SPDO_BUF_VIRTUAL) {
			printk(KERN_ERR "pnx8550spdo: attempt to allocate %d byte buffer"
					" (need %d)\n", size, 2*PNX8550_SPDO_BUF_VIRTUAL);
			return -EINVAL;
		}

		/* allocate DMA-capable buffers */
		chip->buffer = dma_alloc_coherent(NULL, PNX8550_SPDO_BUF_ALLOC,
							 &chip->buf1_dma, GFP_USER);
		if (chip->buffer == NULL) {
			printk(KERN_ERR
				   "pnx8550spdo: could not allocate ring buffers\n");
			return -ENOMEM;
		}
		chip->buf2_dma = chip->buf1_dma + PNX8550_SPDO_BUF_SIZE;
		dev_dbg(chip->dev, "pnx8550spdo: buffer1=%08x buffer2=%08x\n",
				chip->buf1_dma, chip->buf2_dma);

		/* setup buffer base addresses and sizes */
		PNX8550_SPDO_BUF1_BASE = chip->buf1_dma;
		PNX8550_SPDO_BUF2_BASE = chip->buf2_dma;
		PNX8550_SPDO_SIZE = PNX8550_SPDO_BUF_SIZE;
	}

	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

static int snd_pnx8550spdo_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550spdo *chip = snd_pcm_substream_chip(substream);

	if (chip->buffer != NULL) {
		dma_free_coherent(NULL, PNX8550_SPDO_BUF_ALLOC, chip->buffer,
				chip->buf1_dma);
		chip->buffer = NULL;

		chip->buf2_dma = chip->buf1_dma = 0;
		PNX8550_SPDO_BUF1_BASE = chip->buf1_dma;
		PNX8550_SPDO_BUF2_BASE = chip->buf2_dma;
	}

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int snd_pnx8550spdo_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550spdo *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long dds, rem, size, fscode, wlen;
	u64 rate;
	
	dev_dbg(chip->dev, "pnx8550spdo: setting %dch %dHz, format=%d\n",
			runtime->channels, runtime->rate, runtime->format);
	
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		chip->samples = PNX8550_SPDO_SAMPLES;
		size = PNX8550_SPDO_BUF_SIZE;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		chip->samples = PNX8550_SPDO_SAMPLES / 2;
		size = PNX8550_SPDO_BUF_SIZE / 2;
		break;
	default:
		return -EINVAL;
	}
	
	chip->cswl[0] = chip->cswr[0] = IEC958_AES0_CON_NOT_COPYRIGHT
			| IEC958_AES0_CON_EMPHASIS_NONE;
	chip->cswl[1] = chip->cswr[1] = IEC958_AES1_CON_GENERAL
			| IEC958_AES1_CON_ORIGINAL;
	chip->cswl[2] = IEC958_AES2_CON_SOURCE_UNSPEC | (1 << 4);
	chip->cswr[2] = IEC958_AES2_CON_SOURCE_UNSPEC | (2 << 4);
	switch (runtime->rate) {
	case 22050: fscode = IEC958_AES3_CON_FS_22050; break;
	case 24000: fscode = IEC958_AES3_CON_FS_24000; break;
	case 32000: fscode = IEC958_AES3_CON_FS_32000; break;
	case 44100: fscode = IEC958_AES3_CON_FS_44100; break;
	case 48000: fscode = IEC958_AES3_CON_FS_48000; break;
	case 88200: fscode = IEC958_AES3_CON_FS_88200; break;
	case 96000: fscode = IEC958_AES3_CON_FS_96000; break;
	default: fscode = IEC958_AES3_CON_FS_NOTID;
	}
	chip->cswl[3] = chip->cswr[3] = IEC958_AES3_CON_CLOCK_1000PPM | fscode;
	if (runtime->format == SNDRV_PCM_FORMAT_S32_LE)
		wlen = IEC958_AES4_CON_MAX_WORDLEN_24 | IEC958_AES4_CON_WORDLEN_24_20;
	else
		wlen = IEC958_AES4_CON_WORDLEN_20_16;
	chip->cswl[4] = chip->cswr[4] = IEC958_AES4_CON_ORIGFS_NOTID | wlen;
	chip->cswl[5] = chip->cswr[5] = IEC958_AES5_CON_CGMSA_COPYFREELY;
	dev_dbg(chip->dev, "pnx8550spdo: synthesized CSW:"
			" L=%02x%02x%02x%02x%02x%02x R=%02x%02x%02x%02x%02x%02x\n",
			chip->cswl[5], chip->cswl[4], chip->cswl[3], chip->cswl[2],
			chip->cswl[1], chip->cswl[0],
			chip->cswr[5], chip->cswr[4], chip->cswr[3], chip->cswr[2],
			chip->cswr[1], chip->cswr[0]);

	// the DDS can do any rate, with ridculously low jitter specs.
	// the DAC doesn't seem to choke on wildly out-of-spec rates either.
	// unfortunately, ALSA only supports rates in multiples of 1Hz ;)
	
	// fosck = 1.728GHz * N / 2^32; fs = fosck / 128
	// thus: N = 2^32 * (128 * fs) / 1.728GHz
	rate = (((u64) runtime->rate) << 32) << 7;
	rem = do_div(rate, PNX8550_SPDO_DDS_REF);
	dds = rate;
	if (rem > PNX8550_SPDO_DDS_REF / 2)
		dds++; // rounding
	
	// clamp to allowed values, for safety
	if (dds < PNX8550_SPDO_DDS_MIN) {
		printk(KERN_ERR "pnx8550spdo: clamping DDS %ld, must be >%d\n",
				dds, PNX8550_SPDO_DDS_MIN);
		dds = PNX8550_SPDO_DDS_MIN;
	}
	if (dds > PNX8550_SPDO_DDS_MAX) {
		printk(KERN_ERR "pnx8550spdo: clamping DDS %ld, must be <%d\n",
				dds, PNX8550_SPDO_DDS_MAX);
		dds = PNX8550_SPDO_DDS_MAX;
	}

	PNX8550_SPDO_DDS_CTL = dds;
	PNX8550_SPDO_SIZE = size;
	return 0;
}

static int snd_pnx8550spdo_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
	unsigned int control;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		// enable interrupts and start transmission
		control = PNX8550_SPDO_BUF_INTEN | PNX8550_SPDO_TRANS_ENABLE;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		// disable interrupts and stop transmission
		control = 0;
		break;
	default:
		return -EINVAL;
	}
	PNX8550_SPDO_CTL = control | PNX8550_SPDO_UDR | PNX8550_SPDO_HBE
				| PNX8550_SPDO_BUF1 | PNX8550_SPDO_BUF2;

	return 0;
}

static snd_pcm_uframes_t
snd_pnx8550spdo_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550spdo *chip = snd_pcm_substream_chip(substream);

	/* get the current "hardware" pointer */
	return chip->ptr;
}

static int snd_pnx8550spdo_copy(struct snd_pcm_substream *substream, int channel,
               snd_pcm_uframes_t pos, void *src, snd_pcm_uframes_t count)
{
	struct snd_pnx8550spdo *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int offset, len, i, j;
	unsigned char *csw1 = chip->cswl, *csw2 = chip->cswr;
	u16 *source16;
	u32 *buffer, *source32;
	
	if (chip->buffer == NULL) {
		printk(KERN_ERR "pnx8550spdo: refusing to write to null buffer\n");
		return 0;
	}
	
	// copy to correct half of buffer. they're contiguous for 16-bit, but
	// not for 32-bit mode.
	if (pos < chip->samples)
		offset = 8 * pos;
	else
		offset = PNX8550_SPDO_BUF_SIZE + 8 * (pos - chip->samples);
	len = 8 * count;
	if (offset + len > PNX8550_SPDO_BUF_ALLOC) {
		printk(KERN_ERR "pnx8550spdo: refusing to write beyond end of buffer"
				" (offset %d, length %d)\n", offset, len);
		return 0;
	}
	
	buffer = chip->buffer + offset;
	if (runtime->format == SNDRV_PCM_FORMAT_S16_LE) {
		source16 = src;
		for (i = 0; i < count; i += 192) {
			*(buffer++) = (((u32) *(source16++)) << 12) | PNX8550_SPDO_PREAMBLE_BLOCK;
			*(buffer++) = (((u32) *(source16++)) << 12) | PNX8550_SPDO_PREAMBLE_CHAN2;
			#define COPY16(cswbit) \
				*(buffer++) = (((u32) *(source16++)) << 12) | PNX8550_SPDO_PREAMBLE_CHAN1 \
						| (((*csw1) & (1 << cswbit)) << (30 - cswbit)); \
				*(buffer++) = (((u32) *(source16++)) << 12) | PNX8550_SPDO_PREAMBLE_CHAN2 \
						| (((*csw2) & (1 << cswbit)) << (30 - cswbit));
			#define COPY16S() \
				*(buffer++) = (((u32) *(source16++)) << 12) | PNX8550_SPDO_PREAMBLE_CHAN1; \
				*(buffer++) = (((u32) *(source16++)) << 12) | PNX8550_SPDO_PREAMBLE_CHAN2;
			COPY16(1); COPY16(2); COPY16(3);
			COPY16(4); COPY16(5); COPY16(6); COPY16(7);
			for (j = 1; j < 6; j++) {
				csw1++; csw2++;
				COPY16(0); COPY16(1); COPY16(2); COPY16(3);
				COPY16(4); COPY16(5); COPY16(6); COPY16(7);
			}
			for (j = 6; j < 192/8; j++) {
				COPY16S(); COPY16S(); COPY16S(); COPY16S();
				COPY16S(); COPY16S(); COPY16S(); COPY16S();
			}
		}
	} else {
		source32 = src;
		for (i = 0; i < count; i += 192) {
			*(buffer++) = ((((u32) *(source32++)) >> 8) << 4) | PNX8550_SPDO_PREAMBLE_BLOCK;
			*(buffer++) = ((((u32) *(source32++)) >> 8) << 4) | PNX8550_SPDO_PREAMBLE_CHAN2;
			#define COPY32(cswbit) \
				*(buffer++) = ((((u32) *(source32++)) >> 8) << 4) | PNX8550_SPDO_PREAMBLE_CHAN1 \
						| (((*csw1) & (1 << cswbit)) << (30 - cswbit)); \
				*(buffer++) = ((((u32) *(source32++)) >> 8) << 4) | PNX8550_SPDO_PREAMBLE_CHAN2 \
						| (((*csw2) & (1 << cswbit)) << (30 - cswbit));
			#define COPY32S() \
				*(buffer++) = ((((u32) *(source32++)) >> 8) << 4) | PNX8550_SPDO_PREAMBLE_CHAN1; \
				*(buffer++) = ((((u32) *(source32++)) >> 8) << 4) | PNX8550_SPDO_PREAMBLE_CHAN2;
			COPY32(1); COPY32(2); COPY32(3);
			COPY32(4); COPY32(5); COPY32(6); COPY32(7);
			for (j = 1; j < 6; j++) {
				csw1++; csw2++;
				COPY32(0); COPY32(1); COPY32(2); COPY32(3);
				COPY32(4); COPY32(5); COPY32(6); COPY32(7);
			}
			for (j = 6; j < 192/8; j++) {
				COPY32S(); COPY32S(); COPY32S(); COPY32S();
				COPY32S(); COPY32S(); COPY32S(); COPY32S();
			}
		}
	}

	return 0;
}

static struct snd_pcm_ops snd_pnx8550spdo_playback_ops = {
	.open =        snd_pnx8550spdo_open,
	.close =       snd_pnx8550spdo_close,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_pnx8550spdo_hw_params,
	.hw_free =     snd_pnx8550spdo_hw_free,
	.prepare =     snd_pnx8550spdo_prepare,
	.trigger =     snd_pnx8550spdo_trigger,
	.pointer =     snd_pnx8550spdo_pointer,
	.copy =        snd_pnx8550spdo_copy,
	.page =        snd_pcm_lib_get_vmalloc_page,
	.mmap =        snd_pcm_lib_mmap_vmalloc,
};

static int __devinit snd_pnx8550spdo_new_pcm(struct snd_pnx8550spdo *chip)
{
	struct snd_pcm *pcm;
	int err;

	/* create pcm device with one output and no input */
	err = snd_pcm_new(chip->card, "PNX8550 SPDIF Out", 0, 1, 0, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = chip;
	strcpy(pcm->name, "PNX8550 SPDO");

	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_pnx8550spdo_playback_ops);

	return 0;
}

static int snd_pnx8550spdo_free(struct snd_pnx8550spdo *chip)
{
	/* disable interrupts and stop transmission */
	PNX8550_SPDO_CTL = 0;

	/* release IRQ */
	free_irq(PNX8550_SPDO_IRQ, chip);

	/* release card data */
	kfree(chip);
	return 0;
}

static int snd_pnx8550spdo_dev_free(struct snd_device *device)
{
	struct snd_pnx8550spdo *chip = device->device_data;

	return snd_pnx8550spdo_free(chip);
}

static struct snd_device_ops ops = {
	.dev_free = snd_pnx8550spdo_dev_free,
};

static int __devinit snd_pnx8550spdo_create(struct snd_card *card,
					   struct snd_pnx8550spdo **rchip)
{
	struct snd_pnx8550spdo *chip;
	int err;

	*rchip = NULL;

	chip = kzalloc(sizeof(struct snd_pnx8550spdo), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->card = card;

	/* allocate IRQ */
	if (request_irq(PNX8550_SPDO_IRQ, snd_pnx8550spdo_isr, 0,
			"pnx8550spdo", chip)) {
		snd_pnx8550spdo_free(chip);
		printk(KERN_ERR "pnx8550spdo: cannot allocate irq %d\n",
			   PNX8550_SPDO_IRQ);
		return -EBUSY;
	}

	/* configure and start the clock */
	PNX8550_SPDO_DDS_CTL = PNX8550_SPDO_DDS_48;
	PNX8550_SPDO_BCLK_CTL = PNX8550_SPDO_BCLK_ENABLE;
	
	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		snd_pnx8550spdo_free(chip);
		return err;
	}
	*rchip = chip;
	return 0;
}

static int __devinit snd_pnx8550spdo_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct snd_pnx8550spdo *chip;
	int err;

	err = snd_card_create(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			THIS_MODULE, 0, &card);
	if (err < 0) {
		printk(KERN_INFO "pnx8550spdo: snd_card_create %d\n", err);
		return err;
	}

	err = snd_pnx8550spdo_create(card, &chip);
	if (err < 0) {
		printk(KERN_INFO "pnx8550spdo: snd_pnx8550spdo_create %d\n", err);
		snd_card_free(card);
		return err;
	}
	snd_card_set_dev(card, &pdev->dev);
	chip->dev = &pdev->dev;

	err = snd_pnx8550spdo_new_pcm(chip);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->shortname, "PNX8550 SPDO");
	strcpy(card->driver, card->shortname);
	strcpy(card->longname, "PNX8550 SPDIF Out");
	printk(KERN_INFO "pnx8550spdo: %s driver, dynamic buffers\n",
			card->longname);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	platform_set_drvdata(pdev, card);
	return 0;
}

static int __devexit snd_pnx8550spdo_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);

	snd_card_free(card);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver pnx8550spdo_driver = {
	.probe	= snd_pnx8550spdo_probe,
	.remove	= __devexit_p(snd_pnx8550spdo_remove),
	.driver = {
		.name	= "pnx8550spdo",
		.owner	= THIS_MODULE,
	}
};

module_platform_driver(pnx8550spdo_driver);
