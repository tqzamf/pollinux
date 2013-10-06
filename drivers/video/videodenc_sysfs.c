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
    @file   videodenc_sysfs.c
    @brief  Providing a Framework for Video Denc modes to be used

@b  Component:  Video Denc.

    This is a framework to make video dencs easier to use.
    It is designed to be similar to the framebuffer framework as this works
    well.

    Based on linux/drivers/video/videodenc_sysfs.c -- Standard video mode database management

    Set your editor for 4 space indentation.
*//*

Rev Date        Author      Comments
--------------------------------------------------------------------------------
  1 20060328    laird       First Revision.
  2 20070124    neill       Remove compile time warnings
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/

/***********************************************
* INCLUDE FILES                                *
************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#include <linux/videodenc.h>
#include <linux/slab.h>

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

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

/*******************************************************************************
* FUNCTION IMPLEMENTATION                                                      *
********************************************************************************/

/**
 * videodenc_alloc - creates a new video denc info structure
 *
 * @param size: size of driver private data, can be zero
 * @param dev: pointer to the device for this vdenc, this can be NULL
 *
 * Creates a new video denc info structure.
 * Also reserves @size bytes for driver private data
 * (info->par). info->par (if any) will be aligned to
 * sizeof(long).
 *
 * @return New structure
 *         NULL if an error occured.
 */
struct videodenc_info *videodenc_alloc(size_t size, struct device *dev)
{
#define BYTES_PER_LONG (BITS_PER_LONG/8)
#define PADDING (BYTES_PER_LONG - (sizeof(struct videodenc_info) % BYTES_PER_LONG))
	int videodenc_info_size = sizeof(struct videodenc_info);
	struct videodenc_info *info;
	char *p;

	if (size)
		videodenc_info_size += PADDING;

	p = kmalloc(videodenc_info_size + size, GFP_KERNEL);
	if (!p)
		return NULL;
	memset(p, 0, videodenc_info_size + size);
	info = (struct videodenc_info *) p;

	if (size)
		info->par = p + videodenc_info_size;

	info->device = dev;

	return info;
#undef PADDING
#undef BYTES_PER_LONG
}

/**
 * videodenc_release_mem - marks the structure available for freeing
 *
 * @param info: video denc buffer info structure
 *
 * Drop the reference count of the class_device embedded in the
 * framebuffer info structure.
 *
 */
void videodenc_release_mem(struct videodenc_info *info)
{
	kfree(info);
}

/**
 * videodenc_register_sysfs_entries - Register the common video denc sysfs entries.
 *
 * @param dev - The device we are registering
 * @param sysfs_attrs - Structure containing the sysfs entries created with DEVICE_ATTR.
 *
 * @see DEVICE_ATTR
 */
void videodenc_register_sysfs_entries(struct device *dev,
                                      struct videodenc_sysfs_attrs *sysfs_attrs)
{
    int ret;
	ret = device_create_file(dev, &sysfs_attrs->attr_revision);
	ret = device_create_file(dev, &sysfs_attrs->attr_vendor);
    ret = device_create_file(dev, &sysfs_attrs->attr_device_name);
}

/**
 * videodenc_unregister_sysfs_entries - Remove the common video denc sysfs entries.
 *
 * @param dev - The device we are registering
 * @param sysfs_attrs - Structure containing the sysfs entries created with DEVICE_ATTR.
 *
 * @see DEVICE_ATTR
 */
void videodenc_unregister_sysfs_entries(struct device *dev,
                                        struct videodenc_sysfs_attrs *sysfs_attrs)
{
	device_remove_file(dev, &sysfs_attrs->attr_revision);
	device_remove_file(dev, &sysfs_attrs->attr_vendor);
    device_remove_file(dev, &sysfs_attrs->attr_device_name);
}

EXPORT_SYMBOL(videodenc_register_sysfs_entries);
EXPORT_SYMBOL(videodenc_unregister_sysfs_entries);
EXPORT_SYMBOL(videodenc_release_mem);
EXPORT_SYMBOL(videodenc_alloc);
