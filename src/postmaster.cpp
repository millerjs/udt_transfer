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

#include "postmaster.h"
#include "parcel.h"

//
// create_postmaster
//
// malloc's a postmaster to be used to handle messages
// NOTES: for now, since there are so few header types, we're just creating
// an array of callback pointers, giving us a fast lookup for message type.
// however, if we ever got a bunch, probably better to have a lookup and 
// allocate them dynamically

postmaster_t* create_postmaster()
{
    postmaster_t* tmpPostmasterPtr = (postmaster_t*)NULL;
    int i;
    
    tmpPostmasterPtr = (postmaster_t*)malloc(sizeof(postmaster_t));
    if ( tmpPostmasterPtr ) {
        for ( i = 0; i < NUM_XFER_CMDS; i++ ) {
            tmpPostmasterPtr->callback[i] = NULL;
        }
    }
    
    return tmpPostmasterPtr;
    
}

//
// destroy_postmaster
//
// frees the postmaster to go on to other careers, because without a pension,
// really, who'd be a postmaster?

void destroy_postmaster(postmaster_t* postmaster)
{
    if ( postmaster != NULL ) {
        free(postmaster);
    }
    
}

//
// register_callback
//
// given a callback in the form int foo(header_t header, parcel_block package),
// adds it to the callback array by message_type

int register_callback(postmaster_t* postmaster, xfer_t message_type, int (*callback)(header_t header, parcel_block package))
{
    int ret_val = POSTMASTER_OK;
    
    if ( postmaster != NULL ) {
        // verify message type
        if ( message_type < NUM_XFER_CMDS ) {
            // add it to array
            postmaster->callback[message_type] = *callback;
        } else {
            ret_val = POSTMASTER_ERROR_BAD_XFER_CMD;
        }
    } else {
        ret_val = POSTMASTER_ERROR_POSTMASTER_NULL;
    }
    
    return ret_val;
    
}

//
// dispatch_message
//
// dispatches a message type with the data required (header and parcel block)

int dispatch_message(postmaster_t* postmaster, xfer_t message_type, header_t header, parcel_block package)
{
    int ret_val = POSTMASTER_OK;
    
    if ( postmaster != NULL ) {
        // verify message type
        if ( message_type < NUM_XFER_CMDS ) {
            if ( postmaster->callback[message_type] != NULL ) {
                postmaster->callback[message_type](header, package);
            } else {
                ret_val = POSTMASTER_ERROR_CALLBACK_NULL;
            }
        } else {
            ret_val = POSTMASTER_ERROR_BAD_XFER_CMD;
        }
    } else {
        ret_val = POSTMASTER_ERROR_POSTMASTER_NULL;
    }

    return ret_val;
}



int message_handle_data(header_t header, parcel_block package)
{
    
    
    
    return 0;    
    
    
}





