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

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include <asm/mach-pnx8550/audio.h>

MODULE_AUTHOR("Matthias <tqzamf@gmail.com>");
MODULE_DESCRIPTION("PNX8550 AO1 audio");
MODULE_LICENSE("GPL");

struct snd_pnx8550ao1 {
	struct snd_card *card;
	unsigned int control;
	void *buf1_base;
	dma_addr_t buf1_dma;
	void *buf2_base;
	dma_addr_t buf2_dma;
	snd_pcm_uframes_t ptr;
	struct snd_pcm_substream *substream;
	int expand8;
	int repeat;
	int stereo;
};

//static int pnx8550ao1_gain_info(struct snd_kcontrol *kcontrol,
			       //struct snd_ctl_elem_info *uinfo)
//{
	//struct snd_pnx8550ao1 *chip = snd_kcontrol_chip(kcontrol);

	//uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	//uinfo->count = 1;
	//uinfo->value.integer.min = 0;
	//uinfo->value.integer.max = AK4705_VOL_MAX;
	//return 0;
//}

//static int pnx8550ao1_gain_get(struct snd_kcontrol *kcontrol,
			       //struct snd_ctl_elem_value *ucontrol)
//{
	//struct snd_pnx8550ao1 *chip = snd_kcontrol_chip(kcontrol);
	//int vol;

	//vol = AK4705_VOL_DEFAULT; // TODO read from AK4705
	//ucontrol->value.integer.value[0] = vol;

	//return 0;
//}

//static int pnx8550ao1_gain_put(struct snd_kcontrol *kcontrol,
			//struct snd_ctl_elem_value *ucontrol)
//{
	//struct snd_pnx8550ao1 *chip = snd_kcontrol_chip(kcontrol);
	//int newvol, oldvol;

	//oldvol = AK4705_VOL_DEFAULT; // TODO read from AK4705
	//newvol = ucontrol->value.integer.value[0] & 0x3f;
	//if (newvol > AK4705_VOL_MAX)
		//newvol = AK4705_VOL_MAX;
	//newvol = newvol; // TODO write AK4705 volume

	//return newvol != oldvol;
//}

//static struct snd_kcontrol_new pnx8550ao1_ctrl_volume __devinitdata = {
	//.iface          = SNDRV_CTL_ELEM_IFACE_MIXER,
	//.name           = "Main Playback Volume",
	//.index          = 0,
	//.access         = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	//.info           = pnx8550ao1_gain_info,
	//.get            = pnx8550ao1_gain_get,
	//.put            = pnx8550ao1_gain_put,
//};

//static int __devinit snd_pnx8550ao1_new_mixer(struct snd_pnx8550ao1 *chip)
//{
	//return snd_ctl_add(chip->card,
			  //snd_ctl_new1(&pnx8550ao1_ctrl_volume, chip));
//}

/* low-level audio interface DMA */

///* get data out of bounce buffer, count must be a multiple of 32 */
///* returns 1 if a period has elapsed */
//static int snd_pnx8550ao1_dma_pull_frag(struct snd_pnx8550ao1 *chip,
					//unsigned int ch, unsigned int count)
//{
	//int ret;
	//unsigned long src_base, src_pos, dst_mask;
	//unsigned char *dst_base;
	//int dst_pos;
	//u64 *src;
	//s16 *dst;
	//u64 x;
	//unsigned long flags;
	//struct snd_pcm_runtime *runtime = chip->channel[ch].substream->runtime;

	//spin_lock_irqsave(&chip->channel[ch].lock, flags);

	//src_base = (unsigned long) chip->ring_base | (ch << CHANNEL_RING_SHIFT);
	//src_pos = readq(&mace->perif.audio.chan[ch].read_ptr);
	//dst_base = runtime->dma_area;
	//dst_pos = chip->channel[ch].pos;
	//dst_mask = frames_to_bytes(runtime, runtime->buffer_size) - 1;

	///* check if a period has elapsed */
	//chip->channel[ch].size += (count >> 3); /* in frames */
	//ret = chip->channel[ch].size >= runtime->period_size;
	//chip->channel[ch].size %= runtime->period_size;

	//while (count) {
		//src = (u64 *)(src_base + src_pos);
		//dst = (s16 *)(dst_base + dst_pos);

		//x = *src;
		//dst[0] = (x >> CHANNEL_LEFT_SHIFT) & 0xffff;
		//dst[1] = (x >> CHANNEL_RIGHT_SHIFT) & 0xffff;

		//src_pos = (src_pos + sizeof(u64)) & CHANNEL_RING_MASK;
		//dst_pos = (dst_pos + 2 * sizeof(s16)) & dst_mask;
		//count -= sizeof(u64);
	//}

	//writeq(src_pos, &mace->perif.audio.chan[ch].read_ptr); /* in bytes */
	//chip->channel[ch].pos = dst_pos;

	//spin_unlock_irqrestore(&chip->channel[ch].lock, flags);
	//return ret;
//}

///* put some DMA data in bounce buffer, count must be a multiple of 32 */
///* returns 1 if a period has elapsed */
//static int snd_pnx8550ao1_dma_push_frag(struct snd_pnx8550ao1 *chip,
					//unsigned int ch, unsigned int count)
//{
	//int ret;
	//s64 l, r;
	//unsigned long dst_base, dst_pos, src_mask;
	//unsigned char *src_base;
	//int src_pos;
	//u64 *dst;
	//s16 *src;
	//unsigned long flags;
	//struct snd_pcm_runtime *runtime = chip->channel[ch].substream->runtime;

	//spin_lock_irqsave(&chip->channel[ch].lock, flags);

	//dst_base = (unsigned long)chip->ring_base | (ch << CHANNEL_RING_SHIFT);
	//dst_pos = readq(&mace->perif.audio.chan[ch].write_ptr);
	//src_base = runtime->dma_area;
	//src_pos = chip->channel[ch].pos;
	//src_mask = frames_to_bytes(runtime, runtime->buffer_size) - 1;

	///* check if a period has elapsed */
	//chip->channel[ch].size += (count >> 3); /* in frames */
	//ret = chip->channel[ch].size >= runtime->period_size;
	//chip->channel[ch].size %= runtime->period_size;

	//while (count) {
		//src = (s16 *)(src_base + src_pos);
		//dst = (u64 *)(dst_base + dst_pos);

		//l = src[0]; /* sign extend */
		//r = src[1]; /* sign extend */

		//*dst = ((l & 0x00ffffff) << CHANNEL_LEFT_SHIFT) |
			//((r & 0x00ffffff) << CHANNEL_RIGHT_SHIFT);

		//dst_pos = (dst_pos + sizeof(u64)) & CHANNEL_RING_MASK;
		//src_pos = (src_pos + 2 * sizeof(s16)) & src_mask;
		//count -= sizeof(u64);
	//}

	//writeq(dst_pos, &mace->perif.audio.chan[ch].write_ptr); /* in bytes */
	//chip->channel[ch].pos = src_pos;

	//spin_unlock_irqrestore(&chip->channel[ch].lock, flags);
	//return ret;
//}

//static int snd_pnx8550ao1_dma_start(struct snd_pcm_substream *substream)
//{
	//struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	//struct snd_pnx8550ao1_chan *chan = substream->runtime->private_data;
	//int ch = chan->idx;

	///* reset DMA channel */
	//writeq(CHANNEL_CONTROL_RESET, &mace->perif.audio.chan[ch].control);
	//udelay(10);
	//writeq(0, &mace->perif.audio.chan[ch].control);

	//if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		///* push a full buffer */
		//snd_pnx8550ao1_dma_push_frag(chip, ch, CHANNEL_RING_SIZE - 32);
	//}
	///* set DMA to wake on 50% empty and enable interrupt */
	//writeq(CHANNEL_DMA_ENABLE | CHANNEL_INT_THRESHOLD_50,
	       //&mace->perif.audio.chan[ch].control);
	//return 0;
//}

//static int snd_pnx8550ao1_dma_stop(struct snd_pcm_substream *substream)
//{
	//struct snd_pnx8550ao1_chan *chan = substream->runtime->private_data;

	//writeq(0, &mace->perif.audio.chan[chan->idx].control);
	//return 0;
//}

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
	printk(KERN_DEBUG "pnx8550ao1: status %08x control %08x, next pointer is %d\n",
			status, control, chip->ptr);
	
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

///* PCM hardware definition */
//static struct snd_pcm_hardware snd_pnx8550ao1_hw = {
	//.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER),
	//.formats =          SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE
                                //| SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8,
	//.rates =            SNDRV_PCM_RATE_8000_48000,
	//.rate_min =         8000,
	//.rate_max =         48000,
	//.channels_min =     1,
	//.channels_max =     2,
	//.buffer_bytes_max = PNX8550_AO_SAMPLES,
	//.period_bytes_min = PNX8550_AO_SAMPLES,
	//.period_bytes_max = PNX8550_AO_SAMPLES,
	//.periods_min =      1,
	//.periods_max =      1,
//};

/* PCM hardware definition */
static struct snd_pcm_hardware snd_pnx8550ao1_hw = {
	// TODO mmap?
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER
			| SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats =          SNDRV_PCM_FMTBIT_S16_LE,
	.rates =            SNDRV_PCM_RATE_32000,
	.rate_min =         32000,
	.rate_max =         32000,
	.channels_min =     2,
	.channels_max =     2,
	.buffer_bytes_max = 2*PNX8550_AO_BUF_SIZE,
	.period_bytes_min = PNX8550_AO_BUF_SIZE,
	.period_bytes_max = PNX8550_AO_BUF_SIZE,
	.periods_min =      1,
	.periods_max =      2,
};

/* PCM playback open callback */
static int snd_pnx8550ao1_open(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	printk(KERN_DEBUG "pnx8550ao1 open\n");

	chip->ptr = 0;
	chip->substream = substream;
	runtime->hw = snd_pnx8550ao1_hw;
	runtime->private_data = chip;
	return 0;
}

/* PCM close callback */
static int snd_pnx8550ao1_close(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	printk(KERN_DEBUG "pnx8550ao1 close\n");

	runtime->private_data = NULL;
	chip->substream = NULL;
	return 0;
}


/* hw_params callback */
static int snd_pnx8550ao1_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	int err;
	
	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
	//printk(KERN_DEBUG "pnx8550ao1 hw_params %d\n", err);
	
	return err;
}

/* hw_free callback */
static int snd_pnx8550ao1_hw_free(struct snd_pcm_substream *substream)
{
	int err;

	err = snd_pcm_lib_free_vmalloc_buffer(substream);
	//printk(KERN_DEBUG "pnx8550ao1 hw_free %d\n", err);
	
	return err;
}

/* prepare callback */
static int snd_pnx8550ao1_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long control, stereo, expand8, repeat, dds;
	
	printk(KERN_DEBUG "pnx8550ao1 prepare %dch %dHz fmt=%d\n",
			runtime->channels, runtime->rate, runtime->format);

	control = PNX8550_AO_TRANS_MODE_16 | PNX8550_AO_TRANS_MODE_STEREO | PNX8550_AO_SIGN_CONVERT_SIGNED;
	dds = PNX8550_AO_DDS_32;
	stereo = expand8 = 0;
	repeat = 1;
	
	//control = PNX8550_AO_TRANS_MODE_16;
	
	//if (runtime->channels == 2) {
		//control |= PNX8550_AO_TRANS_MODE_STEREO;
		//stereo = 1;
	//} else if (runtime->channels == 1) {
		//control |= PNX8550_AO_TRANS_MODE_MONO;
		//stereo = 0;
	//} else
		//return -EINVAL;
	
	//switch (runtime->format) {
	//case SNDRV_PCM_FORMAT_S8:
	//case SNDRV_PCM_FORMAT_S16_LE:
		//control |= PNX8550_AO_SIGN_CONVERT_SIGNED;
		//break;
	//case SNDRV_PCM_FORMAT_U8:
	//case SNDRV_PCM_FORMAT_U16_LE:
		//control |= PNX8550_AO_SIGN_CONVERT_UNSIGNED;
		//break;
	//default:
		//return -EINVAL;
	//}
	
	//switch (runtime->format) {
	//case SNDRV_PCM_FORMAT_S8:
	//case SNDRV_PCM_FORMAT_U8:
		//expand8 = 1;
		//break;
	//case SNDRV_PCM_FORMAT_S16_LE:
	//case SNDRV_PCM_FORMAT_U16_LE:
		//expand8 = 0;
		//break;
	//default:
		//return -EINVAL;
	//}
	
	//switch (runtime->rate) {
	//case 48000:
	//case 24000:
	//case 12000:
		//dds = PNX8550_AO_DDS_48;
		//break;
	//case 44100:
	//case 22050:
	//case 11025:
		//dds = PNX8550_AO_DDS_441;
		//break;
	//case 32000:
	//case 16000:
	//case 8000:
		//dds = PNX8550_AO_DDS_32;
		//break;
	//default:
		//return -EINVAL;
	//}

	//switch (runtime->rate) {
	//case 48000:
	//case 44100:
	//case 32000:
		//repeat = 1;
		//break;
	//case 24000:
	//case 22050:
	//case 16000:
		//repeat = 2;
		//break;
	//case 12000:
	//case 11025:
	//case 8000:
		//repeat = 4;
		//break;
	//default:
		//return -EINVAL;
	//}

	chip->control = control;
	chip->repeat = repeat;
	chip->expand8 = expand8;
	chip->stereo = stereo;
	PNX8550_AO_DDS_CTL = dds;
	return 0;
}

/* trigger callback */
static int snd_pnx8550ao1_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);

	//printk(KERN_DEBUG "pnx8550ao1 trigger %d\n", cmd);
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

/* pointer callback */
static snd_pcm_uframes_t
snd_pnx8550ao1_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	snd_pcm_uframes_t ptr;

	/* get the current hardware pointer */
	ptr = chip->ptr;
	printk(KERN_DEBUG "pnx8550ao1 pointer %d\n", ptr);
	
	return ptr;
}

static int snd_pnx8550ao1_copy(struct snd_pcm_substream *substream, int channel,
               snd_pcm_uframes_t pos, void *src, snd_pcm_uframes_t count)
{
	struct snd_pnx8550ao1 *chip = snd_pcm_substream_chip(substream);
	
	printk(KERN_DEBUG "pnx8550ao1 copy %d samples from %08x offset %d\n",
			count, (unsigned int) src, pos);

	//if (!chip->ptr)
		//return -EAGAIN;
	//if (pos > PNX8550_AO_SAMPLES)
		//pos -= PNX8550_AO_SAMPLES;
	
	// TODO recoding done here
	memcpy(chip->buf1_base + frames_to_bytes(substream->runtime, pos),
			src, frames_to_bytes(substream->runtime, count));
	
	return 0;
}

/* operators */
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

/* create a pcm device */
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

/* ALSA driver */

static int snd_pnx8550ao1_free(struct snd_pnx8550ao1 *chip)
{
	printk(KERN_DEBUG "pnx8550ao1 free\n");

	/* reset interface and disable transmission */
	PNX8550_AO_STATUS = PNX8550_AO_RESET;

	/* release IRQ */
	free_irq(PNX8550_AO_IRQ, chip);

	dma_free_coherent(NULL, PNX8550_AO_BUF_ALLOC, chip->buf1_base,
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
	chip->buf1_base = dma_alloc_coherent(NULL, PNX8550_AO_BUF_ALLOC,
					     &chip->buf1_dma, GFP_USER);
	if (chip->buf1_base == NULL) {
		printk(KERN_ERR
		       "pnx8550ao1: could not allocate ring buffers\n");
		kfree(chip);
		return -ENOMEM;
	}
	chip->buf2_base = chip->buf1_base + PNX8550_AO_BUF_SIZE;
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
	PNX8550_AO_DDS_CTL = PNX8550_AO_DDS_32;
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
	//err = snd_pnx8550ao1_new_mixer(chip);
	//if (err < 0) {
		//snd_card_free(card);
		//return err;
	//}

	strcpy(card->shortname, "PNX8550 AO1");
	strcpy(card->driver, card->shortname);
	strcpy(card->longname, "PNX8550 Audio Out 1");
	printk(KERN_INFO "pnx8550ao1: %s driver buf1=%08x buf2=%08x\n",
			card->longname, chip->buf1_dma, chip->buf2_dma);

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
