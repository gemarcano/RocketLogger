/*#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>*/

#include "types.h"
#include "util.h"

#define CALIBRATION_FILE "/etc/rocketlogger/calibration.dat"
#define NUM_CALIBRATION_VALUES 10


void reset_offsets();
void reset_scales();
int read_calibration(struct rl_conf* conf);
int write_calibration();
