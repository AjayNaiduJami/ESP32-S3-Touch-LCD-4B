#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "pin_config.h"
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "lv_conf.h"
#include "SensorPCF85063.hpp"
#include "XPowersLib.h"
#include "SensorQMI8658.hpp"
#include <Preferences.h>

/* ================= CONFIG ================= */

#define SWITCH_COUNT 9
#define GRID_COLS 3

#define LCD_BL_PIN 2
#define BL_PWM_CH  0

#define SLEEP_TIMEOUT_MS 30000
#define GT911_ADDR 0x14 

#define MOTION_THRESHOLD 0.20 
#define MAX_NOTIFICATIONS 15
#define MAX_SAVED_NETWORKS 5
#define WIFI_RECONNECT_INTERVAL 60000
#define BACKLIGHT_DIM_LEVEL 10

const char* TOPIC_NOTIFY = "ha/panel/notify";

/* ================= STRUCTS & ENUMS ================= */

enum WifiState { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_SCANNING };

struct SavedWifi {
    char ssid[33];
    char pass[65];
    bool valid;
};

struct HaSwitch { const char* name; const char* topic_cmd; const char* topic_state; bool state; lv_obj_t* btn; lv_obj_t* label; };

/* ================= GLOBALS ================= */

uint32_t last_wifi_check = 0;
uint32_t screenWidth, screenHeight, bufSize;
lv_display_t *disp;
lv_color_t *disp_draw_buf;

lv_obj_t *status_wifi_icon = NULL;
lv_obj_t *status_mqtt_icon = NULL;
lv_obj_t *status_batt_icon = NULL;

// Screens
lv_obj_t *screen_home;
lv_obj_t *screen_notifications;
lv_obj_t *screen_power;
lv_obj_t *sleep_overlay;
lv_obj_t *screen_settings_menu; 
lv_obj_t *screen_about;
lv_obj_t *screen_wifi;
lv_obj_t *screen_ha;
lv_obj_t *screen_time_date;

// Time Screen Handles (Numpad Version)
lv_obj_t *kb_time;
lv_obj_t *sw_ntp_auto;
lv_obj_t *cont_manual_time;
lv_obj_t *btn_ampm, *lbl_ampm;
lv_obj_t *ta_day, *ta_month, *ta_year, *ta_hour, *ta_min;
bool time_is_pm = false;
bool ntp_auto_update = true;

// --- Weather & Location Globals ---
float geo_lat = 0.0;
float geo_lon = 0.0;
String city_name = "Loading...";
float current_temp = 0.0;
int weather_code = 0;
int is_day = 1;
bool initial_weather_fetched = false;
bool trigger_weather_update = false;
lv_obj_t *celestial_body = NULL;
lv_obj_t *celestial_glow = NULL;
lv_anim_t anim_breathe;
lv_style_t style_weather_bg;

// UI Handles
lv_obj_t *clock_label;
lv_obj_t *power_info_label;
lv_obj_t *notification_list; 
lv_obj_t *no_notification_label;
lv_obj_t *btn_clear_all; 
lv_obj_t *sleep_time_lbl;
lv_obj_t *sleep_date_lbl;
lv_obj_t *sleep_status_lbl;
lv_obj_t *sleep_weather_lbl;
lv_obj_t *sleep_notify_badge;
lv_obj_t *lbl_about_info = NULL;

Preferences prefs;
char deviceName[32] = "ESP32-S3-Panel";
lv_obj_t *ta_device_name;
lv_obj_t *kb; 
lv_obj_t *lbl_wifi_status;
lv_obj_t *lbl_ha_status = NULL;
lv_obj_t *loader_overlay = NULL;

// WiFi Handles
char wifi_ssid[32] = "";        
char wifi_pass[64] = ""; 
bool wifi_enabled = false; 

WifiState current_wifi_state = WIFI_IDLE;
SavedWifi saved_networks[MAX_SAVED_NETWORKS];
uint32_t wifi_connect_start = 0;
uint32_t last_scan_check = 0;
int scan_stage = 0;
lv_obj_t *saved_list_ui = NULL;
lv_obj_t *scan_list_ui = NULL;

lv_obj_t *cont_wifi_inputs; 
lv_obj_t *sw_wifi_enable;    
lv_obj_t *ta_ssid;
lv_obj_t *ta_pass;
lv_obj_t *kb_wifi;                
lv_obj_t *wifi_list;

// MQTT / HA Handles
char mqtt_host[64] = "192.168.10.10";
int  mqtt_port = 1883;
char mqtt_user[32] = ""; 
char mqtt_pass[32] = "";
bool mqtt_enabled = false; 
int  mqtt_retry_count = 0; 
uint32_t last_mqtt_retry = 0;

lv_obj_t *cont_ha_inputs; 
lv_obj_t *ta_mqtt_host;
lv_obj_t *ta_mqtt_port;
lv_obj_t *ta_mqtt_user;
lv_obj_t *ta_mqtt_pass;
lv_obj_t *sw_mqtt_enable;
lv_obj_t *kb_ha;

// Popup Handles
lv_obj_t *msg_popup = NULL; 
int selected_notification_index = -1;

// Notification Data
char notification_history[MAX_NOTIFICATIONS][64]; 
bool notification_ui_dirty = false; 

uint32_t lastMillis = 0;
uint32_t last_touch_ms = 0;
uint32_t last_power_update = 0;
float last_acc_x = 0, last_acc_y = 0, last_acc_z = 0;

SensorPCF85063 rtc;
XPowersPMU power;
SensorQMI8658 qmi; 
IMUdata acc;        
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

HaSwitch switches[SWITCH_COUNT] = {
  { "LIGHT", "ha/panel/light/set", "ha/panel/light/state", false, NULL, NULL },
  { "FAN",   "ha/panel/fan/set",   "ha/panel/fan/state",   false, NULL, NULL },
  { "AC",    "ha/panel/ac/set",    "ha/panel/ac/state",    false, NULL, NULL },
  { "PLUG",  "ha/panel/plug/set",  "ha/panel/plug/state",  false, NULL, NULL },
  { "TV",    "ha/panel/tv/set",    "ha/panel/tv/state",    false, NULL, NULL },
  { "BED",   "ha/panel/bed/set",   "ha/panel/bed/state",   false, NULL, NULL },
  { "LOCK",  "ha/panel/lock/set",  "ha/panel/lock/state",  false, NULL, NULL },
  { "HEAT",  "ha/panel/heat/set",  "ha/panel/heat/state",  false, NULL, NULL },
  { "ALL",   "ha/panel/all/set",   "ha/panel/all/state",   false, NULL, NULL }
};

const char* icons[SWITCH_COUNT] = {
  LV_SYMBOL_POWER, LV_SYMBOL_REFRESH, LV_SYMBOL_WARNING,
  LV_SYMBOL_CHARGE, LV_SYMBOL_VIDEO, LV_SYMBOL_HOME,
  LV_SYMBOL_WARNING, LV_SYMBOL_WARNING, LV_SYMBOL_OK
};

/* ================= FORWARD DECLARATIONS ================= */
void create_switch_grid(lv_obj_t *parent);
void create_page_dots(lv_obj_t *parent, int active_idx);
void create_notifications_page(lv_obj_t *parent);
void create_power_screen(lv_obj_t *parent);
void create_settings_menu_screen(lv_obj_t *parent);
void create_wifi_screen(lv_obj_t *parent);
void create_ha_screen(lv_obj_t *parent);
void create_about_screen(lv_obj_t *parent);
void create_time_date_screen(lv_obj_t *parent);
void create_sleep_screen();
void show_notification_popup(const char* text, int index);
void mqtt_callback(char* topic, byte* payload, unsigned int len);
void update_about_text();
void save_device_name(const char* new_name);
void back_event_cb(lv_event_t *e);
void refresh_notification_list();
void add_notification(String msg); 
void load_saved_networks();
void save_current_network_to_list();
void remove_saved_network(const char* ssid_to_remove);
void refresh_saved_wifi_list_ui();
void saved_wifi_click_cb(lv_event_t * e);
void btn_scan_wifi_cb(lv_event_t * e);
void wifi_list_btn_cb(lv_event_t * e);


/* ================= HARDWARE DRIVERS ================= */
bool gt911_read_touch(uint16_t &x, uint16_t &y) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x4E);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(GT911_ADDR, 1);
  if (!Wire.available() || (Wire.read() & 0x80) == 0) return false;
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x50);
  Wire.endTransmission(false);
  Wire.requestFrom(GT911_ADDR, 4);
  if (Wire.available() < 4) return false;
  uint16_t xl = Wire.read(), xh = Wire.read(), yl = Wire.read(), yh = Wire.read();
  x = (xh << 8) | xl; y = (yh << 8) | yl;
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
  Wire.endTransmission();
  return true;
}

void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  static uint16_t lx = 0, ly = 0;
  uint16_t x, y;
  if (gt911_read_touch(x, y)) {
    if (x >= screenWidth) x = screenWidth - 1;
    if (y >= screenHeight) y = screenHeight - 1;
    lx = x; ly = y;
    data->state = LV_INDEV_STATE_PRESSED;    
    last_touch_ms = millis();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  data->point.x = lx; data->point.y = ly;
}

Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(7, 0, 2, 1, &Wire, 0x20);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  17, 3, 46, 9, 10, 11, 12, 13, 14, 21, 8, 18, 45, 38, 39, 40, 41, 42, 2, 1,
  1, 10, 8, 50, 1, 10, 8, 20
);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480, rgbpanel, 0, true, expander, GFX_NOT_DEFINED,
  st7701_type1_init_operations, sizeof(st7701_type1_init_operations)
);

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
    lv_disp_flush_ready(disp);
}

void rounder_event_cb(lv_event_t *e) {
  lv_area_t *a = (lv_area_t*)lv_event_get_param(e);
  a->x1 = (a->x1 >> 1) << 1; a->y1 = (a->y1 >> 1) << 1;
  a->x2 = ((a->x2 >> 1) << 1) + 1; a->y2 = ((a->y2 >> 1) << 1) + 1;
}

void adcOn() {
  power.enableTemperatureMeasure();
  power.enableBattDetection();
  power.enableVbusVoltageMeasure();
  power.enableBattVoltageMeasure();
  power.enableSystemVoltageMeasure();
}

/* ================= HELPERS ================= */

int get_notification_count() {
  int count = 0;
  for(int i = 0; i < MAX_NOTIFICATIONS; i++) {
    if (notification_history[i][0] != '\0') count++;
  }
  return count;
}

void delete_notification(int index) {
  if (index < 0 || index >= MAX_NOTIFICATIONS) return;
  for (int i = index; i < MAX_NOTIFICATIONS - 1; i++) {
    strncpy(notification_history[i], notification_history[i+1], 64);
  }
  notification_history[MAX_NOTIFICATIONS - 1][0] = '\0';
}

void clear_all_notifications() {
  for(int i = 0; i < MAX_NOTIFICATIONS; i++) {
    notification_history[i][0] = '\0';
  }
}

void add_notification(String msg) {
    for (int k = MAX_NOTIFICATIONS - 1; k > 0; k--) {
        strncpy(notification_history[k], notification_history[k - 1], 64);
    }
    strncpy(notification_history[0], msg.c_str(), 63);
    notification_history[0][63] = '\0'; 

    notification_ui_dirty = true;

    if (!lv_obj_has_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN)) {
        ledcWrite(LCD_BL_PIN, 200); 
        lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
        last_touch_ms = millis();
    }
}

void check_sensor_logic() {
  if (!qmi.getDataReady()) return;
  if (qmi.getAccelerometer(acc.x, acc.y, acc.z)) {
    float delta = abs(acc.x - last_acc_x) + abs(acc.y - last_acc_y) + abs(acc.z - last_acc_z);
    last_acc_x = acc.x; last_acc_y = acc.y; last_acc_z = acc.z;
    if (delta > MOTION_THRESHOLD) {
      last_touch_ms = millis();
    }
  }
}

void update_about_text() {
    if (!lbl_about_info) return;
    String aboutInfo = "HARDWARE INFO:\n";
    aboutInfo += "Board: ESP32-S3-Touch-LCD-4B\n";
    aboutInfo += "PMU: AXP2101 I2C\n";
    aboutInfo += "Touch: GT911\n";
    aboutInfo += "IMU: QMI8658\n";
    aboutInfo += "RTC: PCF85063\n\n";
    lv_label_set_text(lbl_about_info, aboutInfo.c_str());
}

/* ================= WIFI LOGIC HELPERS ================= */

void load_saved_networks() {
    prefs.begin("wifi_db", true); 
    for(int i=0; i<MAX_SAVED_NETWORKS; i++) {
        String s = prefs.getString(("s_" + String(i)).c_str(), "");
        String p = prefs.getString(("p_" + String(i)).c_str(), "");
        if (s.length() > 0) {
            strncpy(saved_networks[i].ssid, s.c_str(), 32);
            strncpy(saved_networks[i].pass, p.c_str(), 64);
            saved_networks[i].valid = true;
        } else { saved_networks[i].valid = false; }
    }
    prefs.end();
}

void save_current_network_to_list() {
    for(int i=0; i<MAX_SAVED_NETWORKS; i++) {
        if(saved_networks[i].valid && strcmp(saved_networks[i].ssid, wifi_ssid) == 0) {
            strncpy(saved_networks[i].pass, wifi_pass, 64);
            prefs.begin("wifi_db", false);
            prefs.putString(("p_" + String(i)).c_str(), wifi_pass);
            prefs.end();
            return;
        }
    }
    int slot = -1;
    for(int i=0; i<MAX_SAVED_NETWORKS; i++) { if(!saved_networks[i].valid) { slot = i; break; } }
    if(slot == -1) slot = 0;

    strncpy(saved_networks[slot].ssid, wifi_ssid, 32);
    strncpy(saved_networks[slot].pass, wifi_pass, 64);
    saved_networks[slot].valid = true;

    prefs.begin("wifi_db", false);
    prefs.putString(("s_" + String(slot)).c_str(), wifi_ssid);
    prefs.putString(("p_" + String(slot)).c_str(), wifi_pass);
    prefs.end();
    
    refresh_saved_wifi_list_ui();
}

void remove_saved_network(const char* ssid_to_remove) {
    prefs.begin("wifi_db", false);
    for(int i=0; i<MAX_SAVED_NETWORKS; i++) {
        if(saved_networks[i].valid && strcmp(saved_networks[i].ssid, ssid_to_remove) == 0) {
            saved_networks[i].valid = false;
            prefs.remove(("s_" + String(i)).c_str());
            prefs.remove(("p_" + String(i)).c_str());
        }
    }
    prefs.end();
    refresh_saved_wifi_list_ui();
}

void refresh_saved_wifi_list_ui() {
    if(!saved_list_ui) return;
    lv_obj_clean(saved_list_ui);
    for(int i=0; i<MAX_SAVED_NETWORKS; i++) {
        if(saved_networks[i].valid) {
            lv_obj_t *btn = lv_list_add_btn(saved_list_ui, LV_SYMBOL_WIFI, saved_networks[i].ssid);
            lv_obj_add_event_cb(btn, saved_wifi_click_cb, LV_EVENT_CLICKED, NULL);
        }
    }
}

void wipe_wifi_popup() {
    if(scan_list_ui) lv_obj_add_flag(scan_list_ui, LV_OBJ_FLAG_HIDDEN);
    if(saved_list_ui) lv_obj_clear_flag(saved_list_ui, LV_OBJ_FLAG_HIDDEN);
    
    if(current_wifi_state == WIFI_SCANNING) {
        WiFi.scanDelete(); 
        current_wifi_state = WIFI_IDLE;
    }

    if(wifi_enabled && WiFi.status() != WL_CONNECTED) {
        if(lbl_wifi_status) {
            lv_label_set_text(lbl_wifi_status, "Status: Reconnecting...");
            lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
        }
        WiFi.begin(wifi_ssid, wifi_pass);
        current_wifi_state = WIFI_CONNECTING;
        wifi_connect_start = millis();
    }
}

void load_settings() {
  prefs.begin("sys_config", true); 
  
  String s = prefs.getString("dev_name", "ESP32-S3-Panel");
  s.toCharArray(deviceName, 32);

  String ss = prefs.getString("wifi_ssid", "");
  ss.toCharArray(wifi_ssid, 32);
  String pp = prefs.getString("wifi_pass", "");
  pp.toCharArray(wifi_pass, 64);
  
  wifi_enabled = prefs.getBool("wifi_en", false);

  String mh = prefs.getString("mqtt_host", "192.168.10.10");
  mh.toCharArray(mqtt_host, 64);
  mqtt_port = prefs.getInt("mqtt_port", 1883);
  String mu = prefs.getString("mqtt_user", "");
  mu.toCharArray(mqtt_user, 32);
  String mp = prefs.getString("mqtt_pass", "");
  mp.toCharArray(mqtt_pass, 32);
  
  mqtt_enabled = prefs.getBool("mqtt_en", false);
  ntp_auto_update = prefs.getBool("ntp_auto", true);

  prefs.end();
  
  load_saved_networks();
}

void save_device_name(const char* new_name) {
  prefs.begin("sys_config", false); 
  prefs.putString("dev_name", new_name);
  prefs.end();
  snprintf(deviceName, 32, "%s", new_name);
}

// --- LOADER HELPERS ---
void show_loader() {
    if (loader_overlay) lv_obj_del(loader_overlay);
    loader_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(loader_overlay, 480, 480);
    lv_obj_set_style_bg_color(loader_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(loader_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(loader_overlay, 0, 0);
    lv_obj_clear_flag(loader_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *spinner = lv_spinner_create(loader_overlay);
    lv_spinner_set_anim_params(spinner, 1000, 60);
    
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_center(spinner);
    lv_obj_set_style_arc_color(spinner, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    
    lv_obj_t *lbl = lv_label_create(loader_overlay);
    lv_label_set_text(lbl, "Testing Connection...");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 60);
}

void hide_loader() {
    if (loader_overlay) {
        lv_obj_del(loader_overlay);
        loader_overlay = NULL;
    }
}
// ------------------------

void save_ha_settings(const char* h, const char* p_str, const char* u, const char* p, bool en) {
  prefs.begin("sys_config", false);
  prefs.putString("mqtt_host", h);
  prefs.putInt("mqtt_port", atoi(p_str));
  prefs.putString("mqtt_user", u);
  prefs.putString("mqtt_pass", p);
  prefs.putBool("mqtt_en", en);
  prefs.end();

  snprintf(mqtt_host, 64, "%s", h);
  mqtt_port = atoi(p_str);
  snprintf(mqtt_user, 32, "%s", u);
  snprintf(mqtt_pass, 32, "%s", p);
  
  if (en) {
      show_loader();
      lv_timer_handler(); 
      delay(50);         

      mqtt.setServer(mqtt_host, mqtt_port);
      wifiClient.setTimeout(2000); 

      bool connected = false;
      if (strlen(mqtt_user) > 0) connected = mqtt.connect("esp32_panel", mqtt_user, mqtt_pass);
      else connected = mqtt.connect("esp32_panel");

      hide_loader();
      lv_timer_handler();

      if (connected) {
          Serial.println("MQTT Connected Immediately!");
          mqtt_enabled = true;
          mqtt_retry_count = 0;
          
          if(lbl_ha_status) {
              lv_label_set_text(lbl_ha_status, "Status: Connected");
              lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREEN), 0);
          }
          for(int i=0; i<SWITCH_COUNT; i++) mqtt.subscribe(switches[i].topic_state);
          mqtt.subscribe(TOPIC_NOTIFY);
      } else {
          mqtt_enabled = false; 
          if(sw_mqtt_enable) lv_obj_clear_state(sw_mqtt_enable, LV_STATE_CHECKED);
          if(lbl_ha_status) {
              lv_label_set_text(lbl_ha_status, "Status: Connection Failed");
              lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_RED), 0);
          }
          prefs.begin("sys_config", false);
          prefs.putBool("mqtt_en", false);
          prefs.end();
          show_notification_popup("Error: Connection Failed.\nCheck Host IP/User or WiFi.", -1);
      }
  } else {
      mqtt_enabled = false;
      mqtt.disconnect();
      if(lbl_ha_status) {
          lv_label_set_text(lbl_ha_status, "Status: Disabled");
          lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
      }
  }
}

/* ================= POPUP LOGIC ================= */

void popup_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  intptr_t action_id = (intptr_t)lv_event_get_user_data(e); 

  if(code == LV_EVENT_CLICKED) {
    if(action_id == 1) { 
      delete_notification(selected_notification_index);
      notification_ui_dirty = true;
    } 
    else if (action_id == 3) { 
      clear_all_notifications();
      notification_ui_dirty = true;
    }
    
    if(msg_popup) {
      lv_obj_del(msg_popup);
      msg_popup = NULL;
      selected_notification_index = -1;
    }
  }
}

void show_notification_popup(const char* text, int index) {
  if(msg_popup) lv_obj_del(msg_popup); 
  selected_notification_index = index;

  msg_popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(msg_popup, 360, 300);
  lv_obj_align(msg_popup, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(msg_popup, lv_color_black(), 0);
  lv_obj_set_style_border_color(msg_popup, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_set_style_border_width(msg_popup, 2, 0);
  lv_obj_set_style_shadow_width(msg_popup, 50, 0);

  lv_obj_t *lbl = lv_label_create(msg_popup);
  lv_label_set_text(lbl, text);
  lv_obj_set_width(lbl, 320);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *btn_close = lv_btn_create(msg_popup);
  lv_obj_set_size(btn_close, 140, 50);
  
  if (index >= 0) {
      lv_obj_align(btn_close, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  } else {
      lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -10);
  }
  
  lv_obj_set_style_bg_color(btn_close, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(btn_close, popup_event_cb, LV_EVENT_CLICKED, (void*)0); 

  lv_obj_t *lbl_close = lv_label_create(btn_close);
  lv_label_set_text(lbl_close, "Close");
  lv_obj_center(lbl_close);

  if(index >= 0) {
      lv_obj_t *btn_clear = lv_btn_create(msg_popup);
      lv_obj_set_size(btn_clear, 140, 50);
      lv_obj_align(btn_clear, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
      lv_obj_set_style_bg_color(btn_clear, lv_palette_main(LV_PALETTE_RED), 0);
      lv_obj_add_event_cb(btn_clear, popup_event_cb, LV_EVENT_CLICKED, (void*)1);

      lv_obj_t *lbl_clear = lv_label_create(btn_clear);
      lv_label_set_text(lbl_clear, "Clear");
      lv_obj_center(lbl_clear);
  }
}

void show_clear_all_popup() {
  if(msg_popup) lv_obj_del(msg_popup); 

  msg_popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(msg_popup, 300, 200);
  lv_obj_align(msg_popup, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(msg_popup, lv_color_black(), 0);
  lv_obj_set_style_border_color(msg_popup, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_border_width(msg_popup, 2, 0);

  lv_obj_t *lbl = lv_label_create(msg_popup);
  lv_label_set_text(lbl, "Clear all notifications?");
  lv_obj_set_width(lbl, 260);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 30);

  lv_obj_t *btn_no = lv_btn_create(msg_popup);
  lv_obj_set_size(btn_no, 100, 50);
  lv_obj_align(btn_no, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_bg_color(btn_no, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(btn_no, popup_event_cb, LV_EVENT_CLICKED, (void*)2);

  lv_obj_t *lbl_no = lv_label_create(btn_no);
  lv_label_set_text(lbl_no, "No");
  lv_obj_center(lbl_no);

  lv_obj_t *btn_yes = lv_btn_create(msg_popup);
  lv_obj_set_size(btn_yes, 100, 50);
  lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(btn_yes, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(btn_yes, popup_event_cb, LV_EVENT_CLICKED, (void*)3);

  lv_obj_t *lbl_yes = lv_label_create(btn_yes);
  lv_label_set_text(lbl_yes, "Yes");
  lv_obj_center(lbl_yes);
}

void list_item_clicked_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_active();
  if (lv_indev_get_scroll_obj(indev) != NULL || lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) {
      return;
  }
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  if(idx >= 0 && idx < MAX_NOTIFICATIONS) {
    show_notification_popup(notification_history[idx], (int)idx);
  }
}

void clear_all_event_cb(lv_event_t *e) {
  show_clear_all_popup();
}

void refresh_notification_list() {
  if (!notification_list) return;
  lv_obj_clean(notification_list); 
  int count = get_notification_count();
  if (count == 0) {
    lv_obj_add_flag(notification_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(no_notification_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_clear_all, LV_OBJ_FLAG_HIDDEN); 
  } else {
    lv_obj_clear_flag(notification_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(no_notification_label, LV_OBJ_FLAG_HIDDEN);
    if(count >= 2) lv_obj_clear_flag(btn_clear_all, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(btn_clear_all, LV_OBJ_FLAG_HIDDEN);
    for(int i = 0; i < MAX_NOTIFICATIONS; i++) {
      if (notification_history[i][0] != '\0') {
        lv_obj_t *btn = lv_list_add_btn(notification_list, LV_SYMBOL_BELL, notification_history[i]);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE); 
        lv_obj_add_event_cb(btn, list_item_clicked_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_set_style_text_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_BLUE_GREY, 3), 0);
        lv_obj_set_style_border_color(btn, lv_color_white(), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
      }
    }
  }
}

/* ================= MQTT CALLBACKS ================= */

void mqtt_callback(char* topic, byte* payload, unsigned int len) {
  if (len > 512) return;
  char p_buff[513];
  memcpy(p_buff, payload, len);
  p_buff[len] = '\0';
  String msg = String(p_buff);

  Serial.printf("MQTT Rcv: [%s] Payload: [%s]\n", topic, p_buff);

  for (int i = 0; i < SWITCH_COUNT; i++) {
    if (strcmp(topic, switches[i].topic_state) == 0) {
      switches[i].state = (msg == "ON");
      if(switches[i].label) lv_label_set_text(switches[i].label, switches[i].state ? "ON" : "OFF");
      if(switches[i].btn) {
          lv_obj_set_style_bg_color(switches[i].btn,
            switches[i].state ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
      }
    }
  }
  if (strcmp(topic, TOPIC_NOTIFY) == 0) {
    if (msg.length() > 0) add_notification(msg);
  } 
}

void mqtt_reconnect() {
  if (!mqtt_enabled) return; 
  
  if(lbl_wifi_status) {
      lv_label_set_text(lbl_wifi_status, "Status: MQTT Connecting...");
      lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
      lv_timer_handler();
  }

  wifiClient.setTimeout(1);

  bool connected = false;
  if (strlen(mqtt_user) > 0) {
    connected = mqtt.connect("esp32_panel", mqtt_user, mqtt_pass);
  } else {
    connected = mqtt.connect("esp32_panel");
  }

  if (connected) {
    mqtt_retry_count = 0; 
    for(int i=0; i<SWITCH_COUNT; i++) mqtt.subscribe(switches[i].topic_state);
    mqtt.subscribe(TOPIC_NOTIFY);
    
    Serial.println("MQTT Connected!");
  } else {
      Serial.print("MQTT Connect Failed. RC=");
      Serial.println(mqtt.state());
  }
}

/* ================= UI CALLBACKS ================= */

void create_page_dots(lv_obj_t *parent, int active_idx) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 100, 20);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cont, 10, 0);
    for(int i=0; i<3; i++) {
        lv_obj_t *dot = lv_obj_create(cont);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        if(i == active_idx) {
            lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_ORANGE), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_50, 0);
        }
    }
}

void swipe_event_cb(lv_event_t *e) {
    static uint32_t last_swipe_time = 0;
    if (millis() - last_swipe_time < 300) return; 

    lv_obj_t *screen = (lv_obj_t *)lv_event_get_current_target(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    
    bool navigated = false;

    if (screen == screen_home) {
        if (dir == LV_DIR_RIGHT) {
            lv_scr_load_anim(screen_notifications, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            navigated = true;
        }
        else if (dir == LV_DIR_LEFT) {
            lv_scr_load_anim(screen_settings_menu, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            navigated = true;
        }
    }
    else if (screen == screen_notifications) {
        if (dir == LV_DIR_LEFT) {
            lv_scr_load_anim(screen_home, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            navigated = true;
        }
    }
    else if (screen == screen_settings_menu) {
        if (dir == LV_DIR_RIGHT) {
            lv_scr_load_anim(screen_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
            navigated = true;
        }
    }

    if (navigated) {
        last_swipe_time = millis();
    }
}

void back_event_cb(lv_event_t *e) {
    // Disable WiFi if user leaves error screen
    if (lv_scr_act() == screen_wifi && wifi_enabled && WiFi.status() != WL_CONNECTED) {
        Serial.println("User left WiFi screen without connecting. Disabling.");
        wifi_enabled = false;
        current_wifi_state = WIFI_IDLE;
        
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        
        prefs.begin("sys_config", false);
        prefs.putBool("wifi_en", false);
        prefs.end();
        
        if(sw_wifi_enable) lv_obj_clear_state(sw_wifi_enable, LV_STATE_CHECKED);
        if(cont_wifi_inputs) lv_obj_add_flag(cont_wifi_inputs, LV_OBJ_FLAG_HIDDEN);
        if(lbl_wifi_status) lv_label_set_text(lbl_wifi_status, "Status: Disabled");
    }

    wipe_wifi_popup(); 
    
    // Ensure keyboard on Time screen is hidden when leaving
    if(kb_time && !lv_obj_has_flag(kb_time, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(kb_time, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (lv_scr_act() == screen_wifi || lv_scr_act() == screen_ha || 
        lv_scr_act() == screen_power || lv_scr_act() == screen_about || 
        lv_scr_act() == screen_time_date) {
        
        lv_scr_load_anim(screen_settings_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    } 
    else {
        lv_scr_load_anim(screen_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    }
}

void wifi_list_btn_cb(lv_event_t * e) {
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const char *ssid = lv_list_get_btn_text(scan_list_ui, btn); 
    lv_textarea_set_text(ta_ssid, ssid);
    lv_textarea_set_text(ta_pass, "");
    lv_obj_add_flag(scan_list_ui, LV_OBJ_FLAG_HIDDEN);
    if(saved_list_ui) lv_obj_clear_flag(saved_list_ui, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_clear_state(ta_ssid, LV_STATE_FOCUSED);
    lv_obj_add_state(ta_pass, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(kb_wifi, ta_pass);
    lv_obj_clear_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
}

void btn_scan_wifi_cb(lv_event_t * e) {
    if (current_wifi_state == WIFI_SCANNING || current_wifi_state == WIFI_CONNECTING) return;
    
    if(lbl_wifi_status) {
        lv_label_set_text(lbl_wifi_status, "Status: Starting Scan...");
        lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
    }
    
    if(saved_list_ui) lv_obj_add_flag(saved_list_ui, LV_OBJ_FLAG_HIDDEN); 
    
    if(scan_list_ui) {
        lv_obj_clean(scan_list_ui);
        lv_obj_clear_flag(scan_list_ui, LV_OBJ_FLAG_HIDDEN); 
        lv_list_add_text(scan_list_ui, "Scanning... Please wait");
    }
    
    current_wifi_state = WIFI_SCANNING;
}

void wifi_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if(kb_wifi != NULL) {
            lv_keyboard_set_textarea(kb_wifi, ta);
            lv_obj_clear_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
            lv_obj_scroll_to_view(ta, LV_ANIM_ON);
        }
    }
}

void btn_save_wifi_cb(lv_event_t * e) {
    const char* s = lv_textarea_get_text(ta_ssid);
    const char* p = lv_textarea_get_text(ta_pass);
    bool en = lv_obj_has_state(sw_wifi_enable, LV_STATE_CHECKED);

    strncpy(wifi_ssid, s, 32);
    strncpy(wifi_pass, p, 64);
    wifi_enabled = en;

    prefs.begin("sys_config", false);
    prefs.putBool("wifi_en", en);
    if(en) {
        prefs.putString("wifi_ssid", wifi_ssid);
        prefs.putString("wifi_pass", wifi_pass);
    }
    prefs.end();

    if (en) {
        lv_label_set_text(lbl_wifi_status, "Status: Connecting...");
        lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
        
        mqtt.disconnect(); 
        mqtt_retry_count = 0; 

        WiFi.disconnect();
        WiFi.begin(wifi_ssid, wifi_pass);
        current_wifi_state = WIFI_CONNECTING;
        wifi_connect_start = millis();
    } else {
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        current_wifi_state = WIFI_IDLE;
        lv_label_set_text(lbl_wifi_status, "Status: Disabled");
    }
    
    lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
}

void saved_wifi_click_cb(lv_event_t * e) {
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const char* txt = lv_list_get_btn_text(saved_list_ui, btn);
    
    for(int i=0; i<MAX_SAVED_NETWORKS; i++) {
        if(saved_networks[i].valid && strcmp(saved_networks[i].ssid, txt) == 0) {
            lv_textarea_set_text(ta_ssid, saved_networks[i].ssid);
            lv_textarea_set_text(ta_pass, saved_networks[i].pass);
            return;
        }
    }
}

void sw_wifi_event_cb(lv_event_t * e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    wifi_enabled = is_on;
    prefs.begin("sys_config", false);
    prefs.putBool("wifi_en", is_on);
    prefs.end();

    if(is_on) {
        lv_obj_clear_flag(cont_wifi_inputs, LV_OBJ_FLAG_HIDDEN);
        if (strlen(wifi_ssid) > 0) {
            if(lbl_wifi_status) {
                lv_label_set_text(lbl_wifi_status, "Status: Resuming Connection...");
                lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
            }
            WiFi.begin(wifi_ssid, wifi_pass);
            current_wifi_state = WIFI_CONNECTING;
            wifi_connect_start = millis();
        } else {
             if(lbl_wifi_status) lv_label_set_text(lbl_wifi_status, "Status: Enter Credentials");
        }

    } else {
        lv_obj_add_flag(cont_wifi_inputs, LV_OBJ_FLAG_HIDDEN);
        
        if(scan_list_ui) lv_obj_add_flag(scan_list_ui, LV_OBJ_FLAG_HIDDEN);
        if(kb_wifi) lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
        
        current_wifi_state = WIFI_IDLE;
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        
        if(lbl_wifi_status) lv_label_set_text(lbl_wifi_status, "Status: Disabled");

        if (mqtt_enabled) {
            mqtt_enabled = false;
            mqtt_retry_count = 0; 
            mqtt.disconnect(); 

            prefs.begin("sys_config", false);
            prefs.putBool("mqtt_en", false);
            prefs.end();

            if(sw_mqtt_enable) lv_obj_clear_state(sw_mqtt_enable, LV_STATE_CHECKED);
            if(cont_ha_inputs) lv_obj_add_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);
            if(lbl_ha_status) {
                lv_label_set_text(lbl_ha_status, "Status: Disabled (WiFi Off)");
                lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
            }
        }
    }
}

void sw_ha_event_cb(lv_event_t * e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if(is_on && (!wifi_enabled || WiFi.status() != WL_CONNECTED)) {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
        show_notification_popup("Error: \n\nConnect to WiFi first!", -1);
        return;
    }

    mqtt_enabled = is_on;
    prefs.begin("sys_config", false);
    prefs.putBool("mqtt_en", is_on);
    prefs.end();

    if(is_on) {
        lv_obj_clear_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);
        mqtt_retry_count = 0; 
        if(lbl_ha_status) {
             lv_label_set_text(lbl_ha_status, "Status: Ready to Connect");
             lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
        }
    } else {
        lv_obj_add_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);
        mqtt.disconnect();
        if(lbl_ha_status) {
            lv_label_set_text(lbl_ha_status, "Status: Disabled");
            lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
        }
    }
}

void ha_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if(kb_ha != NULL) {
            lv_keyboard_set_textarea(kb_ha, ta);
            lv_obj_clear_flag(kb_ha, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void btn_save_ha_cb(lv_event_t * e) {
    const char* h = lv_textarea_get_text(ta_mqtt_host);
    const char* port = lv_textarea_get_text(ta_mqtt_port);
    const char* u = lv_textarea_get_text(ta_mqtt_user);
    const char* p = lv_textarea_get_text(ta_mqtt_pass);
    
    bool en = lv_obj_has_state(sw_mqtt_enable, LV_STATE_CHECKED);
    
    if (en && !wifi_enabled) {
        en = false;
        show_notification_popup("Error: WiFi is Disabled", -1);
    }
    
    save_ha_settings(h, port, u, p, en);
    lv_obj_add_flag(kb_ha, LV_OBJ_FLAG_HIDDEN);
}

void ta_event_cb(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
  if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
    if(kb != NULL) {
      lv_keyboard_set_textarea(kb, ta);
      lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if(code == LV_EVENT_READY) { 
    const char* txt = lv_textarea_get_text(ta);
    save_device_name(txt);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); 
    lv_obj_clear_state(ta, LV_STATE_FOCUSED); 
  }
}

void switch_event_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_active();
  if (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;
  HaSwitch* sw = (HaSwitch*)lv_event_get_user_data(e);
  sw->state = !sw->state;
  if (mqtt.connected()) mqtt.publish(sw->topic_cmd, sw->state ? "ON" : "OFF");
  lv_label_set_text(sw->label, sw->state ? "ON" : "OFF");
  lv_obj_set_style_bg_color(sw->btn, sw->state ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
}

void settings_menu_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if(lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;
    intptr_t user_data = (intptr_t)lv_event_get_user_data(e);
    if(code == LV_EVENT_CLICKED) {
        if(user_data == 1) lv_scr_load_anim(screen_about, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
        else if (user_data == 2) lv_scr_load_anim(screen_power, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
        else if (user_data == 3) lv_scr_load_anim(screen_wifi, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
        else if (user_data == 4) {
            if(mqtt_enabled) {
                lv_obj_add_state(sw_mqtt_enable, LV_STATE_CHECKED);
                lv_obj_clear_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);
                lv_textarea_set_text(ta_mqtt_host, mqtt_host);
                char p_buf[10]; sprintf(p_buf, "%d", mqtt_port);
                lv_textarea_set_text(ta_mqtt_port, p_buf);
                lv_textarea_set_text(ta_mqtt_user, mqtt_user);
                lv_textarea_set_text(ta_mqtt_pass, mqtt_pass);
            } else {
                lv_obj_clear_state(sw_mqtt_enable, LV_STATE_CHECKED);
                lv_obj_add_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);
            }
            lv_scr_load_anim(screen_ha, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false); 
        }
        else if (user_data == 5) {
            lv_scr_load_anim(screen_time_date, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
        }
    }
}

/* ================= SCREEN BUILDERS ================= */

void create_power_screen(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  
  lv_obj_t *btn_back = lv_btn_create(parent);
  lv_obj_set_size(btn_back, 60, 40);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_set_style_bg_color(btn_back, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  lv_obj_center(lbl_back);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "System Status");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  power_info_label = lv_label_create(parent);
  lv_label_set_text(power_info_label, "Loading...");
  lv_label_set_long_mode(power_info_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(power_info_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(power_info_label, lv_color_white(), 0);
  lv_obj_align(power_info_label, LV_ALIGN_TOP_LEFT, 20, 70); 
}

void create_notifications_page(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  create_page_dots(parent, 0);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Notifications");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

  btn_clear_all = lv_btn_create(parent);
  lv_obj_set_size(btn_clear_all, 80, 30);
  lv_obj_align(btn_clear_all, LV_ALIGN_TOP_RIGHT, -20, 60); 
  lv_obj_set_style_bg_color(btn_clear_all, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_add_event_cb(btn_clear_all, clear_all_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(btn_clear_all, LV_OBJ_FLAG_HIDDEN); 

  lv_obj_t *lbl_ca = lv_label_create(btn_clear_all);
  lv_label_set_text(lbl_ca, "Clear All");
  lv_obj_center(lbl_ca);

  no_notification_label = lv_label_create(parent);
  lv_label_set_text(no_notification_label, "No Notifications");
  lv_obj_set_style_text_font(no_notification_label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(no_notification_label, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
  lv_obj_align(no_notification_label, LV_ALIGN_CENTER, 0, 0);

  notification_list = lv_list_create(parent);
  lv_obj_set_size(notification_list, 420, 320); 
  lv_obj_align(notification_list, LV_ALIGN_TOP_MID, 0, 100); 
  lv_obj_set_style_bg_opa(notification_list, LV_OPA_TRANSP, 0); 
  lv_obj_set_style_border_width(notification_list, 0, 0);

  refresh_notification_list();

  lv_obj_t *hint = lv_label_create(parent);
  lv_label_set_text(hint, "Swipe Left for Home " LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(hint, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void create_wifi_screen(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

    // --- Header (Back Button & Title) ---
    lv_obj_t *btn_back = lv_btn_create(parent);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn_back, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // --- Enable Switch (Top Right) ---
    sw_wifi_enable = lv_switch_create(parent);
    lv_obj_set_size(sw_wifi_enable, 50, 25); // Slightly compact switch
    lv_obj_align(sw_wifi_enable, LV_ALIGN_TOP_RIGHT, -20, 60); 
    lv_obj_add_event_cb(sw_wifi_enable, sw_wifi_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if(wifi_enabled) lv_obj_add_state(sw_wifi_enable, LV_STATE_CHECKED);

    lv_obj_t *lbl_en = lv_label_create(parent);
    lv_label_set_text(lbl_en, "Enable:");
    lv_obj_set_style_text_color(lbl_en, lv_color_white(), 0);
    lv_obj_align(lbl_en, LV_ALIGN_TOP_RIGHT, -80, 62);

    // --- Main Container (Holds Inputs & List) ---
    cont_wifi_inputs = lv_obj_create(parent);
    lv_obj_set_size(cont_wifi_inputs, 480, 380); // maximize height
    lv_obj_align(cont_wifi_inputs, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(cont_wifi_inputs, LV_OPA_TRANSP, 0); 
    lv_obj_set_style_border_width(cont_wifi_inputs, 0, 0);
    lv_obj_set_style_pad_all(cont_wifi_inputs, 0, 0); // Remove padding to use full edge
    
    // FIX: Disable scrollbar since we are optimizing layout to fit
    lv_obj_clear_flag(cont_wifi_inputs, LV_OBJ_FLAG_SCROLLABLE); 

    if(!wifi_enabled) lv_obj_add_flag(cont_wifi_inputs, LV_OBJ_FLAG_HIDDEN);

    // ================= ROW 1: SSID =================
    // Layout: [Label] [Input Box.......] [Scan Button]
    
    lv_obj_t *lbl_ssid = lv_label_create(cont_wifi_inputs);
    lv_label_set_text(lbl_ssid, "SSID:");
    lv_obj_set_style_text_color(lbl_ssid, lv_color_white(), 0);
    lv_obj_align(lbl_ssid, LV_ALIGN_TOP_LEFT, 10, 15);

    ta_ssid = lv_textarea_create(cont_wifi_inputs);
    lv_textarea_set_text(ta_ssid, wifi_ssid);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_obj_set_width(ta_ssid, 260); // Wider input
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 70, 5); // Shifted right
    lv_obj_add_event_cb(ta_ssid, wifi_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_scan = lv_btn_create(cont_wifi_inputs);
    lv_obj_set_size(btn_scan, 100, 40);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_RIGHT, -20, 5); // Right aligned
    lv_obj_set_style_bg_color(btn_scan, lv_palette_main(LV_PALETTE_PURPLE), 0);
    lv_obj_add_event_cb(btn_scan, btn_scan_wifi_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "Scan");
    lv_obj_center(lbl_scan);

    // ================= ROW 2: PASS =================
    // Layout: [Label] [Input Box.......] [Join Button]

    lv_obj_t *lbl_pass = lv_label_create(cont_wifi_inputs);
    lv_label_set_text(lbl_pass, "Pass:");
    lv_obj_set_style_text_color(lbl_pass, lv_color_white(), 0);
    lv_obj_align(lbl_pass, LV_ALIGN_TOP_LEFT, 10, 65);

    ta_pass = lv_textarea_create(cont_wifi_inputs);
    lv_textarea_set_text(ta_pass, wifi_pass);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_one_line(ta_pass, true);
    lv_obj_set_width(ta_pass, 260);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 70, 55);
    lv_obj_add_event_cb(ta_pass, wifi_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_save = lv_btn_create(cont_wifi_inputs);
    lv_obj_set_size(btn_save, 100, 40);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, -20, 55);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_save, btn_save_wifi_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Join");
    lv_obj_center(lbl_save);

    // ================= ROW 3: STATUS & LIST =================

    lbl_wifi_status = lv_label_create(cont_wifi_inputs);
    lv_label_set_text(lbl_wifi_status, "Status: Ready");
    lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(lbl_wifi_status, LV_ALIGN_TOP_LEFT, 10, 105);

    lv_obj_t *lbl_saved = lv_label_create(cont_wifi_inputs);
    lv_label_set_text(lbl_saved, "Saved Networks:");
    lv_obj_set_style_text_color(lbl_saved, lv_color_white(), 0);
    lv_obj_align(lbl_saved, LV_ALIGN_TOP_LEFT, 10, 135);

    // MAXIMIZED LIST SIZE
    saved_list_ui = lv_list_create(cont_wifi_inputs);
    lv_obj_set_size(saved_list_ui, 440, 190); // Increased height from 120 to 190
    lv_obj_align(saved_list_ui, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_opa(saved_list_ui, LV_OPA_TRANSP, 0);
    
    refresh_saved_wifi_list_ui(); 

    // --- POPUPS & KEYBOARD (Unchanged) ---
    scan_list_ui = lv_list_create(parent); 
    lv_obj_set_size(scan_list_ui, 400, 300);
    lv_obj_align(scan_list_ui, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(scan_list_ui, lv_color_black(), 0); 
    lv_obj_set_style_bg_opa(scan_list_ui, LV_OPA_COVER, 0); 
    lv_obj_set_style_border_color(scan_list_ui, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_border_width(scan_list_ui, 2, 0);
    lv_obj_add_flag(scan_list_ui, LV_OBJ_FLAG_HIDDEN); 

    kb_wifi = lv_keyboard_create(parent);
    lv_obj_set_size(kb_wifi, 480, 220);
    lv_obj_align(kb_wifi, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(kb_wifi, [](lv_event_t* e){
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            lv_obj_add_flag((lv_obj_t*)lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(ta_ssid, LV_STATE_FOCUSED);
            lv_obj_clear_state(ta_pass, LV_STATE_FOCUSED);
        }
    }, LV_EVENT_ALL, NULL);
}

void create_ha_screen(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

    // --- Header ---
    lv_obj_t *btn_back = lv_btn_create(parent);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn_back, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Home Assistant");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // --- Enable Switch ---
    sw_mqtt_enable = lv_switch_create(parent);
    lv_obj_set_size(sw_mqtt_enable, 50, 25);
    lv_obj_align(sw_mqtt_enable, LV_ALIGN_TOP_RIGHT, -20, 60); 
    lv_obj_add_event_cb(sw_mqtt_enable, sw_ha_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if(mqtt_enabled) lv_obj_add_state(sw_mqtt_enable, LV_STATE_CHECKED);

    lv_obj_t *lbl_en = lv_label_create(parent);
    lv_label_set_text(lbl_en, "Enable:");
    lv_obj_set_style_text_color(lbl_en, lv_color_white(), 0);
    lv_obj_align(lbl_en, LV_ALIGN_TOP_RIGHT, -80, 62);

    // --- Main Container ---
    cont_ha_inputs = lv_obj_create(parent);
    lv_obj_set_size(cont_ha_inputs, 480, 380); // Full Height
    lv_obj_align(cont_ha_inputs, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(cont_ha_inputs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_ha_inputs, 0, 0);
    lv_obj_set_style_pad_all(cont_ha_inputs, 0, 0); 
    lv_obj_clear_flag(cont_ha_inputs, LV_OBJ_FLAG_SCROLLABLE);
    
    if(!mqtt_enabled) lv_obj_add_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);

    // ================= ROW 1: HOST & PORT =================
    lv_obj_t *lbl_host = lv_label_create(cont_ha_inputs);
    lv_label_set_text(lbl_host, "Host:");
    lv_obj_set_style_text_color(lbl_host, lv_color_white(), 0);
    lv_obj_align(lbl_host, LV_ALIGN_TOP_LEFT, 10, 15);

    ta_mqtt_host = lv_textarea_create(cont_ha_inputs);
    lv_textarea_set_text(ta_mqtt_host, mqtt_host);
    lv_textarea_set_one_line(ta_mqtt_host, true);
    lv_obj_set_width(ta_mqtt_host, 240);
    lv_obj_align(ta_mqtt_host, LV_ALIGN_TOP_LEFT, 60, 5);
    lv_obj_add_event_cb(ta_mqtt_host, ha_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_port = lv_label_create(cont_ha_inputs);
    lv_label_set_text(lbl_port, "Port:");
    lv_obj_set_style_text_color(lbl_port, lv_color_white(), 0);
    lv_obj_align(lbl_port, LV_ALIGN_TOP_LEFT, 310, 15); 

    ta_mqtt_port = lv_textarea_create(cont_ha_inputs);
    char port_str[6]; sprintf(port_str, "%d", mqtt_port);
    lv_textarea_set_text(ta_mqtt_port, port_str);
    lv_textarea_set_one_line(ta_mqtt_port, true);
    lv_obj_set_width(ta_mqtt_port, 90);
    lv_obj_align(ta_mqtt_port, LV_ALIGN_TOP_LEFT, 360, 5);
    lv_obj_add_event_cb(ta_mqtt_port, ha_ta_event_cb, LV_EVENT_ALL, NULL);

    // ================= ROW 2: USER =================
    lv_obj_t *lbl_user = lv_label_create(cont_ha_inputs);
    lv_label_set_text(lbl_user, "User:");
    lv_obj_set_style_text_color(lbl_user, lv_color_white(), 0);
    lv_obj_align(lbl_user, LV_ALIGN_TOP_LEFT, 10, 65);

    ta_mqtt_user = lv_textarea_create(cont_ha_inputs);
    lv_textarea_set_text(ta_mqtt_user, mqtt_user);
    lv_textarea_set_one_line(ta_mqtt_user, true);
    lv_obj_set_width(ta_mqtt_user, 390);
    lv_obj_align(ta_mqtt_user, LV_ALIGN_TOP_LEFT, 60, 55);
    lv_obj_add_event_cb(ta_mqtt_user, ha_ta_event_cb, LV_EVENT_ALL, NULL);

    // ================= ROW 3: PASS =================
    lv_obj_t *lbl_pass = lv_label_create(cont_ha_inputs);
    lv_label_set_text(lbl_pass, "Pass:");
    lv_obj_set_style_text_color(lbl_pass, lv_color_white(), 0);
    lv_obj_align(lbl_pass, LV_ALIGN_TOP_LEFT, 10, 115);

    ta_mqtt_pass = lv_textarea_create(cont_ha_inputs);
    lv_textarea_set_text(ta_mqtt_pass, mqtt_pass);
    lv_textarea_set_password_mode(ta_mqtt_pass, true);
    lv_textarea_set_one_line(ta_mqtt_pass, true);
    lv_obj_set_width(ta_mqtt_pass, 390);
    lv_obj_align(ta_mqtt_pass, LV_ALIGN_TOP_LEFT, 60, 105);
    lv_obj_add_event_cb(ta_mqtt_pass, ha_ta_event_cb, LV_EVENT_ALL, NULL);

    // ================= ROW 4: STATUS =================
    lbl_ha_status = lv_label_create(cont_ha_inputs);
    lv_label_set_text(lbl_ha_status, "Status: Not Connected");
    lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(lbl_ha_status, LV_ALIGN_TOP_LEFT, 10, 165); 

    // ================= ROW 5: SAVE BUTTON (Bottom Center) =================
    lv_obj_t *btn_save = lv_btn_create(cont_ha_inputs);
    lv_obj_set_size(btn_save, 140, 40);
    // Align to BOTTOM MID of container (Previous Location)
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -10); 
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(btn_save, btn_save_ha_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Save Settings");
    lv_obj_center(lbl_save);

    // --- Keyboard (Unchanged) ---
    kb_ha = lv_keyboard_create(parent);
    lv_obj_set_size(kb_ha, 480, 220);
    lv_obj_align(kb_ha, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb_ha, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(kb_ha, [](lv_event_t* e){
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            lv_obj_add_flag((lv_obj_t*)lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(ta_mqtt_host, LV_STATE_FOCUSED);
            lv_obj_clear_state(ta_mqtt_port, LV_STATE_FOCUSED);
            lv_obj_clear_state(ta_mqtt_user, LV_STATE_FOCUSED);
            lv_obj_clear_state(ta_mqtt_pass, LV_STATE_FOCUSED);
        }
    }, LV_EVENT_ALL, NULL);
}

void create_about_screen(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_t *btn_back = lv_btn_create(parent);
  lv_obj_set_size(btn_back, 60, 40);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_set_style_bg_color(btn_back, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  lv_obj_center(lbl_back);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "About");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *lbl_name = lv_label_create(parent);
  lv_label_set_text(lbl_name, "Device Name:");
  lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
  lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 20, 70);

  ta_device_name = lv_textarea_create(parent);
  lv_textarea_set_text(ta_device_name, deviceName);
  lv_textarea_set_one_line(ta_device_name, true);
  lv_obj_set_width(ta_device_name, 440);
  lv_obj_align(ta_device_name, LV_ALIGN_TOP_MID, 0, 95);
  lv_obj_add_event_cb(ta_device_name, ta_event_cb, LV_EVENT_ALL, NULL);

  lbl_about_info = lv_label_create(parent);
  lv_obj_set_width(lbl_about_info, 440);
  lv_obj_set_style_text_color(lbl_about_info, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_align(lbl_about_info, LV_ALIGN_TOP_LEFT, 20, 150);

  update_about_text();

  kb = lv_keyboard_create(parent);
  lv_obj_set_size(kb, 480, 200);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  
  lv_obj_add_event_cb(kb, [](lv_event_t* e){
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CANCEL) {
      lv_obj_add_flag((lv_obj_t*)lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_state(ta_device_name, LV_STATE_FOCUSED);
    }
  }, LV_EVENT_CANCEL, NULL);
}

void create_settings_menu_screen(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    create_page_dots(parent, 2); 

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t * list = lv_list_create(parent);
    lv_obj_set_size(list, 400, 340); 
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 30);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);

    lv_obj_t *btn_wifi = lv_list_add_btn(list, LV_SYMBOL_WIFI, " WiFi Setup");
    lv_obj_set_height(btn_wifi, 60);
    lv_obj_add_event_cb(btn_wifi, settings_menu_event_cb, LV_EVENT_CLICKED, (void*)3);

    lv_obj_t *btn_ha = lv_list_add_btn(list, LV_SYMBOL_HOME, " Home Assistant");
    lv_obj_set_height(btn_ha, 60);
    lv_obj_add_event_cb(btn_ha, settings_menu_event_cb, LV_EVENT_CLICKED, (void*)4);

    lv_obj_t *btn_time = lv_list_add_btn(list, LV_SYMBOL_REFRESH, " Time & Date");
    lv_obj_set_height(btn_time, 60);
    lv_obj_add_event_cb(btn_time, settings_menu_event_cb, LV_EVENT_CLICKED, (void*)5);

    lv_obj_t *btn_status = lv_list_add_btn(list, LV_SYMBOL_LIST, " System Status");
    lv_obj_set_height(btn_status, 60);
    lv_obj_add_event_cb(btn_status, settings_menu_event_cb, LV_EVENT_CLICKED, (void*)2);

    lv_obj_t *btn_about = lv_list_add_btn(list, LV_SYMBOL_FILE, " About Device");
    lv_obj_set_height(btn_about, 60);
    lv_obj_add_event_cb(btn_about, settings_menu_event_cb, LV_EVENT_CLICKED, (void*)1);
}

void create_switch_grid(lv_obj_t *parent) {
  static lv_coord_t col_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  static lv_coord_t row_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };

  lv_obj_t *grid = lv_obj_create(parent);
  lv_obj_set_size(grid, 420, 360);
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -30);
  
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

  for (int i = 0; i < SWITCH_COUNT; i++) {
    switches[i].btn = lv_btn_create(grid);
    lv_obj_set_grid_cell(switches[i].btn, LV_GRID_ALIGN_STRETCH, i % GRID_COLS, 1, LV_GRID_ALIGN_STRETCH, i / GRID_COLS, 1);
    lv_obj_add_event_cb(switches[i].btn, switch_event_cb, LV_EVENT_CLICKED, &switches[i]);
    lv_obj_t *icon = lv_label_create(switches[i].btn); lv_label_set_text(icon, icons[i]); lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *name = lv_label_create(switches[i].btn); lv_label_set_text(name, switches[i].name); lv_obj_align(name, LV_ALIGN_CENTER, 0, 10);
    switches[i].label = lv_label_create(switches[i].btn); lv_label_set_text(switches[i].label, "OFF"); lv_obj_align(switches[i].label, LV_ALIGN_BOTTOM_MID, 0, -6);
  }
}

void anim_breathe_cb(void * var, int32_t v) {
    lv_obj_set_style_transform_zoom((lv_obj_t*)var, v, 0);
}

void start_breathing_anim(lv_obj_t * obj) {
    lv_anim_init(&anim_breathe);
    lv_anim_set_var(&anim_breathe, obj);
    lv_anim_set_values(&anim_breathe, 256, 280); // Zoom from 100% (256) to ~110% (280)
    lv_anim_set_time(&anim_breathe, 3000);       // 3 seconds in
    lv_anim_set_playback_time(&anim_breathe, 3000); // 3 seconds out
    lv_anim_set_repeat_count(&anim_breathe, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim_breathe, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim_breathe, anim_breathe_cb);
    lv_anim_start(&anim_breathe);
}

void create_sleep_screen() {
    // 1. Initialize Style
    lv_style_init(&style_weather_bg);
    lv_style_set_bg_opa(&style_weather_bg, LV_OPA_COVER);
    lv_style_set_bg_color(&style_weather_bg, lv_palette_darken(LV_PALETTE_BLUE_GREY, 4));
    lv_style_set_bg_grad_color(&style_weather_bg, lv_color_black());
    lv_style_set_bg_grad_dir(&style_weather_bg, LV_GRAD_DIR_VER);

    // 2. Main Overlay
    sleep_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(sleep_overlay, 480, 480);
    lv_obj_add_style(sleep_overlay, &style_weather_bg, 0); 
    lv_obj_set_style_border_width(sleep_overlay, 0, 0);
    lv_obj_set_style_radius(sleep_overlay, 0, 0);
    lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // --- CELESTIAL BODY (Static) ---
    // We keep the object, but we DO NOT start the animation.
    celestial_body = lv_obj_create(sleep_overlay);
    lv_obj_set_size(celestial_body, 180, 180); 
    lv_obj_align(celestial_body, LV_ALIGN_TOP_RIGHT, 40, -40);
    lv_obj_set_style_radius(celestial_body, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(celestial_body, 0, 0);
    // Simple static shadow is fine, but breathing shadow is heavy.
    // Let's remove shadow for maximum stability first.
    // lv_obj_set_style_shadow_width(celestial_body, 40, 0); 
    lv_obj_clear_flag(celestial_body, LV_OBJ_FLAG_SCROLLABLE);
    
    // --- COMMENTED OUT TO PREVENT CRASH ---
    // start_breathing_anim(celestial_body); 
    // --------------------------------------

    // --- LEFT SIDE: WEATHER INFO ---
    sleep_weather_lbl = lv_label_create(sleep_overlay);
    lv_obj_set_style_text_font(sleep_weather_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sleep_weather_lbl, lv_color_white(), 0);
    lv_obj_align(sleep_weather_lbl, LV_ALIGN_TOP_LEFT, 30, 40);
    lv_label_set_text(sleep_weather_lbl, "Loading...");

    sleep_status_lbl = lv_label_create(sleep_overlay); 
    lv_obj_set_style_text_font(sleep_status_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(sleep_status_lbl, lv_color_white(), 0);
    lv_obj_set_style_transform_zoom(sleep_status_lbl, 512, 0); 
    lv_obj_align(sleep_status_lbl, LV_ALIGN_LEFT_MID, 40, 20);
    lv_label_set_text(sleep_status_lbl, "--°");

    lv_obj_t *lbl_city = lv_label_create(sleep_overlay);
    lv_obj_set_style_text_font(lbl_city, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_city, lv_color_white(), 0);
    lv_obj_set_style_text_opa(lbl_city, LV_OPA_80, 0);
    lv_obj_align(lbl_city, LV_ALIGN_BOTTOM_LEFT, 30, -40);
    lv_label_set_text_fmt(lbl_city, "Hyderabad"); 

    // --- RIGHT SIDE ---
    sleep_time_lbl = lv_label_create(sleep_overlay);
    lv_obj_set_style_text_font(sleep_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(sleep_time_lbl, lv_color_white(), 0);
    lv_obj_align(sleep_time_lbl, LV_ALIGN_TOP_RIGHT, -30, 40);
    lv_label_set_text(sleep_time_lbl, "00:00");

    sleep_date_lbl = lv_label_create(sleep_overlay);
    lv_obj_set_style_text_font(sleep_date_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sleep_date_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_opa(sleep_date_lbl, LV_OPA_80, 0);
    lv_obj_align(sleep_date_lbl, LV_ALIGN_TOP_RIGHT, -30, 95);
    lv_label_set_text(sleep_date_lbl, "Mon, 01 Jan");

    // --- BOTTOM RIGHT ---
    lv_obj_t *notify_cont = lv_obj_create(sleep_overlay);
    lv_obj_set_size(notify_cont, LV_SIZE_CONTENT, 35);
    lv_obj_set_style_bg_color(notify_cont, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(notify_cont, LV_OPA_30, 0); 
    lv_obj_set_style_radius(notify_cont, 20, 0);
    lv_obj_set_style_border_width(notify_cont, 0, 0);
    lv_obj_align(notify_cont, LV_ALIGN_BOTTOM_RIGHT, -30, -80);
    
    sleep_notify_badge = lv_label_create(notify_cont);
    lv_obj_center(sleep_notify_badge);
    lv_obj_set_style_text_color(sleep_notify_badge, lv_color_white(), 0);
    lv_label_set_text(sleep_notify_badge, "0 Alerts");
    lv_obj_set_user_data(sleep_notify_badge, notify_cont);
    lv_obj_add_flag(notify_cont, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *status_bar = lv_obj_create(sleep_overlay);
    lv_obj_set_size(status_bar, LV_SIZE_CONTENT, 40);
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_RIGHT, -20, -30);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(status_bar, 15, 0);

    status_wifi_icon = lv_label_create(status_bar);
    lv_label_set_text(status_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(status_wifi_icon, lv_color_white(), 0);

    status_mqtt_icon = lv_label_create(status_bar);
    lv_label_set_text(status_mqtt_icon, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_color(status_mqtt_icon, lv_color_white(), 0);

    status_batt_icon = lv_label_create(status_bar);
    lv_label_set_text(status_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(status_batt_icon, lv_color_white(), 0);

    lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* ================= TIME & DATE SCREEN ================= */
void time_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if(kb_time != NULL) {
            lv_keyboard_set_textarea(kb_time, ta);
            lv_obj_clear_flag(kb_time, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void time_kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag((lv_obj_t*)lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);
        
        if(ta_day) lv_obj_clear_state(ta_day, LV_STATE_FOCUSED);
        if(ta_month) lv_obj_clear_state(ta_month, LV_STATE_FOCUSED);
        if(ta_year) lv_obj_clear_state(ta_year, LV_STATE_FOCUSED);
        if(ta_hour) lv_obj_clear_state(ta_hour, LV_STATE_FOCUSED);
        if(ta_min) lv_obj_clear_state(ta_min, LV_STATE_FOCUSED);
    }
}

void ampm_click_cb(lv_event_t * e) {
    time_is_pm = !time_is_pm;
    if(time_is_pm) {
        lv_label_set_text(lbl_ampm, "PM");
        lv_obj_set_style_bg_color(btn_ampm, lv_palette_main(LV_PALETTE_BLUE_GREY), 0); 
    } else {
        lv_label_set_text(lbl_ampm, "AM");
        lv_obj_set_style_bg_color(btn_ampm, lv_palette_main(LV_PALETTE_ORANGE), 0);
    }
}

void save_time_date_cb(lv_event_t * e) {
    int d = atoi(lv_textarea_get_text(ta_day));
    int mo = atoi(lv_textarea_get_text(ta_month));
    int y = atoi(lv_textarea_get_text(ta_year));
    int h = atoi(lv_textarea_get_text(ta_hour));
    int m = atoi(lv_textarea_get_text(ta_min));

    if(d < 1) d = 1; if(d > 31) d = 31;
    if(mo < 1) mo = 1; if(mo > 12) mo = 12;
    if(y < 2024) y = 2024; if(y > 2099) y = 2099;
    if(m < 0) m = 0; if(m > 59) m = 59;
    if(h < 1) h = 1; if(h > 12) h = 12;
    if (time_is_pm && h < 12) h += 12;
    else if (!time_is_pm && h == 12) h = 0;

    Serial.printf("Saving RTC: %04d-%02d-%02d %02d:%02d\n", y, mo, d, h, m);
    rtc.setDateTime(y, mo, d, h, m, 0);
    back_event_cb(NULL);
}

void time_screen_load_cb(lv_event_t * e) {
    RTC_DateTime dt = rtc.getDateTime();
    
    int h24 = dt.getHour();
    time_is_pm = (h24 >= 12);
    int h12 = h24;
    if (h12 > 12) h12 -= 12;
    else if (h12 == 0) h12 = 12;

    char buf[8];
    sprintf(buf, "%02d", dt.getDay()); lv_textarea_set_text(ta_day, buf);
    sprintf(buf, "%02d", dt.getMonth()); lv_textarea_set_text(ta_month, buf);
    sprintf(buf, "%04d", dt.getYear()); lv_textarea_set_text(ta_year, buf);
    sprintf(buf, "%02d", h12); lv_textarea_set_text(ta_hour, buf);
    sprintf(buf, "%02d", dt.getMinute()); lv_textarea_set_text(ta_min, buf);

    if(time_is_pm) {
        lv_label_set_text(lbl_ampm, "PM");
        lv_obj_set_style_bg_color(btn_ampm, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    } else {
        lv_label_set_text(lbl_ampm, "AM");
        lv_obj_set_style_bg_color(btn_ampm, lv_palette_main(LV_PALETTE_ORANGE), 0);
    }

    if(kb_time) lv_keyboard_set_textarea(kb_time, ta_day); 
}

void sw_ntp_event_cb(lv_event_t * e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    
    ntp_auto_update = is_on;

    prefs.begin("sys_config", false);
    prefs.putBool("ntp_auto", is_on);
    prefs.end();

    if(is_on) {
        lv_obj_add_flag(cont_manual_time, LV_OBJ_FLAG_HIDDEN);
        if(WiFi.status() == WL_CONNECTED) {
            configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
            trigger_weather_update = true; 
        }
    } else {
        lv_obj_clear_flag(cont_manual_time, LV_OBJ_FLAG_HIDDEN);
    }
}

void create_time_date_screen(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_add_event_cb(parent, time_screen_load_cb, LV_EVENT_SCREEN_LOADED, NULL);

    // Header
    lv_obj_t *btn_back = lv_btn_create(parent);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn_back, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Set Date & Time");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Auto Update Switch
    sw_ntp_auto = lv_switch_create(parent);
    lv_obj_set_size(sw_ntp_auto, 50, 25);
    lv_obj_align(sw_ntp_auto, LV_ALIGN_TOP_RIGHT, -20, 60); 
    lv_obj_add_event_cb(sw_ntp_auto, sw_ntp_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if(ntp_auto_update) lv_obj_add_state(sw_ntp_auto, LV_STATE_CHECKED);

    lv_obj_t *lbl_auto = lv_label_create(parent);
    lv_label_set_text(lbl_auto, "Auto Update:");
    lv_obj_set_style_text_color(lbl_auto, lv_color_white(), 0);
    lv_obj_align(lbl_auto, LV_ALIGN_TOP_RIGHT, -80, 62);

    // Manual Input Container
    cont_manual_time = lv_obj_create(parent);
    lv_obj_set_size(cont_manual_time, 480, 360);
    lv_obj_align(cont_manual_time, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(cont_manual_time, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_manual_time, 0, 0);
    lv_obj_clear_flag(cont_manual_time, LV_OBJ_FLAG_SCROLLABLE);

    if(ntp_auto_update) lv_obj_add_flag(cont_manual_time, LV_OBJ_FLAG_HIDDEN);

    // ROW 1: DATE
    lv_obj_t *lbl_date = lv_label_create(cont_manual_time);
    lv_label_set_text(lbl_date, "Date:");
    lv_obj_set_style_text_color(lbl_date, lv_color_white(), 0);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_LEFT, 5, 15);

    ta_day = lv_textarea_create(cont_manual_time);
    lv_obj_set_size(ta_day, 60, 45); lv_obj_align(ta_day, LV_ALIGN_TOP_LEFT, 60, 5);
    lv_textarea_set_one_line(ta_day, true); lv_textarea_set_max_length(ta_day, 2);
    lv_textarea_set_accepted_chars(ta_day, "0123456789"); lv_textarea_set_placeholder_text(ta_day, "DD");
    lv_obj_add_event_cb(ta_day, time_ta_event_cb, LV_EVENT_ALL, NULL);

    ta_month = lv_textarea_create(cont_manual_time);
    lv_obj_set_size(ta_month, 60, 45); lv_obj_align(ta_month, LV_ALIGN_TOP_LEFT, 130, 5);
    lv_textarea_set_one_line(ta_month, true); lv_textarea_set_max_length(ta_month, 2);
    lv_textarea_set_accepted_chars(ta_month, "0123456789"); lv_textarea_set_placeholder_text(ta_month, "MM");
    lv_obj_add_event_cb(ta_month, time_ta_event_cb, LV_EVENT_ALL, NULL);

    ta_year = lv_textarea_create(cont_manual_time);
    lv_obj_set_size(ta_year, 90, 45); lv_obj_align(ta_year, LV_ALIGN_TOP_LEFT, 200, 5);
    lv_textarea_set_one_line(ta_year, true); lv_textarea_set_max_length(ta_year, 4);
    lv_textarea_set_accepted_chars(ta_year, "0123456789"); lv_textarea_set_placeholder_text(ta_year, "YYYY");
    lv_obj_add_event_cb(ta_year, time_ta_event_cb, LV_EVENT_ALL, NULL);

    // ROW 2: TIME
    lv_obj_t *lbl_time = lv_label_create(cont_manual_time);
    lv_label_set_text(lbl_time, "Time:");
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 5, 75);

    ta_hour = lv_textarea_create(cont_manual_time);
    lv_obj_set_size(ta_hour, 60, 45); lv_obj_align(ta_hour, LV_ALIGN_TOP_LEFT, 60, 65);
    lv_textarea_set_one_line(ta_hour, true); lv_textarea_set_max_length(ta_hour, 2);
    lv_textarea_set_accepted_chars(ta_hour, "0123456789"); lv_textarea_set_placeholder_text(ta_hour, "HH");
    lv_obj_add_event_cb(ta_hour, time_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_colon = lv_label_create(cont_manual_time);
    lv_label_set_text(lbl_colon, ":");
    lv_obj_set_style_text_font(lbl_colon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_colon, lv_color_white(), 0);
    lv_obj_align(lbl_colon, LV_ALIGN_TOP_LEFT, 125, 72);

    ta_min = lv_textarea_create(cont_manual_time);
    lv_obj_set_size(ta_min, 60, 45); lv_obj_align(ta_min, LV_ALIGN_TOP_LEFT, 135, 65);
    lv_textarea_set_one_line(ta_min, true); lv_textarea_set_max_length(ta_min, 2);
    lv_textarea_set_accepted_chars(ta_min, "0123456789"); lv_textarea_set_placeholder_text(ta_min, "MM");
    lv_obj_add_event_cb(ta_min, time_ta_event_cb, LV_EVENT_ALL, NULL);

    // AM/PM Button
    btn_ampm = lv_btn_create(cont_manual_time);
    lv_obj_set_size(btn_ampm, 60, 45);
    lv_obj_align(btn_ampm, LV_ALIGN_TOP_LEFT, 210, 65);
    lv_obj_add_event_cb(btn_ampm, ampm_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_ampm = lv_label_create(btn_ampm);
    lv_label_set_text(lbl_ampm, "AM");
    lv_obj_center(lbl_ampm);

    // ROW 3: SAVE BUTTON (Bottom Center)
    lv_obj_t *btn_save = lv_btn_create(cont_manual_time);
    lv_obj_set_size(btn_save, 140, 50); 
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -10); 
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(btn_save, save_time_date_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *l_save = lv_label_create(btn_save);
    lv_label_set_text(l_save, "Save Time");
    lv_obj_center(l_save);

    // Keyboard
    kb_time = lv_keyboard_create(parent);
    lv_keyboard_set_mode(kb_time, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb_time, 480, 200);
    lv_obj_align(kb_time, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_time, ta_day); 
    lv_obj_add_flag(kb_time, LV_OBJ_FLAG_HIDDEN); 
    
    lv_obj_add_event_cb(kb_time, time_kb_event_cb, LV_EVENT_ALL, NULL);
}

/* ================= WEATHER ================= */
void fetch_weather_data() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Weather Skipped: No WiFi");
        return;
    }
    
    // Safety delay to let network stack settle
    delay(50); 
    
    HTTPClient http;
    WiFiClient client; 
    
    // --- 1. GET LOCATION ---
    Serial.println("Fetching Location...");
    // Set timeout to prevent blocking the core for too long
    http.setTimeout(3000); 
    
    http.begin(client, "http://ip-api.com/json/?fields=status,lat,lon,city");
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        // Use a smaller, efficient filter for JSON to save memory
        JsonDocument filter;
        filter["status"] = true;
        filter["lat"] = true;
        filter["lon"] = true;
        filter["city"] = true;
        
        JsonDocument docLoc;
        deserializeJson(docLoc, payload, DeserializationOption::Filter(filter));
        
        if (docLoc["status"] == "success") {
            geo_lat = docLoc["lat"];
            geo_lon = docLoc["lon"];
            const char* city = docLoc["city"];
            if (city) city_name = String(city);
            Serial.printf("Location Success: %s (%.4f, %.4f)\n", city_name.c_str(), geo_lat, geo_lon);
        } else {
            Serial.println("Location API status: fail");
        }
    } else {
        Serial.printf("Location HTTP Fail: %d\n", httpCode);
    }
    http.end();

    // --- 2. GET WEATHER ---
    if (geo_lat != 0.0) {
        String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(geo_lat) + 
                     "&longitude=" + String(geo_lon) + 
                     "&current_weather=true";
                     
        Serial.println("Fetching Weather...");
        http.begin(client, url);
        int wCode = http.GET();
        
        if (wCode == 200) {
            String payload = http.getString();
            
            // Filter again to save memory
            JsonDocument filter;
            filter["current_weather"]["temperature"] = true;
            filter["current_weather"]["weathercode"] = true;
            filter["current_weather"]["is_day"] = true;
            
            JsonDocument docWeather; 
            DeserializationError error = deserializeJson(docWeather, payload, DeserializationOption::Filter(filter));

            if (!error) {
                current_temp = docWeather["current_weather"]["temperature"];
                weather_code = docWeather["current_weather"]["weathercode"];
                is_day = docWeather["current_weather"]["is_day"];

                initial_weather_fetched = true; 
                String bigTempStr = String(current_temp, 0) + "°";
                if(sleep_status_lbl) lv_label_set_text(sleep_status_lbl, bigTempStr.c_str());
                update_weather_visuals();
                Serial.printf("Weather: %.1f C, Code: %d\n", current_temp, weather_code);
            } else {
                Serial.print("Weather JSON Fail: ");
                Serial.println(error.c_str());
            }
        } else {
            Serial.printf("Weather HTTP Fail: %d\n", wCode);
        }
        http.end();
    } else {
        Serial.println("Weather Skipped: Invalid Coordinates.");
    }
}

void update_weather_visuals() {
    lv_color_t color_top, color_bot, celestial_color;
    String conditionText = "Unknown";
    bool show_celestial = true;
    
    // 1. Determine Day/Night
    bool is_night = (is_day == 0);

    if (is_night) {
        // --- NIGHT THEMES ---
        color_bot = lv_color_black();
        celestial_color = lv_color_white(); // Moon is white
        
        if (weather_code == 0) {
            color_top = lv_palette_darken(LV_PALETTE_INDIGO, 4);
            conditionText = "Clear Night";
        } 
        else if (weather_code <= 3) {
            color_top = lv_palette_darken(LV_PALETTE_BLUE_GREY, 3);
            conditionText = "Cloudy Night";
            celestial_color = lv_palette_lighten(LV_PALETTE_GREY, 2); 
        }
        else {
            color_top = lv_palette_darken(LV_PALETTE_GREY, 4);
            conditionText = "Rainy Night";
            show_celestial = false; // Hide moon if raining
        }
    } 
    else {
        // --- DAY THEMES ---
        celestial_color = lv_palette_main(LV_PALETTE_YELLOW); // Sun is yellow
        
        if (weather_code == 0) { // Clear
            color_top = lv_palette_main(LV_PALETTE_ORANGE);
            color_bot = lv_palette_main(LV_PALETTE_RED); 
            conditionText = "Sunny";
        } 
        else if (weather_code <= 3) { // Cloudy
            color_top = lv_palette_lighten(LV_PALETTE_CYAN, 1);
            color_bot = lv_palette_main(LV_PALETTE_BLUE_GREY);
            conditionText = "Cloudy";
            celestial_color = lv_color_white(); 
        } 
        else if (weather_code >= 51) { // Rain
            color_top = lv_palette_darken(LV_PALETTE_GREY, 2);
            color_bot = lv_palette_darken(LV_PALETTE_BLUE_GREY, 3);
            conditionText = "Rain";
            show_celestial = false; 
        }
        else { 
            color_top = lv_palette_lighten(LV_PALETTE_BLUE, 1);
            color_bot = lv_palette_lighten(LV_PALETTE_GREY, 1);
            conditionText = "Normal";
        }
    }

    // 1. Update Background Gradient
    lv_style_set_bg_color(&style_weather_bg, color_top);
    lv_style_set_bg_grad_color(&style_weather_bg, color_bot);
    lv_obj_report_style_change(&style_weather_bg);
    
    // 2. Update Celestial Body (Sun/Moon)
    if (show_celestial) {
        if(celestial_body) lv_obj_clear_flag(celestial_body, LV_OBJ_FLAG_HIDDEN);
        if(celestial_glow) lv_obj_clear_flag(celestial_glow, LV_OBJ_FLAG_HIDDEN);
        
        if(celestial_body) lv_obj_set_style_bg_color(celestial_body, celestial_color, 0);
        if(celestial_glow) lv_obj_set_style_bg_color(celestial_glow, celestial_color, 0);
    } else {
        if(celestial_body) lv_obj_add_flag(celestial_body, LV_OBJ_FLAG_HIDDEN);
        if(celestial_glow) lv_obj_add_flag(celestial_glow, LV_OBJ_FLAG_HIDDEN);
    }
    
    // 3. Update Text
    lv_label_set_text(sleep_weather_lbl, conditionText.c_str());
}

/* ================= SETUP & LOOP ================= */

void setup() {
  Serial.begin(115200);
  Wire.begin(47, 48);
  Wire.setTimeOut(100);

  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81); Wire.write(0x40); Wire.write(0x01);
  Wire.endTransmission();
  delay(100);

  if (power.begin(Wire, AXP2101_SLAVE_ADDRESS, 47, 48)) {
    power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    power.clearIrqStatus();
    adcOn();
  }

  if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, 47, 48)) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
  }

  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);
  ledcAttach(LCD_BL_PIN, 5000, 8);
  ledcWrite(LCD_BL_PIN, 200);

  lv_init();
  lv_tick_set_cb([]{ return millis(); });

  screenWidth = gfx->width(); screenHeight = gfx->height();

  bufSize = screenWidth * 40; 
  disp_draw_buf = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  
  disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
  
  // 3. Init Inputs
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);

  load_settings();

  screen_home = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_home, lv_color_white(), 0);
  
  screen_notifications = lv_obj_create(NULL);
  screen_power = lv_obj_create(NULL);
  screen_settings_menu = lv_obj_create(NULL);
  screen_about = lv_obj_create(NULL);
  screen_wifi = lv_obj_create(NULL);
  screen_ha = lv_obj_create(NULL);
  screen_time_date = lv_obj_create(NULL);

  create_switch_grid(screen_home);
  create_page_dots(screen_home, 1);

  create_notifications_page(screen_notifications);
  create_power_screen(screen_power);
  create_settings_menu_screen(screen_settings_menu); 
  create_wifi_screen(screen_wifi);
  create_ha_screen(screen_ha); 
  create_about_screen(screen_about);
  create_time_date_screen(screen_time_date);
  create_sleep_screen();

  lv_obj_add_event_cb(screen_home, swipe_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(screen_notifications, swipe_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(screen_settings_menu, swipe_event_cb, LV_EVENT_GESTURE, NULL);

  lv_scr_load(screen_home);
  clock_label = lv_label_create(screen_home);
  lv_obj_set_style_text_color(clock_label, lv_color_black(), 0);
  lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_24, 0); 
  lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 10);

  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  if (!rtc.begin(Wire, 47, 48)) { rtc.begin(Wire, 47, 48); }

  if (wifi_enabled) {
      Serial.println("Restoring WiFi Connection...");
      WiFi.begin(wifi_ssid, wifi_pass);
      wifiClient.setTimeout(500);
      
      current_wifi_state = WIFI_CONNECTING; 
      wifi_connect_start = millis();
      
      if(lbl_wifi_status) {
         lv_label_set_text(lbl_wifi_status, "Status: Restoring connection...");
         lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
      }
  } else {
      WiFi.mode(WIFI_OFF);
      current_wifi_state = WIFI_IDLE;
  }
  
  if(wifi_enabled) {
     if(sw_wifi_enable) lv_obj_add_state(sw_wifi_enable, LV_STATE_CHECKED);
     if(cont_wifi_inputs) lv_obj_clear_flag(cont_wifi_inputs, LV_OBJ_FLAG_HIDDEN);
  } else {
     if(sw_wifi_enable) lv_obj_clear_state(sw_wifi_enable, LV_STATE_CHECKED);
     if(cont_wifi_inputs) lv_obj_add_flag(cont_wifi_inputs, LV_OBJ_FLAG_HIDDEN);
  }

  refresh_saved_wifi_list_ui();

  if (strlen(mqtt_host) > 0 && mqtt_port > 0) {
      mqtt.setServer(mqtt_host, mqtt_port);
      mqtt.setCallback(mqtt_callback);
  }
  mqtt_retry_count = 0; 
  last_touch_ms = millis();
}

void loop() {
  lv_timer_handler();
  delay(5);

  if (trigger_weather_update) {
      struct tm ti;
      if (getLocalTime(&ti, 100)) { 
          rtc.setDateTime(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
          if (lv_scr_act() == screen_time_date) time_screen_load_cb(NULL);
      }
      fetch_weather_data();
      trigger_weather_update = false;
  }

  if (notification_ui_dirty) {
      refresh_notification_list();
      notification_ui_dirty = false;
  }

  check_sensor_logic();

  unsigned long now = millis();
  if (now - last_touch_ms > SLEEP_TIMEOUT_MS) {
      if (lv_obj_has_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN)) {
          lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
          ledcWrite(LCD_BL_PIN, BACKLIGHT_DIM_LEVEL);
      }
  } else {
    if (!lv_obj_has_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
        ledcWrite(LCD_BL_PIN, 200);
        if(lv_scr_act() != screen_home) {
            if(msg_popup) { lv_obj_del(msg_popup); msg_popup = NULL; }
            if(kb_wifi) lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
            if(kb_time) lv_obj_add_flag(kb_time, LV_OBJ_FLAG_HIDDEN);
            if(scan_list_ui) lv_obj_add_flag(scan_list_ui, LV_OBJ_FLAG_HIDDEN);
            lv_scr_load_anim(screen_home, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
        }
    }
  }

  switch (current_wifi_state) {
    case WIFI_SCANNING: {
        lv_timer_handler(); 
        delay(50); 
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        delay(100);

        int n = WiFi.scanNetworks(false, false); 
        
        if(scan_list_ui) {
            lv_obj_clean(scan_list_ui);
            lv_obj_t * btn_cancel = lv_list_add_btn(scan_list_ui, LV_SYMBOL_CLOSE, " Close");
            lv_obj_set_style_bg_color(btn_cancel, lv_palette_main(LV_PALETTE_RED), 0); 
            lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e){ wipe_wifi_popup(); }, LV_EVENT_CLICKED, NULL);

            if (n == 0) {
                lv_list_add_text(scan_list_ui, "No networks found");
                if(lbl_wifi_status) lv_label_set_text(lbl_wifi_status, "Status: None Found");
            } else if (n > 0) {
                lv_list_add_text(scan_list_ui, "Select Network:");
                for (int i = 0; i < n; ++i) {
                    String ssidName = WiFi.SSID(i);
                    if(ssidName.length() > 0) {
                        lv_obj_t *btn = lv_list_add_btn(scan_list_ui, LV_SYMBOL_WIFI, ssidName.c_str());
                        lv_obj_add_event_cb(btn, wifi_list_btn_cb, LV_EVENT_CLICKED, NULL);
                    }
                }
                if(lbl_wifi_status) {
                    lv_label_set_text(lbl_wifi_status, "Status: Scan Complete");
                    lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_GREEN), 0);
                }
            } else {
                lv_list_add_text(scan_list_ui, "Scan Failed");
                if(lbl_wifi_status) lv_label_set_text(lbl_wifi_status, "Status: Error");
            }
        }
        WiFi.scanDelete(); 
        current_wifi_state = WIFI_IDLE; 
        break;
    }

    case WIFI_CONNECTING: {
        wl_status_t status = WiFi.status();
        
        if (status == WL_CONNECTED) {
            current_wifi_state = WIFI_CONNECTED;
            
            if(lbl_wifi_status) {
                lv_label_set_text(lbl_wifi_status, "Status: Connected");
                lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_GREEN), 0);
            }
            save_current_network_to_list();
            struct tm ti;
            if (ntp_auto_update && getLocalTime(&ti, 2000)) { 
                rtc.setDateTime(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
                trigger_weather_update = true;
            }
            if (strlen(mqtt_host) > 0) {
                mqtt_enabled = true;
                mqtt_retry_count = 0;
                if(sw_mqtt_enable) lv_obj_add_state(sw_mqtt_enable, LV_STATE_CHECKED);
                if(cont_ha_inputs) lv_obj_clear_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);
                prefs.begin("sys_config", false);
                prefs.putBool("mqtt_en", true);
                prefs.end();
            }
        } 
        else if (status == WL_CONNECT_FAILED) {
            Serial.println("WiFi Auth Failed. Disabling to prevent glitches.");
            
            WiFi.disconnect(); 
            
            current_wifi_state = WIFI_IDLE;
            last_wifi_check = millis(); 

            if(lbl_wifi_status) {
                lv_label_set_text(lbl_wifi_status, "Error: Wrong Password");
                lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_RED), 0);
            }
            
            show_notification_popup("Connection Failed:\nIncorrect Password.", -1);
        }
        else if (status == WL_NO_SSID_AVAIL) {
            current_wifi_state = WIFI_IDLE;
            WiFi.disconnect();
            if(lbl_wifi_status) {
                lv_label_set_text(lbl_wifi_status, "Error: SSID Not Found");
                lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_RED), 0);
            }
        }
        else if (millis() - wifi_connect_start > 15000) {
            current_wifi_state = WIFI_IDLE;
            WiFi.disconnect();
            if(lbl_wifi_status) {
                lv_label_set_text(lbl_wifi_status, "Error: Timeout");
                lv_obj_set_style_text_color(lbl_wifi_status, lv_palette_main(LV_PALETTE_RED), 0);
            }
        }
        break;
    }

    case WIFI_CONNECTED: {
        if(WiFi.status() != WL_CONNECTED) {
            current_wifi_state = WIFI_CONNECTING; 
            wifi_connect_start = millis(); 
            mqtt.disconnect(); 
        }
        break;
    }
      
    case WIFI_IDLE: {
        if (wifi_enabled && WiFi.status() != WL_CONNECTED) {
            if (millis() - last_wifi_check > WIFI_RECONNECT_INTERVAL) {
                last_wifi_check = millis();
                Serial.println("Auto-reconnecting WiFi...");
                WiFi.begin(wifi_ssid, wifi_pass);
                current_wifi_state = WIFI_CONNECTING;
                wifi_connect_start = millis();
                if(lbl_wifi_status) lv_label_set_text(lbl_wifi_status, "Status: Auto-Reconnecting...");
            }
        }
        break;
    }
    default: break;
  }

  // ================= MQTT LOGIC =================
  if (current_wifi_state == WIFI_CONNECTED && mqtt_enabled) {
    if (!mqtt.connected()) {
        if (millis() - last_mqtt_retry > 2000) {
            last_mqtt_retry = millis();
            
            if (mqtt_retry_count >= 3) {
                Serial.println(">> MQTT Failed 3 times. Disabling.");
                mqtt_enabled = false;
                mqtt_retry_count = 0; 
                prefs.begin("sys_config", false);
                prefs.putBool("mqtt_en", false);
                prefs.end();
                
                if(sw_mqtt_enable) lv_obj_clear_state(sw_mqtt_enable, LV_STATE_CHECKED);
                if(cont_ha_inputs) lv_obj_add_flag(cont_ha_inputs, LV_OBJ_FLAG_HIDDEN);
                if(lbl_ha_status) {
                    lv_label_set_text(lbl_ha_status, "Status: Disabled (Failed)");
                    lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_RED), 0);
                }
                add_notification("MQTT Failed: Disabled");
            } 
            else {
                mqtt_retry_count++;
                Serial.printf("MQTT Attempt %d/3...\n", mqtt_retry_count);
                if(lbl_ha_status) {
                    lv_label_set_text(lbl_ha_status, "Status: Connecting...");
                    lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
                }
                wifiClient.setTimeout(1000); 

                bool connected = false;
                if(strlen(mqtt_user) > 0) connected = mqtt.connect("esp32_panel", mqtt_user, mqtt_pass);
                else connected = mqtt.connect("esp32_panel");
                
                if(connected) {
                    Serial.println("MQTT Success!");
                    mqtt_retry_count = 0; 
                    for(int i=0; i<SWITCH_COUNT; i++) mqtt.subscribe(switches[i].topic_state);
                    mqtt.subscribe(TOPIC_NOTIFY);
                }
            }
        }
    } else {
        mqtt.loop();
        mqtt_retry_count = 0; 
    }
  }

  // --- UI UPDATES ---
  // Auto-refresh weather every 30 mins
  static uint32_t last_weather_update = 0;
  if (ntp_auto_update && WiFi.status() == WL_CONNECTED && (millis() - last_weather_update > 1800000)) {
      last_weather_update = millis();
      trigger_weather_update = true;
  }

  if (millis() - lastMillis > 1000) {
    lastMillis = millis();
    RTC_DateTime dt = rtc.getDateTime();
    if (lv_scr_act() == screen_about) update_about_text();
    
    int h = dt.getHour();
    const char* ampm = (h >= 12) ? "PM" : "AM";
    if (h == 0) h = 12; else if (h > 12) h -= 12;
    int m = dt.getMonth(); if (m < 1) m = 1; if (m > 12) m = 12;

    char buf[20], date[20];
    snprintf(buf, sizeof(buf), "%02d:%02d %s", h, dt.getMinute(), ampm);
    snprintf(date, sizeof(date), "%02d %s %04d", dt.getDay(), monthNames[m-1], dt.getYear());

    lv_label_set_text(clock_label, buf);

    if (!lv_obj_has_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN)) {
       lv_label_set_text(sleep_time_lbl, buf);
       lv_label_set_text(sleep_date_lbl, date);

       // 2. Update Weather Temp & Background
       if (initial_weather_fetched) {
           String bigTempStr = String(current_temp, 0) + "°";
           lv_label_set_text(sleep_status_lbl, bigTempStr.c_str());
        }

       // --- Notification Chip Logic ---
       int count = get_notification_count();
       lv_obj_t* notify_chip = (lv_obj_t*)lv_obj_get_user_data(sleep_notify_badge);
       
       if (count > 0) {
         if(notify_chip) lv_obj_clear_flag(notify_chip, LV_OBJ_FLAG_HIDDEN);
         String n = String(LV_SYMBOL_BELL) + "  " + String(count) + " Alerts";
         lv_label_set_text(sleep_notify_badge, n.c_str());
       } else {
         if(notify_chip) lv_obj_add_flag(notify_chip, LV_OBJ_FLAG_HIDDEN);
       }
       
       // --- NEW COLORFUL STATUS LOGIC ---
       
       // 1. WiFi Color Logic
       if(current_wifi_state == WIFI_CONNECTED) {
           lv_obj_set_style_text_color(status_wifi_icon, lv_color_white(), 0); // Green if OK
       } else if (current_wifi_state == WIFI_CONNECTING) {
           lv_obj_set_style_text_color(status_wifi_icon, lv_palette_main(LV_PALETTE_ORANGE), 0); // Orange if connecting
       } else {
           lv_obj_set_style_text_color(status_wifi_icon, lv_palette_main(LV_PALETTE_RED), 0); // Dim Grey if off
       }

       // 2. MQTT Color Logic
       if (mqtt_enabled) {
           if (mqtt.connected()) {
               lv_obj_set_style_text_color(status_mqtt_icon, lv_color_white(), 0); // Blue if Connected
           } else {
               lv_obj_set_style_text_color(status_mqtt_icon, lv_palette_main(LV_PALETTE_ORANGE), 0); // Orange if retrying
           }
       } else {
           lv_obj_set_style_text_color(status_mqtt_icon, lv_palette_main(LV_PALETTE_RED), 0); // Dark if disabled
       }
       
       // 3. Battery Color & Icon Logic
       if (power.isBatteryConnect()) {
           int pct = power.getBatteryPercent();
           String batText = "";
           
           // Color based on percentage
           if (pct > 95) {
               lv_obj_set_style_text_color(status_batt_icon, lv_color_white(), 0);
               batText = String(LV_SYMBOL_BATTERY_FULL);
           }
           else if (pct > 70 && pct <= 95) {
               lv_obj_set_style_text_color(status_batt_icon, lv_color_white(), 0);
               batText = String(LV_SYMBOL_BATTERY_3);
           }
           else if (pct > 40 && pct <= 70) {
               lv_obj_set_style_text_color(status_batt_icon, lv_color_white(), 0);
               batText = String(LV_SYMBOL_BATTERY_2);
           }
           else if (pct > 15 && pct <= 40) {
               lv_obj_set_style_text_color(status_batt_icon, lv_palette_main(LV_PALETTE_YELLOW), 0);
               batText = String(LV_SYMBOL_BATTERY_1);
           }
           else if (pct > 5 && pct <= 15) {
               lv_obj_set_style_text_color(status_batt_icon, lv_palette_main(LV_PALETTE_RED), 0);
               batText = String(LV_SYMBOL_BATTERY_1);
           } else {
               lv_obj_set_style_text_color(status_batt_icon, lv_palette_main(LV_PALETTE_RED), 0);
               batText = String(LV_SYMBOL_BATTERY_EMPTY);
           }
           
           if(power.isCharging()) {
               lv_obj_set_style_text_color(status_batt_icon, lv_palette_main(LV_PALETTE_YELLOW), 0);
               batText = String(LV_SYMBOL_CHARGE);
           }
           lv_label_set_text(status_batt_icon, batText.c_str());
           
       } else {
           // USB Powered
           lv_obj_set_style_text_color(status_batt_icon, lv_color_white(), 0);
           lv_label_set_text(status_batt_icon, LV_SYMBOL_USB);
       }
    }
  }

  // Update MQTT Status Label on HA Screen
  if (lv_scr_act() == screen_ha && lbl_ha_status) {
      if (!mqtt_enabled) {
            // Only update if it doesn't already say "Disabled (Error)"
            const char* cur_txt = lv_label_get_text(lbl_ha_status);
            if(strstr(cur_txt, "Error") == NULL) {
                lv_label_set_text(lbl_ha_status, "Status: Disabled");
                lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
            }
      } else {
          if (mqtt.connected()) {
              lv_label_set_text(lbl_ha_status, "Status: Connected");
              lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_GREEN), 0);
          } else {
              lv_label_set_text(lbl_ha_status, "Status: Connecting...");
              lv_obj_set_style_text_color(lbl_ha_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
          }
      }
  }
  
  if (lv_scr_act() == screen_power) {
      String info = "";
      info += "POWER STATUS:\n";
      bool isPluggedIn = (power.getVbusVoltage() > 4000);
      bool isBatteryConnected = power.isBatteryConnect();
      if (isPluggedIn) {
         info += "Source: USB Power\n";
         if (isBatteryConnected) {
             info += "Battery: " + String(power.getBatteryPercent()) + "%\n";
             if (power.isCharging()) info += "Status: Charging ⚡\n";
             else info += "Status: Fully Charged\n";
         } else {
             info += "Battery: Disconnected\n";
             info += "Status: System Active\n";
         }
         info += "VBUS: " + String(power.getVbusVoltage()) + " mV\n";
      } else {
         info += "Source: Battery\n";
         if (isBatteryConnected) {
             info += "Level: " + String(power.getBatteryPercent()) + "%\n";
             info += "Voltage: " + String(power.getBattVoltage()) + " mV\n";
             info += "Status: Discharging\n";
         } else {
             info += "Status: No Power Source?\n";
         }
      }
      info += "\nNETWORK STATUS:\n";
      if(current_wifi_state == WIFI_CONNECTED) {
          info += "WiFi: Connected\n";
          info += "SSID: " + WiFi.SSID() + "\n";
          info += "IP: " + WiFi.localIP().toString() + "\n";
          long rssi = WiFi.RSSI();
          int quality = 2 * (rssi + 100);
          if (quality > 100) quality = 100; if (quality < 0) quality = 0;
          info += "Signal: " + String(quality) + "%\n";
      } else {
          info += "WiFi: Disconnected\n";
      }
      info += "\nMQTT STATUS:\n";
      if (!mqtt_enabled) {
          info += "State: Disabled\n";
      } else if (mqtt.connected()) {
          info += "State: Connected\n";
          info += "Broker: " + String(mqtt_host) + "\n";
      } else {
          info += "State: Connecting...\n";
      }
      lv_label_set_text(power_info_label, info.c_str());
  }
}