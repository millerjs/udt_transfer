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

#ifndef STATE_H
#define STATE_H

typedef enum: uint8_t {
    PARCEL_STATE_MASTER,
    PARCEL_STATE_SLAVE,
    PARCEL_STATE_IDLE,
    NUM_PARCEL_STATES
} parcel_state;

typedef enum: uint8_t {
    PARCEL_SUBSTATE_SENDING,
    PARCEL_SUBSTATE_RECEIVING,
    PARCEL_SUBSTATE_IDLE,
    NUM_PARCEL_SUBSTATES
} parcel_substate;

typedef struct parcel_state_t {
    parcel_states state;
    parcel_substates substate;
} parcel_state_t;

typedef enum: uint8_t {
    STATE_ERROR = -3,
    STATE_INVALID_STATE = -2,
    STATE_INVALID_SUBSTATE = -1,
    STATE_OK = 0,
    NUM_STATE_VALS
    
};


void state_init();

int update_state();

parcel_state get_state();

parcel_substate get_substate();




#endif //STATE_H