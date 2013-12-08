/*****************************************************************************

	  Descriptive Timer system created by Joshua Miller
			  November 23, 2013

To use:
   init_timers();
   int t1 = start_timer("Timer_description");
   int t2 = start_timer("secondtimer");
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

int init_timers(){
    int i;
    for (i = 0; i < N_TIMERS; i++){
	start[i].tv_sec = 0;
	start[i].tv_nsec = 0;
	end[i].tv_sec = 0;
	end[i].tv_nsec = 0;
	bzero(descrip[i], MAX_DESCRIP_LEN);
    }    
}

int clock_gettime(clockid_t clk_id, struct timespec *tp);

int start_timer(char* str){
    timer_index++;
    if (timer_index > N_TIMERS){
	fprintf(stderr, "Not enoug timers, increase N_TIMERS\n");
	return 1;
    }
    /* strcat(descrip[timer_index], str); */
    memcpy(descrip[timer_index], str, strlen(str)+1);
    clock_gettime(CLOCK_REALTIME, &start[timer_index]);
    return timer_index;
}

int stop_timer(int tag){
    return clock_gettime(CLOCK_REALTIME, &end[tag]);
}

double diff(timespec start, timespec end){
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


double timer_elapsed(int tag){
    if (start[tag].tv_sec+start[tag].tv_nsec){

	if (end[tag].tv_sec+end[tag].tv_nsec){

	    return diff(start[tag], end[tag]);

	} else {
	    
	    return 0.0;

	}
    }
}


int print_timer(int tag){

    printf("%s\t%f\n", descrip[tag], timer_elapsed(tag));

    fflush(stdout);
}

void print_timers(){
    int i;

    for (i = 0; i < 70; i++)
	fprintf(stderr, "-");
    fprintf(stderr, "\nTimers:\n");

    for (i = 0; i < N_TIMERS; i++){
	print_timer(i);
    }
}

