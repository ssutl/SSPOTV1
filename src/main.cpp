#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "time.h"
#include "config.h"  // This includes the wifi ssid, password, firebase api key and database url

const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;
const char *ntpServer = "pool.ntp.org";

#define recievePin 20
#define transmitPin 21
#define max3485_RE_DE 4

FirebaseData fbdo;
FirebaseConfig config;
FirebaseAuth auth;

HardwareSerial RS485Serial(1);

bool signUpOK = false;

const uint8_t npkRequest[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};

String findFirstKey(String jsonString)
{
  int startPos = jsonString.indexOf("\""); // Find the first quote
  if (startPos == -1)
    return ""; // Return empty string if no quote is found

  int endPos = jsonString.indexOf("\"", startPos + 1); // Find the closing quote
  if (endPos == -1)
    return ""; // Return empty string if no closing quote is found

  return jsonString.substring(startPos + 1, endPos); // Extract and return the first key
}

void adjustTimeOffset()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  time_t now = mktime(&timeinfo);
  struct tm *local = localtime(&now);
  bool daylight = local->tm_isdst > 0;
  int offset = daylight ? gmtOffset_sec + daylightOffset_sec : gmtOffset_sec;
  configTime(offset, 0, ntpServer);
}

void setup()
{
  // Initialising serial and wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Serial for debugging
  Serial.begin(115200);
  // Serial for RS485
  RS485Serial.begin(4800, SERIAL_8N1, recievePin, transmitPin);

  // Get max3485 in receive mode
  pinMode(max3485_RE_DE, OUTPUT);
  digitalWrite(max3485_RE_DE, LOW);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    // wait 10 seconds for connection:
    delay(1000);
  }

  // init and get the time
  configTime(0, 0, ntpServer);
  adjustTimeOffset();

  // Once connected to wifi, sign up to firebase
  config.api_key = WEB_API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", ""))
  {
    Serial.println("SignUp Ok");
    signUpOK = true;
  }

  // Begin firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}



String getDateTimeString()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return "";
  }

  char dateTime[20];
  sprintf(dateTime, "%04d-%02d-%02dT%02d:%02d:%02d",
          timeinfo.tm_year + 1900,
          timeinfo.tm_mon + 1,
          timeinfo.tm_mday,
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec);

  return String(dateTime);
}

void logSensorData()
{
  // Log sensor data to firebase
  uint8_t response[19];
  // Set max3485 in transmit mode
  digitalWrite(max3485_RE_DE, HIGH);
  RS485Serial.write(npkRequest, 8);
  RS485Serial.flush(); // Clear any unwanted data from the serial buffer

  // Wait for response
  digitalWrite(max3485_RE_DE, LOW); // Set max3485 in receive mode
  delay(1000);

  if (RS485Serial.available() > 0)
  {
    RS485Serial.readBytes(response, sizeof(response));

    if (response[1] == 0x03)
    { // Check if the function code matches

      if (Firebase.RTDB.get(&fbdo, "/SSPOTV1"))
      {
        if (fbdo.dataType() == "json")
        {
          // count the number of keys in the json object
          FirebaseJson &json = fbdo.jsonObject();
          size_t count = json.iteratorBegin() / 4;

          if (count >= 5000)
          {
            String jsonString = fbdo.jsonString();
            String firstKey = findFirstKey(jsonString);
            std::string path = "/SSPOTV1/" + std::string(firstKey.c_str());
            Firebase.RTDB.deleteNode(&fbdo, path);
          }
        }
      }

      float moisture = (response[3] << 8 | response[4]) / 10.0;
      float temperature = (response[5] << 8 | response[6]) / 10.0;
      float conductivity = (response[7] << 8 | response[8]);
      Serial.println("Conductivity");
      Serial.println(conductivity);
      Serial.println("Temperature");
      Serial.println(temperature);
      Serial.println("Moisture");
      Serial.println(moisture);

      // Get the current time
      String dateTime = getDateTimeString();

      // Skip logging if values are invalid
      if (temperature > 60 || moisture > 100 || conductivity > 1000)
      {
        Serial.println("Invalid sensor readings detected. Skipping upload.");
        return;
      }
      else
      {
        // Construct new database path
        std::string path = "/SSPOTV1/" + std::string(dateTime.c_str());

        // Log only the specified values to Firebase under the new path
        Firebase.RTDB.setFloat(&fbdo, path + "/Moisture", moisture);
        Firebase.RTDB.setFloat(&fbdo, path + "/Temperature", temperature);
        Firebase.RTDB.setFloat(&fbdo, path + "/Conductivity", conductivity);
      }
    }
  }
}

void loop()
{
  if (Firebase.ready() && signUpOK)
  {
    logSensorData();
  }

  // put your main code here, to run repeatedly:
}
