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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

#define MAX_LOG_FILE_NAME_LEN   128

#include "debug_output.h"

verb_t g_verbosity = VERB_1;
int g_debug_file_logging = 0;

char g_logfilename_prefix[] = "debug";
char g_logfilename_suffix[] = "log";
char g_logfilename[MAX_LOG_FILE_NAME_LEN];

void set_verbosity_level(verb_t verbosity)
{
    g_verbosity = verbosity;
}

verb_t get_verbosity_level()
{
    return(g_verbosity);
}

void set_file_logging(int log_state)
{
    g_debug_file_logging = log_state;
}

int get_file_logging()
{
    return(g_debug_file_logging);
}

int init_debug_output_file(int is_master)
{
//    g_verbosity = 1;
//    g_debug_file_logging = 0;
    char tmpPath[128];

    memset(g_logfilename, 0, sizeof(char) * MAX_LOG_FILE_NAME_LEN);

    if ( is_master != 0 ) {
        sprintf(g_logfilename, "%s-%s.%s", g_logfilename_prefix, "master", g_logfilename_suffix);
    } else {
        sprintf(g_logfilename, "%s-%s.%s", g_logfilename_prefix, "minion", g_logfilename_suffix);
    }

    if ( g_debug_file_logging > 0 ) {
        getcwd(tmpPath, sizeof(tmpPath));
        verb(VERB_2, "[%s] opening log file %s/%s", __func__, tmpPath, g_logfilename);
        fflush(stdout);
        FILE* debug_file = fopen(g_logfilename, "a");
        if ( !debug_file ) {
            g_debug_file_logging = 0;
            verb(VERB_2, "[%s] unable to open log file, error %d", __func__, g_logfilename, errno);
        } else {
            fclose(debug_file);
        }
        verb(VERB_2, "********");
        verb(VERB_2, "********");
        verb(VERB_2, "[%s] log file opened as %s", __func__, g_logfilename);
    }
    return 0;
}

/* 
 * void verb
 * - print message to stdout depending on user's selection of verbosity
 */
void verb(verb_t verbosity, char* fmt, ... )
{
    if (g_verbosity >= verbosity) {
        va_list args; va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }
    if ( g_debug_file_logging > 0 ) {
        FILE* debug_file = fopen(g_logfilename, "a+");
        va_list args; va_start(args, fmt);
        vfprintf(debug_file, fmt, args);
        fprintf(debug_file, "\n");
        fflush(debug_file);
        fclose(debug_file);
        va_end(args);
    }
}


/* 
 * void warn
 * - warn the user unless verbosity set to VERB_0 (0)
 */
void warn(char* fmt, ... )
{
    if (g_verbosity > VERB_0) {
        va_list args; va_start(args, fmt);
        fprintf(stderr, "%s: warning:", __func__);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }
}


// /* 
//  * void error
//  * - print error to stdout and quit
//  * - note: error() takes a variable number of arguments with 
//  *   format string (like printf)
//  */
// void error(char* fmt, ... )
// {
//     va_list args; va_start(args, fmt);
//     fprintf(stderr, "%s: error:", __func__);
//     vfprintf(stderr, fmt, args);
//     if (errno) perror(" ");
//     fprintf(stderr, "\n");
//     va_end(args);
//     clean_exit(EXIT_FAILURE);
// }

/*void pric(uchar* s, int len)
{
    int i;
    fprintf(stderr, "data: ");
    for (i = 0; i < len/4; i ++) {
        fprintf(stderr, "%x ",  s[i]);
    }
    fprintf(stderr, "\n");
}

void prii(int i)
{
    if (DEBUG) {
        fprintf(stderr, "             -> %d\n", i);
    }
} */
