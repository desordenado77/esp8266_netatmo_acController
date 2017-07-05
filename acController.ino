#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <time.h>


#define BLUE_LED_PIN     2 // Blue LED.
#define RELAY_PIN        4 // Relay control.
#define OPTOCOUPLER_PIN  5 // Optocoupler input.

#define RELAY_TURN_OFF   LOW
#define RELAY_TURN_ON    HIGH

ESP8266WebServer server(80);

WiFiServer telnet(23);
WiFiClient telnetClients;
int disconnectedClient = 1;

int enableTime = 0;
time_t now; 

#define DEBUG_LOG_LN(x) { Serial.println(x); if(telnetClients) { telnetClients.println(x); } }
#define DEBUG_LOG(x) { Serial.print(x); if(telnetClients) { telnetClients.print(x); } }

#define DEBUG_LOG_INFO_LN(x) { if(enableTime) { now = time(nullptr); DEBUG_LOG(now); DEBUG_LOG(":"); DEBUG_LOG(__LINE__); DEBUG_LOG(":"); DEBUG_LOG_LN(x); } }
#define DEBUG_LOG_INFO(x) { if(enableTime) { now = time(nullptr); DEBUG_LOG(now); DEBUG_LOG(":"); DEBUG_LOG(__LINE__); DEBUG_LOG(":"); DEBUG_LOG(x); } }

#define NORMAL_TIME_FOR_ACTION    (15*60)
#define AC_ON_TIME_FOR_ACTION     (30*60)
#define AC_OFF_TIME_FOR_ACTION    (20*60)


time_t lastAction = 0;
double timeForAction = NORMAL_TIME_FOR_ACTION;

#define FORCED_AC_ON     1
#define FORCED_AC_OFF    2
int forcedAC = 0;

//define your default values here, if there are different values in config.json, they are overwritten.
char client_secret[128] = "";
char client_id[128] = "";
char refresh_token[128] = "";
char access_token[128] = "";
char device_id[128] = "";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  DEBUG_LOG_INFO_LN("Should save config");
  shouldSaveConfig = true;
}

void handleRoot() {
  server.send(200, "text/plain", "hello from esp8266!");
}

void handleDisconnect(){
  WiFi.disconnect();
  ESP.restart();
}

void handlePin(String Name) {
  String value = server.arg("value");
  if(value.equalsIgnoreCase("on")){
    digitalWrite(RELAY_PIN, HIGH);
  }
  else {
    if(value.equalsIgnoreCase("off")){
      digitalWrite(RELAY_PIN, LOW);
    }
    else {
      server.send(200, "text/plain", Name + " Unknown value "+value);
      return;
    }    
  }
  server.send(200, "text/plain", Name + " "+ value);
}


void handleRelay() {
  handlePin("Relay");
}

void handleLed() {
  handlePin("Led");
}

void handleAcControl() {
  String value = server.arg("value");
  if(value.equalsIgnoreCase("on")){
    forcedAC = FORCED_AC_ON;
  }
  else {
    if(value.equalsIgnoreCase("off")){
      forcedAC = FORCED_AC_OFF;
    }
    else {
      server.send(200, "text/plain", "AC control Unknown value " + value);
      return;
    }    
  }
  server.send(200, "text/plain", "AC control " + value);
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  DEBUG_LOG_INFO_LN();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  DEBUG_LOG_INFO_LN("mounting FS...");

  if (SPIFFS.begin()) {
    DEBUG_LOG_INFO_LN("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      DEBUG_LOG_INFO_LN("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_LOG_INFO_LN("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          DEBUG_LOG_INFO_LN("parsed json");

          strcpy(client_secret, json["client_secret"]);
          strcpy(client_id, json["client_id"]);
          strcpy(refresh_token, json["refresh_token"]);
          strcpy(device_id, json["device_id"]);

        } else {
          DEBUG_LOG_INFO_LN("failed to load json config");
        }
      }
    }
  } else {
    DEBUG_LOG_INFO_LN("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_client_secret("client_secret", "client_secret", client_secret, 128);
  WiFiManagerParameter custom_client_id("client_id", "client_id", client_id, 128);
  WiFiManagerParameter custom_refresh_token("refresh_token", "refresh_token", refresh_token, 128);
  WiFiManagerParameter custom_device_id("device_id", "device_id", device_id, 128);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_client_secret);
  wifiManager.addParameter(&custom_client_id);
  wifiManager.addParameter(&custom_refresh_token);
  wifiManager.addParameter(&custom_device_id);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    DEBUG_LOG_INFO_LN("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  DEBUG_LOG_INFO_LN("connected...yeey :)");

  //read updated parameters
  strcpy(client_secret, custom_client_secret.getValue());
  strcpy(client_id, custom_client_id.getValue());
  strcpy(refresh_token, custom_refresh_token.getValue());
  strcpy(device_id, custom_device_id.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    DEBUG_LOG_INFO_LN("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["client_secret"] = client_secret;
    json["client_id"] = client_id;
    json["refresh_token"] = refresh_token;
    json["device_id"] = device_id;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DEBUG_LOG_INFO_LN("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(OPTOCOUPLER_PIN, INPUT);

  DEBUG_LOG_INFO_LN("local ip");
  DEBUG_LOG_INFO_LN(WiFi.localIP());

  if (MDNS.begin("esp8266")) {
    DEBUG_LOG_INFO_LN("MDNS responder started");
  }

  server.on("/", handleRoot);
  server.on("/disconnectWifi", handleDisconnect);
  server.on("/relay", handleRelay);
  server.on("/led", handleLed);
  server.on("/acControl", handleAcControl);
  
  server.onNotFound(handleNotFound);

  server.begin();
  DEBUG_LOG_INFO_LN("HTTP server started");  

  telnet.begin();
  telnet.setNoDelay(true);
  DEBUG_LOG_INFO_LN("Telnet server started");  

  configTime(0, 0, "2.es.pool.ntp.org", "1.europe.pool.ntp.org", "2.europe.pool.ntp.org");
  Serial.println("Waiting for time");
  DEBUG_LOG_INFO(" ");
  while (!time(nullptr)) {
    DEBUG_LOG(".");
    delay(1000);
  }
  DEBUG_LOG_LN("");
  now = time(nullptr);
  DEBUG_LOG_LN(ctime(&now));

  enableTime = 1;
  
  // WiFi.disconnect();
}


int httpsPostRequest(String s_host, uint16_t httpsPort, String url, String payload, String* ret) {
  char host[256];
  s_host.toCharArray(host, 256);
  
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  DEBUG_LOG_INFO_LN("connecting to " + String(host));
  if (!client.connect(host, httpsPort)) {
    DEBUG_LOG_INFO_LN("connection failed");
    return -1;
  }
/*
  if (client.verify(fingerprint, host)) {
    DEBUG_LOG_INFO_LN("certificate matches");
  } else {
    DEBUG_LOG_INFO_LN("certificate doesn't match");
  }
*/

  DEBUG_LOG_INFO_LN("requesting URL: " + url);

  String header = String("POST ") + url + " HTTP/1.0\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: ESP8266\r\n" +
               "Content-Type: application/x-www-form-urlencoded\r\n" +
               "Connection: keep-alive\r\n";
  if(payload.length() != 0) {
    header = header + "Content-Length: "+ payload.length() +
               "\r\n\r\n" + payload;
  }
  else {
    header = header + "Content-Length: 0\r\n\r\n";
  }
  //DEBUG_LOG_INFO_LN(header);

  client.print(header);

  DEBUG_LOG_INFO_LN("request sent");
  String responseHeader = "";
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    responseHeader = responseHeader + line;
    // DEBUG_LOG_INFO_LN("Line headers " + line);
    if (line == "\r") {
      DEBUG_LOG_INFO_LN("headers received");
      if(!responseHeader.startsWith("HTTP/1.1 200")) {
        DEBUG_LOG_INFO_LN("HTTP respose is not 200. Complete Header:\n\n" + responseHeader + "\n\n");
        client.stop();
        return -1;
      }
      break;
    }
  }
  *ret = client.readStringUntil('\n');
//  DEBUG_LOG_INFO_LN("Result " + *ret);

  return 0;
}


int getRefreshToken(){
  String str_refresh_token = String(refresh_token);
  str_refresh_token.replace("|", "%7C");
  String data = "client_secret="+String(client_secret)+"&grant_type=refresh_token&client_id="+client_id+"&refresh_token="+str_refresh_token;

  String authCode;
  int ret = httpsPostRequest("api.netatmo.com", 443, "/oauth2/token", data, &authCode);
  if(ret != 0) {
    DEBUG_LOG_INFO_LN("failed get refresh token, https request failed");
    return ret;
  }

  size_t size = authCode.length();
  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(authCode);
  // json.printTo(Serial);
  if (json.success()) {
    DEBUG_LOG_INFO_LN("parsed json");
    strcpy(access_token, json["access_token"]);
    strcpy(refresh_token, json["refresh_token"]);
  } else {
    DEBUG_LOG_INFO_LN("failed to parse refresh token response");
    return -1;
  }
  return 0;
}


int getThermostatData(float * temp, float* set_temp, int* read_time){
  int ret = 0;
  String str_temp;
  String str_access_token = String(access_token);
  str_access_token.replace("|", "%7C");
  String str_device_id = String(device_id);
  str_device_id.replace(":", "%3A");

  String params = "?access_token="+str_access_token+"&device_id="+str_device_id;
  ret = httpsPostRequest("api.netatmo.com", 443, "/api/syncthermstate"+params, "", &str_temp);
  if(ret != 0) {
    DEBUG_LOG_INFO_LN("failed get sync thermostat");
    return ret;
  }

  ret = httpsPostRequest("api.netatmo.com", 443, "/api/getthermostatsdata"+params, "", &str_temp);
  if(ret != 0) {
    DEBUG_LOG_INFO_LN("failed get temperature");
    return ret;
  }
  
  size_t size = str_temp.length();
  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(str_temp);
  // json.printTo(Serial);
  if (json.success()) {
    char tempArray[128];
    DEBUG_LOG_INFO_LN("parsed json");
    strcpy(tempArray, json["body"]["devices"][0]["modules"][0]["measured"]["setpoint_temp"]);
    *set_temp = atof(tempArray);
    strcpy(tempArray, json["body"]["devices"][0]["modules"][0]["measured"]["temperature"]);
    *temp = atof(tempArray);
    strcpy(tempArray, json["body"]["devices"][0]["modules"][0]["measured"]["time"]);
    *read_time = atoi(tempArray);
  } else {
    DEBUG_LOG_INFO_LN("failed to parse refresh token response");
    return -1;
  }
    
  return 0;
}

int getTemperature(float * temp, float* set_temp, int* read_time){
  int ret = 0;
  
  ret = getThermostatData(temp, set_temp, read_time);
  if(ret != 0) {
    DEBUG_LOG_INFO_LN("getThermostatData failed, Unable to get the temperature");
    ret = getRefreshToken();
    if(ret != 0) {
      DEBUG_LOG_INFO_LN("getRefreshToken failed, Unable to get the refresh token");
    }
    else {
      ret = getThermostatData(temp, set_temp, read_time);
      if(ret != 0) {
        DEBUG_LOG_INFO_LN("getThermostatData failed, Unable to get the temperature");    
      }
    }
  }
  return ret;
}


void loop() {
  uint8_t i;
  static int once = 0;
  server.handleClient();
  time_t currentTime = time(nullptr);
  static float last_temp = 0;
  static float last_set_temp = 0;
  static int last_read_time = 0;
  static int acOn = 0;
  int error = 0;


  if (telnet.hasClient()) {
    if (!telnetClients || !telnetClients.connected()) {
      if (telnetClients) {
        telnetClients.stop();
        DEBUG_LOG_INFO_LN("Telnet Client Stop");
      }
      telnetClients = telnet.available();
      DEBUG_LOG_INFO_LN("New Telnet client");
      telnetClients.flush();  // clear input buffer, else you get strange characters 
      disconnectedClient = 0;
    }
  }
  else {
    if(!telnetClients.connected()) {
      if(disconnectedClient == 0) {
        DEBUG_LOG_INFO_LN("Client Not connected");
        telnetClients.stop();
        disconnectedClient = 1;
      }
    }
  }

  DEBUG_LOG_INFO("Diff time ");
  DEBUG_LOG_LN((currentTime - lastAction));
  DEBUG_LOG_INFO("timeForAction ");
  DEBUG_LOG_LN(timeForAction);

  if(forcedAC == FORCED_AC_ON) {
    DEBUG_LOG_INFO_LN("Turn ON AC");
    forcedAC = 0;

    if(acOn) {
      timeForAction += AC_ON_TIME_FOR_ACTION;
    }
    else {
      timeForAction = AC_ON_TIME_FOR_ACTION;
      acOn = 1;
    }
    lastAction = currentTime;
  }

  if(forcedAC == FORCED_AC_OFF) {
    DEBUG_LOG_INFO_LN("Turn OFF AC");
    forcedAC = 0;

    if(acOn == 0) {
      timeForAction += AC_OFF_TIME_FOR_ACTION;
    }
    else {
      timeForAction = AC_OFF_TIME_FOR_ACTION;
      acOn = 0;
    }
    lastAction = currentTime;
  }
  
  if(lastAction == 0 || ((currentTime - lastAction) >= timeForAction)) {
    float temp, set_temp;
    int read_time;
    int ret = getTemperature(&temp, &set_temp, &read_time);
    if(ret != 0) {
      DEBUG_LOG_INFO_LN("Thermostat get temp error");
      timeForAction = NORMAL_TIME_FOR_ACTION;
      DEBUG_LOG_INFO_LN("Turn Off AC");
      acOn = 0;
      error = 1;
    }
    else {
      if((read_time - last_read_time)<(timeForAction/2)) {
        DEBUG_LOG_INFO_LN("Thermostat read time Error: ");
        DEBUG_LOG_INFO("\tRead Time: ");
        DEBUG_LOG_LN(read_time);
        DEBUG_LOG_INFO("\tLast read Time: ");
        DEBUG_LOG_LN(last_read_time);      
        timeForAction = NORMAL_TIME_FOR_ACTION;
        DEBUG_LOG_INFO_LN("Turn Off AC");
        acOn = 0;
        error = 1;
      }
      else {
        DEBUG_LOG_INFO_LN("Temperature: ");
        DEBUG_LOG_LN(temp);
        DEBUG_LOG_INFO_LN("Set Temperature: ");
        DEBUG_LOG_LN(set_temp);
        DEBUG_LOG_INFO_LN("Reading Time: ");
        DEBUG_LOG_LN(read_time);
        DEBUG_LOG_INFO_LN("Reading Time Diff: ");

        if(set_temp < temp) {
          DEBUG_LOG_INFO_LN("Turn On AC");
          if(acOn) {
            timeForAction = NORMAL_TIME_FOR_ACTION;
          }
          else {
            timeForAction = AC_ON_TIME_FOR_ACTION;
          }
          acOn = 1;
        }
        else {
          DEBUG_LOG_INFO_LN("Turn Off AC");
          if(acOn) {
            timeForAction = AC_OFF_TIME_FOR_ACTION;
          }
          else {
            timeForAction = NORMAL_TIME_FOR_ACTION;
          }
          acOn = 0;
        }
      }

      last_temp = temp;
      last_set_temp = set_temp;
      last_read_time = read_time;
    }
    lastAction = currentTime;    
  }

  if(acOn) {
    digitalWrite(RELAY_PIN, RELAY_TURN_ON);
  }
  else {
    digitalWrite(RELAY_PIN, RELAY_TURN_OFF);
  }

  if(error) {
    digitalWrite(BLUE_LED_PIN, LOW);
    delay(500);
    digitalWrite(BLUE_LED_PIN, HIGH);
    delay(500);
    digitalWrite(BLUE_LED_PIN, LOW);
    delay(500);
    digitalWrite(BLUE_LED_PIN, HIGH);
    delay(500);
  }
  else {
    digitalWrite(BLUE_LED_PIN, LOW);
    delay(1000);
    digitalWrite(BLUE_LED_PIN, HIGH);
    delay(1000);
  }
  DEBUG_LOG_INFO("Memory: ");
  DEBUG_LOG_LN(ESP.getFreeHeap());
  
}
