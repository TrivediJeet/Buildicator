//  Created by Jeet Trivedi
//  Copyright © 2017 Jeet Trivedi. All rights reserved.
//

#include <NeoPixelAnimator.h>
#include <NeoPixelBrightnessBus.h>
#include <NeoPixelBus.h>
#include <NeoPixelSegmentBus.h>

// #include <ws2812_i2s.h>
// #include "rgb2hsv.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
 
const uint16_t PixelPin = 4;  // make sure to set this to the correct pin, ignored for Esp8266
const uint16_t PixelCount = 9; // make sure to set this to the number of pixels in your strip
const uint16_t AnimCount = 2; // we only need two
const float MaxLightness = 0.2f; // max lightness (0.5f is full bright)
 
NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount);
NeoPixelAnimator animations(AnimCount); // NeoPixel animation management object
 
// what is stored for state is specific to the need, in the case of the fadeInOut animation, the colors.
// basically what ever you need inside the animation update function
struct MyAnimationState {
    RgbColor StartingColor;
    RgbColor EndingColor;
};
MyAnimationState animationState[2];
boolean fadeToColor = true;  // general purpose variable used to store effect state for the fadeInOut animation
 
//#define DEBUG_SERIAL_OUT 1
//#define TESTING 1
//#define MOCK_SERVER 1
 
#define REFRESH_TIME 5
#ifdef MOCK_SERVER
const char* ssid     = "insertMockSSIDhere";
const char* password = "mockPassword";
#else
const char* ssid     = "insertSSIDhere";
const char* password = "insertPasswordhere";
#endif
 
int lastTime;
bool gettingData = false;
volatile bool stateValid = false;
volatile bool buildingCycle = false;
 
// simple blend function
void BlendAnimUpdate(const AnimationParam& param) {
   RgbColor updatedColor = RgbColor::LinearBlend(
        animationState[param.index].StartingColor,
        animationState[param.index].EndingColor,
        param.progress);
 
    for (uint16_t pixel = 0; pixel < PixelCount; pixel++)
    {
        strip.SetPixelColor(pixel, updatedColor);
    }
 
    if (param.state == AnimationState_Completed)
    {
        // done, time to restart this position tracking animation/timer with the next state
        FadeInFadeOutRinseRepeat(random(360) / 360.0f);
    }
}
 
void FadeInFadeOutRinseRepeat(float hue) {
    if (fadeToColor)
    {
        // we use HslColor object as it allows us to easily pick a hue
        // with the same saturation and luminance so the colors picked
        // will have similiar overall brightness
        RgbColor target = HslColor(hue, 1.0f, MaxLightness);
 
        animationState[1].StartingColor = strip.GetPixelColor(0);
        animationState[1].EndingColor = target;
 
        animations.StartAnimation(1, 600, BlendAnimUpdate);
    }
    else
    {
        // fade to black
        animationState[1].StartingColor = strip.GetPixelColor(0);
        animationState[1].EndingColor = RgbColor(0);
 
        animations.StartAnimation(1, 600, BlendAnimUpdate);
    }
 
    // toggle to the next effect state
    fadeToColor = !fadeToColor;
}
 
void LoopAnimUpdate(const AnimationParam& param) {
    // rotate the complete strip one pixel to the right on every update
    delay(150);
    strip.RotateRight(1);
   
    // wait for this animation to complete
    if (param.state == AnimationState_Completed)
    {
        // done, time to restart this position tracking animation/timer
        animations.RestartAnimation(param.index);
    }
}
 
void DrawTailPixels(float hue)
{
    float lightness = 0.1;
    for (uint16_t index = 0; index <= strip.PixelCount(); index++)
    {
        lightness+=0.01;
        RgbColor color = HslColor(hue, 0.5f, lightness);
        strip.SetPixelColor(index, colorGamma.Correct(color));
    }
}
 
void handleFadeInOut()
{
  // TODO: recursively pop/stop animations via AnimationsAPI
  if (animations.IsAnimating()) {
    Serial.println("Stopping animations...");
    animations.StopAnimation(0);
    animations.StopAnimation(1);
    Serial.println("Animations stopped");
  }
  float rand = random(360) / 360.0f;
  char buf[10];
  gcvt(rand, 6, buf);
  Serial.println(buf);
  FadeInFadeOutRinseRepeat(rand);
}
 
void handleChangeColor(float hue)
{
  if (animations.IsAnimating()) {
    Serial.println("Stopping animations...");
    animations.StopAnimation(0);
    animations.StopAnimation(1);
    Serial.println("Animations stopped");
  }
 
  DrawTailPixels(hue);
  animations.StartAnimation(0, 66, LoopAnimUpdate);
}
 
extern "C" {
#include "user_interface.h"
}
os_timer_t myTimer;
 
bool tickOccured;
 
void SetRandomSeed()
{
    uint32_t seed;
 
    // random works best with a seed that can use 31 bits
    // analogRead on a unconnected pin tends toward less than four bits
    seed = analogRead(0);
    delay(1);
 
    for (int shifts = 3; shifts < 31; shifts += 3)
    {
        seed ^= analogRead(0) << shifts;
        delay(1);
    }
 
    // Serial.println(seed);
    randomSeed(seed);
}
 
void user_init(void) {
      os_timer_setfn(&myTimer, timerCallback, NULL);
 
      os_timer_arm(&myTimer, 30, true);
} // End of user_init
 
 
#ifdef MOCK_SERVER
const char* host = "build.scenarioo.org";
const char* jenkinsPath = "/jenkins/job/archive-self-docu/lastBuild/api/json";
#else
const char* host = "cd-int.corp.soti.net";
const char* goProperties = "/gocd-dashboard-int/api/latestjobs/assist/main?timezoneOffsetInMinutes=-240";
#endif
 
// NETWORK: Static IP details...
//IPAddress ip(172, 16, 24, 48);
//IPAddress gateway(172, 16, 24, 1);
//IPAddress subnet(255, 255, 252, 0);
//IPAddress dns(10, 0, 0, 50);

//IPAddress ip(10, 1, 16, 99);
//IPAddress gateway(10, 1, 16, 1);
//IPAddress subnet(255, 255, 252, 0);
//IPAddress dns(10, 1, 0, 50);
 
void setup() {
  // put your setup code here, to run once:
  Serial.begin(74880); // usb serial connects to pc
  SetRandomSeed();
#ifdef TESTING
#else

  // initialize LED strip
  strip.Begin();
  strip.Show();

  Serial.println(ssid);
  Serial.println(password);

  delay(2500);

  Serial.print(".");

  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.disconnect(true);
//  WiFi.config(ip, dns, gateway, subnet);
  WiFi.begin(ssid, password);

  Serial.print(".");
  int wait = 20 * 4;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    delay(250);
    if (wait < 0) ESP.restart();
    wait--;
  }
  buildingCycle = true;
#endif
lastTime = -30*10000;
user_init();
}
 
 
//WiFiClient client;
HTTPClient http;
int returnCode = 0;
bool dataAvail() {
  #ifdef TESTING
    return true;
  #else
  //  return client.available() > 0;
  return returnCode>0;
  #endif
}
 
void tryGetBuildState(const char * path) {
  #ifdef TESTING
  #else
                  //WiFiClient client;
                HTTPClient http;
  const int httpPort = 80;
  Serial.println(host);
  String url = "http://";
  url += host;
  url += path;
  http.begin(url);
  returnCode = http.GET();
  Serial.print("returncode: ");
  Serial.println(http.errorToString(returnCode));
  #endif
}
 
String json;
volatile bool building = false;
volatile int buildstate = 3;
 
char *lastjobStr = "last_job\":";
char *stateStr = "state\":";
char *pipelineStr = "pipeline_name\": \"";
char *jobStr = "job_name\": \"";
 
const char* getValue(int initpos, char *key)
{
  String keyStr = key;
  int keyBegin = json.indexOf(key,initpos) + strlen(key);
  int keyEnd = json.indexOf(",", keyBegin);
  String value = json.substring(keyBegin, keyEnd);
  return value.c_str();
}
 
int findPipeline(int initpos, char *key)
{
#ifdef DEBUG_SERIAL_OUT
  Serial.println(key);
#endif
  int pos = initpos;
  bool pipelineNotFound = true;
  while (pipelineNotFound) {
    int keyBegin = json.indexOf(pipelineStr, pos) + strlen(pipelineStr);
    if (keyBegin < 0) break;
    int keyEnd = json.indexOf("\",", keyBegin);
    if (keyEnd < 0) break;
  
    String value = json.substring(keyBegin, keyEnd);
#ifdef DEBUG_SERIAL_OUT
    Serial.println(value);
#endif
    if (value.equals(key)) return keyEnd;
    pos = keyEnd;
  }
 
  return -1;
}
 
int findJobState(int initpos, char *key)
{
#ifdef DEBUG_SERIAL_OUT 
  Serial.println(key);
#endif
  int pos = json.indexOf("jobs",initpos);
  bool pipelineNotFound = true;
  while (pipelineNotFound) {
    int keyBegin = json.indexOf(jobStr, pos) + strlen(jobStr);
    if (keyBegin < 0) break;
    int keyEnd = json.indexOf("\",", keyBegin);
#ifdef DEBUG_SERIAL_OUT
    Serial.println(keyEnd);
#endif
    if (keyEnd < 0) break;
    String value = json.substring(keyBegin, keyEnd);
   
    if (value.equals(key)) {
      pos = keyEnd;
      int stateBegin = json.indexOf(stateStr, pos) + strlen(stateStr);
      if (stateBegin < 0) break;
      stateBegin = json.indexOf("\"", stateBegin);
      if (stateBegin < 0) break;
      stateBegin++;
      int stateEnd = json.indexOf("\",", stateBegin);
      if (stateEnd < 0) break;
      String value = json.substring(stateBegin, stateEnd);
#ifdef DEBUG_SERIAL_OUT
      Serial.println(value);
#endif
     
      if (value.equals("Building")) return 2;
      if (value.equals("Passed")) return 1;
      if (value.equals("Failed")) return -1;
      return 0;
    }
    pos = keyEnd;
  }
  Serial.print(key);
  Serial.println(" -2");
  return -2;
}
 
int findLastJobState(int initpos, char *key)
{
  int pos = json.indexOf("jobs", initpos);
  bool pipelineNotFound = true;
  while (pipelineNotFound) {
    int keyBegin = json.indexOf(jobStr, pos) + strlen(jobStr);
    if (keyBegin < 0) break;
    int keyEnd = json.indexOf("\",", keyBegin);
    if (keyEnd < 0) break;
    String value = json.substring(keyBegin, keyEnd);
 
    pos = keyEnd;
    keyBegin = json.indexOf(lastjobStr, pos) + strlen(lastjobStr);
    if (keyBegin < 0) break;
    keyEnd = json.indexOf("{", keyBegin);
    if (keyEnd < 0) break;
   
    if (value.equals(key)) {
      pos = keyEnd;
      int stateBegin = json.indexOf(stateStr, pos) + strlen(stateStr);
      if (stateBegin < 0) break;
      stateBegin = json.indexOf("\"", stateBegin);
      if (stateBegin < 0) break;
      stateBegin++;
      int stateEnd = json.indexOf("\",", stateBegin);
      if (stateEnd < 0) break;
      String value = json.substring(stateBegin, stateEnd);
      if (value.equals("Building")) return 2;
      if (value.equals("Passed")) return 1;
      if (value.equals("Failed")) return -1;
      return 0;
    }
    pos = keyEnd;
  }
  Serial.println("-2");
  return -2;
}
 
void getData() {
  json = http.getString();
#ifdef DEBUG_SERIAL_OUT 
  Serial.println(json);
#endif
  int states[10];
  int lstates[10];
  int si = 0, lsi = 0;
  int pos;
  //const char *buf = json.c_str();
  pos = findPipeline(0, "Assist_Server_main");
  if (pos<0) return;
  states[si++] = findJobState(pos, "Compile");
  states[si++] = findJobState(pos, "Run-UnitTests");
  states[si++] = findJobState(pos, "Run-QuickBDD");
 
  lstates[lsi++] = findLastJobState(pos, "Compile");
  lstates[lsi++] = findLastJobState(pos, "Run-UnitTests");
  lstates[lsi++] = findLastJobState(pos, "Run-QuickBDD");
 
  pos = findPipeline(0, "Assist_Frontend_main");
  if (pos<0) return;
  states[si++]=findJobState(pos, "Build");
  lstates[lsi++] = findLastJobState(pos, "Build");
 
  pos = findPipeline(0, "Assist_SelfService_main");
  if (pos<0) return;
  states[si++] = findJobState(pos, "Build");
  lstates[lsi++] = findLastJobState(pos, "Build");
 
  pos = findPipeline(0, "Assist_Installer_main");
  if (pos<0) return;
  states[si++] = findJobState(pos, "Build");
  lstates[lsi++] = findLastJobState(pos, "Build");
 
  pos = findPipeline(0, "Assist_Acceptance_main");
  if (pos<0) return;
  states[si++] = findJobState(pos, "ExecuteBdd");
  states[si++] = findJobState(pos, "Artifactory");
  lstates[lsi++] = findLastJobState(pos, "ExecuteBdd");
  lstates[lsi++] = findLastJobState(pos, "Artifactory");
 
  bool foundFail = false;
  building = false;
  for (int i = 0; i < 10; i++) {
    if (states[i] == 2) {
      building = true;
    }
    if (states[i] == -1) {
      foundFail = true;
    }   
  }
 
    for (int i = 0; i < 10; i++) {
      if (states[i] == 2) {
        if (lstates[i] == -1) {
          foundFail = true;
        }   
      }     
    }   
  
  if (!foundFail) {
    buildstate=0;
  } else {
    buildstate=1;
  }
   Serial.println("DONE");
}
 
volatile byte s = 0;
volatile byte cled = 0;
volatile byte ledspeed = 0;
void fadeall() {
//  for(int i = 0; i < NUM_LEDS; i++) {
//    leds[i].R = leds[i].R * 210 / 255;
//    leds[i].G = leds[i].G * 210 / 255;
//    leds[i].B = leds[i].B * 210 / 255;
//  }
}
 
 
// start of timerCallback
void timerCallback(void *pArg) {
  // put your main code here, to run repeatedly:

//   HsvColor hsv;
//   hsv.h = s;
//   hsv.s = 255;
//   hsv.v = 255;
//   RgbColor col;
//   col = HsvToRgb(hsv);
  
   if (stateValid) {
      switch (buildstate) {
        case 0:
          s = 85;
        break;
        case 1:
          s = 0;
        break;
        case 2:
          s = 170;
        break;
        case 3:
          s = 220;
        break;
      }    
    int bright = 15;
     
    if (!building) {
      if (s == 85) {
//        for (int i=0; i< NUM_LEDS; i++) {
//          leds[i].R = col.r * bright / 255;
//          leds[i].G = col.g * bright / 255;
//          leds[i].B = col.b * bright / 255;
            //strip.SetPixelColor(i, col);
//        }
      } else {
//        int brightness = (sin((cled)/(float)NUM_LEDS*2*3.1415926) + 1) * bright;
//        for (int i=0; i< NUM_LEDS; i++) {
//          leds[i].R = col.r * brightness / 255;
//          leds[i].G = col.g * brightness / 255;
//          leds[i].B = col.b * brightness / 255;
            //strip.SetPixelColor(i, col);
//        }
      }
    } else {
//      for (int i=0; i< NUM_LEDS; i++) {
//        int brightness = (sin((i+cled)/(float)NUM_LEDS*2*3.1415926) + 1) * bright;
//        leds[i].R = col.r * brightness / 255;
//        leds[i].G = col.g * brightness / 255;
//        leds[i].B = col.b * brightness / 255;
//      }

//      handleChangeColor(random(360) / 360.0f);
//      handleChangeColor(col);
    }
//
//      if (ledspeed++ > 2) {
//        cled++;
//        ledspeed=0;
//      }
//      if (cled >= NUM_LEDS) {
//        cled=0;
//      }
   
    // Show the leds
//    ledstrip.show(leds);
//    fadeall();
   }
 
} // End of timerCallback
 
void loop() {
   delay(1000);
   if (millis() - lastTime > REFRESH_TIME * 1000) {
    tryGetBuildState(goProperties);//prev_build?jenkinsCompletedPath:jenkinsPath);
    lastTime = millis();
    gettingData = true;
   }
   if (gettingData && dataAvail()) {
    getData();
    stateValid = true;
    lastTime = millis();
    gettingData = false;
   }

    if (animations.IsAnimating()) {
      animations.UpdateAnimations();
      strip.Show();
    } 
}
