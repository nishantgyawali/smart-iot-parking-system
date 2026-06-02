#define BLYNK_TEMPLATE_ID   "TMPL6IAca5X80"
#define BLYNK_TEMPLATE_NAME "smartparking"
#define BLYNK_AUTH_TOKEN    "SxbO8FpTsXLnyuWHv15wkHK0VjtWXJUm"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>

// ─────────────────────────────────────────────
//  WiFi credentials
// ─────────────────────────────────────────────
char ssid[] = "relianthome_2"; // replace your wifi ssid (only 2.4Ghz)
char pass[] = "CLB4378B17"; // repalce your wifi password 

// ─────────────────────────────────────────────
//  Google Apps Script URL
// ─────────────────────────────────────────────
const String googleScript =
  "https://script.google.com/macros/s/"
  "AKfycbw0J0_iY54lrbFirKVLFbHXTXl7T-1s7_iRCfAT3voYOxPzzT6bSQ_96KGXmRE__pRYuQ/exec";

// ─────────────────────────────────────────────
//  Pin definitions
// ─────────────────────────────────────────────
#define GATE_TRIG   13
#define GATE_ECHO   12

#define SLOT1_TRIG  14
#define SLOT1_ECHO  27

#define SLOT2_TRIG  25
#define SLOT2_ECHO  26

#define SLOT3_TRIG  33
#define SLOT3_ECHO  32

#define SLOT4_TRIG  17
#define SLOT4_ECHO  16

#define SERVO_PIN   18

// ─────────────────────────────────────────────
//  Hardware objects
// ─────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo             gateServo;
BlynkTimer        timer;

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
bool slot[4]    = {false, false, false, false};
bool oldSlot[4] = {false, false, false, false};

bool          isGateOpen   = false;
unsigned long gateOpenTime = 0;
const unsigned long GATE_OPEN_DURATION = 4000; // ms — gate stays open 4 s

// ─────────────────────────────────────────────
//  Ultrasonic distance  (returns cm, 999 on timeout)
// ─────────────────────────────────────────────
long readDistance(int trig, int echo)
{
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 30000UL); // 30 ms timeout
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// ─────────────────────────────────────────────
//  Google Sheets logger
// ─────────────────────────────────────────────
void sendGoogle(int slotNo, const String& event)
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Google] WiFi not connected — skipping log");
    return;
  }

  HTTPClient http;
  String url = googleScript + "?slot=" + String(slotNo) + "&event=" + event;

  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(5000); 

  int code = http.GET();
  Serial.printf("[Google] Slot %d %s → HTTP %d\n", slotNo, event.c_str(), code);
  http.end();
}

// ─────────────────────────────────────────────
//  LCD helper — Updated to display Slot A, B, C, D
// ─────────────────────────────────────────────
void updateLCD(int occupied, int freeSlots)
{
  // Row 0: "Occ:X   Free:X  "
  lcd.setCursor(0, 0);
  lcd.print("Occ:");
  lcd.print(occupied);
  lcd.print("   Free:");
  lcd.print(freeSlots);
  lcd.print("  ");   

  // Row 1: slot status updated to letters -> "A:O B:F C:F D:F"
  lcd.setCursor(0, 1);
  char slotLetters[] = {'A', 'B', 'C', 'D'};
  for (int i = 0; i < 4; i++) {
    lcd.print(slotLetters[i]);
    lcd.print(":");
    lcd.print(slot[i] ? "O" : "F");
    if (i < 3) lcd.print(" ");
  }
}

// ─────────────────────────────────────────────
//  Slot update  (called every 1 s by timer)
// ─────────────────────────────────────────────
void updateSlots()
{
  long dist[4];
  dist[0] = readDistance(SLOT1_TRIG, SLOT1_ECHO);
  dist[1] = readDistance(SLOT2_TRIG, SLOT2_ECHO);
  dist[2] = readDistance(SLOT3_TRIG, SLOT3_ECHO);
  dist[3] = readDistance(SLOT4_TRIG, SLOT4_ECHO);

  for (int i = 0; i < 4; i++) {
    slot[i] = (dist[i] > 0 && dist[i] <= 15);
  }

  Serial.printf("[Slots] dA=%lcm dB=%lcm dC=%lcm dD=%lcm  →  %s %s %s %s\n",
    dist[0], dist[1], dist[2], dist[3],
    slot[0]?"OCC":"free", slot[1]?"OCC":"free",
    slot[2]?"OCC":"free", slot[3]?"OCC":"free");

  // Log changes dynamically to Google Sheets
  for (int i = 0; i < 4; i++) {
    if (slot[i] != oldSlot[i]) {
      sendGoogle(i + 1, slot[i] ? "ENTRY" : "EXIT");
      oldSlot[i] = slot[i];
    }
  }

  int occupied  = 0;
  for (int i = 0; i < 4; i++) if (slot[i]) occupied++;
  int freeSlots = 4 - occupied;

  updateLCD(occupied, freeSlots);

  // Blynk updates
  for (int i = 0; i < 4; i++) {
    Blynk.virtualWrite(V0 + i, slot[i] ? 1 : 0);
  }
  Blynk.virtualWrite(V4, freeSlots);
}

// ─────────────────────────────────────────────
//  Gate control  (called every 200 ms by timer)
// ─────────────────────────────────────────────
void gateControl()
{
  long d = readDistance(GATE_TRIG, GATE_ECHO);

  int freeSlots = 0;
  for (int i = 0; i < 4; i++) if (!slot[i]) freeSlots++;

  // Door logic: Ultrasonic detects vehicle < 20cm, moves servo from 0 to 90 degrees
  if (!isGateOpen && d > 0 && d < 20 && freeSlots > 0) {
    gateServo.write(90);
    isGateOpen   = true;
    gateOpenTime = millis();
    Serial.printf("[Gate] OPENED — car at %lcm, %d free slots\n", d, freeSlots);
  }

  // Close gate after duration window runs out
  if (isGateOpen && (millis() - gateOpenTime >= GATE_OPEN_DURATION)) {
    gateServo.write(0);
    isGateOpen = false;
    Serial.println("[Gate] CLOSED");
  }

  if (!isGateOpen && d > 0 && d < 20 && freeSlots == 0) {
    Serial.println("[Gate] Car detected but lot FULL — gate stays closed");
  }
}

BLYNK_WRITE(V5)
{
  int val = param.asInt();
  if (val == 1) {
    gateServo.write(90);
    isGateOpen   = true;
    gateOpenTime = millis();
    Serial.println("[Gate] Manual OPEN via Blynk V5");
  }
}

// ─────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  Serial.println("\n=== Smart Parking System Booting ===");

  int trigPins[] = {GATE_TRIG, SLOT1_TRIG, SLOT2_TRIG, SLOT3_TRIG, SLOT4_TRIG};
  int echoPins[] = {GATE_ECHO, SLOT1_ECHO, SLOT2_ECHO, SLOT3_ECHO, SLOT4_ECHO};
  for (int i = 0; i < 5; i++) {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);
  }

  gateServo.setPeriodHertz(50);
  gateServo.attach(SERVO_PIN, 500, 2400);
  gateServo.write(0); 

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Smart Parking   ");
  lcd.setCursor(0, 1); lcd.print("Connecting WiFi ");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  lcd.setCursor(0, 1); lcd.print("System Ready!   ");
  delay(1500);
  lcd.clear();

  timer.setInterval(1000L, updateSlots);  
  timer.setInterval(200L,  gateControl);  

  Serial.println("=== System Ready ===");
}

void loop()
{
  Blynk.run();
  timer.run();
}
