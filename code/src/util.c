#include "util.h"

// channel functions // TODO: use
int is_current(int index) {
	if(index == I1H_INDEX || index == I1L_INDEX || index == I2H_INDEX || index == I2L_INDEX) {
		return 1;
	} else {
		return 0;
	}
}

int is_low_current(int index) {
	if(index == I1L_INDEX || index == I2L_INDEX) {
		return 1;
	} else {
		return 0;
	}
}

int count_channels(int channels[NUM_CHANNELS]) {
	int i = 0;
	int c = 0;
	for(i=0; i<NUM_CHANNELS; i++) {
		if(channels[i] > 0) {
			c++;
		}
	}
	return c;
}

int count_i_channels(int channels[NUM_CHANNELS]) {
	int i = 0;
	int c = 0;
	for(i=0; i<NUM_CHANNELS; i++) {
		if(channels[i]> 0 && is_current(i)) {
			c++;
		}
	}
	return c;
}

int count_v_channels(int channels[NUM_CHANNELS]) {
	int i = 0;
	int c = 0;
	for(i=0; i<NUM_CHANNELS; i++) {
		if(channels[i]> 0 && !is_current(i)) {
			c++;
		}
	}
	return c;
}


// TODO: move to other file

int read_status(struct rl_status* status) {
	
	// map shared memory
	int shm_id = shmget(SHMEM_STATUS_KEY, sizeof(struct rl_status), SHMEM_PERMISSIONS);
	if (shm_id == -1) {
		rl_log(ERROR, "In read_status: failed to get shared status memory id; %d message: %s", errno, strerror(errno));
		return FAILURE;
	}
	struct rl_status* shm_status = (struct rl_status*) shmat(shm_id, NULL, 0);
	
	if (shm_status == (void *) -1) {
		rl_log(ERROR, "In read_status: failed to map shared status memory; %d message: %s", errno, strerror(errno));
		return FAILURE;
	}
	
	// read status
	*status = *shm_status;
	
	// unmap shared memory
	shmdt(shm_status);
	
	return SUCCESS;
}

int write_status(struct rl_status* status) {
	
	// map shared memory
	int shm_id = shmget(SHMEM_STATUS_KEY, sizeof(struct rl_status), IPC_CREAT | SHMEM_PERMISSIONS);
	if (shm_id == -1) {
		rl_log(ERROR, "In write_status: failed to get shared status memory id; %d message: %s", errno, strerror(errno));
		return FAILURE;
	}

	struct rl_status* shm_status = (struct rl_status*) shmat(shm_id, NULL, 0);	
	if (shm_status == (void *) -1) {
		rl_log(ERROR, "In write_status: failed to map shared status memory; %d message: %s", errno, strerror(errno));
		return FAILURE;
	}
	
	// write status
	*shm_status = *status;
	
	// unmap shared memory
	shmdt(shm_status);
	
	return SUCCESS;
}


/**
 * Count the number of bits set in an integer.
 * @param x
 * @return Number of bits.
 */
int count_bits(int x) {
	int MASK = 1;
	int i;
	int sum = 0;
	for (i=0;i<32;i++) {
		if ((x&MASK) > 0) {
			sum = sum + 1;
		}
		MASK = MASK << 1;
	}
	return sum;
}


/**
 * Integer division with ceiling.
 * @param n Numerator
 * @param d Denominator
 * @return Result
 */
int ceil_div(int n, int d) {
	if(n%d == d || n%d == 0) {
		return n/d;
	} else {
		return n/d + 1;
	}
}


// ------------------------------ SIGNAL HANDLER ------------------------------ //

void sig_handler(int signo) { // TODO: combine?

	// signal generated by stop function
    if (signo == SIGQUIT) {
		// stop sampling
		status.state = RL_OFF; 
	}

    // Ctrl+C handling
    if (signo == SIGINT) {
    	signal(signo, SIG_IGN);
    	printf("Stopping RocketLogger ...\n");
    	status.state = RL_OFF;
    }
}
// TODO: allow forced Ctrl+C


// ------------------------------ FILE READING/WRITING  ------------------------------ //

// reading integer from file
int read_file_value(char filename[]) {
	FILE* fp;
	unsigned int value = 0;
	fp = fopen(filename, "rt");
	if (fp == NULL) {
		rl_log(ERROR, "failed to open file");
		return FAILURE;
	}
	if(fscanf(fp, "%x", &value) <= 0) {
		rl_log(ERROR, "failed to read from file");
		return FAILURE;
	}
	fclose(fp);
	return value;
}


// ------------------------------ LOG HANDLING ------------------------------ //

int log_created = 0;

void rl_log(rl_log_type type, const char* format, ... ) {
	
	// open/init file
	FILE* log_fp;
	if(access( LOG_FILE, F_OK ) == -1) {
		// create file
		log_fp = fopen(LOG_FILE, "w");
		if (log_fp == NULL) {
			printf("Error: failed to open log file\n");
			return;
		}
		fprintf(log_fp, "--- RocketLogger Log File ---\n\n");
	} else {
		log_fp = fopen(LOG_FILE, "a");
		if (log_fp  == NULL) {
			printf("Error: failed to open log file\n");
			return;
		}
	}
	
	// reset, if file too large
	int file_size = ftell(log_fp);
	if (file_size > MAX_LOG_FILE_SIZE) {
		fclose(log_fp);
		fopen(LOG_FILE, "w");
		fprintf(log_fp, "--- RocketLogger Log File ---\n\n");
	}
	
	
	
	// print date/time
	struct timeval current_time;
	gettimeofday(&current_time, NULL);
	time_t nowtime = current_time.tv_sec;
	fprintf(log_fp, "  %s", ctime(&nowtime));
	
	// get arguments
	va_list args;
	va_start(args, format);
	
	// print error message
	if(type == ERROR) {
		
		// file
		fprintf(log_fp, "     Error: ");
		vfprintf(log_fp, format, args);
		fprintf(log_fp, "\n");
		// terminal
		printf("Error: ");
		vprintf(format, args);
		printf("\n\n");
		
		// set state to error // TODO: handle error everywhere
		status.state = RL_ERROR;
		
	} else if(type == WARNING) {
		
		// file
		fprintf(log_fp, "     Warning: ");
		vfprintf(log_fp, format, args);
		fprintf(log_fp, "\n");
		// terminal
		printf("Warning: ");
		vprintf(format, args);
		printf("\n\n");
		
	} else if(type == INFO) {
		
		fprintf(log_fp, "     Info: ");
		vfprintf(log_fp, format, args);
		fprintf(log_fp, "\n");
		
	} else {
		// for debugging purposes
		printf("Error: wrong error-code\n");
	}
	
	// facilitate return
	va_end(args);
	
	// close file
	fflush(log_fp);
	fclose(log_fp);
}
