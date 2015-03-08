/* 
 * PNX8550 STB100 AK4705 (SCART switch) driver definitions.
 *
 * Public domain.
 * 
 */

#ifndef __AK4705_H
#define __AK4705_H

#define AK4705_SUSPEND  0
#define AK4705_CVBS_RGB 1
#define AK4705_YUV      2

// sets AK4705 operating mode: suspend, YUV, CVBS+RGB
void ak4705_set_mode(int blank);
// sets the main volume in the AK4705
void ak4705_set_volume(int volume);

#endif
