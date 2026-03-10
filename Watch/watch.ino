#include <Arduino.h> // REQUIRED FOR PLATFORMIO!

#define LILYGO_WATCH_2020_V2                 
#define LILYGO_WATCH_HAS_MOTOR      
#define MOTOR_PIN 4                 
#include <LilyGoWatch.h>
#include <WiFi.h>                           
#include <PubSubClient.h>                   
 
TTGOClass *watch;
TFT_eSPI *tft;
BMA *sensor;
bool irq = false;
 
char touch[128];
char buf[128];
bool rtcIrq = false;
int16_t start_status = 0; // 0 = STOPPED, 1 = RUNNING
 
// --- WIFI & MQTT CONFIGURATION ---
const char* ssid = "Something_happen";     
const char* password = "03081998";    
const char* mqtt_server = "172.20.10.4";
const int mqtt_port = 1883; 

// Static IP Configuration
IPAddress local_IP(172, 20, 10, 12);
IPAddress gateway(172, 20, 10, 4);
IPAddress subnet(255, 255, 255, 240);
IPAddress dns(172, 20, 10, 4);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastWiFiCheck = 0;
unsigned long lastMQTTCheck = 0;
 
// GPS pointers
TinyGPSPlus *gps = nullptr;
HardwareSerial *GNSS = NULL;

// --- MODERN UI COLORS ---
#define COLOR_BG     TFT_BLACK
#define COLOR_ACCENT TFT_CYAN       
#define COLOR_TEXT   TFT_WHITE
#define COLOR_DIM    TFT_DARKGREY
#define COLOR_BTN_ON 0xE8E4         
#define COLOR_BTN_OFF 0x07E0        

// --- NEW DISTINCT COLORS FOR STATS ---
#define COLOR_STEPS  TFT_SKYBLUE    
#define COLOR_DIST   TFT_YELLOW     
#define COLOR_CALS   TFT_ORANGE     

// --- SWIPE & PAGE VARIABLES ---
int current_page = 0; 
int last_page = -1;
int current_sub_page = 0; 
int last_sub_page = -1;

int16_t start_x = 0, start_y = 0;
bool is_touching = false;

// --- SCROLLING MENU VARIABLES ---
int menu_scroll_y = 0; 
const int MENU_ITEM_HEIGHT = 45;
const int MENU_ITEM_SPACING = 10;
const int NUM_MENU_ITEMS = 9;
const char* menu_items[NUM_MENU_ITEMS] = {
  "All-Time Totals", "Battery Status", "Set Step Goal", 
  "Altitude & Speed", "Set Clock Time", "Screen Timeout", 
  "Passcode Lock", "Screen Brightness", "Activity History"
};

// --- ACTIVITY MENU VARIABLES ---
const char* act_names[5] = {"Cycling", "Walking", "Running", "Snowboard", "Hiking"};
const float act_mets[5] = {9.5, 3.8, 9.8, 7.0, 6.0};
const uint16_t act_colors[5] = {TFT_CYAN, TFT_GREEN, TFT_RED, TFT_WHITE, TFT_ORANGE}; 
float current_met = 3.8; 
int current_activity_idx = 1; 
bool is_activity_selected = false;

// --- HIKE TRACKING VARIABLES ---
uint32_t current_hike_steps = 0;
double current_hike_distance = 0.0;
double current_hike_calories = 0.0;

uint32_t total_steps = 0;
double total_distance = 0.0;
double total_calories = 0.0;

unsigned long hike_start_time = 0;
unsigned long elapsed_hike_time_sec = 0;
double prev_lat = 0.0;
double prev_lng = 0.0;
bool has_start_fix = false;

// --- ACTIVITY HISTORY ---
struct ActivityRecord {
  String act_name;
  uint32_t steps;
  double distance;
  double calories;
};
ActivityRecord activity_history[5];
int history_count = 0;

// User Profile & Goals
float user_weight = 70.0; 
uint32_t step_goal = 10000;
bool goal_reached = false;

// Setup Time Variables
int edit_hour = 12;
int edit_minute = 0;
bool editing_hour = true; 

// --- PASSCODE & SCREEN STATE VARIABLES ---
bool is_screen_on = true;
unsigned long last_touch_time = 0;
unsigned long screen_timeout_ms = 15000; 
unsigned long last_tap_time = 0; 

bool is_locked = true;
int lock_state = 0; 
bool passcode_enabled = false; 
String current_passcode_input = "";
String correct_passcode = "1234"; 
int screen_brightness = 255; 

// =========================================================================
// ==================== WIFI & MQTT LOGIC ==================================
// =========================================================================

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet, dns);
  WiFi.begin(ssid, password);
  Serial.println("📡 Wi-Fi connection started in background...");
}

boolean reconnectMQTT() {
  Serial.print("Connecting to MQTT... ");
  if (client.connect("ESP32_WatchClient")) {
    Serial.println("✅ MQTT Connected!");
    return true;
  } else {
    Serial.print("❌ Failed, rc=");
    Serial.println(client.state()); 
    return false;
  }
}

// =========================================================================
// ==================== UI RENDERING FUNCTIONS =============================
// =========================================================================

void drawWiFiStatus(int x, int y) {
  uint16_t color = (WiFi.status() == WL_CONNECTED) ? TFT_GREEN : TFT_RED;
  tft->fillCircle(x, y, 4, color);
}

void drawToggleButton(bool isRunning) {
  if (isRunning) {
    tft->fillRoundRect(40, 150, 160, 50, 25, COLOR_BTN_ON);
    tft->setTextColor(TFT_WHITE);
    tft->setTextDatum(MC_DATUM); 
    tft->drawString("STOP HIKE", 120, 175, 4);
  } else {
    tft->fillRoundRect(40, 150, 160, 50, 25, COLOR_BTN_OFF);
    tft->setTextColor(TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("START HIKE", 120, 175, 4);
  }
}

void drawPageIndicators() {
  for(int i=0; i<3; i++) {
    tft->fillCircle(100 + (i*20), 225, 4, (i==current_page) ? COLOR_TEXT : COLOR_DIM);
  }
}

void drawStats(uint32_t steps, double dist, double cals) {
  tft->setTextColor(COLOR_STEPS, COLOR_BG);
  snprintf(buf, sizeof(buf), "Steps: %u", steps);
  tft->drawString(buf, 120, 90, 4);

  tft->setTextColor(COLOR_DIST, COLOR_BG);
  if (dist < 1000) snprintf(buf, sizeof(buf), "Dist: %.0f m", dist);
  else snprintf(buf, sizeof(buf), "Dist: %.2f km", dist / 1000.0);
  tft->drawString(buf, 120, 130, 4);

  tft->setTextColor(COLOR_CALS, COLOR_BG);
  snprintf(buf, sizeof(buf), "Cals: %.1f kcal", cals);
  tft->drawString(buf, 120, 170, 4);
}

// ---- LOCK SCREEN ----
void drawLockScreen(bool isStatic) {
  if (lock_state == 0) {
    if (isStatic) {
      tft->fillScreen(COLOR_BG);
      tft->setTextColor(COLOR_DIM);
      tft->setTextDatum(MC_DATUM);
      if (passcode_enabled) tft->drawString("Tap to Unlock", 120, 180, 2);
      else tft->drawString("Swipe or Tap to open", 120, 180, 2);
      
      if (passcode_enabled) {
        tft->fillRoundRect(112, 30, 16, 14, 2, COLOR_TEXT);
        tft->drawRoundRect(115, 22, 10, 10, 4, COLOR_TEXT);
      }
    }
    
    RTC_Date now = watch->rtc->getDateTime();
    tft->setTextColor(COLOR_TEXT, COLOR_BG); 
    snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
    tft->drawString(buf, 120, 105, 7); 
    
    drawWiFiStatus(220, 20);
    
  } else {
    if (isStatic) {
      tft->fillScreen(COLOR_BG);
      tft->setTextColor(COLOR_ACCENT);
      tft->setTextDatum(MC_DATUM);
      tft->drawString("ENTER PASSCODE", 120, 30, 2);

      int key_w = 60, key_h = 40, start_x = 25, start_y = 70;
      for(int i=0; i<3; i++) {
          for(int j=0; j<3; j++) {
              int num = i*3 + j + 1;
              int x = start_x + j*(key_w + 5), y = start_y + i*(key_h + 5);
              tft->fillRoundRect(x, y, key_w, key_h, 5, COLOR_DIM);
              tft->setTextColor(COLOR_TEXT);
              snprintf(buf, sizeof(buf), "%d", num);
              tft->drawString(buf, x + key_w/2, y + key_h/2, 4);
          }
      }
      tft->fillRoundRect(start_x + (key_w + 5), start_y + 3*(key_h + 5), key_w, key_h, 5, COLOR_DIM);
      tft->drawString("0", start_x + (key_w + 5) + key_w/2, start_y + 3*(key_h + 5) + key_h/2, 4);
      
      tft->fillRoundRect(start_x + 2*(key_w + 5), start_y + 3*(key_h + 5), key_w, key_h, 5, COLOR_BTN_ON);
      tft->drawString("C", start_x + 2*(key_w + 5) + key_w/2, start_y + 3*(key_h + 5) + key_h/2, 2);
    }

    tft->fillRect(60, 45, 120, 20, COLOR_BG); 
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    for(int i=0; i<current_passcode_input.length(); i++) {
       String digit = String(current_passcode_input.charAt(i));
       tft->drawString(digit, 90 + i*20, 55, 4); 
    }
  }
}

// ---- PAGE 0: MAIN DASHBOARD & ACTIVITY MENU ----
void drawPageMain(bool isStatic) {
  if (!is_activity_selected) {
    if (isStatic) {
      tft->setTextColor(COLOR_ACCENT);
      tft->drawString("SELECT ACTIVITY", 120, 20, 2);
      for (int i=0; i<5; i++) {
        int y_pos = 45 + i*34;
        tft->fillRoundRect(25, y_pos, 190, 30, 8, COLOR_DIM);
        tft->fillCircle(45, y_pos + 15, 6, act_colors[i]); 
        tft->setTextColor(COLOR_TEXT);
        tft->setTextDatum(ML_DATUM); 
        tft->drawString(act_names[i], 65, y_pos + 15, 2);
      }
      tft->setTextDatum(MC_DATUM); 
    }
    drawWiFiStatus(220, 20);
    
  } else {
    if (isStatic) {
      tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
      tft->setTextColor(COLOR_TEXT);
      tft->drawString("<", 25, 19, 4);
      
      tft->fillCircle(200, 19, 6, act_colors[current_activity_idx]); 
      
      tft->setTextColor(COLOR_DIM);
      tft->drawString("STEPS", 60, 45, 2);
      tft->drawString("DIST", 180, 45, 2);
      tft->drawString("CALS", 120, 95, 2);
      drawToggleButton(start_status == 1);
    }
    
    RTC_Date now = watch->rtc->getDateTime();
    tft->setTextColor(COLOR_TEXT, COLOR_BG); 
    snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
    tft->drawString(buf, 120, 15, 4); 
    
    tft->setTextColor(COLOR_STEPS, COLOR_BG);
    snprintf(buf, sizeof(buf), "%u", current_hike_steps);
    tft->drawString(buf, 60, 65, 4); 
    
    tft->setTextColor(COLOR_DIST, COLOR_BG);
    if (current_hike_distance < 1000) snprintf(buf, sizeof(buf), "%.0fm   ", current_hike_distance);
    else snprintf(buf, sizeof(buf), "%.2fkm   ", current_hike_distance / 1000.0);
    tft->drawString(buf, 180, 65, 4); 
    
    tft->setTextColor(COLOR_CALS, COLOR_BG);
    snprintf(buf, sizeof(buf), "%.1f", current_hike_calories);
    tft->drawString(buf, 120, 115, 4); 
    
    drawWiFiStatus(220, 20);
  }
}

// ---- PAGE 1: SCROLLABLE FEATURES MENU ----
void drawPageFeaturesMenu(bool isStatic) {
  if (isStatic) {
    tft->fillScreen(COLOR_BG); 
    for (int i = 0; i < NUM_MENU_ITEMS; i++) {
      int item_y = 40 + (i * (MENU_ITEM_HEIGHT + MENU_ITEM_SPACING)) + menu_scroll_y;
      if (item_y > -MENU_ITEM_HEIGHT && item_y < 240) {
        tft->fillRoundRect(20, item_y, 200, MENU_ITEM_HEIGHT, 8, COLOR_DIM);
        tft->setTextColor(COLOR_TEXT);
        tft->drawString(menu_items[i], 120, item_y + (MENU_ITEM_HEIGHT/2), 2);
      }
    }
    tft->fillRect(0, 0, 240, 35, COLOR_BG);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("FEATURES", 120, 15, 2);
    tft->fillRect(0, 215, 240, 25, COLOR_BG);
    drawPageIndicators(); 
  }
}

void drawSubPageTotals(bool isStatic) {
  if (isStatic) {
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("ALL-TIME TOTALS", 120, 30, 2);
  }
  drawStats(total_steps, total_distance, total_calories);
}

void drawSubPageBattery(bool isStatic) {
  if (isStatic) {
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("SYSTEM STATUS", 120, 30, 2);
    tft->drawRect(80, 60, 70, 30, COLOR_TEXT); 
    tft->fillRect(150, 68, 6, 14, COLOR_TEXT);  
  }
  int battPercent = watch->power->getBattPercentage();
  float battVoltage = watch->power->getBattVoltage() / 1000.0;

  int fillW = (battPercent * 66) / 100;
  tft->fillRect(82, 62, fillW, 26, (battPercent > 20) ? TFT_GREEN : TFT_RED);
  tft->fillRect(82 + fillW, 62, 66 - fillW, 26, COLOR_BG); 

  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  snprintf(buf, sizeof(buf), "%d %%   ", battPercent);
  tft->drawString(buf, 120, 110, 4);
  
  tft->setTextColor(COLOR_DIM, COLOR_BG);
  snprintf(buf, sizeof(buf), "Voltage: %.2f V   ", battVoltage);
  tft->drawString(buf, 120, 150, 2);
  
  String wifiStr = (WiFi.status() == WL_CONNECTED) ? "Wi-Fi: Connected   " : "Wi-Fi: Disconnected";
  tft->setTextColor((WiFi.status() == WL_CONNECTED) ? TFT_GREEN : TFT_RED, COLOR_BG);
  tft->drawString(wifiStr, 120, 180, 2);
}

void drawSubPageGoal(bool isStatic) {
  if (isStatic) {
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("SET STEP GOAL", 120, 30, 2);
    tft->fillRoundRect(30, 150, 50, 40, 5, COLOR_BTN_ON);
    tft->fillRoundRect(160, 150, 50, 40, 5, COLOR_BTN_OFF);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("-", 55, 170, 4);
    tft->setTextColor(TFT_BLACK);
    tft->drawString("+", 185, 170, 4);
  }
  tft->fillRect(40, 80, 160, 50, COLOR_BG); 
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  snprintf(buf, sizeof(buf), "%u", step_goal);
  tft->drawString(buf, 120, 105, 6); 
}

void drawSubPageAltSpeed(bool isStatic) {
  if (isStatic) {
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("ALTITUDE & SPEED", 120, 30, 2);
  }
  if (gps->location.isValid()) {
    tft->setTextColor(COLOR_DIST, COLOR_BG); 
    snprintf(buf, sizeof(buf), "Alt: %.1f m     ", gps->altitude.meters());
    tft->drawString(buf, 120, 100, 4);
    tft->setTextColor(COLOR_STEPS, COLOR_BG);
    snprintf(buf, sizeof(buf), "Spd: %.1f km/h  ", gps->speed.kmph());
    tft->drawString(buf, 120, 160, 4);
  } else {
    tft->setTextColor(COLOR_DIM, COLOR_BG);
    tft->drawString("Waiting for GPS...    ", 120, 130, 2);
  }
}

void drawSubPageSetTime(bool isStatic) {
  if (isStatic) {
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("SET CLOCK TIME", 120, 30, 2);
    tft->fillRoundRect(40, 65, 70, 30, 5, editing_hour ? COLOR_STEPS : COLOR_DIM);
    tft->setTextColor(editing_hour ? TFT_BLACK : COLOR_TEXT);
    tft->drawString("HOUR", 75, 80, 2);
    tft->fillRoundRect(130, 65, 70, 30, 5, !editing_hour ? COLOR_STEPS : COLOR_DIM);
    tft->setTextColor(!editing_hour ? TFT_BLACK : COLOR_TEXT);
    tft->drawString("MIN", 165, 80, 2);
    tft->fillRoundRect(30, 150, 50, 40, 5, COLOR_BTN_ON); 
    tft->fillRoundRect(160, 150, 50, 40, 5, COLOR_BTN_OFF); 
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("-", 55, 170, 4);
    tft->setTextColor(TFT_BLACK);
    tft->drawString("+", 185, 170, 4);
    tft->fillRoundRect(90, 195, 60, 30, 5, TFT_PURPLE);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("SAVE", 120, 210, 2);
  }
  tft->fillRect(40, 105, 160, 40, COLOR_BG); 
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  snprintf(buf, sizeof(buf), "%02d:%02d", edit_hour, edit_minute);
  tft->drawString(buf, 120, 125, 6); 
}

void drawSubPageScreenTimeout(bool isStatic) {
   if (isStatic) {
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("SCREEN TIMEOUT", 120, 30, 2);
    tft->fillRoundRect(30, 150, 50, 40, 5, COLOR_BTN_ON);
    tft->fillRoundRect(160, 150, 50, 40, 5, COLOR_BTN_OFF);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("-", 55, 170, 4);
    tft->setTextColor(TFT_BLACK);
    tft->drawString("+", 185, 170, 4);
  }
  tft->fillRect(40, 80, 160, 50, COLOR_BG); 
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  snprintf(buf, sizeof(buf), "%d s", (int)(screen_timeout_ms / 1000));
  tft->drawString(buf, 120, 105, 6); 
}

void drawSubPagePasscode(bool isStatic) {
   if (isStatic) {
    tft->fillScreen(COLOR_BG);
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("PASSCODE SETUP", 120, 20, 2);
    
    tft->fillRoundRect(20, 45, 200, 35, 5, passcode_enabled ? TFT_GREEN : COLOR_DIM);
    tft->setTextColor(passcode_enabled ? TFT_BLACK : COLOR_TEXT);
    tft->drawString(passcode_enabled ? "Status: ON" : "Status: OFF", 120, 62, 2);

    int key_w = 45, key_h = 30, start_sx = 45, start_sy = 95;
    for(int i=0; i<3; i++) {
        for(int j=0; j<3; j++) {
            int num = i*3 + j + 1;
            int x = start_sx + j*(key_w + 5), y = start_sy + i*(key_h + 5);
            tft->fillRoundRect(x, y, key_w, key_h, 5, COLOR_DIM);
            tft->setTextColor(COLOR_TEXT);
            snprintf(buf, sizeof(buf), "%d", num);
            tft->drawString(buf, x + key_w/2, y + key_h/2, 2);
        }
    }
    tft->fillRoundRect(start_sx + (key_w + 5), start_sy + 3*(key_h + 5), key_w, key_h, 5, COLOR_DIM);
    tft->drawString("0", start_sx + (key_w + 5) + key_w/2, start_sy + 3*(key_h + 5) + key_h/2, 2);
    
    tft->fillRoundRect(start_sx + 2*(key_w + 5), start_sy + 3*(key_h + 5), key_w, key_h, 5, COLOR_BTN_ON);
    tft->drawString("C", start_sx + 2*(key_w + 5) + key_w/2, start_sy + 3*(key_h + 5) + key_h/2, 2);
  }
  
  tft->fillRect(60, 220, 120, 20, COLOR_BG); 
  tft->setTextColor(COLOR_TEXT);
  tft->setTextDatum(MC_DATUM);
  for(int i=0; i<current_passcode_input.length(); i++) {
     String digit = String(current_passcode_input.charAt(i));
     tft->drawString(digit, 90 + i*20, 230, 4); 
  }
}

void drawSubPageBrightness(bool isStatic) {
   if (isStatic) {
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("<", 25, 19, 4);
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("SCREEN BRIGHTNESS", 120, 30, 2);
    tft->fillRoundRect(30, 150, 50, 40, 5, COLOR_BTN_ON);
    tft->fillRoundRect(160, 150, 50, 40, 5, COLOR_BTN_OFF);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("-", 55, 170, 4);
    tft->setTextColor(TFT_BLACK);
    tft->drawString("+", 185, 170, 4);
  }
  tft->fillRect(40, 80, 160, 50, COLOR_BG); 
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  int bright_percent = (screen_brightness * 100) / 255;
  snprintf(buf, sizeof(buf), "%d %%", bright_percent);
  tft->drawString(buf, 120, 105, 6); 
}

void drawSubPageHistory(bool isStatic) {
  if (isStatic) {
    tft->fillScreen(COLOR_BG);
    tft->fillRoundRect(5, 5, 40, 28, 5, COLOR_DIM);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("<", 25, 19, 4);

    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("ACTIVITY HISTORY", 120, 20, 2);

    if (history_count == 0) {
      tft->setTextColor(COLOR_DIM);
      tft->drawString("No history yet.", 120, 120, 2);
    } else {
      for (int i = 0; i < history_count; i++) {
        int y = 50 + (i * 35);
        tft->setTextDatum(ML_DATUM); 
        
        tft->setTextColor(COLOR_TEXT);
        snprintf(buf, sizeof(buf), "%d. %s", i + 1, activity_history[i].act_name.c_str());
        tft->drawString(buf, 15, y, 2);

        tft->setTextColor(COLOR_DIM);
        snprintf(buf, sizeof(buf), "%u steps | %.1fkm | %.1fkcal", 
                 activity_history[i].steps, 
                 (activity_history[i].distance / 1000.0), 
                 activity_history[i].calories);
        tft->drawString(buf, 25, y + 16, 1);
      }
      tft->setTextDatum(MC_DATUM); 
    }
  }
}

// ---- PAGE 2: PROFILE ----
void drawPageProfile(bool isStatic) {
  if (isStatic) {
    tft->setTextColor(COLOR_ACCENT);
    tft->drawString("USER PROFILE", 120, 30, 2);
    tft->setTextColor(COLOR_DIM);
    tft->drawString("Name:", 120, 70, 2);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("SmartHiker", 120, 95, 4);
    tft->setTextColor(COLOR_DIM);
    tft->drawString("Weight (kg):", 120, 135, 2);
    tft->fillRoundRect(30, 155, 40, 35, 5, COLOR_BTN_ON);
    tft->fillRoundRect(170, 155, 40, 35, 5, COLOR_BTN_OFF);
    tft->setTextColor(COLOR_TEXT);
    tft->drawString("-", 50, 172, 4);
    tft->setTextColor(TFT_BLACK);
    tft->drawString("+", 190, 172, 4);
  }
  
  tft->setTextColor(COLOR_TEXT, COLOR_BG);
  snprintf(buf, sizeof(buf), "%.1f", user_weight);
  tft->drawString(buf, 120, 172, 4); 
}

// =========================================================================
// ==================== MAIN SETUP & LOOP ==================================
// =========================================================================

void setup() {
  Serial.begin(115200);
  watch = TTGOClass::getWatch();
  watch->begin();
  
  tft = watch->tft;

  watch->openBL();
  watch->bl->adjust(screen_brightness);
  tft->fillScreen(TFT_BLACK);
  tft->setTextColor(TFT_CYAN);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("BOOTING UP...", 120, 120, 4);

  watch->power->setPowerOutPut(AXP202_LDO3, AXP202_ON);
  
  watch->motor_begin();
  watch->trunOnGPS();
  watch->gps_begin();
 
  sensor = watch->bma;
  GNSS = watch->hwSerial;
  gps = watch->gps;
 
  Acfg cfg;
  cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
  cfg.range = BMA4_ACCEL_RANGE_2G;
  cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
  cfg.perf_mode = BMA4_CONTINUOUS_MODE;
 
  sensor->accelConfig(cfg);
  sensor->enableAccel();
 
  watch->rtc->disableAlarm();
  watch->rtc->setDateTime(2025, 3, 25, 12, 00, 00); 
 
  pinMode(BMA423_INT1, INPUT);
  attachInterrupt(BMA423_INT1, [] { irq = 1; }, RISING); 
  sensor->enableFeature(BMA423_STEP_CNTR, true);
  sensor->resetStepCounter();
  sensor->enableStepCountInterrupt();
 
  connectWiFi(); 
  client.setServer(mqtt_server, 1883);

  last_touch_time = millis(); 
}
 
void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - lastWiFiCheck > 10000) { 
      Serial.println("⚠️ WiFi lost! Attempting reconnect...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      lastWiFiCheck = currentMillis;
    }
  } else {
    if (!client.connected()) {
      if (currentMillis - lastMQTTCheck > 10000) { 
        reconnectMQTT();
        lastMQTTCheck = currentMillis;
      }
    } else {
      client.loop(); 
    }
  }


  int16_t x, y;
  static int16_t last_x = 0, last_y = 0;
  bool touched = watch->getTouch(x, y);

  if (touched) {
      if (!is_screen_on) {
          watch->openBL();
          watch->bl->adjust(screen_brightness); 
          is_screen_on = true;
          is_locked = true; 
          lock_state = 0;   
          last_page = -1; 
          last_sub_page = -1;
          is_touching = true; 
          last_x = x; last_y = y;
          last_touch_time = currentMillis;
          return; 
      }
      last_touch_time = currentMillis;
  } else if (is_screen_on && currentMillis - last_touch_time > screen_timeout_ms) {
      watch->closeBL();
      is_screen_on = false;
      is_locked = true; 
      lock_state = 0; 
  }

  if (!is_screen_on) {
      goto data_tracking_section;
  }

  if (touched) {
    if (!is_touching) {
      is_touching = true;
      start_x = x;
      start_y = y;
    }
    last_x = x;
    last_y = y;
  } else {
    if (is_touching) {
      is_touching = false;
      int16_t delta_x = last_x - start_x;
      int16_t delta_y = last_y - start_y;
      
      if (abs(delta_x) < 20 && abs(delta_y) < 20) {
          if (currentMillis - last_tap_time < 300) {
              watch->closeBL();
              is_screen_on = false;
              is_locked = true; 
              lock_state = 0;
              last_tap_time = 0; 
              goto data_tracking_section;
          }
          last_tap_time = currentMillis;
      }

      if (is_locked) {
          if (lock_state == 0) { 
              if (abs(delta_x) > 50 || (abs(delta_x) < 20 && abs(delta_y) < 20)) { 
                  if (passcode_enabled) {
                      lock_state = 1; 
                      current_passcode_input = "";
                      last_page = -1; 
                  } else {
                      is_locked = false; 
                      last_page = -1; 
                  }
              }
          } 
          else if (lock_state == 1) { 
              if (abs(delta_x) < 20 && abs(delta_y) < 20) {
                int key_w = 60, key_h = 40, start_sx = 25, start_sy = 70;
                
                for(int i=0; i<3; i++) {
                    for(int j=0; j<3; j++) {
                        int bx = start_sx + j*(key_w + 5), by = start_sy + i*(key_h + 5);
                        if(start_x > bx && start_x < bx+key_w && start_y > by && start_y < by+key_h) {
                            if(current_passcode_input.length() < 4) current_passcode_input += String(i*3 + j + 1);
                        }
                    }
                }
                int bx_0 = start_sx + (key_w + 5), by_0 = start_sy + 3*(key_h + 5);
                if(start_x > bx_0 && start_x < bx_0+key_w && start_y > by_0 && start_y < by_0+key_h) {
                    if(current_passcode_input.length() < 4) current_passcode_input += "0";
                }
                int bx_c = start_sx + 2*(key_w + 5), by_c = start_sy + 3*(key_h + 5);
                if(start_x > bx_c && start_x < bx_c+key_w && start_y > by_c && start_y < by_c+key_h) {
                    current_passcode_input = "";
                }

                if (current_passcode_input.length() == 4) {
                    if (current_passcode_input == correct_passcode) {
                        is_locked = false;
                        lock_state = 0; 
                        last_page = -1; 
                        if (watch->motor) watch->motor->onec(100);
                    } else {
                        current_passcode_input = ""; 
                        if (watch->motor) { watch->motor->onec(50); delay(100); watch->motor->onec(50); } 
                    }
                }
                last_page = -1; 
              }
          }
      } else {
          if (current_page == 1 && current_sub_page == 0 && abs(delta_y) > 30) {
            menu_scroll_y += delta_y;
            if (menu_scroll_y > 0) menu_scroll_y = 0;
            int max_scroll = -((NUM_MENU_ITEMS * (MENU_ITEM_HEIGHT + MENU_ITEM_SPACING)) - 140); 
            if (menu_scroll_y < max_scroll) menu_scroll_y = max_scroll;
            last_page = -1; 
          }
          
          else if (current_sub_page == 0 && abs(delta_x) > 50) {
            if (delta_x < -50) current_page = (current_page + 1) % 3; 
            else if (delta_x > 50) current_page = (current_page + 2) % 3; 
          }

          else if (abs(delta_x) < 20 && abs(delta_y) < 20) {
            
            if (current_page == 0) {
              if (!is_activity_selected) {
                for (int i=0; i<5; i++) {
                  int y_pos = 45 + i*34;
                  if (start_y > y_pos && start_y < y_pos + 30) {
                    is_activity_selected = true;
                    current_activity_idx = i;
                    current_met = act_mets[i]; 
                    last_page = -1; 
                    break;
                  }
                }
              } else {
                if (start_status == 0 && start_y < 40 && start_x < 50) {
                   is_activity_selected = false;
                   last_page = -1;
                }
                else if (start_y > 140 && start_y < 210) {
                  if (watch->motor) watch->motor->onec(500); 

                  if (start_status == 0) {
                    start_status = 1; 
                    sensor->resetStepCounter();
                    current_hike_steps = 0;
                    current_hike_distance = 0.0;
                    current_hike_calories = 0.0;
                    has_start_fix = false;
                    hike_start_time = millis();
                  } else {
                    start_status = 0; 
                    total_steps += current_hike_steps;
                    total_distance += current_hike_distance;
                    total_calories += current_hike_calories;

                    for (int i = 4; i > 0; i--) {
                        activity_history[i] = activity_history[i - 1];
                    }
                    
                    activity_history[0].act_name = act_names[current_activity_idx];
                    activity_history[0].steps = current_hike_steps;
                    activity_history[0].distance = current_hike_distance;
                    activity_history[0].calories = current_hike_calories;
                    
                    if (history_count < 5) history_count++;
                  }
                  last_page = -1; 
                }
              }
            }
            
            else if (current_page == 1) {
              if (current_sub_page == 0) {
                 if (start_y > 35 && start_y < 215) {
                     for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                        int item_y = 40 + (i * (MENU_ITEM_HEIGHT + MENU_ITEM_SPACING)) + menu_scroll_y;
                        if (start_y > item_y && start_y < item_y + MENU_ITEM_HEIGHT && start_x > 20 && start_x < 220) {
                           current_sub_page = i + 1; 
                           last_sub_page = -1;
                           
                           if (current_sub_page == 5) {
                              RTC_Date now = watch->rtc->getDateTime();
                              edit_hour = now.hour; edit_minute = now.minute;
                           } else if (current_sub_page == 7) {
                              current_passcode_input = ""; 
                           }
                           break;
                        }
                     }
                 }
              } else {
                if (start_y < 40 && start_x < 50) {
                   current_sub_page = 0; 
                   last_sub_page = -1;
                   current_passcode_input = ""; 
                }
                else if (current_sub_page == 3 && start_y > 140 && start_y < 200) {
                  if (start_x < 90) {
                    if (step_goal > 500) step_goal -= 500; 
                    goal_reached = false;
                    if (watch->motor) watch->motor->onec(50);
                  } else if (start_x > 150) {
                    step_goal += 500; 
                    goal_reached = false;
                    if (watch->motor) watch->motor->onec(50);
                  }
                  last_sub_page = -1;
                }
                else if (current_sub_page == 5) {
                   if (start_y > 65 && start_y < 95) {
                     editing_hour = (start_x < 110);
                     last_sub_page = -1; 
                   }
                   else if (start_y > 150 && start_y < 190) {
                     if (start_x < 90) { 
                        if (editing_hour) { edit_hour--; if(edit_hour < 0) edit_hour = 23; }
                        else { edit_minute--; if(edit_minute < 0) edit_minute = 59; }
                     } else if (start_x > 150) { 
                        if (editing_hour) { edit_hour++; if(edit_hour > 23) edit_hour = 0; }
                        else { edit_minute++; if(edit_minute > 59) edit_minute = 0; }
                     }
                     if (watch->motor) watch->motor->onec(50);
                     last_sub_page = -1;
                   }
                   else if (start_y > 195 && start_x > 90 && start_x < 150) {
                     RTC_Date now = watch->rtc->getDateTime();
                     watch->rtc->setDateTime(now.year, now.month, now.day, edit_hour, edit_minute, 0); 
                     current_sub_page = 0; 
                     last_sub_page = -1;
                     if (watch->motor) watch->motor->onec(200); 
                   }
                }
                else if (current_sub_page == 6 && start_y > 140 && start_y < 200) {
                  if (start_x < 90) {
                    if (screen_timeout_ms > 5000) screen_timeout_ms -= 5000; 
                    if (watch->motor) watch->motor->onec(50); 
                  } else if (start_x > 150) {
                    if (screen_timeout_ms < 60000) screen_timeout_ms += 5000; 
                    if (watch->motor) watch->motor->onec(50); 
                  }
                  last_sub_page = -1;
                }
                else if (current_sub_page == 7) {
                   if (start_y > 45 && start_y < 80) { 
                       passcode_enabled = !passcode_enabled;
                       if (!passcode_enabled) current_passcode_input = "";
                       if (watch->motor) watch->motor->onec(50);
                       last_sub_page = -1;
                   }
                   else if (start_y > 90) { 
                        int key_w = 45, key_h = 30, start_sx = 45, start_sy = 95;
                        for(int i=0; i<3; i++) {
                            for(int j=0; j<3; j++) {
                                int bx = start_sx + j*(key_w + 5), by = start_sy + i*(key_h + 5);
                                if(start_x > bx && start_x < bx+key_w && start_y > by && start_y < by+key_h) {
                                    if(current_passcode_input.length() < 4) current_passcode_input += String(i*3 + j + 1);
                                }
                            }
                        }
                        int bx_0 = start_sx + (key_w + 5), by_0 = start_sy + 3*(key_h + 5);
                        if(start_x > bx_0 && start_x < bx_0+key_w && start_y > by_0 && start_y < by_0+key_h) {
                            if(current_passcode_input.length() < 4) current_passcode_input += "0";
                        }
                        int bx_c = start_sx + 2*(key_w + 5), by_c = start_sy + 3*(key_h + 5);
                        if(start_x > bx_c && start_x < bx_c+key_w && start_y > by_c && start_y < by_c+key_h) {
                            current_passcode_input = "";
                        }

                        if (current_passcode_input.length() == 4) {
                            correct_passcode = current_passcode_input; 
                            current_passcode_input = "";
                            passcode_enabled = true; 
                            if (watch->motor) watch->motor->onec(200); 
                        }
                        last_sub_page = -1;
                   }
                }
                else if (current_sub_page == 8 && start_y > 140 && start_y < 200) {
                  if (start_x < 90) {
                    if (screen_brightness > 25) screen_brightness -= 25; 
                    watch->bl->adjust(screen_brightness); 
                    if (watch->motor) watch->motor->onec(50); 
                  } else if (start_x > 150) {
                    if (screen_brightness < 255) screen_brightness += 25; 
                    if (screen_brightness > 255) screen_brightness = 255; 
                    watch->bl->adjust(screen_brightness); 
                    if (watch->motor) watch->motor->onec(50); 
                  }
                  last_sub_page = -1;
                }
              }
            }

            else if (current_page == 2 && start_y > 130 && start_y < 190) {
              if (start_x < 100) {
                user_weight -= 1.0; 
                if (user_weight < 30.0) user_weight = 30.0;
                if (watch->motor) watch->motor->onec(50); 
              } else if (start_x > 140) {
                user_weight += 1.0; 
                if (user_weight > 200.0) user_weight = 200.0;
                if (watch->motor) watch->motor->onec(50); 
              }
              last_page = -1;
            }
          }
      }
    }
  }

data_tracking_section:
  while (GNSS->available()) { gps->encode(GNSS->read()); }
  
  if (start_status == 1) {
    sensor->enableFeature(BMA423_STEP_CNTR, true);
    current_hike_steps = sensor->getCounter();
    
    elapsed_hike_time_sec = (millis() - hike_start_time) / 1000;
    current_hike_calories = (elapsed_hike_time_sec * current_met * 3.5 * user_weight) / 12000.0;

    if (gps->location.isValid() && gps->location.isUpdated()) {
      if (!has_start_fix) {
        prev_lat = gps->location.lat();
        prev_lng = gps->location.lng();
        has_start_fix = true;
      } else {
        double dist = TinyGPSPlus::distanceBetween(prev_lat, prev_lng, gps->location.lat(), gps->location.lng());
        if (dist > 2.0) { 
          current_hike_distance += dist;
          prev_lat = gps->location.lat();
          prev_lng = gps->location.lng();
        }
      }
    }
  } else {
    sensor->enableFeature(BMA423_STEP_CNTR, false);
  }

  uint32_t effective_total_steps = total_steps;
  if (start_status == 1) {
    effective_total_steps += current_hike_steps; 
  }

  if (effective_total_steps >= step_goal && !goal_reached && step_goal > 0) {
    goal_reached = true;
    if (watch->motor) watch->motor->onec(1000); 
    
    if (is_screen_on && !is_locked) {
        tft->fillScreen(COLOR_BG);
        tft->setTextColor(TFT_YELLOW);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("CONGRATULATIONS!", 120, 100, 4);
        tft->setTextColor(TFT_WHITE);
        tft->drawString("You reached the goal, bro!", 120, 140, 2);
        delay(4000); 
        last_page = -1;
        last_sub_page = -1;
    }
  }

  if (is_screen_on) {
      if (is_locked) {
          if (last_page != 99) { 
              drawLockScreen(true);
              last_page = 99;
          } else {
              drawLockScreen(false);
          }
      } else {
          bool isPageChanged = (current_page != last_page) || (current_sub_page != last_sub_page);
          
          if (isPageChanged) {
            if (current_page != 1 || current_sub_page != 0) tft->fillScreen(COLOR_BG);
            if (current_sub_page == 0) drawPageIndicators();
            
            tft->setTextDatum(MC_DATUM);
            last_page = current_page;
            last_sub_page = current_sub_page;
          }

          switch(current_page) {
            case 0: 
              drawPageMain(isPageChanged); 
              break;
            case 1: 
              if (current_sub_page == 0) drawPageFeaturesMenu(isPageChanged); 
              else if (current_sub_page == 1) drawSubPageTotals(isPageChanged);
              else if (current_sub_page == 2) drawSubPageBattery(isPageChanged);
              else if (current_sub_page == 3) drawSubPageGoal(isPageChanged);
              else if (current_sub_page == 4) drawSubPageAltSpeed(isPageChanged);
              else if (current_sub_page == 5) drawSubPageSetTime(isPageChanged);
              else if (current_sub_page == 6) drawSubPageScreenTimeout(isPageChanged);
              else if (current_sub_page == 7) drawSubPagePasscode(isPageChanged);
              else if (current_sub_page == 8) drawSubPageBrightness(isPageChanged);
              else if (current_sub_page == 9) drawSubPageHistory(isPageChanged);
              break;
            case 2: 
              drawPageProfile(isPageChanged); 
              break;
          }
      }
  }
  
  if (start_status == 1 && irq) {
    irq = 0;
    bool rlst;
    do { rlst = sensor->readInterrupt(); } while (!rlst);

    if (sensor->isStepCounter()) {
      if (client.connected()) {
        
        String actJSON = "{\"activity\": \"" + String(act_names[current_activity_idx]) + "\"}";
        String stepsJSON = "{\"steps\": " + String(current_hike_steps) + "}";
        String distJSON = "{\"distance\": " + String(current_hike_distance, 1) + "}";
        String calsJSON = "{\"calories\": " + String(current_hike_calories, 1) + "}";
        
        String totalStepsJSON = "{\"total_steps\": " + String(effective_total_steps) + "}";
        String totalDistJSON = "{\"total_distance\": " + String((total_distance + current_hike_distance), 1) + "}";
        String totalCalsJSON = "{\"total_calories\": " + String((total_calories + current_hike_calories), 1) + "}";
        
        String gpsJSON = "{\"lat\": " + String(gps->location.lat(), 6) + ", \"lng\": " + String(gps->location.lng(), 6) + "}";

        client.publish("sensor/watch/activity", actJSON.c_str());
        client.publish("sensor/watch/steps", stepsJSON.c_str());
        client.publish("sensor/watch/distance", distJSON.c_str());
        client.publish("sensor/watch/calories", calsJSON.c_str());
        
        client.publish("sensor/watch/total_steps", totalStepsJSON.c_str());
        client.publish("sensor/watch/total_distance", totalDistJSON.c_str());
        client.publish("sensor/watch/total_calories", totalCalsJSON.c_str());
        
        client.publish("sensor/watch/gps", gpsJSON.c_str());
      }
    }
  }
  
  delay(50); 
}
