
/* WeMos DHT Server
 *
 * Connect to WiFi and respond to http requests with temperature and humidity
 *
 * Based on Adafruit ESP8266 Temperature / Humidity Webserver
 * https://learn.adafruit.com/esp8266-temperature-slash-humidity-webserver
 *
 * Depends on Adafruit DHT Arduino library
 * https://github.com/adafruit/DHT-sensor-library
 */

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <DHT.h>

//WiFiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <Ticker.h>
Ticker ticker;


#define DHTTYPE DHT22   // DHT Shield uses DHT 11
#define DHTPIN D4       // DHT Shield uses pin D4

const char* DEVNAME = ""; const char* ISSUEID  = ""; const char* ssid = ""; const char* password = ""; //


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





IPAddress server(120,138,27,109);

// Initialize DHT sensor
// Note that older versions of this library took an optional third parameter to
// tweak the timings for faster processors.  This parameter is no longer needed
// as the current DHT reading algorithm adjusts itself to work on faster procs.
DHT dht(DHTPIN, DHTTYPE);

float humidity, temperature;                 // Raw float values from the sensor
char str_humidity[10], str_temperature[10];  // Rounded sensor values and as strings

// Generally, you should use "unsigned long" for variables that hold time
unsigned long previousMillis = 0;            // When the sensor was last read
const long interval = 5000;                  // Wait this long until reading again
unsigned long lastConnectionTime = 0;             // last time you connected to the server, in milliseconds
enum WiFiStateEnum { DOWN, STARTING, UP }    ;
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

    // Reading temperature and humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    humidity = dht.readHumidity();        // Read humidity as a percent
    temperature = dht.readTemperature();  // Read temperature as Celsius

    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // Convert the floats to strings and round to 2 decimal places
    dtostrf(humidity, 1, 2, str_humidity);
    dtostrf(temperature, 1, 2, str_temperature);

    Serial.print("Humidity: ");
    Serial.print(str_humidity);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(str_temperature);
    Serial.println(" °C");
  }
}

void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);

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
void setup(void)
{
  // Open the Arduino IDE Serial Monitor to see what the code is doing
  Serial.begin(115200);
  dht.begin();

  Serial.println("WeMos DHT Server");
  _ESP_id = ESP.getChipId();  // uint32 -> unsigned long on arduino
  Serial.println(_ESP_id,HEX);
  Serial.println("");

  WiFi.onEvent(WiFiEvent);

  // Initial read
  read_sensor();

}

void loop(void)
{
  delay(1000);
  read_sensor();
  
  if (WiFiState == UP) { httpsRequest(temperature,humidity); }

  // Connect to your WiFi network
  if (WiFiState == DOWN) {
    WifiTryUp();
    WiFiState = STARTING;
    WiFiStartTime = millis();
  }
  if (WiFiState == STARTING) {
    if (millis() - WiFiStartTime > 30000) { WiFiState = DOWN; Serial.println("WiFi Connect Timeout"); }
  }


}




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



// Use web browser to view and copy
// SHA1 fingerprint of the certificate
const char* fingerprint = "CF 05 98 89 CA FF 8E D8 5E 5C E0 C2 E4 F7 E6 C3 C7 50 DD 5C";

// this method makes a HTTP connection to the server:
void httpsRequest(float temp, float humid) {

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


}

