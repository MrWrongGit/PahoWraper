#include "lib/mqtt_handler.h"

using namespace std;

MqttHandler::MqttHandler(string broker_addr, string client_id, string username, string password)
{
    // flags
    _deliver_token = 0;
    _reconnect_cnt = 0;
    // states
    _mqtt_puber_free = true;
    _mqtt_connected  = false;
    // connection info
    _mqtt_broker_addr = broker_addr;
    _mqtt_client_id = client_id;
    _mqtt_username   = username;
    _mqtt_password   = password;
}

MqttHandler::~MqttHandler()
{
    mqttDisconnect();
    
    // release mem
    for(int i=0; i<_mqtt_pub_queue.size(); i++) {
        free(_mqtt_pub_queue[i]->message);
        delete _mqtt_pub_queue[i];
    }
    _mqtt_pub_queue.clear();

    // release mem
    std::map<string, MqttSubMeta *>::iterator iter;
    for(iter=_mqtt_sub_map.begin(); iter!=_mqtt_sub_map.end(); iter++)
        delete iter->second;
    _mqtt_sub_map.clear();

    debug("MqttHD: exit!");
}

void MqttHandler::spinOnce()
{
    // connect or reconnect
    if(_mqtt_connected==false) {
        // reconnect delay
        if(_reconnect_cnt > 0) {
            _reconnect_cnt --;
            return;
        }
        // do connect
        if(mqttConnect(_mqtt_broker_addr, _mqtt_client_id, _mqtt_username, _mqtt_password)) {
            _mqtt_connected = true;
            // re-subscribe all topic when connected
            std::map<string, MqttSubMeta *>::iterator iter;
            for(iter=_mqtt_sub_map.begin(); iter!=_mqtt_sub_map.end(); iter++)
                MQTTClient_subscribe(_client, iter->first.c_str(), iter->second->qos);

            // release all buffered message, this is not the best stratege, but the most simple stratege
            for(int i=0; i<_mqtt_pub_queue.size(); i++) {
                free(_mqtt_pub_queue[i]->message);
                delete _mqtt_pub_queue[i];
            }
            _mqtt_pub_queue.clear();
            _mqtt_puber_free = true;
        } else {
            // connect fail, reset delay counter
            _reconnect_cnt = RECONNECT_DELAY;
            debug("MqttHD: connect to mqtt broker fail! retry later");
        }
        return ;
    }

    // do publish message here
    if(_mqtt_puber_free && (_mqtt_pub_queue.size()>0)) {
        _mqtt_puber_free = false;
        mqttDoPublish();
    }
}

void MqttHandler::mqttSubscribe(string msg_topic, uint8_t msg_qos, void (*callback)(const char *, int))
{
    // add to map for connect or reconnect
    _mqtt_sub_map.insert(map<string, MqttSubMeta *>::value_type(msg_topic.data(), new MqttSubMeta(msg_qos, callback)));
    // subscribe directly if already connected
    if(_mqtt_connected)
        MQTTClient_subscribe(_client, msg_topic.c_str(), msg_qos);
}

void MqttHandler::mqttUnsubscribe(string msg_topic)
{
    std::map<string, MqttSubMeta *>::iterator iter = _mqtt_sub_map.find(msg_topic);
    if(iter!=_mqtt_sub_map.end()) {
        // free mem
        delete iter->second;
        // remove
        _mqtt_sub_map.erase(iter);

        // unsubscribe directly if already connected
        if(_mqtt_connected)
            MQTTClient_unsubscribe(_client, msg_topic.c_str());
    } 
}

void MqttHandler::mqttPublish(string msg_topic, uint8_t msg_qos, bool msg_retained, const char *message, int len)
{
    if(!_mqtt_connected)
        return;
    // queue is full, and low qos message will be abandon
    if((_mqtt_pub_queue.size()>PUB_QUEUE_DEEPTH) && (msg_qos < 1))
        return;
    // push mqtt msg to queue for further diliver
    _mqtt_pub_queue.push_back(new MqttPubMeta(msg_topic, msg_qos, msg_retained ? 1 : 0, message, len));
}

void MqttHandler::mqttDoPublish()
{
    MqttPubMeta *mqtt_pub_meta = _mqtt_pub_queue.front();

    // send mqtt msg
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = mqtt_pub_meta->message;
    pubmsg.payloadlen = mqtt_pub_meta->len;
    pubmsg.qos = mqtt_pub_meta->qos;
    pubmsg.retained = mqtt_pub_meta->retained;

    MQTTClient_publishMessage(_client, mqtt_pub_meta->topic.c_str(), &pubmsg, &_deliver_token);
    // if qos = 0, mqttMsgDelivered will not callback
    if(mqtt_pub_meta->qos < 1) {
        // release memory
        free(_mqtt_pub_queue.front()->message);
        delete _mqtt_pub_queue.front();
        _mqtt_pub_queue.erase(_mqtt_pub_queue.begin());
        // mqtt message send finish
        _mqtt_puber_free = true;
    }
}

bool MqttHandler::mqttConnect(string broker_addr, string client_id, string username, string password)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.username = username.data();
    conn_opts.password = password.data();
    MQTTClient_create(&_client, broker_addr.c_str(), client_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTClient_setCallbacks(_client, this, mqttConnectLost, mqttMsgArrive, mqttMsgDelivered);
    // connect to broker
    if(MQTTClient_connect(_client, &conn_opts)!= MQTTCLIENT_SUCCESS)
        return false;
    debug("MqttHD: connected to mqtt broker!");
    return true;
}

void MqttHandler::mqttDisconnect()
{
    _mqtt_connected = false;
    MQTTClient_disconnect(_client, 10000);
    MQTTClient_destroy(&_client);
}

/************************* MQTT Callbacks *************************/
void MqttHandler::mqttMsgDelivered(void *context, MQTTClient_deliveryToken dt)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    if(mqtt_handler->_deliver_token!=dt)
        debug("MqttHD: delivery token not match!");
    // release memory
    free(mqtt_handler->_mqtt_pub_queue.front()->message);
    delete mqtt_handler->_mqtt_pub_queue.front();
    mqtt_handler->_mqtt_pub_queue.erase(mqtt_handler->_mqtt_pub_queue.begin());
    // mqtt message send finish
    mqtt_handler->_mqtt_puber_free = true;
}

int MqttHandler::mqttMsgArrive(void *context, char *msg_topic, int msg_len, MQTTClient_message *message)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    // search related MqttSubMeta object by msg_topic
    std::map<string, MqttSubMeta *>::iterator iter = mqtt_handler->_mqtt_sub_map.find(msg_topic);
    if(iter!=mqtt_handler->_mqtt_sub_map.end()) {
        // diliver message to callback
        iter->second->callback((const char *)(message->payload), msg_len);
    } else {
        debug(string("MqttHD: could not find callback for topic ") + msg_topic);
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(msg_topic);
    return 1;
}

void MqttHandler::mqttConnectLost(void *context, char *cause)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    // set flag, reconnect in spinOnce()
    mqtt_handler->_mqtt_connected = false;
    debug("MqttHD: connection lost!");
}
