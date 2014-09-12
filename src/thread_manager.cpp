/*****************************************************************************
Copyright 2014 Laboratory for Advanced Computing at the University of Chicago

This file is part of parcel, being a basic way of tracking threads

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
int thread_count;
int time_to_exit;

void InitThreadManager(void) 
{
    int i;
    time_to_exit = 0;
    thread_count = 0;
    for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
        activeThreads[i].threadId = 0;
        activeThreads[i].threadUsed = 0;
        memset(activeThreads[i].threadName, 0, sizeof(char)*MAX_THREAD_NAME);
    }
}

int RegisterThread(pthread_t threadId, char* threadName, thread_type_t threadType)
{
    int i;

    verb(VERB_2, "[%s] Thread %s (%lu) added", __func__, threadName, threadId);

    for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
        if ( !(activeThreads[i].threadUsed) ) {
            break;
        }
    }
    if ( i < THREAD_POOL_SIZE ) {
        strncpy(activeThreads[i].threadName, threadName, MAX_THREAD_NAME - 1);
        activeThreads[i].threadId = threadId;
        activeThreads[i].threadUsed = 1;
        if ( threadType < NUM_THREAD_TYPES ) {
            activeThreads[i].threadType = threadType;
        } else {
            activeThreads[i].threadType = THREAD_TYPE_1;
        }
        thread_count++;
    }
    verb(VERB_2, "[%s] Thread count now %d", __func__, thread_count);

    return(thread_count);
}

int ExitThread(pthread_t threadId)
{
    int i;
    verb(VERB_2, "[%s] Thread ID %lu requested", __func__, threadId);

    for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
//        if ( activeThreads[i].threadId == threadId ) {
        if ( pthread_equal(activeThreads[i].threadId, threadId) ) {
                verb(VERB_2, "[%s]: Thread %s found (%lu), clearing", __func__, activeThreads[i].threadName, threadId);
                activeThreads[i].threadId = 0;
                activeThreads[i].threadUsed = 0;
                memset(activeThreads[i].threadName, 0, sizeof(char)*MAX_THREAD_NAME);
                thread_count--;
                break;
        }
    }

    verb(VERB_2, "[%s]: Thread count now %d", __func__, thread_count);
    return(thread_count);
}

int GetThreadCount(thread_type_t threadType)
{
    int i, tmpThreadCount = thread_count;

    if ( threadType < THREAD_TYPE_ALL ) {
        for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
            if ( activeThreads[i].threadType != threadType ) {
                tmpThreadCount--;
            }
        }
    }

    return(thread_count);
}

pthread_t GetMyThreadId(void)
{
    return(pthread_self());
}

void PrintThreads(void)
{
    int i;

    for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
        if ( activeThreads[i].threadUsed ) {
            verb(VERB_2, "%d: Thread ID %lu - %s", i, activeThreads[i].threadId, activeThreads[i].threadName);
        }
    }

}

void SetExit(void)
{
    verb(VERB_2, "[%s]: Time to wrap this up", __func__);
    time_to_exit = 1;

}


int CheckForExit(thread_type_t threadType)
{
    int i, should_exit = time_to_exit;
//    verb(VERB_2, "[%s]: threadType = %d", __func__, threadType);

    // type 1 needs to check if any 2s are running, if we even have to
    // worry about exiting
    if ( (threadType == THREAD_TYPE_1) && should_exit) { 
        for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
            // bail from loop & don't exit if any 2s are still running
            if ( (activeThreads[i].threadType == THREAD_TYPE_2) && activeThreads[i].threadUsed ) {
//                verb(VERB_2, "[%s] %d: still active - thread ID %lu - %s", __func__, i, activeThreads[i].threadId, activeThreads[i].threadName);
                should_exit = 0;
                break;
            }
        }
    }

    return should_exit;
}

