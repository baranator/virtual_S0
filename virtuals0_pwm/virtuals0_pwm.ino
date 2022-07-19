#if !defined(ESP8266)
  #error This code is designed to run on ESP8266 and ESP8266-based boards! Please check your Tools->Board setting.
#endif

#include <ESP8266WiFi.h>
#include <MQTT.h>
#include <WiFiUdp.h>
#include <IotWebConf.h>
#include <IotWebConfOptionalGroup.h>

//PWM-lib stuff

#define USING_MICROS_RESOLUTION       true  
// _PWM_LOGLEVEL_ from 0 to 4, Don't define _PWM_LOGLEVEL_ > 0. Only for special ISR debugging only. Can hang the system.
#define _PWM_LOGLEVEL_                0
#define USING_TIM_DIV1                true              // for shortest and most accurate timer
#include "ESP8266_PWM.h"
#define HW_TIMER_INTERVAL_US      20L
volatile uint32_t startMicros = 0;
// Init ESP8266Timer
ESP8266Timer ITimer;
// Init ESP8266_ISR_PWM
ESP8266_PWM ISR_PWM;
void IRAM_ATTR TimerHandler(){
  ISR_PWM.run();
}

//program constants

#define STRING_LEN 64
#define NUMBER_LEN 8
#define CONFIG_VERSION "vs0-0.01"
#define PULSE_DUR_MS 35
#define S0_PIN 14
#define S0_TIMEOUT_MS 3*60*1000

//wifi & mqtt stuff

IPAddress ipMqttBroker;
String clientId;
WiFiClient net;
MQTTClient client;
DNSServer dnsServer;
WebServer server(80);

//iotwebconf-stuff

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "virtualS0";
// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "virtualS0";
IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);


//iotwebconf storage-labels
static char chooserValues[][STRING_LEN] = { "positive", "negative", "both" };
//static char chooserNames[][STRING_LEN] = { "Red", "Blue", "Dark yellow" };

iotwebconf::ParameterGroup* mqttG;
iotwebconf::ParameterGroup* s0G;


char pulsePerKWhVal[STRING_LEN];
iotwebconf::TextParameter pulsePerKWhP("Pulses per kWh", "pulse_per_kwh", pulsePerKWhVal, STRING_LEN,"1000");

char wattageModeVal[STRING_LEN];
iotwebconf::SelectParameter wattageModeP("Wattage mode for pulses", "wattage_mode", wattageModeVal, STRING_LEN, (char*)chooserValues, (char*)chooserValues, sizeof(chooserValues) / STRING_LEN, STRING_LEN,"both");


char mqttTopicVal[STRING_LEN];
iotwebconf::TextParameter mqttTopicP("MQTT Topic with wattage", "mqtt_topic", mqttTopicVal, STRING_LEN,"");

char mqttBrokerIpVal[STRING_LEN];
iotwebconf::TextParameter mqttBrokerIpP("MQTT Broker IP", "mqtt_ip", mqttBrokerIpVal, STRING_LEN);

char mqttPortVal[NUMBER_LEN];
iotwebconf::NumberParameter mqttPortP("MQTT Broker Port", "mqtt_port", mqttPortVal, NUMBER_LEN, "1883", "1..65535", "min='1' max='65535' step='1'");

iotwebconf::OptionalParameterGroup* mqttCredG;

char mqttAuthVal[STRING_LEN];
iotwebconf::CheckboxParameter mqttAuthP ("Auth required?", "mqtt_auth", mqttAuthVal, STRING_LEN,  false);


char mqttUserVal[STRING_LEN];
iotwebconf::TextParameter mqttUserP("MQTT Username", "mqtt_user", mqttUserVal, STRING_LEN);

char mqttPasswordVal[STRING_LEN];
iotwebconf::TextParameter mqttPasswordP("MQTT Password", "mqtt_password", mqttPasswordVal, STRING_LEN);



//S0-Stuff

unsigned long lastUpdate=0;
int ledChan;
int s0Chan;



void loopAndD(unsigned int minDur=0){
  int skip=5; //ms
  int steps=0;
  steps=minDur/skip;
  
  for(int i=0;i<max(1,steps);i++) {
    iotWebConf.doLoop();
    client.loop();
    delay(skip);  
  }
}



void mqttCallback(MQTTClient *client, char topic[], char payload[], int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("]");
  Serial.print("Payload: ");
  Serial.println((char*)payload);
  
  lastUpdate=millis();
  double wattage=((String)payload).toFloat();

  
  if(wattage<0 && strcmp(wattageModeVal,"positive")==0){
    Serial.println("mode is positive, only wattages >=0 will generate pulses");
    disablePulses();
    return;  
  }

  if(wattage>0 && strcmp(wattageModeVal,"negative")==0){
    Serial.println("mode is negative, only wattages <=0 will generate pulses");
    disablePulses();
    return;  
  }

  if(abs(wattage)<1){
    Serial.println("almost no wattage, disable pulses");
    disablePulses();
    return;
  }

  //calculate pulse-frequency: first calc seconds needed per Wh, then invert for frequency: Wh/s = Imp/s
  double kwhPerSecond=abs(wattage)/(60.0*60.0*1000.0);
  double pulsesPerSecond=kwhPerSecond*((String)pulsePerKWhVal).toFloat();
  double periodLength=1000.0/pulsesPerSecond;
  
  Serial.print("Setting ");Serial.print(pulsesPerSecond);Serial.print(" pulses/s for a wattage of ");Serial.println(wattage);


  ISR_PWM.modifyPWMChannel_Period(ledChan, LED_BUILTIN, periodLength*1000, 100.0-100.0*PULSE_DUR_MS/periodLength);
  ISR_PWM.modifyPWMChannel_Period(s0Chan, S0_PIN, periodLength*1000, 100.0*PULSE_DUR_MS/periodLength);
 //ISR_PWM.setPWM_Period(S0_PIN, periodLength*1000, 100.0*PULSE_DUR_MS/periodLength);
  //ISR_PWM.setPWM_Period();
}

void mqttSetup(){
  pinMode(S0_PIN,OUTPUT);
  pinMode(LED_BUILTIN,OUTPUT);
  ipMqttBroker.fromString(mqttBrokerIpVal);
  // Create a random client ID
  clientId = "virtualS0-";
  clientId += String(random(0xffff), HEX);
  client.begin(ipMqttBroker, atoi(mqttPortVal), net);
  client.onMessageAdvanced(mqttCallback);

  mqttReconnect();
}

boolean mqttReconnect() {
  
  Serial.print("checking wifi...");
  if(iotWebConf.getState()!=iotwebconf::OnLine || String(mqttBrokerIpVal).compareTo("")==0 ){
    Serial.println("No wifi present, aborting...");
    return false;
  }
  Serial.println("connected");
  Serial.print("connecting to broker...");
  
  if((mqttAuthP.isChecked() && !client.connect(clientId.c_str(), mqttUserVal, mqttPasswordVal)) ||
     (!mqttAuthP.isChecked() && !client.connect(clientId.c_str()))
  ) {
    Serial.println("failed. Aborting.");
    return false; 
  }
  Serial.print("connected. subscribing to topic ");Serial.println(mqttTopicVal);

  client.subscribe(mqttTopicVal);
  return true;
} 


void iotwebconfSetup(){
    mqttG=new iotwebconf::ParameterGroup("MQTT","mqtt");
    s0G=new iotwebconf::ParameterGroup("S0-Prefs","s0prefs");

    s0G->addItem(&wattageModeP);
    s0G->addItem(&pulsePerKWhP);
    
    mqttG->addItem(&mqttTopicP);
    mqttG->addItem(&mqttBrokerIpP);
    mqttG->addItem(&mqttPortP);

    iotWebConf.addParameterGroup(s0G);
    iotWebConf.addParameterGroup(mqttG);
    
    mqttG->addItem(&mqttAuthP);
    mqttG->addItem(&mqttUserP);
    mqttG->addItem(&mqttPasswordP);
    
//    iotWebConf.setFormValidator(&formValidator);
    
    iotWebConf.init();
    iotWebConf.setApTimeoutMs(10000);
    iotWebConf.doLoop();

    // -- Set up required URL handlers on the web server.
    server.on("/", []{ iotWebConf.handleConfig(); });
    server.onNotFound([](){ iotWebConf.handleNotFound(); });
    iotWebConf.doLoop();

}

void pwmSetup(){
  // Interval in microsecs
  if (ITimer.attachInterruptInterval(HW_TIMER_INTERVAL_US, TimerHandler)){
    startMicros = micros();
    Serial.print(F("Starting ITimer OK, micros() = ")); Serial.println(startMicros);
  }else{
    Serial.println(F("Can't set ITimer. Select another freq. or timer"));  
  }
}

void disablePulses(){
  ISR_PWM.modifyPWMChannel_Period(ledChan, LED_BUILTIN, 10*1000, 100);
  ISR_PWM.modifyPWMChannel_Period(s0Chan, S0_PIN, 10*1000, 0);
}

void setup(void) {
  Serial.begin(115200);
  // -- Initializing the (wifi-)configuration.
  iotwebconfSetup();
  pwmSetup();
  mqttSetup();
  s0Chan=ISR_PWM.setPWM_Period(S0_PIN, 10000, 0);
  ledChan=ISR_PWM.setPWM_Period(LED_BUILTIN, 10000, 100.0);
  
  loopAndD(500);
}

void loop(void) {
  loopAndD();
  if (!client.connected()) {
    mqttReconnect();
  }

  if(millis()-lastUpdate  > S0_TIMEOUT_MS){
    //...
    disablePulses();
    //prevent repeated execution after timeout was triggered once and pwm is already off until next mqtt
    lastUpdate=millis();
  }
}
  
