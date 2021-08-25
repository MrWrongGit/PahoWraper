#ifndef __MQTT_HANDLER_H__
#define __MQTT_HANDLER_H__

#include <iostream>
#include <vector>
#include <map>
#include<cstring>
extern "C" {
#include "MQTTClient.h"
#include "MQTTClientPersistence.h"
}

using namespace std;

/*********************** configuration ***********************/
#define RECONNECT_DELAY 60
#define PUB_QUEUE_DEEPTH 200
#define DEBUG_ENABLE

/************************** debug ***************************/
static void debug(string info) {
    #ifdef DEBUG_ENABLE
    cout << info << endl;
    #endif
}

/******************* private data structure *******************/
class MqttSubMeta
{
public:
    MqttSubMeta(uint8_t qos, void (*callback)(const char *, int)) :  qos(qos), callback(callback) {};

    uint8_t qos;
    void (*callback)(const char *msg, int len);
};

class MqttPubMeta
{
public:
    MqttPubMeta(string topic, uint8_t qos, uint8_t retained, const char *message, int len) :  topic(topic), qos(qos), retained(retained), len(len) {
        this->message = (char *)malloc(len);
        memcpy(this->message, (void *)message, len);
    };

    string topic;
    uint8_t qos;
    uint8_t retained;
    char *message;
    int len;
};

/********************** libpaho wraper  **********************/
class MqttHandler
{
public:
    MqttHandler(string broker_addr, string client_id, string username, string password);
    ~MqttHandler();

    void mqttSubscribe(string mqtt_topic, uint8_t mqtt_qos, void (*callback)(const char *, int ));
    void mqttPublish(string msg_topic, uint8_t msg_qos, bool msg_retained, const char *message, int len);
    void spinOnce();

private:
    // flags
    MQTTClient _client;
    MQTTClient_deliveryToken _deliver_token;
    uint8_t _reconnect_cnt;
    // states
    bool _mqtt_puber_free;
    bool _mqtt_connected;
    // connection info
    string _mqtt_broker_addr;
    string _mqtt_client_id;
    string _mqtt_username;
    string _mqtt_password;
    // mqtt sub and pub data
    std::vector<MqttPubMeta *> _mqtt_pub_queue;
    std::map<string, MqttSubMeta *> _mqtt_sub_map;

    void mqttDoPublish();
    bool mqttConnect(string broker_addr, string client_id, string username, string password);
    void mqttDisconnect();
    static void mqttMsgDelivered(void *context, MQTTClient_deliveryToken dt);
    static int  mqttMsgArrive(void *context, char *msg_topic, int msg_len, MQTTClient_message *message);
    static void mqttConnectLost(void *context, char *cause);
};

#endif