/*
* PackageLicenseDeclared: Apache-2.0
* Copyright (c) 2017 ARM Limited
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef _NETWORK_PARAMS_H
#define _NETWORK_PARAMS_H

#include "mDot.h"
#include "dot_util.h"
#include "ChannelPlans.h"

#define EU868
// #define US915

#define TTN

// Application EUI
const uint8_t network_id[] = { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
// Application Key
const uint8_t network_key[] = { 0x59, 0xA9, 0xB9, 0xD5, 0x97, 0xD4, 0xB9, 0x50, 0xAE, 0x06, 0x6C, 0x99, 0x64, 0x69, 0x00, 0xBF };

#ifdef US915
lora::ChannelPlan_US915 plan;
const mDot::DataRates tx_data_rate = mDot::DR4;
const mDot::DataRates join_rx2_data_rate = mDot::DR8;
#ifdef TTN
const uint8_t frequency_sub_band = 2;
#else
const uint8_t frequency_sub_band = 0; // try all subbands
#endif // TTN
#endif // US915

#ifdef EU868
lora::ChannelPlan_EU868 plan;
const mDot::DataRates tx_data_rate = mDot::DR5;
const uint8_t frequency_sub_band = 0; // not applicable
#ifdef TTN
const mDot::DataRates join_rx2_data_rate = mDot::DR3; // SF9
#else
const mDot::DataRates join_rx2_data_rate = mDot::DR0; // SF12
#endif // TTN
#endif // EU868

#endif // _NETWORK_PARAMS_H_
