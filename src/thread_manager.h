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
#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#define THREAD_POOL_SIZE    256
#define MAX_THREAD_NAME     256

#include <pthread.h>
#include "debug_output.h"

typedef enum : unsigned char {
	THREAD_TYPE_NONE,
	THREAD_TYPE_1,
	THREAD_TYPE_2,
	THREAD_TYPE_ALL,
	NUM_THREAD_TYPES
} thread_type_t;

typedef struct thread_info_t {
	pthread_t		threadId;
	char			threadName[MAX_THREAD_NAME];
	int				threadReady;
	int				threadUsed;
	thread_type_t	threadType;
} thread_info_t;

void init_thread_manager(void);
int create_thread(pthread_t* thread_id, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg, char* threadName, thread_type_t threadType);
int register_thread(pthread_t threadId, char* threadName, thread_type_t threadType);
int unregister_thread(pthread_t threadId);
int get_thread_count(thread_type_t threadType);
pthread_t get_my_thread_id(void);
void print_threads(verb_t verbosity);
void set_thread_exit(void);
int check_for_exit(thread_type_t threadType);

#endif // THREAD_MANAGER_H
