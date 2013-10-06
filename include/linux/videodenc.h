#ifndef _LINUX_VIDEODENC_H
#define _LINUX_VIDEODENC_H

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
    @file   videodenc.h
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
  2 20060628    laird       XCheck rework.
  3 20060711    laird       Adding connectors support.
  4 20060724    laird       Adding a frequency enum and ability to get mode database.
  5 20060905    laird       Porting to 2.6.17.7
  6 20070124    neill       Porting to 2.6.19.1
  7 20070723    neill       Add gamma correction adjustment
  8 20070725    neill       Add read/write file operations
  9 20071107    neill       Modify macrovision values
 10 20071122    neill       Add CGMS support
 11 20080107    neill       Add WSS support
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/

/*******************
* INCLUDE FILES    *
********************/
#include <asm/types.h>

#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**************************************************************************
* EXPORTED MACROS                          PH_STBTEMPLATE_<_WORD>+        *
***************************************************************************/
/**
 * @brief The major device number for the VideoDenc
 */
#define VIDEODENC_MAJOR        234

/**
 * @brief The maximum number of video dencs.
 */
#define VIDEODENC_MAX          8

/**
 * @brief The maximum number of connectors on a video dencs.
 */
#define VIDEODENC_CONNECTORS_MAX          4

/**
 * @brief The different types of DENCS that are available.
 */
#define VIDEODENC_TYPE_SD      0
#define VIDEODENC_TYPE_HD      1
#define VIDEODENC_TYPE_COMBI   2
#define VIDEODENC_TYPE_HDMI    3

/**
 * @brief The Different Capabilities of the Video Dencs
 */
#define VIDEODENC_FEATURE_ASPECT_RATIO           1 << 0
#define VIDEODENC_FEATURE_WIDE_SCREEN_SIGNALLING 1 << 1
#define VIDEODENC_FEATURE_TELETEXT_INSERTION     1 << 2
#define VIDEODENC_FEATURE_CLOSE_CAPTIONING       1 << 3
#define VIDEODENC_FEATURE_VPS                    1 << 4
#define VIDEODENC_FEATURE_INTERLACED             1 << 5
#define VIDEODENC_FEATURE_PROGRESSIVE            1 << 6
#define VIDEODENC_FEATURE_RGB_GAIN               1 << 7
#define VIDEODENC_FEATURE_MACROVISION            1 << 8
#define VIDEODENC_FEATURE_TESTPATTERN            1 << 9
#define VIDEODENC_FEATURE_CGMS                   1 << 10

/**
 * @brief Signal is interlaced/progressive.
 */
typedef enum _videodenc_scantype_t
{
    VIDEODENC_SCANTYPE_INTERLACED = 0,
    VIDEODENC_SCANTYPE_PROGRESSIVE = 1
} videodenc_scantype_t;

/**
 * @brief Signal output type i.e is it CVBS etc.
 */
typedef enum _videodenc_output_signal_t
{
    VIDEODENC_OUTSIGNAL_CVBS       = 1 << 0,
    VIDEODENC_OUTSIGNAL_YC         = 1 << 1,
    VIDEODENC_OUTSIGNAL_YPRPB      = 1 << 2,
    VIDEODENC_OUTSIGNAL_RGB        = 1 << 3,
    VIDEODENC_OUTSIGNAL_DIGITAL    = 1 << 4,
    VIDEODENC_OUTSIGNAL_MAX_ENTRIES = 1 << 5
} videodenc_output_signal_t;

/**
 * @brief All of the TV_STANDARDS that can be supported.
 */
typedef enum _videodenc_tv_standard_t
{
    VIDEODENC_TV_STANDARD_PAL_BG     = 1 << 0,
    VIDEODENC_TV_STANDARD_PAL_I      = 1 << 1,
    VIDEODENC_TV_STANDARD_PAL_N      = 1 << 2,
    VIDEODENC_TV_STANDARD_PAL_M      = 1 << 3,
    VIDEODENC_TV_STANDARD_PAL_NC     = 1 << 4,
    VIDEODENC_TV_STANDARD_SECAM      = 1 << 5,
    VIDEODENC_TV_STANDARD_NTSC_M     = 1 << 6,
    VIDEODENC_TV_STANDARD_NTSC_M_JPN = 1 << 7,
    VIDEODENC_TV_STANDARD_DIGITAL    = 1 << 8,
    VIDEODENC_TV_STANDARD_MAX_ENTRIES = 1 << 9
} videodenc_tv_standard_t;

/**
 * @brief All of the macrovision types that can be supported.
 */
typedef enum _videodenc_macrovision_type_t
{
    VIDEODENC_MACROVISION_TYPE_DISABLE = 0, /* Disable Macrovision. */
    VIDEODENC_MACROVISION_TYPE_2_LINE  = 1, /* 2 Line colour stripe + AGC */
    VIDEODENC_MACROVISION_TYPE_4_LINE  = 2, /* 4 Line colour stripe + AGC */
    VIDEODENC_MACROVISION_TYPE_AGC     = 3, /* AGC pulse normal/static mode select */
} videodenc_macrovision_type_t;

/**
 * @brief All of the CGMS types that can be supported.
 */
typedef enum _videodenc_cgms_type_t
{
    VIDEODENC_CGMS_DISABLE              = 0, /* Disable CGMS. */
    VIDEODENC_CGMS_NO_COPY_RESTRICTION  = 1, /* No copy restriction */
    VIDEODENC_CGMS_COPY_NO_MORE         = 2, /* No more copying allowed */
    VIDEODENC_CGMS_COPY_ONCE_ALLOWED    = 3, /* Copy once */
    VIDEODENC_CGMS_NO_COPYING_PERMITTED = 4, /* No copying */
} videodenc_cgms_type_t;

/**
 * @brief Different output connectors that may be supported by a video denc.
 */
typedef enum _videodenc_outputconnector_type_t
{
    VIDEODENC_CONNECTOR_NONE       = 1 << 0, // No output.
    VIDEODENC_CONNECTOR_SINGLE_RCA = 1 << 1, // Typically a CVBS only output.
    VIDEODENC_CONNECTOR_SCART      = 1 << 2, // A Scart connector
    VIDEODENC_CONNECTOR_MINI_DIN   = 1 << 3, // S-Video Connector
    VIDEODENC_CONNECTOR_TRIPLE_RCA = 1 << 4, // Typically used for YPrPb
    VIDEODENC_CONNECTOR_VGA        = 1 << 5, // VGA
    VIDEODENC_CONNECTOR_DVI        = 1 << 6, // DVI
    VIDEODENC_CONNECTOR_HDMI       = 1 << 7, // HDMI
    VIDEODENC_CONNECTOR_LVDS       = 1 << 8  // LVDS connector.
} videodenc_outputconnector_type_t;


/*
 * Frequency of output signal.
 */
typedef enum _videodenc_refresh_rate_t {
     VIDEODENC_REFRESH_RATE_UNKNOWN        = 1 << 0, /* Unknown Frequency */
     VIDEODENC_REFRESH_RATE_25HZ           = 1 << 1, /* 25 Hz Output. */
     VIDEODENC_REFRESH_RATE_29_97HZ        = 1 << 2, /* 29.97 Hz Output. */
     VIDEODENC_REFRESH_RATE_50HZ           = 1 << 3, /* 50 Hz Output. */
     VIDEODENC_REFRESH_RATE_59_94HZ        = 1 << 4, /* 59.94 Hz Output. */
     VIDEODENC_REFRESH_RATE_60HZ           = 1 << 5, /* 60 Hz Output. */
     VIDEODENC_REFRESH_RATE_75HZ           = 1 << 6 /* 75 Hz Output. */
} videodenc_refresh_rate_t;

/*
 * WSS output aspect ratio
 */
typedef enum _videodenc_wss_aspect_type_t {
     VIDEODENC_ASPECT_RATIO_FF_4TO3        = 1 << 0, /* Full Format 4:3           */
     VIDEODENC_ASPECT_RATIO_LB_14TO9_CTR   = 1 << 1, /* Letterbox 14:9 Center     */
     VIDEODENC_ASPECT_RATIO_LB_14TO9_TOP   = 1 << 2, /* Letterbox 14:9 Top        */
     VIDEODENC_ASPECT_RATIO_LB_16TO9_CTR   = 1 << 3, /* Letterbox 16:9 Center     */
     VIDEODENC_ASPECT_RATIO_LB_16TO9_TOP   = 1 << 4, /* Letterbox 16:9 Top        */
     VIDEODENC_ASPECT_RATIO_LB_P16TO9_CTR  = 1 << 5, /* Letterbox > 16:9 Center   */
     VIDEODENC_ASPECT_RATIO_FF_14TO9_CTR   = 1 << 6, /* Full Format 14:9 Center shoot and protect 14:9 */
     VIDEODENC_ASPECT_RATIO_FF_16TO9       = 1 << 7  /* Full Format 16:9 */
} videodenc_wss_aspect_type_t;

/*
 * WSS output mode
 */
typedef enum _videodenc_wss_mode_type_t {
     VIDEODENC_MODE_CAMERA  = 1 << 0, /* Camera Mode */
     VIDEODENC_MODE_FILM    = 1 << 1, /* Film Mode   */
} videodenc_wss_mode_type_t;

/*
 * WSS subtitle type
 */
typedef enum _videodenc_wss_subtitle_type_t {
     VIDEODENC_SBT_TYPE_NO_OPEN       = 1 << 0, /* No open subtitles */
     VIDEODENC_SBT_TYPE_INSIDE_IMAGE  = 1 << 1, /* Subtitles in active image area  */
     VIDEODENC_SBT_TYPE_OUTSIDE_IMAGE = 1 << 2  /* Subtitles out of active image area   */
} videodenc_wss_subtitle_type_t;

/**
 * @brief IOCTL handler IDS for Video denc, 0x56 is 'V'.
 */
#define VIDEODENC_IOGET_VSCREENINFO 0x5600
#define VIDEODENC_IOGET_FSCREENINFO 0x5601
#define VIDEODENC_IOSET_MODE        0x5602
#define VIDEODENC_IOGET_MODES       0x5603
#define VIDEODENC_IOBLANK           0x5604
#define VIDEODENC_IOTEST_PATTERN    0x5605
#define VIDEODENC_IOGET_NUM_MODES   0x5606
#define VIDEODENC_IOSET_GAMMA       0x5607

/**
 * @brief Whether you actually want to set or not .
 * Can use this so that you can check if a group
 * of setting is valid form user space.
 */
#define VIDEODENC_ACTIVATE_NOW      0
#define VIDEODENC_ACTIVATE_TEST     1

/*********************************************************************
* EXPORTED TYPEDEFS                      [p]phStbTemplate_<Word>+_t  *
**********************************************************************/

/**
 * @brief  A supported video connector.
 */
struct videodenc_outputconnector
{
    __u32 in_use;
    videodenc_outputconnector_type_t type;   /* Type i.e is it scart or DIN*/
    __u32 connector_id;                      /* ID allows you to have SCART 0 / 1 etc */
};

/**
 * @brief  Wide Screen signalling information
 */
struct videodenc_wss_type_t
{
    videodenc_wss_aspect_type_t aspect;
    videodenc_wss_mode_type_t mode;
    bool subtitles_enabled;
    videodenc_wss_subtitle_type_t subtitle_type;
    bool motion_adaptive_colour;
    bool helper;
    bool surround_sound;
    bool copyright;
    bool copy_restricted;
    bool enable;
};
/**
 * @bried The structure that indicates what things to chnage.
 */
struct videodenc_var_screeninfo
{
    __u32                            activate;        /* see VIDEODENC_ACTIVATE_*       */

    __u32                            xres;            /* visible resolution       */
    __u32                            yres;

    __u8                             bits_per_pixel;  /* Bits per Pixel*/
    videodenc_refresh_rate_t         refresh;         /* Refresh rate in hz*/
    __u32                            subcarrier_freq; /* Sub Carrier Frequency in hz*/

    videodenc_scantype_t             scantype;        /* Chosen Scantype*/
    videodenc_output_signal_t        output_signal;   /* Chosen output signal*/
    videodenc_tv_standard_t          tv_standard;     /* Chosen TV Output Signal Type.*/
    videodenc_macrovision_type_t     macro_type;      /* Chosen Type of Macrovision Support.*/
    videodenc_cgms_type_t            cgms_type;       /* Chosen Type of CGMS Support.*/
    struct videodenc_wss_type_t      wss_type;        /* Chosen Type of WSS Support.*/
    struct videodenc_outputconnector chosen_connectors[VIDEODENC_CONNECTORS_MAX]; /* The chosen output connectors to use.*/

    __u32 reserved[5];      /* Reserved for future compatibility */
};

/**
 * Fixed screen information that never changes.
 */
struct videodenc_fix_screeninfo
{
    char id[32];            /* identification string eg "PNX8330 SD Denc" */
    __u32 type;             /* see VIDEODENC_TYPE_* */
    __u32 features;         /* Supported Video Denc Features see  VIDEODENC_FEATURE_* */
    struct videodenc_outputconnector all_connectors[VIDEODENC_CONNECTORS_MAX]; /* Supported output connectors,
                                                                                  may not be valid on all signals but at least one.*/
    videodenc_output_signal_t sout_signals; /* The supported output signals */
    __u16 reserved[3];      /* Reserved for future compatibility */
};

/**
 * @brief A video mode structure for the mode database.
 */
struct videodenc_videomode {
    const char *name;   /* OPTIONAL */
    __u32 refresh;
    __u32 xres;
    __u32 yres;
    __u8  bits_per_pixel; /* OPTIONAL */
    videodenc_scantype_t      scantype; /* Chosen Scantype*/
    videodenc_output_signal_t output_signal; /* Chosen output signal*/
    videodenc_tv_standard_t   tv_standard; /* Chosen TV Output Signal Type. OPTIONAL*/
    struct videodenc_outputconnector supported_connectors[VIDEODENC_CONNECTORS_MAX]; /* Supported connectors */
};

#ifdef __KERNEL__

/**
 * @brief Video denc Information Structure.
 */
struct videodenc_info
{
    int node; /* The node */
    int renderer; /*The renderer to which we are associated*/
    struct videodenc_var_screeninfo var;   /* Current var */
    struct videodenc_fix_screeninfo fix;   /* Current fix */
    struct videodenc_ops *videodenc_ops;   /* Function pointers */
    struct device *device;
    struct cdev   *char_device;
    struct class_device *class_device; /* sysfs per device attrs */

    /* From here on everything is device dependent */
    void *par;
};

/*
 * @brief Video Denc operations.
 */
struct videodenc_ops
{
    /* open/release and usage marking */
    struct module *owner;

    int (*videodenc_open)(struct videodenc_info *info, struct inode *inode, struct file *file);
    int (*videodenc_release)(struct videodenc_info *info, struct inode *inode, struct file *file);
    int (*videodenc_write)(struct videodenc_info *info, struct file *file, const char __user *buf, size_t count, loff_t *pos);
    int (*videodenc_read)(struct videodenc_info *info, struct file *file, char __user *buf, size_t count, loff_t *pos);

    /* checks var return negative number on failure, 1 needs changing, 0 no need to update settings*/
    int (*videodenc_check_mode)(struct videodenc_var_screeninfo *var, struct videodenc_info *info);

    /* set the video mode according to info->var */
    int (*videodenc_set_mode)(struct videodenc_info *info);

    /* get the number of video mode in the database.*/
    int (*videodenc_get_number_modes)(int *size);

    /* get the video mode database pointer to first video mode and size of database.*/
    int (*videodenc_get_modes)(struct videodenc_videomode **ppVideoMode, int *size);

    /* set gamma correction values */
    int (*videodenc_gamma_set)(struct videodenc_info *info, unsigned int *gamma);

    /* blank display */
    int (*videodenc_blank)(struct videodenc_info *info, int blank);

    /* Enable / Disable test pattern */
    int (*videodenc_test_pattern)(struct videodenc_info *info, int test_pattern);

    /* perform vdenc specific ioctl (optional) */
    int (*videodenc_ioctl)(struct inode *inode, struct file *file, unsigned int cmd,
                           unsigned long arg, struct videodenc_info *info);
};

/**
 * @brief fucntions that can be called from sysfs
 */
struct videodenc_sysfs_attrs
{
    struct device_attribute attr_revision;
    struct device_attribute attr_vendor;
    struct device_attribute attr_device_name;
};

/**************************************************************************
* EXPORTED DATA                     g[k|p|kp|pk|kpk]phStbTemplate_<Word>+ *
***************************************************************************/

/****************************************************************************
* EXPORTED FUNCTION PROTOTYPES                    phStbTemplate[_<Word>+]   *
*****************************************************************************/
/* drivers/video/videodenc.c */
extern int videodenc_register_driver(char* name,
                                     int device_id,
                                     struct device_driver *device_info,
                                     struct platform_device *platform_device_info,
                                     int    (*probe_function)   (struct device * dev),
                                     int    (*remove_function)  (struct device * dev));

extern void videodenc_unregister_driver(struct device_driver *device_driver_info);

extern int register_videodenc(struct videodenc_info *videodenc_info,
                              int renderer);

extern int unregister_videodenc(struct videodenc_info *videodenc_info);

extern int videodenc_get_options(char *name,
                                  char **option);

/* drivers/video/videodenc_sysfs.c */
extern struct videodenc_info *videodenc_alloc(size_t size, struct device *dev);
extern void videodenc_release_mem(struct videodenc_info *info);

extern void videodenc_register_sysfs_entries(struct device *dev,
                                             struct videodenc_sysfs_attrs *sysfs_attrs);

extern void videodenc_unregister_sysfs_entries(struct device *dev,
                                               struct videodenc_sysfs_attrs *sysfs_attrs);

/* drivers/video/videodenc_modedb.c */
extern void videodenc_var_to_videomode(struct videodenc_videomode *mode,
                                       struct videodenc_var_screeninfo *var);

extern void videodenc_videomode_to_var(struct videodenc_var_screeninfo *var,
                                        struct videodenc_videomode *mode);

extern int videodenc_mode_is_equal(struct videodenc_videomode *mode1,
                                    struct videodenc_videomode *mode2);

extern int videodenc_find_mode(struct videodenc_var_screeninfo *var,
                                struct videodenc_info *info,
                                const char *mode_option,
                                const struct videodenc_videomode *db,
                                unsigned int dbsize,
                                const struct videodenc_videomode *default_mode);
#endif

/******************************************************************************/

/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif /* _LINUX_VIDEODENC_H      Do not add any thing below this line */
