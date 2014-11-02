/* 
 * PNX8550 STB100 AK4705 (SCART switch) driver definitions.
 *
 * Public domain.
 * 
 */

#ifndef __AK4705_H
#define __AK4705_H

// suspends AK4705, ie. disables all video outputs
void ak4705_suspend(void);
// resumes AK4705, ie. enables all video outputs
void ak4705_resume(void);
// sets the main volume in the AK4705
void ak4705_set_volume(int volume);

#endif
