/*****************************************************************************
Copyright 2014 Laboratory for Advanced Computing at the University of Chicago

	This file is part of parcel by Joshua Miller,
	being a basic way of tracking threads
	Created by Joe Sislow (fly)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions
and limitations under the License.
*****************************************************************************/

#include <string.h>

#include "thread_manager.h"
#include "debug_output.h"

struct thread_info_t    activeThreads[THREAD_POOL_SIZE];
int g_thread_count;
int g_time_to_exit;
pthread_mutex_t g_thread_mutex;

//
// init_thread_manager
//
// initializes the thread manager system
//

void init_thread_manager(void)
{
	int i;
	g_time_to_exit = 0;
	g_thread_count = 0;
	pthread_mutex_init(&g_thread_mutex, NULL);

	for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
		activeThreads[i].threadType = THREAD_TYPE_NONE;
		activeThreads[i].threadId = 0;
		activeThreads[i].threadUsed = 0;
		memset(activeThreads[i].threadName, 0, sizeof(char)*MAX_THREAD_NAME);
	}
}

int create_thread(pthread_t* thread_id,const pthread_attr_t *attr,  void *(*start_routine) (void *), void *args, char* threadName, thread_type_t threadType)
{
	int ret_val = pthread_create(thread_id, attr, start_routine, args);
	if ( !ret_val ) {
		register_thread(*thread_id, threadName, threadType);
	}

	return ret_val;
}

//
// register_thread
//
// registers a thread with the system
//

int register_thread(pthread_t threadId, char* threadName, thread_type_t threadType)
{
	int i;

	verb(VERB_2, "[%s] Thread %s (%lu) added", __func__, threadName, threadId);

	for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
		if ( !(activeThreads[i].threadUsed) ) {
			break;
		}
	}
	if ( i < THREAD_POOL_SIZE ) {
		pthread_mutex_lock (&g_thread_mutex);
		strncpy(activeThreads[i].threadName, threadName, MAX_THREAD_NAME - 1);
		activeThreads[i].threadId = threadId;
		activeThreads[i].threadUsed = 1;
		if ( threadType < NUM_THREAD_TYPES ) {
			activeThreads[i].threadType = threadType;
		} else {
			activeThreads[i].threadType = THREAD_TYPE_1;
		}
		g_thread_count++;
		pthread_mutex_unlock (&g_thread_mutex);
	}
	verb(VERB_2, "[%s] Thread count now %d", __func__, g_thread_count);

	return(g_thread_count);
}


//
// unregister_thread
//
// unregisters a thread with the system, telling the system the thread is complete
//

int unregister_thread(pthread_t threadId)
{
	int i;
	verb(VERB_2, "[%s] Thread ID %lu requested", __func__, threadId);

	for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
	//	if ( activeThreads[i].threadId == threadId ) {
		if ( pthread_equal(activeThreads[i].threadId, threadId) ) {
				verb(VERB_2, "[%s]: Thread %s found (%lu), clearing", __func__, activeThreads[i].threadName, threadId);
				pthread_mutex_lock (&g_thread_mutex);
				activeThreads[i].threadType = THREAD_TYPE_NONE;
				activeThreads[i].threadId = 0;
				activeThreads[i].threadUsed = 0;
				g_thread_count--;
				memset(activeThreads[i].threadName, 0, sizeof(char)*MAX_THREAD_NAME);
				pthread_mutex_unlock (&g_thread_mutex);
				break;
		}
	}

	verb(VERB_2, "[%s]: Thread count now %d", __func__, g_thread_count);
	return(g_thread_count);
}

//
// get_thread_count
//
// checks with the thread system and sees how many threads of a type
// are currently in the system

int get_thread_count(thread_type_t threadType)
{
	int i, tmpThreadCount = g_thread_count;

	if ( threadType < THREAD_TYPE_ALL ) {
		for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
			if ( activeThreads[i].threadType != threadType ) {
				if ( activeThreads[i].threadUsed ) {
					tmpThreadCount--;
				}
			}
		}
	}

	return(tmpThreadCount);
}

//
// get_my_thread_id
//
// gets the thread id of the current thread

pthread_t get_my_thread_id(void)
{
	return(pthread_self());
}

//
// get_my_thread_id
//
// prints the threads that are currently active

void print_threads(verb_t verbosity)
{
	int i;

	for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
		if ( activeThreads[i].threadUsed ) {
			verb(verbosity, "%d: Thread ID %lu - %s", i, activeThreads[i].threadId, activeThreads[i].threadName);
		}
	}

}

void set_thread_exit(void)
{
	verb(VERB_2, "[%s]: Time to wrap this up", __func__);
	g_time_to_exit = 1;

}


int check_for_exit(thread_type_t threadType)
{
	int i, should_exit = g_time_to_exit;
//	verb(VERB_2, "[%s]: threadType = %d", __func__, threadType);

	// type 1 needs to check if any 2s are running, if we even have to
	// worry about exiting
	if ( (threadType == THREAD_TYPE_1) && should_exit) {
		for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
			// bail from loop & don't exit if any 2s are still running
			if ( (activeThreads[i].threadType == THREAD_TYPE_2) && activeThreads[i].threadUsed ) {
//				verb(VERB_2, "[%s] %d: still active - thread ID %lu - %s", __func__, i, activeThreads[i].threadId, activeThreads[i].threadName);
				should_exit = 0;
				break;
			}
		}
	}

	return should_exit;
}

