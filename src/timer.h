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

#ifndef TIMER_H
#define TIMER_H

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

typedef struct timespec timespec;

#define N_TIMERS 200
#define MAX_DESCRIP_LEN 45

extern timespec start[N_TIMERS];
extern timespec end[N_TIMERS];
extern char descrip[N_TIMERS][MAX_DESCRIP_LEN];

extern int timer_index;

int init_timers();

int clock_gettime(clockid_t clk_id, struct timespec *tp);

int new_timer(char* str);

void start_timer(int tag);

int stop_timer(int tag);

double diff(timespec start, timespec end);

double timer_elapsed(int tag);

int print_timer(int tag);

void print_timers();

#endif
