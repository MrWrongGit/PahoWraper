#include "mqtt_handler.h"

using namespace std;

MqttHandler::MqttHandler(string broker_addr, string client_id, string username, string password)
{
    _pub_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
    // flags
    _mqtt_puber_free = true;
    _mqtt_connected  = false;
    // connection info
    _mqtt_broker_addr = broker_addr;
    _mqtt_client_id = client_id;
    _mqtt_username   = username;
    _mqtt_password   = password;
    // connect to broker
    mqttConnect(_mqtt_broker_addr, _mqtt_client_id, _mqtt_username, _mqtt_password);
}

MqttHandler::~MqttHandler()
{
    // disconnect
    _mqtt_connected = false;
    MQTTAsync_destroy(&_client);
    // release mem
    pthread_mutex_lock(&_pub_queue_mutex); {
        for(int i=0; i<_mqtt_pub_queue.size(); i++) {
            free(_mqtt_pub_queue[i]->message);
            delete _mqtt_pub_queue[i];
        }
        _mqtt_pub_queue.clear();
    }
    pthread_mutex_unlock(&_pub_queue_mutex);
    pthread_mutex_destroy(&_pub_queue_mutex);
    // release mem
    std::map<string, MqttSubMeta *>::iterator iter;
    for(iter=_mqtt_sub_map.begin(); iter!=_mqtt_sub_map.end(); iter++)
        delete iter->second;
    _mqtt_sub_map.clear();
    debug("MqttHD: exit!");
}

void MqttHandler::spinOnce()
{
    // do publish message here
    if(_mqtt_connected && _mqtt_puber_free && (_mqtt_pub_queue.size()>0)) {
        _mqtt_puber_free = false;
        mqttDoPublish();
    }
}

void MqttHandler::mqttSubscribe(string msg_topic, uint8_t msg_qos, std::function<void(const char *, int)> callback)
{
    // add to map for connect or reconnect
    _mqtt_sub_map.insert(map<string, MqttSubMeta *>::value_type(msg_topic.data(), new MqttSubMeta(msg_qos, callback)));
    // subscribe directly if already connected
    if(_mqtt_connected)
        MQTTAsync_subscribe(_client, msg_topic.c_str(), msg_qos, NULL);
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
            MQTTAsync_unsubscribe(_client, msg_topic.c_str(), NULL);
    } 
}

void MqttHandler::mqttPublish(string msg_topic, uint8_t msg_qos, bool msg_retained, const char *message, int len)
{
    if(!_mqtt_connected && (msg_qos < 1))
        return;
    // queue is full, and low qos message will be abandon
    if((_mqtt_pub_queue.size()>PUB_QUEUE_DEEPTH) && (msg_qos < 1))
        return;
    // push mqtt msg to queue for further diliver
    pthread_mutex_lock(&_pub_queue_mutex); {
        _mqtt_pub_queue.push_back(new MqttPubMeta(msg_topic, msg_qos, msg_retained ? 1 : 0, message, len));
    }
    pthread_mutex_unlock(&_pub_queue_mutex);
}

void MqttHandler::mqttDoPublish()
{
    // try lock, if fail, try in next loop
    if(pthread_mutex_trylock(&_pub_queue_mutex)==0) {
        MqttPubMeta *mqtt_pub_meta = _mqtt_pub_queue.front();
        // set callback
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        opts.onSuccess = mqttMsgSendSuccess;
        opts.onFailure = mqttMsgSendFailed;
        opts.context = this;
        // send mqtt msg
        MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
        pubmsg.payload = mqtt_pub_meta->message;
        pubmsg.payloadlen = mqtt_pub_meta->len;
        pubmsg.qos = mqtt_pub_meta->qos;
        pubmsg.retained = mqtt_pub_meta->retained;
        if(MQTTAsync_sendMessage(_client, mqtt_pub_meta->topic.c_str(), &pubmsg, &opts) != MQTTASYNC_SUCCESS)
            _mqtt_puber_free = true; // reset _mqtt_puber_free for send again

        pthread_mutex_unlock(&_pub_queue_mutex);
    }
}

bool MqttHandler::mqttConnect(string broker_addr, string client_id, string username, string password)
{
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    // basic setup
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = username.data();
    conn_opts.password = password.data();
    // connect callback
    //conn_opts.onSuccess = mqttConnected; // this is not need since we got mqttReconnected()
    conn_opts.onFailure = mqttConnectFailed;
    conn_opts.context = this;
    // auto reconnect
    conn_opts.automaticReconnect = 1;
    conn_opts.minRetryInterval = 1;
    conn_opts.maxRetryInterval = 4;
 
    MQTTAsync_create(&_client, broker_addr.c_str(), client_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, NULL);
    // this fuctiong must called after MQTTAsync_create()
    MQTTAsync_setConnected(_client, this, mqttReconnected);
    MQTTAsync_setCallbacks(_client, this, mqttConnectLost, mqttMsgArrive, NULL);
    // connect to broker
    if(MQTTAsync_connect(_client, &conn_opts) != MQTTASYNC_SUCCESS) {
        debug("MqttHD: start connect failed!");
        return false;
    }
    debug("MqttHD: start connecting...");
    return true;
}

/************************* MQTT Callbacks *************************/
int MqttHandler::mqttMsgArrive(void *context, char *msg_topic, int topic_len, MQTTAsync_message *message)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    // search related MqttSubMeta object by msg_topic
    std::map<string, MqttSubMeta *>::iterator iter = mqtt_handler->_mqtt_sub_map.find(msg_topic);
    if(iter!=mqtt_handler->_mqtt_sub_map.end()) {
        // diliver message to callback
        iter->second->callback((const char *)(message->payload), message->payloadlen);
    } else {
        debug(string("MqttHD: could not find callback for topic ") + msg_topic);
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(msg_topic);
    return 1;
}

void MqttHandler::mqttConnectLost(void *context, char *cause)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    mqtt_handler->_mqtt_connected = false;
    mqtt_handler->_mqtt_puber_free = false;
    debug("MqttHD: on connection lost!");
}

void MqttHandler::mqttReconnected(void *context, char *cause)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    // re-subscribe all topic when connected
    std::map<string, MqttSubMeta *>::iterator iter;
    for(iter=mqtt_handler->_mqtt_sub_map.begin(); iter!=mqtt_handler->_mqtt_sub_map.end(); iter++)
        MQTTAsync_subscribe(mqtt_handler->_client, iter->first.c_str(), iter->second->qos, NULL);
    // release qos0 message
    pthread_mutex_lock(&(mqtt_handler->_pub_queue_mutex)); {
        auto iter_q = mqtt_handler->_mqtt_pub_queue.begin();
        while(iter_q != mqtt_handler->_mqtt_pub_queue.end()) {
            if((*iter_q)->qos < 1) {
				free((*iter_q)->message);
				delete (*iter_q);
                iter_q = mqtt_handler->_mqtt_pub_queue.erase(iter_q);
			}
            else
                ++ iter_q;
        }
    }
    pthread_mutex_unlock(&(mqtt_handler->_pub_queue_mutex));
    // set flags
    mqtt_handler->_mqtt_connected = true;
    mqtt_handler->_mqtt_puber_free = true;
    debug("MqttHD: on connected!");
}

void MqttHandler::mqttConnectFailed(void* context, MQTTAsync_failureData* response)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    mqtt_handler->_mqtt_connected = false;
    mqtt_handler->_mqtt_puber_free = false;
    debug("MqttHD: on connect failed!");
}

void MqttHandler::mqttMsgSendSuccess(void* context, MQTTAsync_successData* response)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    // release memory
    pthread_mutex_lock(&(mqtt_handler->_pub_queue_mutex)); {
        free(mqtt_handler->_mqtt_pub_queue.front()->message);
        delete mqtt_handler->_mqtt_pub_queue.front();
        mqtt_handler->_mqtt_pub_queue.erase(mqtt_handler->_mqtt_pub_queue.begin());
    }
    pthread_mutex_unlock(&(mqtt_handler->_pub_queue_mutex));
    // mqtt message send finish
    mqtt_handler->_mqtt_puber_free = true;
}

void MqttHandler::mqttMsgSendFailed(void* context, MQTTAsync_failureData* response)
{
    MqttHandler *mqtt_handler = (MqttHandler *)context;
    // reset _mqtt_puber_free for send again
    mqtt_handler->_mqtt_puber_free = true;
}
