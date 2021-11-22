#include <jsoncpp/json/json.h>
#include <unistd.h>
#include <functional>
#include "mqtt_handler.h" 

using namespace std;
using namespace std::placeholders;

bool g_quit = false;

// json type message parse
void poseCallback(const char *msg, int len)
{
    Json::Value jpayload;
    Json::Reader jreader;

    if(!jreader.parse(msg, jpayload)) {
        cout << "message not json type" << endl;
    } else  {
        if(jpayload["xyz"].isArray() && (jpayload["xyz"].size() > 2)) {
            cout << "xyz: [ " << jpayload["xyz"][0].asDouble() << ", " << jpayload["xyz"][1].asDouble() << ", " << jpayload["xyz"][2].asDouble() << " ]" << endl;
        }
     }
}

void quitCallback(const char *msg, int len)
{
    g_quit = true;
}

void echoCallback(MqttHandler *mqtt_hd, const char *msg, int len)
{
    mqtt_hd->mqttPublish("/echo/back", 0, false, msg, len);
}

int main(int argc, char **argv)
{
    MqttHandler *mqtt_hd = new MqttHandler("127.0.0.1:1883", "put_an_unique_code_here", "username", "password");
    // subscribe
    mqtt_hd->mqttSubscribe("/localization/pose", 0, poseCallback);
    mqtt_hd->mqttSubscribe("/quit", 0, quitCallback);
    mqtt_hd->mqttSubscribe("/echo", 0, bind(echoCallback,mqtt_hd,_1,_2));

    int alive_cnt = 0;
    while(!g_quit) {
        // keep mqtt running
        mqtt_hd->spinOnce();

        // send alive message every 2 seconds
        alive_cnt ++;
        if(alive_cnt == 20) {
            // publish string
            mqtt_hd->mqttPublish("/alive/string", 2, false, "i am alive!", 11);
            // publish json
            Json::Value jpayload;
            jpayload["cmd"] = "alive";
            string message = jpayload.toStyledString();
            mqtt_hd->mqttPublish("/alive/json", 2, false, message.c_str(), message.length());
        
            alive_cnt = 0;
        }

        usleep(100000);
    }
    cout << "exit!" << endl;
}
