#include <Arduino.h>
#include <Keypad.h>
#include <SPI.h>
#include <MFRC522.h>
#include <time.h>
#include <ESP32PWM.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>

#define num_rows 4
#define num_cols 4

#define BUZZER_PIN 3
#define SS_PIN 17
#define RST_PIN 4
#define MISO_PIN 6
#define MOSI_PIN 7
#define SCK_PIN 15
#define SUCCESS_LED 14
#define FAIL_LED 13

#define trig1 35
#define echo1 36
#define trig2 48
#define echo2 45
#define THRESHOLD 15
#define LOWCOUNT 7
#define servoPin 21

bool insideLow = false;
bool outsideLow = false;
int insideLowCount = 0;
int outsideLowCount = 0;
char flipflopvar[2];

Servo lock;
bool lockStat; // false if unlocked

int count = 0;
String x = "";
MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

String pass = "";
String correct_pass = "";

bool RFID_set = false;

byte row_pins[num_rows] = {1, 2, 42, 41};
byte col_pins[num_cols] = {40, 39, 38, 37};
char keys[num_rows][num_cols] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

std::vector<String> approvedUIDs = {};

Keypad k = Keypad(makeKeymap(keys), row_pins, col_pins, num_rows, num_cols);

void unlockSuccessEvent();
void unlockFailEvent();

int flipflop(char input){
  // if(input == 'a'){
  //   flipflopvar = 'a';
  // }
  // if(flipflopvar == 'a' || flipflopvar == input){
  //     flipflopvar = input;
  //     return -1;
  // } else {
  //   if(flipflopvar == 'o' && input == 'i'){
  //     printf("Going Inside\n");
  //     return 1;
  //   }
  //   if(flipflopvar == 'i' && input == 'o'){
  //     printf("Going Outside\n");
  //     return 0;
  //   }
  //   return -1;
  // }
  if(input == 'a' && flipflopvar[0] != 'a' && flipflopvar[1] != 'a'){
    flipflopvar[0] = 'a';
    flipflopvar[1] = 'a';
  }
  if(flipflopvar[0] == 'a' || flipflopvar[0] == input) flipflopvar[0] = input;
  else if(flipflopvar[1] == 'a' || flipflopvar[1] == input) flipflopvar[1] = input;

  if(flipflopvar[0] == 'o' && flipflopvar[1] == 'i'){
    printf("Going Inside\n");
    return 1;
  }
  if(flipflopvar[0] == 'i' && flipflopvar[1] == 'o'){
    printf("Going Outside\n");
    return 0;
  }
  return -1;
}

const char* ssid = "SmartLock-ESP32";
const char* password = "12345678";

WebServer server(80);

String logs = "[\"System initialized\"]";
bool sensorTriggered = false;

void handleLogs() {
  server.send(200, "application/json", logs);
}

void handleSensor() {
  server.send(200, "application/json", "{\"sensor\": \"" + String(sensorTriggered ? "abnormal" : "normal") + "\"}");
}

void handleAddUID() {
  if (server.hasArg("plain")) {
    approvedUIDs.push_back(server.arg("plain"));
    logs += ",\"UID added: " + server.arg("plain") + "\"";
    server.send(200, "application/json", "{\"status\":\"UID added\"}");
  }
}

void handleRemoveUID() {
  if (server.hasArg("plain")) {
    String toRemove = server.arg("plain");
    approvedUIDs.erase(std::remove(approvedUIDs.begin(), approvedUIDs.end(), toRemove), approvedUIDs.end());
    logs += ",\"UID removed: " + toRemove + "\"";
    server.send(200, "application/json", "{\"status\":\"UID removed\"}");
  }
}


void handleSetPassword() {
  if (server.hasArg("plain")) {
    String newPass = server.arg("plain");
    if (newPass.length() == 4) {
      correct_pass = newPass;
      logs += ",\"Password changed\"";
      server.send(200, "application/json", "{\"status\":\"Password changed\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Password must be 4 digits\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"No password provided\"}");
  }
}

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);

  WiFi.softAP(ssid, password);
  Serial.println("AP IP address: " + WiFi.softAPIP().toString());

  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
      server.send(500, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  
  server.on("/logs.html", HTTP_GET, []() {
    File file = SPIFFS.open("/logs.html", "r");
    if (!file) {
      server.send(500, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/sensor", HTTP_GET, handleSensor);
  server.on("/api/config/rfid/add", HTTP_POST, handleAddUID);
  server.on("/api/config/rfid/remove", HTTP_POST, handleRemoveUID);

  server.on("/api/config/password", HTTP_POST, handleSetPassword);


  server.begin();

  Serial.begin(115200);
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(trig1, OUTPUT);
  pinMode(echo1, INPUT);
  pinMode(trig2, OUTPUT);
  pinMode(echo2, INPUT);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);      // Initiate  SPI bus
  mfrc522.PCD_Init();   // Initiate MFRC522
  Serial.println("Approximate your card to the reader...");
  Serial.println();
  flipflopvar[0] = 'a';
  flipflopvar[1] = 'a';
	lock.setPeriodHertz(50);    // standard 50 hz servo
	lock.attach(servoPin, 1000, 2000); // attaches the servo on pin 18 to the servo object
  lock.write(180);
}

void loop() {
  server.handleClient();

  if(RFID_set) {
    if(correct_pass.length() < 4) {
      char key = k.getKey();
      if(key) {
        correct_pass.concat(key);
      }
    }
    else {
      digitalWrite(trig1, LOW);
      delayMicroseconds(2);
      digitalWrite(trig1, HIGH);
      delayMicroseconds(10);
      digitalWrite(trig1, LOW);
      float distance1 = (pulseIn(echo1, HIGH) * 0.0343) / 2;
    
      digitalWrite(trig2, LOW);
      delayMicroseconds(2);
      digitalWrite(trig2, HIGH);
      delayMicroseconds(10);
      digitalWrite(trig2, LOW);
      float distance2 = (pulseIn(echo2, HIGH) * 0.0343) / 2;
    
      if(distance1 < THRESHOLD){
        insideLow = true;
        if(insideLowCount < LOWCOUNT) insideLowCount++;
      } else {
        insideLow = false;
        insideLowCount = 0;
      }
    
      if(distance2 < THRESHOLD){
        outsideLow = true;
        if(outsideLowCount < LOWCOUNT) outsideLowCount++;
      } else {
        outsideLow = false;
        outsideLowCount = 0;
      }
    
      printf("%d %d %c %c\n", insideLowCount, outsideLowCount, flipflopvar[0], flipflopvar[1]);

      int retVal;
      if(outsideLowCount == LOWCOUNT){
        retVal = flipflop('o');
      } else if(insideLowCount == LOWCOUNT){
        retVal = flipflop('i');
      } else {
        flipflop('a');
      }
    
      if(retVal == 1 && lockStat == true){
        sensorTriggered = true;
        logs+=",\"Movement detected:Going Inside\"";
        tone(BUZZER_PIN, 2000); // Play 500 Hz tone
        delay(1000);            // Let it play for 200ms
        noTone(BUZZER_PIN);     // Stop the tone
        flipflop('a');
      }

      delay(5);
      
      if (!mfrc522.PICC_IsNewCardPresent()) {
        char key = k.getKey();
        if(key) {
          pass.concat(key);
        }
        if(pass.length() == 4) {
          if(pass.equals(correct_pass)) {
            unlockSuccessEvent();
          }
          else {
            unlockFailEvent();
          }
          pass.clear();
        }
        return;
      }
      // Select one of the cards
      if (!mfrc522.PICC_ReadCardSerial())  {
        return;
      }
      String content= "";
      byte letter;
      for (byte i = 0; i < mfrc522.uid.size; i++)  {
        content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
        content.concat(String(mfrc522.uid.uidByte[i], HEX));
      }
      content.toUpperCase();
      if (content.substring(1).equalsIgnoreCase(x.substring(1))) //change here the UID of the card/cards that you want to give access
      {
        unlockSuccessEvent();
      }
     
      else {
        unlockFailEvent();
      }

      pass.clear();
      printf("Password: %s Correct Password: %s%c", pass, correct_pass, '\n');
    }
  }
  else {
    if (!mfrc522.PICC_IsNewCardPresent()) 
    {
      return;
    }
    // Select one of the cards
    if (!mfrc522.PICC_ReadCardSerial()) 
    {
      return;
    }

    for (byte i = 0; i < mfrc522.uid.size; i++) 
    {
      x.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
      x.concat(String(mfrc522.uid.uidByte[i], HEX));
    }

    RFID_set = true;
  }
  printf("Password: %s Correct Password: %s%c", pass, correct_pass, '\n');
}

void setPass(String s) {
  correct_pass = s;
}

void unlockSuccessEvent() {
  tone(BUZZER_PIN, 1000); // Play 1000 Hz tone
  digitalWrite(SUCCESS_LED, HIGH); // Turn Unlock indicator LED on
  delay(500);             // Let it play for 200ms
  //implement unlocking
  lock.write(0);
  lockStat = false;
  digitalWrite(SUCCESS_LED, LOW); // Turn Unlock indicator LED OFF
  noTone(BUZZER_PIN);     // Stop the tone
  delay(500);             // Wait before next beep
}

void unlockFailEvent() {
  tone(BUZZER_PIN, 500); // Play 500 Hz tone
  digitalWrite(FAIL_LED, HIGH);
  delay(500);             // Let it play for 200ms
  lock.write(180);
  lockStat = true;
  noTone(BUZZER_PIN);     // Stop the tone
  digitalWrite(FAIL_LED, LOW);
  delay(500);             // Wait before next beep
}

bool isApproved(String uid) {
  for (String id : approvedUIDs) {
    if (uid == id) return true;
  }
  return false;
}
