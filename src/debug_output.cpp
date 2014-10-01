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
#include <ctype.h>

#define MAX_LOG_FILE_NAME_LEN   256

#include "debug_output.h"

verb_t g_verbosity = VERB_1;
int g_debug_file_logging = 0;

char g_logfilename_prefix[] = "debug";
char g_logfilename_suffix[] = "log";
char g_logfilename[MAX_LOG_FILE_NAME_LEN / 2] = "";
char g_full_log_filename[MAX_LOG_FILE_NAME_LEN] = "";

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

	getcwd(tmpPath, sizeof(tmpPath));
	memset(g_logfilename, 0, sizeof(char) * MAX_LOG_FILE_NAME_LEN);

	if ( is_master != 0 ) {
		sprintf(g_logfilename, "%s-%s.%s", g_logfilename_prefix, "master", g_logfilename_suffix);
	} else {
		sprintf(g_logfilename, "%s-%s.%s", g_logfilename_prefix, "minion", g_logfilename_suffix);
	}

	snprintf(g_full_log_filename, MAX_LOG_FILE_NAME_LEN, "%s/%s", tmpPath, g_logfilename);

	if ( g_debug_file_logging > 0 ) {
		verb(VERB_2, "[%s] opening log file g_full_log_filename", __func__, g_full_log_filename);
		fflush(stderr);
		FILE* debug_file = fopen(g_full_log_filename, "a");
		if ( !debug_file ) {
			g_debug_file_logging = 0;
			verb(VERB_2, "[%s] unable to open log file, error %d", __func__, g_full_log_filename, errno);
		} else {
			fclose(debug_file);
		}
		verb(VERB_2, "********");
		verb(VERB_2, "********");
		verb(VERB_2, "[%s] log file opened as %s", __func__, g_full_log_filename);
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
	if ( (g_debug_file_logging > 0) && strlen(g_full_log_filename) ) {
		FILE* debug_file = fopen(g_full_log_filename, "a+");
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


/*
 * void print_backtrace
 * - prints a backtrace from strings generated
 */
void print_backtrace()
{
	void *trace[32];
	size_t size, i;
	char **strings;

	fprintf( stderr, "\n********* BACKTRACE *********\n\n" );
	size    = backtrace( trace, 32 );
	strings = backtrace_symbols( trace, size );

	for( i = 0; i < size; i++ ) {
		fprintf( stderr, "  %s\n", strings[i] );
	}
	fprintf( stderr, "\n***************************************\n" );
}


//
// print_bytes
//
// prints a buffer of data, [output_line_len] bytes in a line
// 32 bytes max
//
#define TMP_STR_SIZE 64
void print_bytes(char* data, int length, int output_line_len)
{
	char asciiStr[TMP_STR_SIZE], hexStr[TMP_STR_SIZE], tmpChar[8];
	int i;

	if ( output_line_len > (TMP_STR_SIZE - 1) / 2 ) {
		output_line_len = (TMP_STR_SIZE - 1) / 2;
	}

	verb(VERB_2, "[%s] printing buffer of len %d at %x", __func__, length, data);

	while (length > 0 ) {
		memset(asciiStr, '\0', TMP_STR_SIZE);
		memset(hexStr, '\0', TMP_STR_SIZE);
		memset(tmpChar, '\0', 8);

		for (i = 0; i < output_line_len; i++ ) {
			if ( length > 0 ) {
				if ( (data[i] > 31) && (data[i] != 127) ) {
					sprintf(tmpChar, " %c", data[i]);
				} else {
					sprintf(tmpChar, "  ");
				}
				strcat(asciiStr, tmpChar);
				sprintf(tmpChar, "%02X ", (unsigned char)data[i]);
				strcat(hexStr, tmpChar);
				length--;
			} else {
				sprintf(tmpChar, "  ");
				strcat(asciiStr, tmpChar);
				sprintf(tmpChar, "-- ");
				strcat(hexStr, tmpChar);
			}
		}
		data += output_line_len;
		verb(VERB_2, "%s *** %s", hexStr, asciiStr);
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
