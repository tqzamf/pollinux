/**
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.#include <linux/module.h>
 *
 * Copyright (C) 2005 Koninklijke Philips Electronics N.V.
 * All Rights Reserved.
 */
/*
    @file   videodenc.c
    @brief  Providing a Framework for Video Dencs to be used

@b  Component:  Video Denc.

    This is a framework to make video dencs easier to use.
    It is designed to be similar to the framebuffer framework as this works
    well.

    Set your editor for 4 space indentation.
*//*

Rev Date        Author      Comments
--------------------------------------------------------------------------------
  1 20060328    laird       First Revision.
  2 20060628    laird       Xcheck rework.
  3 20060713    laird       Adding ability to test a mode.
  4 20060726    laird       Adding ability to get mode database.
  5 20060905    laird       Porting to 2.6.17.7
  6 20070723    neill       Add gamma correction adjustment
  7 20070725    neill       Add read/write file operations
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/

/***********************************************
* INCLUDE FILES                                *
************************************************/
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h> 
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/linux_logo.h>
#include <linux/proc_fs.h>
#include <linux/console.h>
#ifdef CONFIG_KMOD
    #include <linux/kmod.h>
#endif
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/videodenc.h>
/***********************************************
* LOCAL MACROS                                 *
* recommendation only: <MODULE><_WORD>+        *
************************************************/

/******************************************************************
* LOCAL TYPEDEFS                                                  *
* recommendation only:                [p]phStbTemplate_<Word>+_t  *
*******************************************************************/

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/
static int videodenc_ioctl(/*struct inode *inode,*/
                           struct file *file,
                           unsigned int cmd,
                           unsigned long arg);

static int videodenc_mode_set(struct videodenc_info *info,
                              struct videodenc_var_screeninfo *var);

static int videodenc_gamma_set(struct videodenc_info *info,
                               unsigned int *gamma);

static int videodenc_blank(struct videodenc_info *info,
                           int blank);

static int videodenc_test_pattern(struct videodenc_info *info,
                                int colour_bar);

static int videodenc_open(struct inode *inode,
                          struct file *file);

static int videodenc_release(struct inode *inode,
                             struct file *file);


static int videodenc_write(struct file *file, 
                           const char __user *buf, 
                           size_t count, 
                           loff_t *pos);

static int videodenc_read(struct file *file, 
                          char __user *buf, 
                          size_t count, 
                          loff_t *pos);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/
static DEFINE_MUTEX(drv_mutex);

/**************************************************************************
* EXPORTED DATA                     g[k|p|kp|pk|kpk]phStbTemplate_<Word>+ *
***************************************************************************/

/* Data held by a driver instance */
struct videodenc_t
{
    dev_t devRegion; /* The start of the reserved VDENC device number region */
    struct videodenc_info *registered_videodenc[VIDEODENC_MAX];
    unsigned int major; /* Major number of our device, obtained from devRegion and held for convenience */
    unsigned int first_minor; /*The first minor number we are assigned*/
    struct proc_dir_entry *procDir; /* Our directory under /proc */
};


/* The driver instance data */
static struct videodenc_t gInstance;

/**
 * @brief VideoDenc useful information.
 */
int num_registered_videodenc;
static char *video_options[VIDEODENC_MAX];

static struct file_operations videodenc_fops = {
    .owner =    THIS_MODULE,
    .unlocked_ioctl =    videodenc_ioctl,
    .open =     videodenc_open,
    .release =  videodenc_release,
    .write =    videodenc_write,
    .read =     videodenc_read,
};

static struct class *videodenc_class;
EXPORT_SYMBOL(register_videodenc);
EXPORT_SYMBOL(unregister_videodenc);
EXPORT_SYMBOL(videodenc_register_driver);
EXPORT_SYMBOL(videodenc_unregister_driver);

EXPORT_SYMBOL(videodenc_get_options);
EXPORT_SYMBOL(videodenc_mode_set);
EXPORT_SYMBOL(videodenc_gamma_set);
EXPORT_SYMBOL(videodenc_blank);
EXPORT_SYMBOL(videodenc_test_pattern);

MODULE_LICENSE("GPL");

/*******************************************************************************
* FUNCTION IMPLEMENTATION                                                      *
********************************************************************************/
static int videodenc_read_proc(char *buf, char **start, off_t offset,
                               int len, int *eof, void *private)
{
    struct videodenc_info **fi;
    int clen;

    clen = 0;
    for(fi = gInstance.registered_videodenc; fi < &gInstance.registered_videodenc[VIDEODENC_MAX] && len < 4000; fi++)
        if(*fi)
            clen += sprintf(buf + clen, "%d %s\n",
                            (*fi)->node,
                            (*fi)->fix.id);
    *start = buf + offset;
    if(clen > offset)
        clen -= offset;
    else
        clen = 0;
    return clen < len ? clen : len;
}

/**
 * This will register the video denc properly with all of the sysfs infrastructure
 *
 * @param name - The Name of the driver
 * @param device_id - Device ID
 * @param device_info - The device info structure
 * @param platform_device_info - The platform device information
 * @param probe_function - The probe function to be registered
 * @param remove_function - The function to remove the videodenc from sysfs.
 *
 * @return 0 Success else failure.
 */
int videodenc_register_driver(char* name,
                              int device_id,
                              struct device_driver *device_driver_info,
                              struct platform_device *platform_device_info,
                              int   (*probe_function)   (struct device * dev),
                              int   (*remove_function)  (struct device * dev))
{
    int ret;
    platform_device_info->name = name;
    device_driver_info->name = name;
    device_driver_info->bus = &platform_bus_type;
    device_driver_info->probe = probe_function;
    device_driver_info->remove = __devexit_p(remove_function);

    ret = driver_register(device_driver_info);
    if (!ret)
    {
        platform_device_info->id = device_id;

        ret = platform_device_register(platform_device_info);
        if (ret)
        {
            driver_unregister(device_driver_info);
        }
    }
    return ret;
}

/**
 *
 *
 */
void videodenc_unregister_driver(struct device_driver *device_driver_info)
{
    driver_unregister(device_driver_info);
}

/**
 *  register_videodenc - registers a video denc device
 *  @param videodenc_info: video denc info structure.
 *  @param renderer      : The renderer this encoder is connected to.
 *
 *  Registers a video denc device @videodenc_info.
 *
 *  @return negative errno on error, or zero for success.
 */
int register_videodenc(struct videodenc_info *videodenc_info, int renderer)
{
    int i;
    int dev_index = 0;
    dev_t device;
    int result;

    if(num_registered_videodenc == VIDEODENC_MAX)
    {
        return -ENXIO;
    }
    num_registered_videodenc++;

    for(i = 0 ; i < VIDEODENC_MAX; i++)
    {
        if(gInstance.registered_videodenc[i] == NULL)
        {
            break;
        }
        else
        {
            if(gInstance.registered_videodenc[i]->renderer == renderer)
            {
                dev_index++;
            }
        }
    }
    videodenc_info->node = i;
    device = MKDEV(gInstance.major, (gInstance.first_minor + videodenc_info->node));
    videodenc_info->renderer = renderer;

    videodenc_info->char_device = cdev_alloc();
    videodenc_info->char_device->dev = device;
    videodenc_info->char_device->ops = &videodenc_fops;
    videodenc_info->char_device->owner = THIS_MODULE;

    result = cdev_add (videodenc_info->char_device,
                       videodenc_info->char_device->dev,
                       1);
    if (result)
    {
        printk(KERN_INFO "%s: Failed to add character device (0x%X)\n", __FILE__, result);
        return result;
    }

    /*Now store the simple class this allows udev to do its magic*/
    videodenc_info->class_device = device_create(videodenc_class,
                                                       NULL,
                                                       device,
                                                       videodenc_info->device,
                                                       "vdenc.rend%d.vdenc%d", renderer, dev_index);


    if (IS_ERR(videodenc_info->class_device))
    {
        /* Not fatal */
        printk(KERN_WARNING "Unable to create class_device for videodenc %d; errno = %ld\n",
               i,
               PTR_ERR(videodenc_info->class_device));

        videodenc_info->class_device = NULL;
    }

    /*Copy into my array of registered videodencs*/
    gInstance.registered_videodenc[videodenc_info->node] = videodenc_info;
    /*Increment number of instances*/
    num_registered_videodenc++;

    return 0;
}


/**
 *  unregister_video denc - releases a video denc device
 *  @param videodenc_info: video denc info structure
 *
 *  Unregisters a video denc device @videodenc_info.
 *
 *  @return negative errno on error, or zero for success.
 */
int unregister_videodenc(struct videodenc_info *videodenc_info)
{
    int i;

    i = videodenc_info->node;
    if(!gInstance.registered_videodenc[i])
    {
        return -EINVAL;
    }

    gInstance.registered_videodenc[i]=NULL;
    num_registered_videodenc--;
    device_destroy(videodenc_class, MKDEV(gInstance.major, (gInstance.first_minor + videodenc_info->node)));
    return 0;
}

/**
 * videodenc_mode_set - set the mode of the video denc.
 *
 * It will attempt to check the mode before setting it.
 *
 * @param info The fixed info.
 * @param var  The variable info to set.
 *
 * @return 0 success, else failure.
 */
int
videodenc_mode_set(struct videodenc_info *info, struct videodenc_var_screeninfo *var)
{
    int err = 0;
    int updateRequired = 1;

    if (!info->videodenc_ops->videodenc_check_mode)
    {
        *var = info->var;
    }
    else
    {
        updateRequired = info->videodenc_ops->videodenc_check_mode(var, info);
        if(updateRequired < 0)
        {
            /*If we have a negative number then not supported*/
            return updateRequired;
        }
    }

    /*If we actually want to activte the settings (ie this was not a test)
      And something was actually changed.*/
    if((var->activate == VIDEODENC_ACTIVATE_NOW) && updateRequired)
    {
        info->var = *var;
        if (info->videodenc_ops->videodenc_set_mode)
        {
            if ((err = info->videodenc_ops->videodenc_set_mode(info)))
            {
                return err;
            }
        }
    }
    return err;
}

/**
 * videodenc_gamma_set - Adjust the gamma settings for the video denc output.
 *
 * @param info The fixed information.
 * @param pointer to array containing gamma correction curves offsets for values:
 *        16, 32, 64, 96, 128, 160, 192, 224
 *
 * @return 0 success, else failure.
 */
int videodenc_gamma_set(struct videodenc_info *info,
                        unsigned int *gamma)
{
    int err = 0;
    if (info->videodenc_ops->videodenc_gamma_set)
    {
        err = info->videodenc_ops->videodenc_gamma_set(info, gamma);
    }

    return err;
}

/**
 * videodenc_blank - Blank the video denc output.
 *
 * @param info The fixed information.
 * @param blank 1 to blank 0 to unblank.
 *
 * @return 0 success, else failure.
 */
int
videodenc_blank(struct videodenc_info *info, int blank)
{
    int err = 0;

    if (info->videodenc_ops->videodenc_blank)
    {
        err = info->videodenc_ops->videodenc_blank(info, blank);
    }

    return err;
}

/**
 * videodenc_test_pattern - Enable a test pattern on vdenc.
 *
 * @param info The fixed information.
 * @param blank 1 to blank 0 to unblank.
 *
 * @return 0 success, else failure.
 */
int
videodenc_test_pattern(struct videodenc_info *info, int test_pattern)
{
    int err = 0;

    if (info->videodenc_ops->videodenc_test_pattern)
    {
        err = info->videodenc_ops->videodenc_test_pattern(info, test_pattern);
    }

    return err;
}

/**
 * videodenc_get_modes - Get the mode database of a vdenc.
 *
 * @param info The fixed information.
 * @param videoMode Ptr to first DB entry.
 * @param size The size in bytes of DB.
 *
 * @return 0 success, else failure.
 */
int
videodenc_get_modes(struct videodenc_info *info, struct videodenc_videomode **ppVideoMode, int *size)
{
    int err = 0;

    if (info->videodenc_ops->videodenc_get_modes)
    {
        err = info->videodenc_ops->videodenc_get_modes(ppVideoMode, size);
    }

    return err;
}

/**
 * videodenc_get_number_modes - Get the number of modes in the DB.
 *
 * @param info The fixed information.
 * @param num_modes The number of videomodes in the DB.
 *
 * @return 0 success, else failure.
 */
int
videodenc_get_number_modes(struct videodenc_info *info, int *num_modes)
{
    int err = 0;

    if (info->videodenc_ops->videodenc_get_number_modes)
    {
        err = info->videodenc_ops->videodenc_get_number_modes(num_modes);
    }

    return err;
}

/**
 * videodenc_ioctl - The ioctl handler for video dencs.
 *
 * @return 0 success, else failure.
 */
int videodenc_ioctl(/*struct inode *inode,*/
                    struct file *file,
                    unsigned int cmd,
                    unsigned long arg)
{
    int videodenc_idx = iminor(file->f_dentry->d_inode);
    struct videodenc_info *info = gInstance.registered_videodenc[videodenc_idx];
    struct videodenc_ops *vdenc = info->videodenc_ops;
    struct videodenc_var_screeninfo var;
    struct videodenc_fix_screeninfo fix;
    void __user *argp = (void __user *)arg;
    int i;
    struct videodenc_videomode *pVideoMode;
    int dbSize;

    if(!vdenc)
    {
        return -ENODEV;
    }

    switch(cmd)
    {
        case VIDEODENC_IOGET_VSCREENINFO:
            return copy_to_user(argp, &info->var, sizeof(var)) ? -EFAULT : 0;

        case VIDEODENC_IOGET_FSCREENINFO:
            return copy_to_user(argp, &info->fix, sizeof(fix)) ? -EFAULT : 0;

        case VIDEODENC_IOSET_MODE:
            if (copy_from_user(&var, argp, sizeof(var)))
            {
                return -EFAULT;
            }
            i = videodenc_mode_set(info, &var);
            if (i < 0) return i;
            if (copy_to_user(argp, &var, sizeof(var)))
            {
                return -EFAULT;
            }
            return 0;

        case VIDEODENC_IOGET_MODES:
            i = videodenc_get_modes(info, &pVideoMode, &dbSize);
            if (i < 0) return i;
            if (copy_to_user(argp, pVideoMode, dbSize))
            {
                return -EFAULT;
            }
            return 0;

        case VIDEODENC_IOSET_GAMMA:
            i = videodenc_gamma_set(info, (unsigned int *)arg);
            if (i) return i;

            return 0;

        case VIDEODENC_IOBLANK:
            i = videodenc_blank(info, arg);
            if (i) return i;

            return 0;

        case VIDEODENC_IOTEST_PATTERN:
            i = videodenc_test_pattern(info, arg);
            if (i) return i;

            return 0;

        case VIDEODENC_IOGET_NUM_MODES:
            i = videodenc_get_number_modes(info, (__u32*) argp);
            return i;

        default:
            if(vdenc->videodenc_ioctl == NULL)
                return -EINVAL;
            return vdenc->videodenc_ioctl(file->f_dentry->d_inode, file, cmd, arg, info);
    }
}

#ifdef CONFIG_KMOD
static void try_to_load(int vdenc)
{
    request_module("vdenc%d", vdenc);
}
#endif /* CONFIG_KMOD */

/**
 * videodenc_open - Create a vdenc device.
 *
 * @return 0 success, else failure.
 */
int
videodenc_open(struct inode *inode, struct file *file)
{
    int vdencidx = iminor(inode);
    struct videodenc_info *info;
    int res = 0;

    if(vdencidx >= VIDEODENC_MAX)
        return -ENODEV;
#ifdef CONFIG_KMOD
    if(!(info = gInstance.registered_videodenc[vdencidx]))
        try_to_load(vdencidx);
#endif /* CONFIG_KMOD */
    if(!(info = gInstance.registered_videodenc[vdencidx]))
        return -ENODEV;
    if(!try_module_get(info->videodenc_ops->owner))
        return -ENODEV;
    if(info->videodenc_ops->videodenc_open)
    {
        res = info->videodenc_ops->videodenc_open(info, inode, file);
        if(res)
            module_put(info->videodenc_ops->owner);
    }
    return res;
}

/**
 * videodenc_release - close a vdenc device.
 *
 * @return 0 success, else failure.
 */
int videodenc_release(struct inode *inode, struct file *file)
{
    int vdencidx = iminor(inode);
    struct videodenc_info *info;

    mutex_lock(&drv_mutex);
    info = gInstance.registered_videodenc[vdencidx];
    if(info->videodenc_ops->videodenc_release)
        info->videodenc_ops->videodenc_release(info, inode, file);
    module_put(info->videodenc_ops->owner);
    mutex_unlock(&drv_mutex);
    return 0;
}

/**
 * videodenc_write - write to a vdenc device.
 *
 * @return 0 success, else failure.
 */
int videodenc_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    struct inode *inode = file->f_dentry->d_inode;
    int vdencidx = iminor(inode);
    int ret = 0;
    struct videodenc_info *info;

    info = gInstance.registered_videodenc[vdencidx];
    if(info->videodenc_ops->videodenc_write)
        ret = info->videodenc_ops->videodenc_write(info, file, buf, count, pos);

    return ret;
}

/**
 * videodenc_read - read from a vdenc device.
 *
 * @return 0 success, else failure.
 */
int videodenc_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    struct inode *inode = file->f_dentry->d_inode;
    int vdencidx = iminor(inode);
    int ret = 0;
    struct videodenc_info *info;

    info = gInstance.registered_videodenc[vdencidx];
    if(info->videodenc_ops->videodenc_read)
        ret = info->videodenc_ops->videodenc_read(info, file, buf, count, pos);

    return ret;
}

/**
 * videodenc_get_options - get kernel boot parameters
 *
 * @param name - videodenc name as it would appear in
 *         the boot parameter line
 *         (videodenc=<name>:<options>)
 *
 * NOTE: Needed to maintain backwards compatibility
 */
int videodenc_get_options(char *name, char **option)
{
    char *opt, *options = NULL;
    int opt_len, retval = 0;
    int name_len = strlen(name), i;

    if(name_len && !retval)
    {
        for(i = 0; i < VIDEODENC_MAX; i++)
        {
            if(video_options[i] == NULL)
                continue;
            opt_len = strlen(video_options[i]);
            if(!opt_len)
                continue;
            opt = video_options[i];
            if(!strncmp(name, opt, name_len) &&
               opt[name_len] == ':')
                options = opt + name_len + 1;
        }
    }

    if(option)
        *option = options;

    return retval;
}


/**
 *  videodenc_init - init video denc subsystem
 *
 *  Initialize the video denc subsystem.
 */
int __init videodenc_init(void)
{
    int result;

    create_proc_read_entry("vdenc", 0, NULL, videodenc_read_proc, NULL);

    /* Allocate a region of minor numbers under a major number for our GPIO device */
    result = alloc_chrdev_region(&gInstance.devRegion, 0, VIDEODENC_MAX, "vdenc");
    if (result < 0)
    {
        printk(KERN_INFO "%s: alloc_chrdev_region failed %d\n", __FILE__, result);
        return result;
    }
    gInstance.major = MAJOR(gInstance.devRegion);
    gInstance.first_minor = MINOR(gInstance.devRegion);

    videodenc_class = class_create(THIS_MODULE, "videodencs");
    if(IS_ERR(videodenc_class))
    {
        printk(KERN_WARNING "Unable to create video output class; errno = %ld\n", PTR_ERR(videodenc_class));
        videodenc_class = NULL;
    }
    return 0;
}
module_init(videodenc_init);

/**
 *  videodenc_setup - process command line options
 * @param options: string of options
 *
 *  Process command line options for video denc subsystem.
 *
 * @param note This function is a __setup and __init function.
 *        It only stores the options.
 *        Drivers have to call videodenc_get_options()
 *        as necessary.
 *
 * @return Returns zero.
 */
int __init videodenc_setup(char *options)
{
    int i;

    if(!options || !*options)
        return 0;

    for(i = 0; i < VIDEODENC_MAX; i++)
    {
        if(video_options[i] == NULL)
        {
            video_options[i] = options;
            break;
        }
    }

    return 0;
}
__setup("videodenc=", videodenc_setup);

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
