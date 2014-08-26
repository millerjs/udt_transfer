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
#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#define THREAD_POOL_SIZE    256
#define MAX_THREAD_NAME     256

#include <pthread.h>

typedef struct thread_info_t {
    pthread_t   threadId;
    char        threadName[MAX_THREAD_NAME];
    int         threadUsed;
} thread_info_t;

int RegisterThread(pthread_t threadId, char* threadName);
int ExitThread(pthread_t threadId);
int GetThreadCount(void);
pthread_t GetMyThreadId(void);
void PrintThreads(void);
void SetExit(void);
int CheckForExit(void);

#endif // THREAD_MANAGER_H
