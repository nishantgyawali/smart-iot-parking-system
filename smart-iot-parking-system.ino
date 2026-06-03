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
char ssid[] = "relianthome_2";
char pass[] = "CLB4378B17";

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
//  State variables
// ─────────────────────────────────────────────
bool slot[4]    = {false, false, false, false};
bool oldSlot[4] = {false, false, false, false};

// Google Sheet Queue parameters to avoid latency spikes
int  pendingGoogleSlot = -1;
String pendingGoogleEvent = "";

bool          isGateOpen   = false;
unsigned long gateOpenTime = 0;
const unsigned long GATE_OPEN_DURATION = 4000; // ms

// ─────────────────────────────────────────────
//  Ultrasonic distance
// ─────────────────────────────────────────────
long readDistance(int trig, int echo)
{
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 20000UL); // Reduced timeout to 20ms for faster execution
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// ─────────────────────────────────────────────
//  Google Sheets logger (Optimized timeouts)
// ─────────────────────────────────────────────
void sendGoogle(int slotNo, const String& event)
{
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  String url = googleScript + "?slot=" + String(slotNo) + "&event=" + event;

  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(1500); // Drastically cut down from 5000ms to 1500ms max

  int code = http.GET();
  Serial.printf("[Google] Slot %d %s → HTTP %d\n", slotNo, event.c_str(), code);
  http.end();
}

// ─────────────────────────────────────────────
//  Asynchronous Google Sheet Processor
// ─────────────────────────────────────────────
void processGoogleQueue()
{
  // If there's a pending Google Sheet event, log it here outside the main slot reader loop
  if (pendingGoogleSlot != -1) {
    int tempSlot = pendingGoogleSlot;
    String tempEvent = pendingGoogleEvent;

    // Clear queue variables immediately before the slow network call
    pendingGoogleSlot = -1;
    pendingGoogleEvent = "";

    sendGoogle(tempSlot, tempEvent);
  }
}

// ─────────────────────────────────────────────
//  LCD helper
// ─────────────────────────────────────────────
void updateLCD(int occupied, int freeSlots)
{
  lcd.setCursor(0, 0);
  lcd.print("Occ:");
  lcd.print(occupied);
  lcd.print("   Free:");
  lcd.print(freeSlots);
  lcd.print("  ");

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
//  Slot update (Instant Blynk Push execution)
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

  // Detect changes and dispatch instantly to Blynk dashboards
  bool statusChanged = false;
  for (int i = 0; i < 4; i++) {
    if (slot[i] != oldSlot[i]) {
      // Direct Blynk Write happens immediately without waiting for Google!
      Blynk.virtualWrite(V0 + i, slot[i] ? 1 : 0);

      // Push Google update request into the non-blocking background runner
      pendingGoogleSlot = i + 1;
      pendingGoogleEvent = slot[i] ? "ENTRY" : "EXIT";

      oldSlot[i] = slot[i];
      statusChanged = true;
    }
  }

  int occupied  = 0;
  for (int i = 0; i < 4; i++) if (slot[i]) occupied++;
  int freeSlots = 4 - occupied;

  // Periodic heartbeat sync to Blynk to ensure dashboards remain accurate
  if (!statusChanged) {
    for (int i = 0; i < 4; i++) {
      Blynk.virtualWrite(V0 + i, slot[i] ? 1 : 0);
    }
  }
  Blynk.virtualWrite(V4, freeSlots);
  updateLCD(occupied, freeSlots);
}

// ─────────────────────────────────────────────
//  Gate control
// ─────────────────────────────────────────────
void gateControl()
{
  long d = readDistance(GATE_TRIG, GATE_ECHO);

  int freeSlots = 0;
  for (int i = 0; i < 4; i++) if (!slot[i]) freeSlots++;

  if (!isGateOpen && d > 0 && d < 20 && freeSlots > 0) {
    gateServo.write(90);
    isGateOpen   = true;
    gateOpenTime = millis();
    Serial.printf("[Gate] OPENED — car at %lcm\n", d);
  }

  if (isGateOpen && (millis() - gateOpenTime >= GATE_OPEN_DURATION)) {
    gateServo.write(0);
    isGateOpen = false;
    Serial.println("[Gate] CLOSED");
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

  // Optimized operational intervals
  timer.setInterval(800L, updateSlots);        // Read slots slightly faster
  timer.setInterval(200L, gateControl);       // Responsive servo gate logic
  timer.setInterval(3000L, processGoogleQueue); // Google operations scheduled completely isolated
}

void loop()
{
  Blynk.run();
  timer.run();
}
