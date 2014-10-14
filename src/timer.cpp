/*****************************************************************************

	  Descriptive Timer system created by Joshua Miller
			  November 23, 2013

To use:
   init_timers();
   int t1 = new_timer("Timer_description");
   int t2 = new_timer("secondtimer");
   ....
   start_timer(t1);
   start_timer(t2);
   ....
   stop_timer(t1);
   stop_timer(t2);

   print_timers();

*****************************************************************************/

#include "timer.h"

timespec start[N_TIMERS];
timespec end[N_TIMERS];
char descrip[N_TIMERS][MAX_DESCRIP_LEN];

int timer_index = -1;

int init_timers()
{
	for (int i = 0; i < N_TIMERS; i++) {
		start[i].tv_sec = 0;
		start[i].tv_nsec = 0;
		end[i].tv_sec = 0;
		end[i].tv_nsec = 0;
		memset(descrip[i], 0, sizeof(char) * MAX_DESCRIP_LEN);
	}

	return 0;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp);

int new_timer(char* str)
{
	timer_index++;
	if (timer_index > N_TIMERS) {
		fprintf(stderr, "Not enough timers, increase N_TIMERS\n");
		return 1;
	}
	/* strcat(descrip[timer_index], str); */
	memcpy(descrip[timer_index], str, strlen(str)+1);
	return timer_index;
}

void start_timer(int tag)
{
	clock_gettime(CLOCK_REALTIME, &start[tag]);
}

int stop_timer(int tag)
{
	return clock_gettime(CLOCK_REALTIME, &end[tag]);
}

double diff(timespec start, timespec end)
{
	timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp.tv_sec + temp.tv_nsec/(1.0e9);
}

double timer_elapsed(int tag)
{

	if (start[tag].tv_sec+start[tag].tv_nsec
	&&
	end[tag].tv_sec+end[tag].tv_nsec) {
		return diff(start[tag], end[tag]);
	}

	return 0.0;
}

int print_timer(int tag)
{
	printf("%s\t%f\n", descrip[tag], timer_elapsed(tag));
	return fflush(stdout);
}


void print_timers()
{
	for (int i = 0; i < 70; i++)
	fprintf(stderr, "-");
	fprintf(stderr, "\nTimers:\n");

	for (int i = 0; i < N_TIMERS; i++) {
		print_timer(i);
	}
}

