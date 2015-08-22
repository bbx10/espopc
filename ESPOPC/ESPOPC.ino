/*
ESPOPC -- Open Pixel Control server for ESP8266.

The MIT License (MIT)

Copyright (c) 2015 by bbx10node@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

const char* ssid = "xxxxxx";
const char* password = "yyyyyyyyyyyyyyyy";

MDNSResponder mdns;
// Actual name will be "espopc.local"
const char myDNSName[] = "espopc";

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(7890);

#define OSCDEBUG    (0)

#define NEOPIXELBUS (1)
#if NEOPIXELBUS
#include <NeoPixelBus.h>
#define SET_PIXEL_COLOR SetPixelColor
#define SHOW            Show
#define BEGIN           Begin
#else
#include <Adafruit_NeoPixel.h>
#define SET_PIXEL_COLOR setPixelColor
#define SHOW            show
#define BEGIN           begin
#endif
const int MAX_LEDS = 1024;
const int LED_PIN = 2;

#if NEOPIXELBUS
NeoPixelBus strip = NeoPixelBus(MAX_LEDS, LED_PIN);
// NeoPixelBus strip = NeoPixelBus(pixelCount, pixelPin, NEO_RGB);
//
// some pixels require the color components to be in a different order
// using the flag NEO_GRB will use the order; green, red, then blue.
//
#else
Adafruit_NeoPixel strip = Adafruit_NeoPixel(MAX_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

// Gamma correction 2.2 look up table
uint8_t GammaLUT[256];

void fillGammaLUT(float gamma)
{
  int i;

  for (i = 0; i < 256; i++) {
    float intensity = (float)i / 255.0;
    GammaLUT[i] = (uint8_t)(pow(intensity, gamma) * 255.0);
    //Serial.printf("GammaLUT[%d] = %u\r\n", i, GammaLUT[i]);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("");

  // this resets all the neopixels to an off state
  strip.BEGIN();
  strip.SHOW();

  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Print the IP address
  Serial.println(WiFi.localIP());

  // Set up mDNS responder:
  if (!mdns.begin(myDNSName, WiFi.localIP())) {
    Serial.println("Error setting up MDNS responder!");
  }
  else {
    Serial.println("mDNS responder started");
    Serial.printf("My name is [%s]\r\n", myDNSName);
  }

  // Start the server listening for incoming client connections
  server.begin();
  Serial.println("Server listening on port 7890");

  fillGammaLUT(2.2);
}

WiFiClient client;

void loop() {
  clientEvent();
}

#define minsize(x,y) (((x)<(y))?(x):(y))

void clientEvent()
{
  static int packetParse = 0;
  static uint8_t pktChannel, pktCommand;
  static uint16_t pktLength, pktLengthAdjusted, bytesIn;
  static uint8_t pktData[MAX_LEDS*3];
  uint16_t bytesRead;
  size_t frame_count = 0, frame_discard = 0;

  if (!client) {
    // Check if a client has connected
    client = server.available();
    if (!client) {
      return;
    }
    Serial.println("new OPC client");
  }

  if (!client.connected()) {
    Serial.println("OPC client disconnected");
    client = server.available();
    if (!client) {
      return;
    }
  }

  while (client.available()) {
    switch (packetParse) {
      case 0: // Get pktChannel
        pktChannel = client.read();
        packetParse++;
#if OSCDEBUG
        Serial.printf("pktChannel %u\r\n", pktChannel);
#endif
        break;
      case 1: // Get pktCommand
        pktCommand = client.read();
        packetParse++;
#if OSCDEBUG
        Serial.printf("pktCommand %u\r\n", pktCommand);
#endif
        break;
      case 2: // Get pktLength (high byte)
        pktLength = client.read() << 8;
        packetParse++;
#if OSCDEBUG
        Serial.printf("pktLength high byte %u\r\n", pktLength);
#endif
        break;
      case 3: // Get pktLength (low byte)
        pktLength = pktLength | client.read();
        packetParse++;
        bytesIn = 0;
#if OSCDEBUG
        Serial.printf("pktLength %u\r\n", pktLength);
#endif
        if (pktLength > sizeof(pktData)) {
          Serial.println("Packet length exceeds size of buffer! Data discarded");
          pktLengthAdjusted = sizeof(pktData);
        }
        else {
          pktLengthAdjusted = pktLength;
        }
        break;
      case 4: // Read pktLengthAdjusted bytes into pktData
        bytesRead = client.read(&pktData[bytesIn],
            minsize(sizeof(pktData), pktLengthAdjusted) - bytesIn);
        bytesIn += bytesRead;
        if (bytesIn >= pktLengthAdjusted) {
          if ((pktCommand == 0) && (pktChannel <= 1)) {
            int i;
            uint8_t *pixrgb;
            pixrgb = pktData;
            for (i = 0; i < minsize((pktLengthAdjusted / 3), MAX_LEDS); i++) {
              strip.SET_PIXEL_COLOR(i, GammaLUT[*pixrgb++], GammaLUT[*pixrgb++],
                  GammaLUT[*pixrgb++]);
            }
            // Display only the first frame in this cycle. Buffered frames
            // are discarded.
            if (frame_count == 0) {
#if OSCDEBUG
              Serial.print("=");
              unsigned long startMicros = micros();
#endif
              strip.SHOW();
#if OSCDEBUG
              Serial.printf("%lu\r\n", micros() - startMicros);
#endif
            }
            else {
              frame_discard++;
            }
            frame_count++;
          }
          if (pktLength == pktLengthAdjusted)
            packetParse = 0;
          else
            packetParse++;
        }
        break;
      default:  // Discard data that does not fit in pktData
        bytesRead = client.read(pktData, pktLength - bytesIn);
        bytesIn += bytesRead;
        if (bytesIn >= pktLength) {
          packetParse = 0;
        }
        break;
    }
  }
#if OSCDEBUG
  if (frame_discard) {
    Serial.printf("discard %u\r\n", frame_discard);
  }
#endif
}
