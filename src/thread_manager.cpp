/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

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
#include "parcel.h"

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

int RegisterThread(pthread_t threadId, char* threadName)
{
    int i;

    verb(VERB_2, "[RegisterThread]: Thread %s (%lu) added", threadName, threadId);
    
    for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
        if ( !(activeThreads[i].threadUsed) ) {
            break;
        }
    }
    if ( i < THREAD_POOL_SIZE ) {
        strncpy(activeThreads[i].threadName, threadName, MAX_THREAD_NAME - 1);
        activeThreads[i].threadId = threadId;
        activeThreads[i].threadUsed = 1;
        thread_count++;
    }
    verb(VERB_2, "[RegisterThread]: Thread count now %d", thread_count);
    
    return(thread_count);
}

int ExitThread(pthread_t threadId)
{
    int i;
    verb(VERB_2, "[ExitThread]: Thread ID %lu requested", threadId);
    
    for ( i = 0; i < THREAD_POOL_SIZE; i++ ) {
        if ( activeThreads[i].threadId == threadId ) {
                verb(VERB_2, "[ExitThread]: Thread %s found (%lu), clearing", activeThreads[i].threadName, threadId);
                activeThreads[i].threadId = 0;
                activeThreads[i].threadUsed = 0;
                memset(activeThreads[i].threadName, 0, sizeof(char)*MAX_THREAD_NAME);
                thread_count--;
                break;
        }
    }
    
    verb(VERB_2, "[ExitThread]: Thread count now %d", thread_count);
    return(thread_count);
}

int GetThreadCount(void)
{
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
    verb(VERB_2, "[SetExit]: Time to wrap this up");
    time_to_exit = 1;
    
}


int CheckForExit(void)
{
    return time_to_exit;
    
}

