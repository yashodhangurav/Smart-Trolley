#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

// ---------------- PIN CONFIG ----------------
#define SS_PIN  5    // GPIO5 for RFID SS
#define RST_PIN 11   // GPIO11 for RFID RST
MFRC522 rfid(SS_PIN, RST_PIN);

// LCD 16x2
LiquidCrystal_I2C lcd(0x27,16,2);

// Buzzer pin
#define BUZZER_PIN 6

// WiFi + Google Sheet
const char* ssid = "shri";
const char* password = "12345678";
const char* scriptURL = "https://script.google.com/macros/s/AKfycbxI0n_sXaOLdHH_eakPvGaENa5C2jhoqZ9GZBC1eD6HBR9USzPf9JLx990iJ7ah4TK4jQ/exec";

// ---------------- ACCESS CARD ----------------
byte accessCard[4] = {0x73, 0x96, 0x27, 0x0E}; // Admin card

// ---------------- PRODUCT CARDS ----------------
byte product1[4] = {0xDA, 0x23, 0x90, 0x04};
byte product2[4] = {0xC0, 0x2D, 0xCD, 0xCF};
byte product3[4] = {0x45, 0x3F, 0xB2, 0xCF};
byte product4[4] = {0x41, 0x4D, 0x90, 0x04};

// Product names and prices
String productName[4] = {"Dairy Milk", "Lays Chips", "Notebook", "Juice Bottle"};
int productPrice[4] = {40, 20, 30, 50};
bool productAdded[4] = {false, false, false, false};

// ---------------- KEYPAD ----------------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {13,14,15,16}; // Rows GPIO
byte colPins[COLS] = {7,8,9,10};    // Cols GPIO
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------------- VARIABLES ----------------
bool accessGranted = false;
String correctPassword = "1234";
int totalAmount = 0;

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Connecting WiFi...");

  WiFi.begin(ssid, password);
  while(WiFi.status()!=WL_CONNECTED){
    delay(300);
    lcd.print(".");
  }

  pinMode(BUZZER_PIN, OUTPUT);

  lcd.clear();
  lcd.print("Smart Trolley");
  lcd.setCursor(0,1);
  lcd.print("Scan Admin Card");
}

// ---------------- LOOP ----------------
void loop() {
  // ---------------- RFID ----------------
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    byte *uid = rfid.uid.uidByte;

    // ---------------- ACCESS CARD ----------------
    if(compareTag(uid, accessCard)){
      if(!accessGranted){
        lcd.clear();
        lcd.print("Enter Password:");
        if(getPassword()){
          accessGranted = true;
          lcd.clear();
          lcd.print("Trolley Unlocked");
          sendToSheet("ADMIN", 0, "TROLLEY_UNLOCKED");
          tone(BUZZER_PIN, 2000, 300); // Unlock beep
          delay(1000);
          lcd.clear();
          lcd.print("Scan Products");
        } else {
          lcd.clear();
          lcd.print("Access Denied");
          // Continuous buzzer for 5 seconds
          unsigned long startTime = millis();
          while(millis() - startTime < 5000){
            digitalWrite(BUZZER_PIN, HIGH);
          }
          digitalWrite(BUZZER_PIN, LOW);
          delay(500); // small delay after buzzer
          lcd.clear();
          lcd.print("Scan Admin Card");
        }
      } else {
        accessGranted = false;
        lcd.clear();
        lcd.print("Checkout Done");
        sendToSheet("ADMIN", 0, "TROLLEY_LOCKED");
        tone(BUZZER_PIN, 2500, 500); // Checkout beep
        delay(1500);
        resetTrolley();
      }
      rfid.PICC_HaltA();
      return;
    }

    // ---------------- PRODUCT CARDS ----------------
    if(accessGranted) handleProduct(uid);

    rfid.PICC_HaltA();
  }

  // ---------------- CHECK FOR KEYPAD ----------------
  char key = keypad.getKey();
  if(key){
    if(key=='A' && accessGranted){
      printBill();
      accessGranted=false; // Lock trolley after bill
      resetTrolley();
    }
    if(key=='D'){ // Reset trolley
      resetTrolley();
      tone(BUZZER_PIN, 3000, 400); // Reset beep
    }
  }
}

// ---------------- HELPER FUNCTIONS ----------------
bool compareTag(byte *tag, byte *validTag){
  for(int i=0;i<4;i++)
    if(tag[i]!=validTag[i]) return false;
  return true;
}

// Handle product toggle
void handleProduct(byte *uid){
  int index=-1;
  if(compareTag(uid, product1)) index=0;
  else if(compareTag(uid, product2)) index=1;
  else if(compareTag(uid, product3)) index=2;
  else if(compareTag(uid, product4)) index=3;

  if(index==-1){
    lcd.clear(); lcd.print("Unknown Tag!"); delay(1000); return;
  }

  if(productAdded[index]){
    productAdded[index]=false;
    totalAmount -= productPrice[index];
    lcd.clear(); lcd.print("Removed:");
    lcd.setCursor(0,1); lcd.print(productName[index]);
    sendToSheet(productName[index], productPrice[index], "REMOVED");
    tone(BUZZER_PIN, 1000, 200); // beep for removed product
  } else {
    productAdded[index]=true;
    totalAmount += productPrice[index];
    lcd.clear(); lcd.print("Added:");
    lcd.setCursor(0,1); lcd.print(productName[index]);
    sendToSheet(productName[index], productPrice[index], "ADDED");
    tone(BUZZER_PIN, 2000, 200); // beep for added product
  }
  delay(1200);
  showTotal();
}

// Show total on LCD
void showTotal(){
  lcd.clear();
  lcd.print("Total: "); lcd.print(totalAmount); lcd.print(" Rs");
  delay(1200);
  lcd.clear();
  lcd.print("Scan Products");
}

// Get password from keypad
bool getPassword(){
  String input="";
  int attempts=0;
  while(attempts<3){
    input="";
    lcd.setCursor(0,1);
    while(true){
      char key = keypad.getKey();
      if(key!=NO_KEY){
        if(key=='#') break;
        else if(key=='*'){ input=""; lcd.setCursor(0,1); lcd.print("Cleared "); }
        else{ input+=key; lcd.setCursor(input.length()-1,1); lcd.print("*"); }
      }
    }
    if(input==correctPassword) return true;
    attempts++;
    lcd.clear(); lcd.print("Wrong! Try again");
    // Continuous buzzer for 5 seconds
    unsigned long startTime = millis();
    while(millis() - startTime < 5000){
      digitalWrite(BUZZER_PIN, HIGH);
    }
    digitalWrite(BUZZER_PIN, LOW);
    lcd.clear(); lcd.print("Enter Password:");
  }
  return false;
}

// Print bill and send to Google Sheets
void printBill(){
  lcd.clear();
  lcd.print("BILL:");
  tone(BUZZER_PIN, 3000, 300); // beep when bill printing starts
  delay(1000);
  for(int i=0;i<4;i++){
    if(productAdded[i]){
      lcd.clear();
      lcd.print(productName[i]);
      lcd.setCursor(0,1);
      lcd.print("Price: "); lcd.print(productPrice[i]); lcd.print(" Rs");
      tone(BUZZER_PIN, 1500, 200); // beep for each product in bill
      delay(1200);
    }
  }
  lcd.clear();
  lcd.print("TOTAL: "); lcd.print(totalAmount); lcd.print(" Rs");
  tone(BUZZER_PIN, 2500, 400); // beep for total
  sendToSheet("CUSTOMER", totalAmount, "CHECKOUT");
  delay(2000);
}

// Reset trolley function
void resetTrolley(){
  for(int i=0;i<4;i++) productAdded[i]=false;
  totalAmount=0;
  accessGranted=false;
  lcd.clear();
  lcd.print("Scan Admin Card");
}

// Send data to Google Sheets
void sendToSheet(String name, int price, String eventType){
  if(WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http;
  String url=String(scriptURL)+"?product="+name+"&price="+price+"&event="+eventType+"&total="+totalAmount;
  http.begin(url);
  http.GET();
  http.end();
}