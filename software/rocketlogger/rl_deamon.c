/**
 * Copyright (c) 2016-2019, Swiss Federal Institute of Technology (ETH Zurich)
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

#define LOG_FILE "/var/www/rocketlogger/log/daemon.log"

#include <stdlib.h>

#include <unistd.h>

#include "gpio.h"
#include "log.h"

/// Minimal time interval between two interrupts (in seconds)
#define RL_DAEMON_MIN_INTERVAL 1

/**
 * Setup power GPIO and power of RocketLogger cape
 *
 * @return {@link SUCCESS} on success, {@link FAILURE} otherwise
 */
int power_init(void) {
    int ret1 = gpio_init(GPIO_POWER, GPIO_MODE_OUT);
    int ret2 = gpio_set_value(GPIO_POWER, 1);

    if (ret1 == FAILURE || ret2 == FAILURE) {
        return FAILURE;
    }
    return SUCCESS;
}

/**
 * Deinitialize power GPIO
 *
 * @return {@link SUCCESS} on success, {@link FAILURE} otherwise
 */
int power_deinit(void) {
    int ret1 = gpio_set_value(GPIO_POWER, 0);
    int ret2 = gpio_deinit(GPIO_POWER);

    if (ret1 == FAILURE || ret2 == FAILURE) {
        return FAILURE;
    }
    return SUCCESS;
}

/**
 * Setup button GPIO and enable interrup
 *
 * @return {@link SUCCESS} on success, {@link FAILURE} otherwise
 */
int button_init(void) {
    int ret1 = gpio_init(GPIO_BUTTON, GPIO_MODE_IN);
    int ret2 = gpio_interrupt(GPIO_BUTTON, GPIO_INT_FALLING);

    if (ret1 == FAILURE || ret2 == FAILURE) {
        return FAILURE;
    }
    return SUCCESS;
}

/**
 * Deinitialize button GPIO
 *
 * @return {@link SUCCESS} on success, {@link FAILURE} otherwise
 */
int button_deinit(void) {
    int ret1 = gpio_interrupt(GPIO_BUTTON, GPIO_INT_NONE);
    int ret2 = gpio_deinit(GPIO_BUTTON);

    if (ret1 == FAILURE || ret2 == FAILURE) {
        return FAILURE;
    }
    return SUCCESS;
}

/**
 * GPIO interrupt handler
 *
 * @param value GPIO value after interrupt
 */
void button_interrupt_handler(int value) {
    if (value == 0) { // only react if button pressed enough long

        // get RL status
        int status = system("rocketlogger status > /dev/null");

        if (status > 0) {
            system("rocketlogger stop > /dev/null");
        } else {
            system("rocketlogger cont > /dev/null");
        }

        // interrupt rate control
        sleep(RL_DAEMON_MIN_INTERVAL);
    }
}

/**
 * RocketLogger deamon program. Continuously waits on interrupt on button GPIO
 * and starts/stops RocketLogger
 *
 * Arguments: none
 * @return standard Linux return codes
 */
int main(void) {
    // reset all GPIOs to known reset state
    gpio_reset(GPIO_POWER);
    gpio_reset(GPIO_BUTTON);
    gpio_reset(GPIO_FHR1);
    gpio_reset(GPIO_FHR2);
    gpio_reset(GPIO_LED_STATUS);
    gpio_reset(GPIO_LED_ERROR);

    sleep(1);

    if (power_init() == FAILURE) {
        rl_log(ERROR, "Failed powering up cape.");
        exit(EXIT_FAILURE);
    }
    if (button_init() == FAILURE) {
        rl_log(ERROR, "Failed configuring button.");
        exit(EXIT_FAILURE);
    }

    rl_log(INFO, "RocketLogger daemon started.");

    while (1) {
        // wait for interrupt with infinite timeout (-1)
        int val = gpio_wait_interrupt(GPIO_BUTTON, GPIO_INT_TIMEOUT_INF);
        button_interrupt_handler(val);
    }

    rl_log(INFO, "RocketLogger daemon stopping...");

    button_deinit();
    power_deinit();

    exit(EXIT_SUCCESS);
}
