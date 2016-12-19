/*
The MIT License (MIT)

Copyright (c) 2016 British Broadcasting Corporation.
This software is provided by Lancaster University by arrangement with the BBC.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "MicroBit.h"
#include "MicroBitSerial.h"
#include "MicroBitUARTService.h"

#include "MQTTSNPacket.h"

MicroBit uBit;
MicroBitUARTService *uart;
MicroBitSerial serial(USBTX, USBRX); 

int connected = 0;

int rc = 0;
unsigned char buf[200];
int buflen = sizeof(buf);
MQTTSN_topicid topic;
int len = 0;
int dup = 0;
int qos = 0;
int retained = 0;
short packetid = 0;
//  char *topicname = "a long topic name";
MQTTSNPacket_connectData options = MQTTSNPacket_connectData_initializer;

#define BLE_CHUNK_SIZE 18

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

int send_in_chunks(unsigned char *buffer, size_t length, size_t chunk_size)
{
    ssize_t n;
    const unsigned char *p = buffer;
    while (length > 0)
    {
        n = uart->send(p, min(length, chunk_size));
        if (n <= 0) break;
        p += n;
        length -= n;
    }
    return (n <= 0) ? -1 : 0;
}


/** MQTT-SN transport helpers **/
int transport_sendPacketBuffer(unsigned char* buf, int buflen)
{
    int rc = send_in_chunks(buf, buflen, BLE_CHUNK_SIZE);
    return rc;
}


int transport_getdata(unsigned char* buf, int count)
{
    int rc = uart->read(buf, count, ASYNC);
    return rc;
}

int transport_open()
{
    // Note GATT table size increased from default in MicroBitConfig.h
    // #define MICROBIT_SD_GATT_TABLE_SIZE             0x500
    uart = new MicroBitUARTService(*uBit.ble, 128, 128);

    return 0;
}

int transport_close()
{
    return 0;
}


void onConnected(MicroBitEvent)
{
    uBit.display.scroll("C");

    connected = 1;

    options.clientID.cstring = microbit_friendly_name();
    len = MQTTSNSerialize_connect(buf, buflen, &options);
    rc = transport_sendPacketBuffer(buf, len);

    /* wait for connack */
    if (MQTTSNPacket_read(buf, buflen, transport_getdata) == MQTTSN_CONNACK)
    {
        int connack_rc = -1;

        if (MQTTSNDeserialize_connack(&connack_rc, buf, buflen) != 1 || connack_rc != 0)
        {
            // TODO goto exit;
        }
        else 
            printf("connected rc %d\n", connack_rc);
    } else {
        // TODO exit
    }
}

void onDisconnected(MicroBitEvent)
{
    uBit.display.scroll("D");
    connected = 0;
}

// publishes an MQTT-SN message. 
// keep in mind that BLE payloads over >19bytes might not work that well so try to 
// stay concise!
void publish(char * payload) {
    /* publish with short name */
    topic.type = MQTTSN_TOPIC_TYPE_SHORT;
    memcpy(topic.data.short_name, "mb", 2);

    len = MQTTSNSerialize_publish(buf, buflen, dup, qos, retained, packetid,
            topic, (unsigned char *) payload, strlen(payload));
    rc = transport_sendPacketBuffer(buf, len);
}


void onButtonA(MicroBitEvent)
{
    if (connected == 0) {
        return;
    }
    publish("A");
}

void onButtonB(MicroBitEvent)
{
    if (connected == 0) {
        return;
    }
    publish("BBB");
}

void onButtonAB(MicroBitEvent)
{
    if (connected == 0) {
        return;
    }

    char mypayload[250];
    sprintf((char*)mypayload, 
        "{\"x\":%d,\"y\":%d,\"z\":%d}",
        uBit.accelerometer.getX(), 
        uBit.accelerometer.getY(), 
        uBit.accelerometer.getZ()
    ); 

    publish(mypayload);
}

int main()
{
    // Initialise the micro:bit runtime.
    uBit.init();

    uBit.messageBus.listen(MICROBIT_ID_BLE, MICROBIT_BLE_EVT_CONNECTED, onConnected);
    uBit.messageBus.listen(MICROBIT_ID_BLE, MICROBIT_BLE_EVT_DISCONNECTED, onDisconnected);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_A, MICROBIT_BUTTON_EVT_CLICK, onButtonA);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_B, MICROBIT_BUTTON_EVT_CLICK, onButtonB);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_AB, MICROBIT_BUTTON_EVT_CLICK, onButtonAB);

    transport_open();

    // If main exits, there may still be other fibers running or registered event handlers etc.
    // Simply release this fiber, which will mean we enter the scheduler. Worse case, we then
    // sit in the idle task forever, in a power efficient sleep.
    release_fiber();
}
