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

#include "state.h"

parcel_state_t g_cur_state;

void state_init()
{
    g_cur_state.state = PARCEL_STATE_IDLE;
    g_cur_state.substate = PARCEL_SUBSTATE_IDLE;
}


int set_state(parcel_state cur_state, parcel_substate cur_substate)
{
    int ret_val = STATE_OK;
    
    if ( cur_state <= NUM_PARCEL_STATES ) {
        if ( cur_state < NUM_PARCEL_STATES ) {
            g_cur_state.state = cur_state;
        }
    } else {
        ret_val = STATE_INVALID_STATE;
    }

    if ( cur_substate <= NUM_PARCEL_SUBSTATES ) {
        if ( cur_substate < NUM_PARCEL_SUBSTATES ) {
            g_cur_state.substate = cur_substate;
        }
    } else {
        ret_val = STATE_INVALID_SUBSTATE;
    }

    return ret_val;
}


int update_state()
{
    
    
    
    
}

parcel_state get_state()
{
    return g_cur_state.state;
}

parcel_substate get_substate()
{
    return g_cur_state.substate;
}

