#include <SPI.h>
// WiFi
#include <WiFi.h>

// NTP
#include "time.h"

//MQTT
#include <PubSubClient.h> //MQTT

//Display
#include <SSD1306.h> //Display

//BME280
#include <BME280I2C.h>
#include <Wire.h>


// WiFi Config
const char *ssid = "******************";
const char *password = "******************";

// NTP Config
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// MQTT Config
const char *mqttServer = "192.168.3.5";
const char *mqttClientName = "Geigercounter";
const char *mqttTopicCpm = "/backyard/geigercounter/cpm";
const char *mqttTopicuSvh = "/backyard/geigercounter/usvh";
const char *mqttTopicTemperature = "/backyard/geigercounter/temperature";
const char *mqttTopicPressure = "/backyard/geigercounter/pressure";
const char *mqttTopicHumidity = "/backyard/geigercounter/humidity";

// Geigercounter Config
const int clickImputPin = 15;
const float tubeCpmOffset = 7.2;       //Correct Ghost counts create by the tube itself without radiation (M4011: 0,2/s = 12/min)
const float conversionIndex = 153.8;  //how many cpm are 1 uSv/h?

volatile long counts = 0;      // Tube events
float cpm = 0;                  // CPM
float uSvh = 0;                // Micro Sievert per hour
unsigned long previousMillis = 0;           // Time measurement
unsigned long currentMillis = millis();
unsigned long start = 0;
#define MINUTE_PERIOD 60000             // One Minute in ms

//BME280 Config (0x76)
#define BME_SDA 5 //19
#define BME_SCL 4 //18
float temperature(NAN), humidity(NAN), pressure(NAN);

BME280I2C bme;    // Default : forced mode, standby time = 1000 ms
                  // Oversampling = pressure ×1, temperature ×1, humidity ×1, filter off,


WiFiClient espClient;

PubSubClient mqttClient(espClient);

SSD1306 display(0x3c, 5, 4);

// Method definitions
void connectWiFi();

void disconnectWiFi();

void connectMqtt();

void getNtpTime();

void ISR_impulse();

void sendGeigercounterData(double, double);

void updateBmeData();

void sendBmeData(double, double, double);

void updateDisplayValues();

void displayString(String dispString, int x, int y);

void displayInt(int dispInt, int x, int y);

void displayInit();

// initializes everything
void setup() {
    Serial.begin(115200);
    Wire.begin(BME_SDA, BME_SCL);
    pinMode(clickImputPin, INPUT); // Set pin for capturing Tube events
    
    displayInit();
    displayString("Welcome", 64, 15);

    connectWiFi(); // Connect WiFi
    
    getNtpTime(); // Get Time

    //Start MQTT
    connectMqtt();

    //Start BME280
    if (!bme.begin()) {
        Serial.println("Could not find a valid BME280 !");
        displayString("No BME280!", 64, 15);
    }

    //Start counting
    interrupts(); // Enable interrupts                                                     // Enable interrupts
    attachInterrupt(digitalPinToInterrupt(clickImputPin), ISR_impulse, FALLING); // Define interrupt on falling edge
    unsigned long clock1 = millis();
    start = clock1;

    unsigned long previousMillis = 0;           // Time measurement
    unsigned long currentMillis = millis();
    unsigned long start = 0;

}

void loop() {
    currentMillis = millis();

    //1 Minute over
    if (currentMillis - previousMillis >= MINUTE_PERIOD) {
        previousMillis = currentMillis;
        cpm = counts;

        if (cpm >= tubeCpmOffset) {
            cpm = cpm - tubeCpmOffset;
            counts = 0;
        } else {
            cpm = 0;
            counts = counts - tubeCpmOffset;
        }
        uSvh = cpm / conversionIndex;


        Serial.println("CPM: " + String(cpm));
        Serial.println("uSvh: " + String(uSvh));

        sendGeigercounterData(cpm, uSvh);

        //Update also BME280 data
        updateBmeData();

        updateDisplayValues();
    } else if ((currentMillis - previousMillis) >= (MINUTE_PERIOD / 10)) {
        updateDisplayValues();
    }
    cpm = counts;
}

/**
*
* Sub methods
*
**/

void ISR_impulse() { // Captures count of events from Geiger counter board
    Serial.println("Click!");
    Serial.println(millis());
    Serial.println(currentMillis);
    Serial.println(previousMillis);
    counts++;
    
    //updateDisplayValues();  
    
}

//Connect to WiFi. Without WiFi no time
void connectWiFi() {
    WiFi.mode(WIFI_STA);
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        display.clear();
        displayString(String(WiFi.status()), 64, 15);
        delay(10);
    }
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    display.clear();
    displayString(WiFi.localIP().toString(), 64, 15);
}

void connectMqtt() {
    mqttClient.setServer(mqttServer, 1883);
    mqttClient.setKeepAlive(90);
    if (mqttClient.connect(mqttClientName)) {
        Serial.println("MQTT connected");
    } else {
        Serial.print("Error while connecting to MQTT");
    }
}

void disconnectWiFi() {
    Serial.println("Disconnect WiFi");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

void getNtpTime() {
    Serial.println("Get NTP Time");
    Serial.println(ntpServer);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void sendGeigercounterData(double cpmValue, double usvhValue) {
    Serial.println("Start converting Geigercounter Data");

    char cpmString[10];
    sprintf(cpmString, "%lf", cpmValue);

    char usvhString[10];
    sprintf(usvhString, "%lf", usvhValue);

    Serial.println("Start pushing Data");

    Serial.println(usvhString);

    if (!mqttClient.connected()) {
        mqttClient.connect(mqttClientName);
    }

    mqttClient.publish(mqttTopicCpm, cpmString);
    mqttClient.publish(mqttTopicuSvh, usvhString);

}

/**
 * Update data read from the BME
 */
void updateBmeData() {

    if(!bme.begin())
    {
      Serial.println("Could not find BME280 sensor!");
      return;
    }

    BME280::TempUnit temperatureUnit(BME280::TempUnit_Celsius);
    BME280::PresUnit pressureUnit(BME280::PresUnit_hPa);

    bme.read(pressure, temperature, humidity, temperatureUnit, pressureUnit);

    Serial.println(temperature);
    Serial.println(pressure);
    Serial.println(humidity);
    sendBmeData(temperature, pressure, humidity);
}

void sendBmeData(float temperature, float pressure, float humidity) {
    Serial.println("Start converting BME Data");

    char temperatureString[20];
    sprintf(temperatureString, "%lf", temperature);

    char pressureString[20];
    sprintf(pressureString, "%lf", pressure);

    char humidityString[20];
    sprintf(humidityString, "%lf", humidity);

    Serial.println("Publish BME Data");
    mqttClient.publish(mqttTopicTemperature, temperatureString);
    mqttClient.publish(mqttTopicPressure, pressureString);
    mqttClient.publish(mqttTopicHumidity, humidityString);
}



// Display stuff
void displayInit() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
}

void displayInt(int dispInt, int x, int y) {
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x, y, String(dispInt));
  display.setFont(ArialMT_Plain_10);
  display.display();
}

void displayString(String dispString, int x, int y) {
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x, y, dispString);
  display.setFont(ArialMT_Plain_24);
  display.display();
}


void updateDisplayValues() {
  display.clear();
  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  
  display.drawString(0, 0, "µSv/h");
  display.drawString(40, 0, String(uSvh));
  display.drawString(80, 0, String((int) roundf(cpm))+" CpM");

  display.drawString(0, 15, "Temperatur");
  display.drawString(80, 15, String(temperature)+" °C");

  display.drawString(0, 30, "Luftfeuchtigkeit");
  display.drawString(80, 30, String(humidity)+" %");

  display.drawString(0, 45, "Luftdruck");
  display.drawString(55, 45, String(pressure)+" hPa");



 
  display.setFont(ArialMT_Plain_10);
  display.display();
  
}
