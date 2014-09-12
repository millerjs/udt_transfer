/*****************************************************************************
Copyright 2014 Laboratory for Advanced Computing at the University of Chicago

    This file is part of parcel by Joshua Miller, being the handler
    of the debug output, created by Joe Sislow (fly)

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

#include <execinfo.h>

#ifndef DEBUG_OUTPUT_H
#define DEBUG_OUTPUT_H


/* 
Levels of Verbosity:
 VERB_0: Withhold WARNING messages 
 VERB_1: Update user on file transfers
 VERB_2: Update user on underlying processes, i.e. directory creation 
 VERB_3: Unassigned 
 VERB_4: Unassigned 
*/

typedef enum{
    VERB_0,
    VERB_1,
    VERB_2,
    VERB_3,
    VERB_4
} verb_t;

//void pric(uchar* s, int len);

//void prii(char* str, int i);

int init_debug_output_file(int is_master);

void set_verbosity_level(verb_t verbosity);

void set_file_logging(int log_state);

int get_file_logging();

verb_t get_verbosity_level();

void msg(char* str);

void warn(char* fmt, ...);

void verb(verb_t verbosity, char* fmt, ...);

void print_backtrace();

//void error(char* fmt, ...);

#endif // DEBUG_OUTPUT_H