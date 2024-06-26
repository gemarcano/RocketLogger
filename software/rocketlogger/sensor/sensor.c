/**
 * Copyright (c) 2016-2020, ETH Zurich, Computer Engineering Group
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../log.h"

#include "bme280.h"
#include "tsl4531.h"

#include "sensor.h"

/// I2C sensor bus identifier for communication
int sensor_bus = -1;

/**
 * The sensor registry structure.
 *
 * Register your sensor (channels) here. Multiple channels from the same
 * sensor should be added as consecutive entries.
 *
 * @note The SENSOR_REGISTRY_SIZE needs to be adjusted accordingly.
 */
const rl_sensor_t SENSOR_REGISTRY[SENSOR_REGISTRY_SIZE] = {
    {
        "TSL4531_left",
        TSL4531_I2C_ADDRESS_LEFT,
        TSL4531_CHANNEL_DEFAULT,
        RL_UNIT_LUX,
        RL_SCALE_UNIT,
        &tsl4531_init,
        &tsl4531_deinit,
        &tsl4531_read,
        &tsl4531_get_value,
    },
    {
        "TSL4531_right",
        TSL4531_I2C_ADDRESS_RIGHT,
        TSL4531_CHANNEL_DEFAULT,
        RL_UNIT_LUX,
        RL_SCALE_UNIT,
        &tsl4531_init,
        &tsl4531_deinit,
        &tsl4531_read,
        &tsl4531_get_value,
    },
    {
        "BME280_temp",
        BME280_I2C_ADDRESS_LEFT,
        BME280_CHANNEL_TEMPERATURE,
        RL_UNIT_DEG_C,
        RL_SCALE_MILLI,
        &bme280_init,
        &bme280_deinit,
        &bme280_read,
        &bme280_get_value,
    },
    {
        "BME280_rh",
        BME280_I2C_ADDRESS_LEFT,
        BME280_CHANNEL_HUMIDITY,
        RL_UNIT_INTEGER,
        RL_SCALE_MICRO,
        &bme280_init,
        &bme280_deinit,
        &bme280_read,
        &bme280_get_value,
    },
    {
        "BME280_press",
        BME280_I2C_ADDRESS_LEFT,
        BME280_CHANNEL_PRESSURE,
        RL_UNIT_PASCAL,
        RL_SCALE_MILLI,
        &bme280_init,
        &bme280_deinit,
        &bme280_read,
        &bme280_get_value,
    },
};

int sensors_init(void) {
    sensor_bus = sensors_open_bus();
    return sensor_bus;
}

void sensors_deinit(void) {
    sensors_close_bus(sensor_bus);
    int sensor_bus = -1;
    (void)sensor_bus; // suppress unused parameter warning
}

int sensors_open_bus(void) {
    int bus = open(I2C_BUS_FILENAME, O_RDWR);
    if (bus < 0) {
        rl_log(RL_LOG_ERROR, "failed to open the I2C bus; %d message: %s", bus,
               errno, strerror(errno));
    }
    return bus;
}

int sensors_close_bus(int bus) {
    int result = close(bus);
    if (result < 0) {
        rl_log(RL_LOG_ERROR, "failed to close the I2C bus; %d message: %s", bus,
               errno, strerror(errno));
    }
    return result;
}

int sensors_get_bus(void) { return sensor_bus; }

int sensors_init_comm(uint8_t device_address) {
    return ioctl(sensor_bus, I2C_SLAVE, device_address);
}

int sensors_scan(bool sensor_available[SENSOR_REGISTRY_SIZE]) {

    // log message
    char message[MAX_MESSAGE_LENGTH] =
        "List of available ambient sensors:\n\t- ";

    // Scan for available sensors //
    int sensor_count = 0;
    int multi_channel_initialized = -1;
    for (int i = 0; i < SENSOR_REGISTRY_SIZE; i++) {
        // do not initialize multi channel sensors more than once
        int result = SUCCESS;
        if (SENSOR_REGISTRY[i].identifier != multi_channel_initialized) {
            result = SENSOR_REGISTRY[i].init(SENSOR_REGISTRY[i].identifier);
        }

        if (result == SUCCESS) {
            // sensor available
            sensor_available[i] = true;
            multi_channel_initialized = SENSOR_REGISTRY[i].identifier;
            sensor_count++;

            // message
            strncat(message, SENSOR_REGISTRY[i].name, sizeof(message) - 1);
            strncat(message, "\n\t- ", sizeof(message) - 1);
        } else {
            // sensor not available
            sensor_available[i] = false;
            multi_channel_initialized = -1;
        }
    }

    // message & return
    if (sensor_count > 0) {
        message[strlen(message) - 3] = 0;
        rl_log(RL_LOG_INFO, "%s", message);
    } else {
        rl_log(RL_LOG_WARNING, "no ambient sensor found.");
    }
    return sensor_count;
}

int sensors_read(int32_t *const sensor_data,
                 bool const sensor_available[SENSOR_REGISTRY_SIZE]) {
    int sensor_count = 0;
    int multi_channel_read = -1;
    for (int i = 0; i < SENSOR_REGISTRY_SIZE; i++) {
        // only read registered sensors
        if (sensor_available[i]) {
            // read multi-channel sensor data only once
            if (SENSOR_REGISTRY[i].identifier != multi_channel_read) {
                int res =
                    SENSOR_REGISTRY[i].read(SENSOR_REGISTRY[i].identifier);
                if (res < 0) {
                    return res;
                }
                multi_channel_read = SENSOR_REGISTRY[i].identifier;
            }
            sensor_data[sensor_count] = SENSOR_REGISTRY[i].get_value(
                SENSOR_REGISTRY[i].identifier, SENSOR_REGISTRY[i].channel);
            sensor_count++;
        }
    }
    return sensor_count;
}

void sensors_close(bool const sensor_available[SENSOR_REGISTRY_SIZE]) {
    for (int i = 0; i < SENSOR_REGISTRY_SIZE; i++) {
        if (sensor_available[i]) {
            SENSOR_REGISTRY[i].deinit(SENSOR_REGISTRY[i].identifier);
        }
    }
}
