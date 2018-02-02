#ifndef _FOTA_HELPER
#define _FOTA_HELPER

#include "mbed.h"
#include "mDot.h"
#include "RadioEvent.h"
#include "CayenneLPP.h"

// // fwd declaration
void send_mac_msg(uint8_t port, vector<uint8_t>* data);
static void class_switch(char cls);

// Custom event handler for automatically displaying RX data
RadioEvent radio_events(&send_mac_msg, &class_switch);

typedef struct {
    uint8_t port;
    bool is_mac;
    std::vector<uint8_t>* data;
} UplinkMessage;

vector<UplinkMessage*>* message_queue = new vector<UplinkMessage*>();
static bool in_class_c_mode = false;

static void get_current_credentials(LoRaWANCredentials_t* creds) {
    memcpy(creds->DevAddr, &(dot->getNetworkAddress()[0]), 4);
    memcpy(creds->NwkSKey, &(dot->getNetworkSessionKey()[0]), 16);
    memcpy(creds->AppSKey, &(dot->getDataSessionKey()[0]), 16);

    creds->UplinkCounter = dot->getUpLinkCounter();
    creds->DownlinkCounter = dot->getDownLinkCounter();

    creds->TxDataRate = dot->getTxDataRate();
    creds->RxDataRate = dot->getRxDataRate();

    creds->Rx2Frequency = dot->getJoinRx2Frequency();
    // somehow this still goes wrong when switching back to class A...
}

static void set_class_a_creds();

static void set_class_c_creds() {
    LoRaWANCredentials_t* credentials = radio_events.GetClassCCredentials();

    // logInfo("Switching to class C (DevAddr=%s)", mts::Text::bin2hexString(credentials->DevAddr, 4).c_str());

    // @todo: this is weird, ah well...
    std::vector<uint8_t> address;
    address.push_back(credentials->DevAddr[3]);
    address.push_back(credentials->DevAddr[2]);
    address.push_back(credentials->DevAddr[1]);
    address.push_back(credentials->DevAddr[0]);
    std::vector<uint8_t> nwkskey(credentials->NwkSKey, credentials->NwkSKey + 16);
    std::vector<uint8_t> appskey(credentials->AppSKey, credentials->AppSKey + 16);

    dot->setNetworkAddress(address);
    dot->setNetworkSessionKey(nwkskey);
    dot->setDataSessionKey(appskey);

    // dot->setTxDataRate(credentials->TxDataRate);
    // dot->setRxDataRate(credentials->RxDataRate);

    dot->setUpLinkCounter(credentials->UplinkCounter);
    dot->setDownLinkCounter(credentials->DownlinkCounter);

    // update_network_link_check_config(0, 0);

    // fake MAC command to switch to DR5
    std::vector<uint8_t> mac_cmd;
    mac_cmd.push_back(0x05);
    mac_cmd.push_back(credentials->RxDataRate);
    mac_cmd.push_back(credentials->Rx2Frequency & 0xff);
    mac_cmd.push_back(credentials->Rx2Frequency >> 8 & 0xff);
    mac_cmd.push_back(credentials->Rx2Frequency >> 16 & 0xff);

    int32_t ret;
    if ((ret = dot->injectMacCommand(mac_cmd)) != mDot::MDOT_OK) {
        printf("Failed to set Class C Rx parameters (%lu)\n", ret);
        set_class_a_creds();
        return;
    }

    dot->setClass("C");

    printf("Switched to class C\n");

    radio_events.switchedToClassC();
}

static void set_class_a_creds() {
    LoRaWANCredentials_t* credentials = radio_events.GetClassACredentials();

    // logInfo("Switching to class A (DevAddr=%s)", mts::Text::bin2hexString(credentials->DevAddr, 4).c_str());

    std::vector<uint8_t> address(credentials->DevAddr, credentials->DevAddr + 4);
    std::vector<uint8_t> nwkskey(credentials->NwkSKey, credentials->NwkSKey + 16);
    std::vector<uint8_t> appskey(credentials->AppSKey, credentials->AppSKey + 16);

    dot->setNetworkAddress(address);
    dot->setNetworkSessionKey(nwkskey);
    dot->setDataSessionKey(appskey);

    // dot->setTxDataRate(credentials->TxDataRate);
    // dot->setRxDataRate(credentials->RxDataRate);

    dot->setUpLinkCounter(credentials->UplinkCounter);
    dot->setDownLinkCounter(credentials->DownlinkCounter);

    // update_network_link_check_config(3, 5);

    // reset rx2 datarate... however, this gets rejected because the datarate is not valid for receiving
    // wondering if we actually need to do this...
    std::vector<uint8_t> mac_cmd;
    mac_cmd.push_back(0x05);
    mac_cmd.push_back(credentials->RxDataRate);
    mac_cmd.push_back(credentials->Rx2Frequency & 0xff);
    mac_cmd.push_back(credentials->Rx2Frequency >> 8 & 0xff);
    mac_cmd.push_back(credentials->Rx2Frequency >> 16 & 0xff);

    // printf("Setting RX2 freq to %02x %02x %02x\n", credentials->Rx2Frequency & 0xff,
    //     credentials->Rx2Frequency >> 8 & 0xff, credentials->Rx2Frequency >> 16 & 0xff);

    // int32_t ret;
    // if ((ret = dot->injectMacCommand(mac_cmd)) != mDot::MDOT_OK) {
    //     printf("Failed to set Class A Rx parameters (%lu)\n", ret);
    //     // don't fail here...
    // }

    dot->setClass("A");

    printf("Switched to class A\n");

    radio_events.switchedToClassA();
}

void send_packet(UplinkMessage* message) {
    if (message_queue->size() > 0 && !message->is_mac) {
        // logInfo("MAC messages in queue, dropping this packet");
        delete message->data;
        delete message;
    }
    else {
        // otherwise, add to queue
        message_queue->push_back(message);
    }

    // take the first item from the queue
    UplinkMessage* m = message_queue->at(0);

    // OK... soooooo we can only send in Class A
    bool switched_creds = false;
    if (in_class_c_mode) {
        logError("Cannot send in Class C mode. Switch back to Class A first.\n");
        return;
    }

    dot->setAppPort(m->port);

    printf("[INFO] Going to send a message. port=%d, dr=%s, data=", m->port, dot->getDateRateDetails(dot->getTxDataRate()).c_str());
    for (size_t ix = 0; ix < m->data->size(); ix++) {
        printf("%02x ", m->data->at(ix));
    }
    printf("\n");

    uint32_t ret;

    radio_events.OnTx(dot->getUpLinkCounter() + 1);

    if (m->is_mac) {
#if MBED_CONF_APP_ACK_MAC_COMMANDS == 1
        dot->setAck(true);
#endif

        ret = dot->send(*(m->data));

#if MBED_CONF_APP_ACK_MAC_COMMANDS == 1
        dot->setAck(false);
#endif
    }
    else {
        dot->setAck(false);
        ret = dot->send(*(m->data));
    }

    if (ret != mDot::MDOT_OK) {
        printf("[ERROR] Failed to send data to %s [%d][%s]\n", dot->getJoinMode() == mDot::PEER_TO_PEER ? "peer" : "gateway", ret, mDot::getReturnCodeString(ret).c_str());
    } else {
        if (m->is_mac) {
            printf("successfully sent data to gateway\n");
        }
    }

    // Message was sent, or was not mac message? remove from queue
    if (ret == mDot::MDOT_OK || !m->is_mac) {
        // logInfo("Removing first item from the queue\n");

        // remove message from the queue
        delete m->data;
        delete m;
        message_queue->erase(message_queue->begin());
    }

    // update credentials with the new counter
    LoRaWANCredentials_t* creds = in_class_c_mode ?
        radio_events.GetClassCCredentials() :
        radio_events.GetClassACredentials();

    creds->UplinkCounter = dot->getUpLinkCounter();
    creds->DownlinkCounter = dot->getDownLinkCounter();

    // switch back
    if (switched_creds) {
        // switch to class A credentials
        set_class_c_creds();
    }
}

void send_packet(CayenneLPP* payload, uint8_t port) {
    // Copy Cayenne buffer
    vector<uint8_t>* tx_data = new vector<uint8_t>();
    for (size_t ix = 0; ix < payload->getSize(); ix++) {
        tx_data->push_back(payload->getBuffer()[ix]);
    }

    // Queue uplink message
    UplinkMessage* uplink = new UplinkMessage();
    uplink->port = 5;
    uplink->data = tx_data;

    send_packet(uplink);
}

void send_mac_msg(uint8_t port, std::vector<uint8_t>* data) {
    UplinkMessage* m = new UplinkMessage();
    m->is_mac = true;
    m->data = data;
    m->port = port;

    message_queue->push_back(m);
}

static void class_switch(char cls) {
    logInfo("class_switch to %c", cls);

    // in class A mode? then back up credentials and counters...
    if (!in_class_c_mode) {
        LoRaWANCredentials_t creds;
        get_current_credentials(&creds);
        radio_events.UpdateClassACredentials(&creds);
    }

    // @todo; make enum
    if (cls == 'C') {
        in_class_c_mode = true;
        set_class_c_creds();
    }
    else if (cls == 'A') {
        in_class_c_mode = false;
        set_class_a_creds();
    }
    else {
        logError("Cannot switch to class %c", cls);
    }
}


#endif // _FOTA_HELPER
