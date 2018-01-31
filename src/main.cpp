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

#include "mbed.h"
#include "NetworkParams.h"
#include "FotaHelper.h"
#include "CayenneLPP.h"

#define APP_VERSION         28
#define IS_NEW_APP          0

using namespace std;

mDot* dot = NULL;

// For an overview of all the pins, see https://os.mbed.com/platforms/L-TEK-FF1705/
AnalogIn moisture(PA_0);
AnalogIn thermistor(GPIO2);
DigitalOut led(LED1);

float read_temperature() {
    unsigned int a, beta = 3975;
    float temperature, resistance;

    a = thermistor.read_u16(); /* Read analog value */

    /* Calculate the resistance of the thermistor from analog votage read. */
    resistance = (float) 10000.0 * ((65536.0 / a) - 1.0);

    /* Convert the resistance to temperature using Steinhart's Hart equation */
    temperature = (1/((log(resistance/10000.0)/beta) + (1.0/298.15)))-273.15;

    return temperature;
}

void blink() {
    led = !led;
}

int main() {
    printf("Hello from application version %d\n", APP_VERSION);

#if IS_NEW_APP == 1
    Ticker t;
    t.attach(callback(blink), 1.0f);
#else
    Ticker t;
    t.attach(callback(blink), 0.5f);
#endif

    mts::MTSLog::setLogLevel(mts::MTSLog::INFO_LEVEL);

    dot = mDot::getInstance(&plan);

    // attach the custom events handler
    dot->setEvents(&radio_events);

    if (!dot->getStandbyFlag()) {
        // start from a well-known state
        logInfo("defaulting Dot configuration");
        dot->resetConfig();
        dot->resetNetworkSession();

        logInfo("setting data rate to %d", tx_data_rate);
        if (dot->setTxDataRate(tx_data_rate) != mDot::MDOT_OK) {
            logError("failed to set data rate");
        }

        logInfo("setting join RX2 data rate to %d", join_rx2_data_rate);
        if (dot->setJoinRx2DataRate(join_rx2_data_rate) != mDot::MDOT_OK) {
            logError("failed to set join RX2 data rate");
        }

        // update configuration if necessary
        if (dot->getJoinMode() != mDot::OTA) {
            logInfo("changing network join mode to OTA");
            if (dot->setJoinMode(mDot::OTA) != mDot::MDOT_OK) {
                logError("failed to set network join mode to OTA");
            }
        }
        update_ota_config_id_key(network_id, network_key, frequency_sub_band, true, false /*ack*/);

        dot->setAdr(false); // @todo enable

        dot->setDisableDutyCycle(true);

        // save changes to configuration
        logInfo("saving configuration");
        if (!dot->saveConfig()) {
            logError("failed to save configuration");
        }

        // display configuration
        display_config();

        dot->setLogLevel(mts::MTSLog::ERROR_LEVEL);
    } else {
        // restore the saved session if the dot woke from deepsleep mode
        // useful to use with deepsleep because session info is otherwise lost when the dot enters deepsleep
        logInfo("restoring network session from NVM");
        dot->restoreNetworkSession();
    }

    while (true) {
        if (!in_class_c_mode) {

            // join network if not joined
            if (!dot->getNetworkJoinStatus()) {
                join_network();

                LoRaWANCredentials_t creds;
                get_current_credentials(&creds);
                radio_events.OnClassAJoinSucceeded(&creds);

                // turn duty cycle back on after joining
                dot->setDisableDutyCycle(false);
            }

            // send some data in CayenneLPP format
            CayenneLPP payload(50);
            payload.addAnalogOutput(1, moisture.read());
            payload.addTemperature(2, read_temperature());
            payload.addDigitalOutput(3, APP_VERSION);

            // Copy Cayenne buffer
            vector<uint8_t>* tx_data = new vector<uint8_t>();
            for (size_t ix = 0; ix < payload.getSize(); ix++) {
                tx_data->push_back(payload.getBuffer()[ix]);
            }

            // Queue uplink message
            UplinkMessage* uplink = new UplinkMessage();
            uplink->port = 5;
            uplink->data = tx_data;

            send_packet(uplink);
        }

        uint32_t sleep_time = calculate_actual_sleep_time(3 + (rand() % 8));
        // logInfo("going to wait %d seconds for duty-cycle...", sleep_time);

        // @todo: in class A can go to deepsleep, in class C cannot
        if (in_class_c_mode) {
            wait(sleep_time);
            continue; // for now just send as fast as possible
        }
        else {
            wait(sleep_time); // @todo, wait for all frames to be processed before going to sleep. need a wakelock.
            // sleep_wake_rtc_or_interrupt(10, deep_sleep);
        }
    }

    return 0;
}
