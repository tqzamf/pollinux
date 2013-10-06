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
    @file   videodenc_modedb.c
    @brief  Providing a Framework for Video Denc modes to be used

@b  Component:  Video Denc.

    This is a framework to make video dencs easier to use.
    It is designed to be similar to the framebuffer framework as this works
    well.

    Based on linux/drivers/video/modedb.c -- Standard video mode database management

    Set your editor for 4 space indentation.
*//*

Rev Date        Author      Comments
--------------------------------------------------------------------------------
  1 20060328    laird       First Revision.
  2 20060628    laird       XCheck Rework.
  3 20060713    laird       Connector Support
  4 20070124    neill       Remove compile time warnings
  5 20070315    neill       Correct initialisation problem
--------------------------------------------------------------------------------
    For consistency and standardisation retain the Section Separators.
*/

/***********************************************
* INCLUDE FILES                                *
************************************************/
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/videodenc.h>
#include <linux/sched.h>

/***********************************************
* LOCAL MACROS                                 *
* recommendation only: <MODULE><_WORD>+        *
************************************************/

/**
 * @brief check if name s matches the name stored in v.
 */
#define name_matches(v, s, l) \
    ((v).name && !strncmp((s), (v).name, (l)) && strlen((v).name) == (l))

/**
 * @brief check if 2 modes match.
 */
#define mode_matches(v, __xres, __yres, __refresh, __scantype, __signal_type) \
    ((v).xres == (__xres) && \
     (v).yres == (__yres) && \
     (v).refresh == (__refresh) && \
     (v).scantype == (__scantype) && \
     (v).output_signal == (__signal_type))

/**
 * @brief check if 2 modes match including bpp value.
 */
#define mode_matches_bpp(v, __xres, __yres, __refresh, __scantype, __signal_type, __bpp) \
    ((mode_matches(v, __xres, __yres, __refresh, __scantype, __signal_type)) && \
     (v).bits_per_pixel == (__bpp))

/**
 * @brief check if 2 modes match including tv_standard value.
 */
#define mode_matches_tv_standard(v, __xres, __yres, __refresh, __scantype, __signal_type, __tv_standard) \
    ((mode_matches(v, __xres, __yres, __refresh, __scantype, __signal_type)) && \
     (v).tv_standard == (__tv_standard))

/**
 * @brief check if 2 modes match including both tv_standard and bpp values.
 */
#define mode_matches_exact(v, __xres, __yres, __refresh, __scantype, __signal_type, __bpp, __tv_standard) \
     ((mode_matches(v, __xres, __yres, __refresh, __scantype, __signal_type)) && \
      (v).bits_per_pixel == (__bpp)) && \
      (v).tv_standard == (__tv_standard)

#ifdef DEBUG
    #define DPRINTK(fmt, args...)   printk(KERN_WARNING "videodenc_modedb %s: " fmt, __FUNCTION__ , ## args)
#else
    #define DPRINTK(fmt, args...)
#endif

/**
 * @brief Return codes when trying to find a mode.
 */
#define SPECIFIED_MODE_NOT_FOUND  0
#define USING_SPECIFIED_VIDEOMODE 1
#define USING_DEFAULT_VIDEOMODE   3

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

/**
 * @brief Standard video denc mode definitions.
 */

#define DEFAULT_MODEDB_INDEX    0

/**
 * @brief standard modedb.
 */
static const struct videodenc_videomode modedb[] =
{
    {
        /* PAL-CVBS*/
        "PAL-CVBS", 50, 720, 576, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_CVBS, VIDEODENC_TV_STANDARD_PAL_BG
    },
    {
        /* PAL-YC*/
        "PAL-YC", 50, 720, 576, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_YC, VIDEODENC_TV_STANDARD_PAL_BG
    },
    {
        /* PAL-M-CVBS*/
        "PAL-M-CVBS", 60, 720, 480, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_CVBS, VIDEODENC_TV_STANDARD_PAL_M
    },
    {
        /* PAL-M-YC*/
        "PAL-M-YC", 60, 720, 480, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_YC, VIDEODENC_TV_STANDARD_PAL_M
    },
    {
        /* NTSC-M-CVBS */
        "NTSC-M-CVBS", 50, 720, 480, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_CVBS, VIDEODENC_TV_STANDARD_NTSC_M
    },
    {
        /* NTSC-M-YC */
        "NTSC-M-YC", 50, 720, 480, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_YC, VIDEODENC_TV_STANDARD_NTSC_M
    },
    {
        /* SECAM */
        "SECAM-CVBS", 50, 720, 576, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_CVBS, VIDEODENC_TV_STANDARD_SECAM
    },
    {
        /* SECAM */
        "SECAM-YC", 50, 720, 576, 32,
        VIDEODENC_SCANTYPE_INTERLACED, VIDEODENC_OUTSIGNAL_YC, VIDEODENC_TV_STANDARD_SECAM
    }
};

/**
 * @brief array of tv standard names, must be in same order as enum.
 * @see videodenc_tv_standard_t.
 */
static const char* videodenc_tv_standard_names[VIDEODENC_TV_STANDARD_MAX_ENTRIES] =
{
    "pal_bg","pal_i","pal_m","pal_n","pal_nc","secam","ntsc_m","ntsc_m_jpn","digital"
};

/**
 * @brief array of output signal names, must be in same order as enum.
 * @see videodenc_output_signal_t.
 */
static const char* videodenc_output_signal_names[VIDEODENC_OUTSIGNAL_MAX_ENTRIES] =
{
    "cvbs","yc","yprpb","rgb","digital"
};

/**************************************************************************
* EXPORTED DATA                     g[k|p|kp|pk|kpk]phStbTemplate_<Word>+ *
***************************************************************************/
/**
 * @brief utility function to convert ascii to int.
 */
static int my_atoi(const char *name)
{
    int val = 0;

    for(;; name++)
    {
        switch(*name)
        {
            case '0'...'9':
                val = 10*val+(*name-'0');
                break;
            default:
                return val;
        }
    }
}

/*******************************************************************************
* FUNCTION IMPLEMENTATION                                                      *
********************************************************************************/

/**
 *  video_denc_try_mode - test a video denc mode
 *
 * @param param var: video denc user defined part of display
 * @param param info: video denc info structure
 * @param param mode: video denc video mode structure
 *
 *  Tries a video mode to test it's validity for device @info.
 *
 * @param param return 0 on success.
 */
int videodenc_try_mode(struct videodenc_var_screeninfo *var, struct videodenc_info *info,
                       const struct videodenc_videomode *mode)
{
    int err = 0;

    DPRINTK("Trying mode %s %dx%d-%d@%d\n", mode->name ? mode->name : "noname",
            mode->xres, mode->yres, mode->bits_per_pixel, mode->refresh);

    var->xres = mode->xres;
    var->yres = mode->yres;
    var->bits_per_pixel = mode->bits_per_pixel;
    var->refresh = mode->refresh;
    var->scantype = mode->scantype;
    var->output_signal = mode->output_signal;
    var->tv_standard = mode->tv_standard;
    /*We do not copy across connectors as mode stores supported connectors where as
      var uses the chosen_connectors*/

    if(info->videodenc_ops->videodenc_check_mode)
    {
        DPRINTK("Checking using %p\n", info->videodenc_ops->videodenc_check_mode);    
        err = info->videodenc_ops->videodenc_check_mode(var, info);
    }
    return err;
}

/**
 *  videodenc_find_mode - finds a valid video_denc mode
 *
 *  @var: video denc user defined part of display
 *  @info: video denc info structure
 *  @mode_option: string video_denc mode to find
 *  @db: videodenc mode database
 *  @dbsize: size of @db
 *  @default_mode: default video denc  mode to fall back to
 *
 *  Finds a suitable video denc mode, starting with the specified mode
 *  in @mode_option with fallback to @default_mode.  If
 *  @default_mode fails, all modes in the video denc mode database will
 *  be tried.
 *
 *  Valid mode specifiers for @mode_option:
 *
 *  <xres>x<yres>[-<bpp>]@<refresh><scantype>:<output_signal>:[<tv_standard>] or
 *  <name>[-<bpp>][@<refresh>]
 *
 *  with <xres>, <yres>, <bpp>, <scantype>, <output_signal>, <tv_standard> and <refresh> decimal numbers and
 *  <name> a string.
 *
 *  NOTE: The passed struct @var is _not_ cleared!  This allows you
 *  to supply values for e.g. the grayscale and accel_flags fields.
 *
 *  Returns zero for failure,
 *  1 if using specified @mode_option,
 *  3 if default mode is used
 *
 */
int videodenc_find_mode(struct videodenc_var_screeninfo *var,
                        struct videodenc_info *info, const char *mode_option,
                        const struct videodenc_videomode *db, unsigned int dbsize,
                        const struct videodenc_videomode *default_mode)
{
    int i, matching_mode = 0;
    int refresh_specified = 0;
    int scantype_specified=0, signal_specified=0, tv_standard_specified = 0, bpp_specified = 0;
    unsigned int xres = 0, yres = 0, bpp = 0, refresh = 0, scantype = 0, signal_type = 0, tv_standard = 0;

    /* Set up defaults */
    if(!db)
    {
        db = modedb;
        dbsize = sizeof(modedb)/sizeof(*modedb);
    }
    if(!default_mode)
    {
        default_mode = &modedb[DEFAULT_MODEDB_INDEX];
    }

    /* Did the user specify a video mode? */
    if(mode_option)
    {
        unsigned int res_bpp_length = 0;
        char* tmp_string = NULL;
        char* res_bpp = NULL;
        char* other_options = NULL;
        char *mode_option_local = mode_option;

        /*Split the string on the @ sign Xxy[-bpp]@hz scantype:signal;[standard]*/
        if(strchr(mode_option_local, '@') != NULL)
        {
            while ((tmp_string = strsep(&mode_option_local, "@")) != NULL)
            {
                if(res_bpp == NULL)
                {
                    res_bpp = tmp_string;
                    res_bpp_length = (mode_option_local - tmp_string) - 1;
                }
                else if(other_options == NULL)
                {
                    other_options = tmp_string;
                }
                else
                {
                    /*Should not get called means badly formed mode_option*/
                    DPRINTK("Badly formed mode_option %s\n", mode_option);
                    return -EINVAL;
                }
            }
            DPRINTK("res_bpp %s use only first %d chars\n", res_bpp, res_bpp_length);
            DPRINTK("other_options %s\n", other_options);

            /*Lets see if we can find a '-' if yes then we have a string in format XxY-bpp
              else we have a string in format XxY.*/
            if(strchr(res_bpp, '-') == NULL)
            {
                /*We got no - so we have only got XxY*/
                sscanf(res_bpp, "%dx%d", &xres, &yres);
            }
            else
            {
                /*We have got s - so we have got bpp as well*/
                sscanf(res_bpp, "%dx%d-%d", &xres, &yres, &bpp);
                bpp_specified = 1;
            }
            DPRINTK("video denc mode %dx%d-%dbpp\n", xres, yres, bpp);

            while ((tmp_string = strsep(&other_options, ":")) != NULL)
            {
                /*other options are in format refresh scantype:signal:[standard]*/
                if(refresh_specified == 0 && scantype_specified == 0)
                {
                    if(*(other_options - 2) == 'i')
                    {
                        scantype = VIDEODENC_SCANTYPE_INTERLACED;
                        scantype_specified = 1;
                    }
                    else if(*(other_options - 2) == 'p')
                    {
                        scantype = VIDEODENC_SCANTYPE_PROGRESSIVE;
                        scantype_specified = 1;
                    }
                    else
                    {
                        scantype_specified = 0;
                    }
                    if(scantype_specified)
                    {
                        refresh = my_atoi(tmp_string);
                        refresh_specified = 1;
                    }
                    DPRINTK("refresh = %d, scantype = %d\n", refresh_specified ? refresh : -1, scantype_specified ? scantype : -1);
                }
                else if(signal_specified == 0)
                {
                    for(i = 0; i < VIDEODENC_OUTSIGNAL_MAX_ENTRIES; i++)
                    {
                        if(!strncmp(tmp_string, videodenc_output_signal_names[i], strlen(videodenc_output_signal_names[i])))
                        {
                            /*Match found*/
                            signal_specified = 1;
                            /*We need to bit shift here to match the enum declaration.*/
                            signal_type = 1 << i;
                            DPRINTK("signal name %s\n", videodenc_output_signal_names[i]);
                            break;
                        }
                    }

                }
                else if(tv_standard_specified == 0)
                {
                    for(i = 0; i < VIDEODENC_TV_STANDARD_MAX_ENTRIES; i++)
                    {
                        if(!strncmp(tmp_string, videodenc_tv_standard_names[i], strlen(videodenc_tv_standard_names[i])))
                        {
                            /*Match found*/
                            tv_standard_specified = 1;
                            /*We need to bit shift here to match the enum declaration.*/
                            tv_standard = 1 << i;
                            DPRINTK("tv_standard name %s\n", videodenc_tv_standard_names[i]);
                            break;
                        }
                    }

                }
            }
            DPRINTK("Finished parsing string options\n");

            /*Do a safety check on what they have specified*/
            if(xres == 0 || \
               yres == 0 || \
               refresh_specified == 0 || \
               scantype_specified == 0 || \
               signal_specified == 0)
            {
                DPRINTK("You have specified an incorrect format string %s should be the format "
                        "Xxy[-bpp]@refresh{i,p}:signal[:tvstandard]\n", mode_option);
                return SPECIFIED_MODE_NOT_FOUND;
            }
        }
    }

    /*the modeoption might just be a name so check that first.*/
    if(mode_option)
    {
        DPRINTK("Trying to use specified mode %s\n", mode_option);
    }
    for(i = 0; i < dbsize; i++)
    {
        DPRINTK("Mode is '%s' vs '%s'\n", db[i].name, mode_option);
        if(name_matches(db[i], mode_option, strlen(mode_option)))
        {
            DPRINTK("Name matches (%s)\n", mode_option);
            /*We have matched on name so use that*/
            matching_mode = 1;
        }
        /*Now we have to find a mode that matches what they specified it will be the first one that works.*/
        else if(!bpp_specified && !tv_standard_specified)
        {
            DPRINTK("if(mode_matches(db[i], xres, yres, refresh, scantype, signal_type))\n");
            if(mode_matches(db[i], xres, yres, refresh, scantype, signal_type))
            {
                matching_mode = 1;
            }
        }
        else if(!bpp_specified)
        {
            DPRINTK("mode_matches_tv_standard(db[i], xres, yres, refresh, scantype, signal_type, tv_standard)\n");
            if(mode_matches_tv_standard(db[i], xres, yres, refresh, scantype, signal_type, tv_standard))
            {
                matching_mode = 1;
            }
        }
        else if(!tv_standard_specified)
        {
            DPRINTK("mode_matches_bpp(db[i], xres, yres, refresh, scantype, signal_type, bpp)\n");
            if(mode_matches_bpp(db[i], xres, yres, refresh, scantype, signal_type, bpp))
            {
                matching_mode = 1;
            }
        }
        else
        {
            DPRINTK("mode_matches_exact(db[i], xres, yres, refresh, scantype, signal_type, bpp, tv_standard)\n");
            /*We have specified everything*/
            if(mode_matches_exact(db[i], xres, yres, refresh, scantype, signal_type, bpp, tv_standard))
            {
                matching_mode = 1;
            }
        }

        /*If we found a matching mode try it*/
        if(matching_mode)
        {
            DPRINTK("Trying selected mode\n");
            if(!videodenc_try_mode(var, info, &db[i]))
            {
               return USING_SPECIFIED_VIDEOMODE;
            }
        }
    }

    DPRINTK("Trying default video mode %s\n", default_mode->name);
    if(!videodenc_try_mode(var, info, default_mode))
    {
        return USING_DEFAULT_VIDEOMODE;
    }

    DPRINTK("No valid mode found\n");
    return SPECIFIED_MODE_NOT_FOUND;
}

/**
 * videodenc_var_to_videomode - convert videodenc_var_screeninfo to videodenc_videomode
 *
 * @param mode: pointer to struct videodenc_videomode
 * @param var: pointer to struct videodenc_var_screeninfo
 */
void videodenc_var_to_videomode(struct videodenc_videomode *mode,
                                struct videodenc_var_screeninfo *var)
{
    mode->name = NULL;
    mode->xres = var->xres;
    mode->yres = var->yres;
    mode->bits_per_pixel = var->bits_per_pixel;
    mode->refresh = var->refresh;
    mode->scantype = var->scantype;
    mode->output_signal = var->output_signal;
    mode->tv_standard = var->tv_standard;
}

/**
 * videodenc_videomode_to_var - convert videodenc_videomode to videodenc_var_screeninfo
 *
 * @param var: pointer to struct videodenc_var_screeninfo
 * @param mode: pointer to struct videodenc_videomode
 */
void videodenc_videomode_to_var(struct videodenc_var_screeninfo *var,
                                struct videodenc_videomode *mode)
{
    var->xres = mode->xres;
    var->yres = mode->yres;
    var->bits_per_pixel = mode->bits_per_pixel;
    var->refresh = mode->refresh;
    var->scantype = mode->scantype;
    var->output_signal = mode->output_signal;
    var->tv_standard = mode->tv_standard;
}

/**
 * videodenc_mode_is_equal - compare 2 videomodes
 * @param mode1: first videomode
 * @param mode2: second videomode
 *
 * RETURNS:
 * 1 if equal, 0 if not
 */
int videodenc_mode_is_equal(struct videodenc_videomode *mode1,
                            struct videodenc_videomode *mode2)
{
    return(mode1->refresh          == mode2->refresh &&
           mode1->xres             == mode2->xres &&
           mode1->yres             == mode2->yres &&
           mode1->bits_per_pixel   == mode2->bits_per_pixel &&
           mode1->scantype         == mode2->scantype &&
           mode1->output_signal    == mode2->output_signal &&
           mode1->tv_standard      == mode2->tv_standard);
}

EXPORT_SYMBOL(videodenc_videomode_to_var);
EXPORT_SYMBOL(videodenc_var_to_videomode);
EXPORT_SYMBOL(videodenc_mode_is_equal);
EXPORT_SYMBOL(videodenc_find_mode);
