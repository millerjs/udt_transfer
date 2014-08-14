/*****************************************************************************
Copyright 2014 Laboratory for Advanced Computing at the University of Chicago

    This file is part of parcel by Joshua Miller
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

#ifndef POSTMASTER_H
#define POSTMASTER_H

#include "parcel.h"

typedef enum : uint8_t {
    POSTMASTER_OK = 0,
    POSTMASTER_ERROR_BAD_XFER_CMD,
    POSTMASTER_ERROR_CALLBACK_NULL,
    POSTMASTER_ERROR_POSTMASTER_NULL,
    NUM_POSTMASTER_STATUSES
} postmaster_error_t;

typedef struct message_t {
    
    xfer_t messageType;
    int (*callback) (header_t header, parcel_block package);
    
} message_t;

typedef struct postmaster_t {
    
    int (*callback[NUM_XFER_CMDS]) (header_t header, parcel_block package);
    
} postmaster_t;

// creates a postmaster for later use
postmaster_t* create_postmaster();

// destroys an existing postmaster
void destroy_postmaster(postmaster_t* postmaster);

// register a callback with a postmaster for a given message type
int register_callback(postmaster_t* postmaster, xfer_t messageType, int (*callback)(header_t header, parcel_block package));

// dispatch a message to a given postmaster
int dispatch_message(postmaster_t* postmaster, xfer_t messageType, header_t header, parcel_block package);

#endif //POSTMASTER_H