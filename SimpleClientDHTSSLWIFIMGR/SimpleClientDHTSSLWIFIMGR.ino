/* ESP8266-based weather server
 *
 * Connect to WiFi and makes https requests with temperature and humidity
 *
 * Originally ased on Adafruit ESP8266 Temperature / Humidity Webserver
 * https://learn.adafruit.com/esp8266-temperature-slash-humidity-webserver
 */

#define MOONBASE_BOARD true

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h> 
#include <Ticker.h>

#include "CaptiveConfig.h"

#if MOONBASE_BOARD
  #include <Wire.h>

  // SmartEverything HTS221 and LPS25H libraries.  To add via Arduino IDE:
  // Sketch->Include Library->Manage Libraries->filter "SmartEverything"
  // https://github.com/ameltech 
  #include <HTS221.h>
  #include <LPS25H.h>

#else

  // Adafruit DHT Arduino library
  // https://github.com/adafruit/DHT-sensor-library
  #include <DHT.h>
  #define DHTTYPE DHT22   // DHT Shield uses DHT 11
  #define DHTPIN D4       // DHT Shield uses pin D4
#endif // if/else MOONBASE_BOARD

Ticker ticker;

//TODO: What are these for?
const char* DEVNAME = "";
const char* ISSUEID  = "";
const char* ssid = "";
const char* password = "";

void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


/// IP address of the server we send data to via HTTP(S) requests.
IPAddress server(120,138,27,109);

#if MOONBASE_BOARD
  /// Raw float values from the sensor
  float pressure;
  /// Rounded sensor value as string
  char str_pressure[10];
#else
  // Initialize DHT sensor
  // Note that older versions of this library took an optional third parameter to
  // tweak the timings for faster processors.  This parameter is no longer needed
  // as the current DHT reading algorithm adjusts itself to work on faster procs.
  DHT dht(DHTPIN, DHTTYPE);
#endif // if/else MOONBASE_BOARD

float humidity, temperature;                 // Raw float values from the sensor
char str_humidity[10], str_temperature[10];  // Rounded sensor values and as strings

// Generally, you should use "unsigned long" for variables that hold time
unsigned long previousMillis = 0;            // When the sensor was last read
const long interval = 5000;                  // Wait this long until reading again
unsigned long lastConnectionTime = 0;             // last time you connected to the server, in milliseconds
enum WiFiStateEnum { DOWN, STARTING, UP };
WiFiStateEnum WiFiState = DOWN;
unsigned long WiFiStartTime = 0;

void read_sensor() {
  // Wait at least 2 seconds seconds between measurements.
  // If the difference between the current time and last time you read
  // the sensor is bigger than the interval you set, read the sensor.
  // Works better than delay for things happening elsewhere also.
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    // Save the last time you read the sensor
    previousMillis = currentMillis;

    #if MOONBASE_BOARD
      humidity = smeHumidity.readHumidity();
      temperature = smeHumidity.readTemperature();
      pressure = smePressure.readTemperature();
    #else
      // Reading temperature and humidity takes about 250 milliseconds!
      // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
      humidity = dht.readHumidity();        // Read humidity as a percent
      temperature = dht.readTemperature();  // Read temperature as Celsius
    #endif // if/else MOONBASE_BOARD

    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Failed to read from sensors!");
      return;
    }

    // Convert the floats to strings and round to 2 decimal places
    dtostrf(humidity, 1, 2, str_humidity);
    dtostrf(temperature, 1, 2, str_temperature);

    #if MOONBASE_BOARD
       dtostrf(pressure, 1, 2, str_pressure);
       Serial.print("Pressure: ");
       Serial.print(str_pressure);
    #endif // #if MOONBASE_BOARD

    Serial.print("Humidity: ");
    Serial.print(str_humidity);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(str_temperature);
    Serial.println(" °C");
  }
}


const char * WiFiEvent_t_strings[] = {
    "WIFI_EVENT_STAMODE_CONNECTED",
    "WIFI_EVENT_STAMODE_DISCONNECTED",
    "WIFI_EVENT_STAMODE_AUTHMODE_CHANGE",
    "WIFI_EVENT_STAMODE_GOT_IP",
    "WIFI_EVENT_STAMODE_DHCP_TIMEOUT",
    "WIFI_EVENT_SOFTAPMODE_STACONNECTED",
    "WIFI_EVENT_SOFTAPMODE_STADISCONNECTED",
    "WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED",
    "WIFI_EVENT_MAX / WIFI_EVENT_ANY",
    "WIFI_EVENT_MODE_CHANGE" // IR: May be problematic to have this > WIFI_EVENT_MAX?
};

void WiFiEvent(WiFiEvent_t event) {
    if (event <= WIFI_EVENT_MODE_CHANGE) {
        Serial.printf("[WiFi-event] event: %d (%s)\n", event, WiFiEvent_t_strings[event]);
    } else {
        Serial.printf("[WiFi-event] event has invalid value\n");
    }

    switch(event) {
        case WIFI_EVENT_STAMODE_GOT_IP:
            Serial.println("WiFi connected");
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
            WiFiState = UP;
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:
            WiFiState = DOWN;
            Serial.println("WiFi lost connection");
            break;
    }
}

void WifiTryUp() {
    // delete old config

    WiFi.disconnect(true);

    delay(1000);

    WiFi.begin(ssid, password);

    Serial.println();
    Serial.println();
    Serial.println("Wait for WiFi... ");
}


unsigned long _ESP_id;

/// Points to instance of captive configuration manager, when there is one
CaptiveConfig *captiveConfig(nullptr);

void setup(void)
{
  // Open the Arduino IDE Serial Monitor to see what the code is doing
  Serial.begin(115200);

  #if MOONBASE_BOARD
    Wire.begin();
    smePressure.begin();
    smeHumidity.begin();
    Serial.println("VCW sensor Server");
  #else
    dht.begin();

    Serial.println("WeMos DHT Server");
  #endif // if/else MOONBASE_BOARD

  _ESP_id = ESP.getChipId();  // uint32 -> unsigned long on arduino
  Serial.println(_ESP_id, HEX);
  Serial.println("");

  // Initial read
  read_sensor();

  // Instantiate CaptiveConfig
  captiveConfig = new CaptiveConfig;
}

const char * wlStatusTToCString(wl_status_t in)
{
  if (in == 255) {
    return "No Shield";
  }

  const char * statuses[] = {
    "Idle",
    "No SSID Available",
    "Scan Completed",
    "Connected",
    "Connect Failed",
    "Connection Lost",
    "Disconnected" };

  if (in > 6)
    return "ERROR: Bad enum value!";

  return statuses[in];
}


void loop(void)
{
  static auto lastStatus(WL_NO_SHIELD);

  if( captiveConfig && captiveConfig->haveConfig() ) {

    APCredentials requestedNetwork( captiveConfig->getConfig() );

    Serial.print("User wants to connect with SSID:\n\t\"");
    Serial.print(requestedNetwork.ssid);
    Serial.print("\"\nwith passphrase:\n\t\"");
    Serial.print(requestedNetwork.passphrase);
    Serial.print("\"\n");

    delete captiveConfig;
    captiveConfig = nullptr;

    WiFi.mode(WIFI_STA);
    WiFi.begin( requestedNetwork.ssid.c_str(),
                requestedNetwork.passphrase.c_str() );
  } else {
    auto thisStatus(WiFi.status());

    if (thisStatus != lastStatus) {
      Serial.print("Status changed to ");
      Serial.print(wlStatusTToCString(thisStatus));
      Serial.print("\n");
      lastStatus = thisStatus;
    }
  }
}


#if 0 // IR: Doesn't look like this is currently used
// this method makes a HTTP connection to the server:
void httpRequest(float temp, float humid) {

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(server, httpPort)) {
    Serial.println("connection failed");
    return;
  }
  
  String url = "/updateweatherstation.php?action=updateraw";
  url += "&devid=";
  url += String(_ESP_id,HEX);
  url += "&devname=";
  url += DEVNAME;
  url += "&issueid=";
  url += ISSUEID;
  url += "&dateutc=now&indoortemp=";
  url += temp;
  url += "&indoorhumidity=";
  url += humid;
 
  
  Serial.print("Requesting URL: ");
  Serial.println(url);
  
  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + "www.shac.org.nz" + "\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    delay(10);
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  
  // Read all the lines of the reply from server and print them to Serial
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  
  Serial.println();
  Serial.println("closing connection"); 

    // note the time that the connection was made:
  lastConnectionTime = millis();
 
}
#endif // #if 0



// Use web browser to view and copy
// SHA1 fingerprint of the certificate
const char* fingerprint = "CF 05 98 89 CA FF 8E D8 5E 5C E0 C2 E4 F7 E6 C3 C7 50 DD 5C";

// this method makes a HTTP connection to the server:
#if MOONBASE_BOARD
  void httpsRequest(float temp, float humid, float pressure) {
#else
  void httpsRequest(float temp, float humid) {
#endif // if/else MOONBASE_BOARD
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  Serial.print("connecting HTTPS ");
  Serial.println(server);
  if (!client.connect(server, 443)) {
    Serial.println("connection failed");
    return;
  }

  if (client.verify(fingerprint, "www.shac.org.nz")) {
    Serial.println("certificate matches");
  } else {
    Serial.println("certificate doesn't match");
  }

 // We now create a URI for the request
  String url = "/updateweatherstation.php?action=updateraw";
  url += "&devid=";
  url += String(_ESP_id,HEX);
  url += "&devname=";
  url += DEVNAME;
  url += "&issueid=";
  url += ISSUEID;
  url += "&dateutc=now&indoortemp=";
  url += temp;
  url += "&indoorhumidity=";
  url += humid;
  #if MOONBASE_BOARD
    url += "&indoorpressure=";
    url += pressure;
  #endif // #if MOONBASE_BOARD
 
 
  
  Serial.print("Requesting URL: ");
  Serial.println(url);
  
  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + "www.shac.org.nz" + "\r\n" + 
               "User-Agent: indoor-wx-v0.1" + "\r\n" +
               "Connection: close\r\n\r\n");

  Serial.println("request sent");

  //delay(100);

/*   
  Serial.print("01"); Serial.println(client.readStringUntil('\n'));
  Serial.print("02"); Serial.println(client.readStringUntil('\n'));
  Serial.print("03"); Serial.println(client.readStringUntil('\n'));
  Serial.print("04"); Serial.println(client.readStringUntil('\n'));
  Serial.print("05"); Serial.println(client.readStringUntil('\n'));
  Serial.print("06"); Serial.println(client.readStringUntil('\n'));
  Serial.print("07"); Serial.println(client.readStringUntil('\n'));
  Serial.print("08"); Serial.println(client.readStringUntil('\n'));
  Serial.print("09"); Serial.println(client.readStringUntil('\n'));
  Serial.print("10"); Serial.println(client.readStringUntil('\n'));
  Serial.print("11"); Serial.println(client.readStringUntil('\n'));
  Serial.print("12"); Serial.println(client.readStringUntil('\n'));
  Serial.print("13"); Serial.println(client.readStringUntil('\n'));
  Serial.print("14"); Serial.println(client.readStringUntil('\n'));
  Serial.print("15"); Serial.println(client.readStringUntil('\n'));
  Serial.print("16"); Serial.println(client.readStringUntil('\n'));
*/     



  unsigned long _timeout = 1000;
  unsigned long _startMillis = millis();
  //while (client.connected()) {     // client.connected seems to not always work! - returns FALSE at this point sometimes.
  
  while (millis() - _startMillis < _timeout) {
    String line = client.readStringUntil('\n');
    yield();
    Serial.print("Header Line: ");
    Serial.println(line);
    if (line == String("\r") ) {
      Serial.println("headers received");
      break;
    }
  }
  
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    yield();
    if (line.startsWith("GMT:")) {
      Serial.print("Got GMT: ");
      Serial.println(line);
    } else {
      Serial.print("Body Line: ");
      Serial.println(line);
    }
  }


  Serial.println("Finished");
  // read until return -1?????
  //while (client.read() >= 0) {Serial.print("."); }
  
  //Serial.println("reply was:");
  //Serial.println("==========");
  //Serial.println(line);
  //Serial.println("==========");
  //Serial.println("closing connection");


} // end httpsRequest()

