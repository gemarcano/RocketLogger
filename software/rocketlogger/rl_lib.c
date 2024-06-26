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
#include <string.h>

#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "rl.h"
#include "rl_hw.h"
#include "rl_socket.h"
#include "util.h"

#include "rl_lib.h"

/**
 * Signal handler to catch stop signals.
 *
 * @todo allow for forced user interrupt (ctrl+C)
 *
 * @param signal_number The number of the signal to handle
 */
static void rl_signal_handler(int signal_number);

bool rl_is_sampling(void) {
    rl_status_t status;
    rl_status_reset(&status);

    int res = rl_get_status(&status);
    if (res < 0) {
        // if shared memory does not exists, RocketLogger is not sampling
        if (errno == ENOENT) {
            return false;
        }
        rl_log(RL_LOG_ERROR,
               "failed getting status to check sampling state; %d message: %s",
               errno, strerror(errno));
    }

    return status.sampling;
}

int rl_get_status(rl_status_t *const status) {
    // get the status from the shared memory
    int res = rl_status_read(status);
    if (res < 0) {
        // on error return reset value
        rl_status_reset(status);
        return res;
    }

    // get file system state
    int64_t disk_free = fs_space_free(RL_CONFIG_FILE_DIR_DEFAULT);
    int64_t disk_total = fs_space_total(RL_CONFIG_FILE_DIR_DEFAULT);

    status->disk_free = disk_free;
    if (disk_total > 0) {
        status->disk_free_permille = (1000 * disk_free) / disk_total;
    } else {
        status->disk_free_permille = 0;
    }

    return res;
}

int rl_run(rl_config_t *const config) {
    if (rl_is_sampling()) {
        rl_log(RL_LOG_ERROR,
               "cannot run measurement, RocketLogger is already running.");
        return ERROR;
    }

    // register signal handler for SIGTERM and SIGINT (for stopping)
    struct sigaction sigterm_action_backup;
    struct sigaction sigint_action_backup;
    struct sigaction signal_action;
    signal_action.sa_handler = rl_signal_handler;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;

    int ret;
    ret = sigaction(SIGTERM, &signal_action, &sigterm_action_backup);
    if (ret < 0) {
        rl_log(RL_LOG_ERROR,
               "can't register signal handler for SIGTERM; %d message: %s",
               errno, strerror(errno));
        return ERROR;
    }
    ret = sigaction(SIGINT, &signal_action, &sigint_action_backup);
    if (ret < 0) {
        rl_log(RL_LOG_ERROR,
               "can't register signal handler for SIGINT; %d message: %s",
               errno, strerror(errno));
        return ERROR;
    }

    // INITIATION

    // init status
    rl_status_reset(&rl_status);
    rl_status.config = config;

    // init status publishing and publish (to not be received, see zeromq docs)
    rl_status_pub_init();
    rl_status_write(&rl_status);

    // init hardware
    hw_init(config);

    // initialize socket if webserver enabled
    if (config->web_enable) {
        rl_socket_init();
        rl_socket_metadata(config);
    }

    // check ambient sensor available
    if (config->ambient_enable && rl_status.sensor_count == 0) {
        config->ambient_enable = false;
        rl_log(RL_LOG_WARNING, "No ambient sensor found. Disabling ambient...");
    }

    // SAMPLING
    rl_log(RL_LOG_INFO, "sampling start");
    hw_sample(config);
    rl_log(RL_LOG_INFO, "sampling finished");

    // FINISH
    if (config->web_enable) {
        rl_socket_deinit();
    }
    rl_status_pub_deinit();
    hw_deinit(config);

    // restore signal handlers for SIGTERM and SIGINT
    ret = sigaction(SIGTERM, &sigterm_action_backup, NULL);
    if (ret < 0) {
        rl_log(RL_LOG_WARNING,
               "can't restore signal handler for SIGTERM; %d message: %s",
               errno, strerror(errno));
    }
    ret = sigaction(SIGINT, &sigint_action_backup, NULL);
    if (ret < 0) {
        rl_log(RL_LOG_WARNING,
               "can't restore signal handler for SIGINT; %d message: %s", errno,
               strerror(errno));
    }

    return SUCCESS;
}

int rl_stop(void) {
    if (!rl_is_sampling()) {
        rl_log(RL_LOG_ERROR, "RocketLogger is not running.");
        return ERROR;
    }

    // get pid and send termination signal
    pid_t pid = rl_pid_get();
    if (pid < 0) {
        rl_log(RL_LOG_ERROR, "RocketLogger PID file not found; %d message: %s",
               errno, strerror(errno));
        return ERROR;
    }
    kill(pid, SIGTERM);

    return SUCCESS;
}

static void rl_signal_handler(int signal_number) {
    // signal generated by stop function
    if (signal_number == SIGTERM) {
        // stop sampling
        rl_status.sampling = false;
    }

    // signal generated by interactive user (ctrl+C)
    if (signal_number == SIGINT) {
        signal(signal_number, SIG_IGN);
        rl_status.sampling = false;
    }
}
