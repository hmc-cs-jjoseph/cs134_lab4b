/* \author Jesse Joseph
 * \email jjoseph@hmc.edu
 * \ID 040161840
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include "mraa.h"
#include "mraa/aio.h"
#include "mraa/gpio.h"
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

/* Global flags:
 * user_int indicates whether the user has interrupted the program
 * through an OFF command, a button press, or a ^C.
 * gen_reports is used to pause and continue measurements.
 */
volatile int user_int = 0;
volatile int gen_reports = 1;

/* Other global attributes:
 * period : sample period (default 1 second)
 * scale : temperature scale (default 'F')
 */
int period = 1;
char scale = 'F';

/* async_button_check_t
 * A struct to be passed into the button check thread
 * \params
 * 	button : the mraa_gpio_context object to sample
 * 	check_period : sample period to check for button press
 */
typedef struct __async_button_check_t {
	mraa_gpio_context button;
	int check_period;
} async_button_check_t;

/* \brief polls a button at a fixed rate
 * \param a pointer to an async_button_check_t struct defining the button and the rate
 * \detail should execute in a new thread
 * \returns always NULL
 */
void *check_button(void *args);

/* \brief signal handler - only deals with SIGINT
 * \params signum
 * \detail sets global flag user_int to 1. All user interrupts are routed through this function.
 */
void signal_handler(int sig);

/* \brief reads commands from stdin
 * \param a pointer to a file descriptor
 * \detail continues reading until user_int has been set. should be used in a new thread.
 */
void *get_commands(void* arg);

/* \brief converts 10 bit adc reading into temperature
 * \param adc_val, an integer between 0 and 1023
 * \param units: 'C' for celcius, 'F' for fahrenheit
 */
float get_temperature(int adc_val, char units);

int main(int argc, char **argv) {
	/* register the sigint handler */
	signal(SIGINT, signal_handler);

	/* parse command line options */
	char *logfile = NULL;
	int logfd = -1;
	char opt;
	int optind;
	struct option options[] = {
		{"period", required_argument, 0, 'p'},
		{"scale", required_argument, 0, 's'},
		{"log", required_argument, 0, 'l'},
		{0, 0, 0, 0}};
	while((opt = getopt_long(argc, argv, "p:s:l:", options, &optind)) != -1) {
		switch(opt) {
			case 'p':
				period = atoi(optarg);
				break;
			case 's':
				scale = *optarg;
				break;
			case 'l':
				logfile = optarg;
				break;
			case '?':
				fprintf(stderr, "Usage: \n"
						"./lab4b [...options...]\n"
						"Options:\n"
						"--period=<sample period, in seconds>\n"
						"--scale=<temp scale; valid C or F>\n"
						"--log=<filename>\n");
				return 1;
			default:
				break;
		}
	}

	/* Open log file for appending if a filename has been provided.
	 * Create file if it does not exist, create with full r/w permissions for all users.
	 */
	if(logfile != NULL) {
		logfd = open(logfile, O_WRONLY | O_APPEND | O_CREAT, S_IROTH | S_IWOTH);
		if(logfd < 0) {
			perror("Couldn't open log file");
			exit(1);
		}
	}

	/* Open and initialize io */
	mraa_aio_context adc_a0;
	size_t adc_val = 0;
	adc_a0 = mraa_aio_init(0);
	if(adc_a0 == NULL) {
		fprintf(stderr, "Failed to open analog input port\n");
		exit(1);
	}
	mraa_gpio_context button;
	button = mraa_gpio_init(3);
	if(button == NULL) {
		fprintf(stderr, "Failed to open digital input port\n");
		exit(1);
	}
	mraa_gpio_dir(button, MRAA_GPIO_IN);

	/* create threads for polling button, input commands */
	pthread_t input_thread, button_thread;
	async_button_check_t button_check = {button, 10};
	pthread_create(&button_thread, NULL, check_button, &button_check);
	pthread_create(&input_thread, NULL, get_commands, &logfd);

	/* set up time zone */
	time_t timenow;
	struct tm *info;
	char timebuff[16];
	char buff[32];
	setenv("TZ", "PST8PST", 1);
	tzset();

	float temperature;

	/* Continue until user_int has been set */
	while(!user_int ) {
		/* Only sample if gen_reports is true; allows pause and restart */
		if(gen_reports) {
			/* get time */
			time(&timenow);
			info = localtime(&timenow);
			strftime(timebuff, 32, "%T", info);

			/* get temperature */
			adc_val = mraa_aio_read(adc_a0);
			temperature = get_temperature(adc_val, scale);

			/* create output string */
			memset(buff, 0, 32);
			sprintf(buff, "%s %.1f\n", timebuff, temperature);
			fprintf(stdout, "%s", buff);

			/* log if applicable */
			if(logfile != NULL) {
				if(write(logfd, buff, 32) < 0) {
					perror("Couldn't write to log file");
					exit(1);
				}
			}

			/* sleep until for measurement period
			 * This isn't the most precise approach - it would
			 * be better to get the time at the beginning of the loop
			 * and continously poll until the desired time has elapsed.
			 * However, since the sample period is long, much longer than
			 * the actual measurement time, this is probably good enough.
			 */
			sleep(period);
		}
	}
	
	/* Create, display, and log the shutdown message */
	time(&timenow);
	info = localtime(&timenow);
	strftime(timebuff, 32, "%T", info);
	memset(buff, 0, 32);
	sprintf(buff, "%s SHUTDOWN\n", timebuff);
	fprintf(stdout, "%s", buff);
	if(logfile != NULL) {
		if(write(logfd, buff, 32) < 0) {
			perror("Couldn't write to log file");
			exit(1);
		}
	}

	/* close log file */
	if(logfile != NULL) {
		if(close(logfd) < 0) {
			perror("Couldn't close log file");
			exit(1);
		}
	}

	/* Collect input and button threads */
	pthread_join(button_thread, NULL);
	pthread_join(input_thread, NULL);

	/* close io */
	mraa_aio_close(adc_a0);
	mraa_gpio_close(button);
	return 0;
}

void signal_handler(int sig) {
	/* OFF command, button press, ^C all route through here.
	 * Global flag user_int determines whether program should
	 * continue, so when a SIGINT is received, user_int is set.
	 * Atomicity doesn't matter here, because user_int is initialized
	 * to 0 and will only ever be set to 1. It could technically be
	 * set to 1 multiple times, but that will not change any behavior.
	 */
	if(sig == SIGINT) {
		user_int = 1;
	}
}

float get_temperature(int adc_val, char units) {
	/* Thermistor constants and conversion formula taken from:
	 * http://wiki.seeed.cc/Grove-Temperature_Sensor_V1.2/
	 * Thermistor B val was not actually measured/calibrated, this
	 * is just the nominal value.
	 */
	int B = 4275;
	float R = 1023.0/((float) adc_val) - 1.0;
	float temp = 1.0/(log(R)/B + 1.0/298.15) - 273.15;
	/* Default to celcius */
	if(units == 'F') {
		temp = temp*9.0/5.0 + 32.0;
	}
	return temp;
}

void *get_commands(void *arg) {
	int *argint = (int *) arg;
	int fd = *argint;
	fd_set read_set;
	struct timeval timeout;
	int select_rc;
	char buff[128];
	int nbytes;
	/* Continuously check global flag user_int */
	while(!user_int) {
		/* We will use select to only read when data is available on stdin
		 * with a timeout of 10 ms (10000 us)
		 */
		FD_ZERO(&read_set);
		FD_SET(STDIN_FILENO, &read_set);
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;
		select_rc = select(FD_SETSIZE, &read_set, NULL, NULL, &timeout);
		if(select_rc > 0) {
			/* Only read if data is available. 
			 * Keep terminal in canonical input so that entire command is read at once.
			 */
			nbytes = read(0, buff, 128);
			if(nbytes < 0) {
				perror("Failed to read from stdin");
				exit(1);
			} else {
				if(fd > 0) {
					if(write(fd, buff, nbytes) < 0) {
						perror("Failed to log command");
						exit(1);
					}
				}
				if(strncmp("OFF", buff, 3) == 0) {
					raise(SIGINT);
					pthread_exit(NULL);
				} else if(strncmp("STOP", buff, 4) == 0) {
					gen_reports = 0;
				} else if(strncmp("START", buff, 5) == 0) {
					gen_reports = 1;
				} else if(strncmp("SCALE=F", buff, 7) == 0) {
					scale = 'F';
				} else if(strncmp("SCALE=C", buff, 7) == 0) {
					scale = 'C';
				} else if(strncmp("PERIOD=", buff, 7) == 0) {
					period = atoi(&buff[7]);
				} else {
					fprintf(stderr, "Bad command\n");
				}
			}
		} else if(select_rc == -1) {
			perror("Select failed");
			exit(1);
		}
	}
	return NULL;
}

void *check_button(void *args) {
	async_button_check_t *argstruct = (async_button_check_t *) args;
	struct timespec timer;
	timer.tv_sec = argstruct->check_period / 1000;
	timer.tv_nsec = (argstruct->check_period % 1000) * 1000000;
	/* Continously check global flag */
	while(!user_int) {
		if(mraa_gpio_read(argstruct->button) != 0) {
			raise(SIGINT);
			pthread_exit(NULL);
		}
		nanosleep(&timer, NULL);
	}
	return NULL;
}
