/*
 *  STB810 I2C-bus definitions
 *
 *  Public domain.
 *
 */

#ifndef __PNX8550_I2C_H
#define __PNX8550_I2C_H

typedef void (* i2c_ip0105_callback)(struct device *device,
	unsigned char *data, unsigned int len);
int ip0105_set_slave_callback(struct i2c_adapter *i2c_adap, unsigned int addr,
	struct device *device, i2c_ip0105_callback callback);

#define PNX8550_I2C_IP3203_BUS0 0
#define PNX8550_I2C_IP3203_BUS1 1
#define PNX8550_I2C_IP0105_BUS0 2
#define PNX8550_I2C_IP0105_BUS1 3

#endif
