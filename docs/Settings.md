# Settings Page Documentation

The Settings interface is the central hub for configuring the ESP32 Smart Home Panel. It is built using the **LVGL** graphics library and organizes configuration into a scrollable list of sub-menus.

**Entry Point:** `create_settings_menu_screen()`
**Event Handler:** `settings_menu_event_cb()`

## 1. WiFi Setup

**Function:** `create_wifi_screen()`
**Storage Namespace:** `sys_config` (Enable/Creds), `wifi_db` (Saved Networks)

This screen manages wireless connectivity.

### Components

* **Enable Toggle (`sw_wifi_enable`):**
    * **Logic:** When toggled ON, it unhides input fields and attempts to connect using stored credentials. When OFF, it disconnects WiFi, turns off the radio mode, and hides inputs.

* **SSID & Password Fields:**
    * Text areas for manual entry. Clicking them opens the on-screen keyboard (`kb_wifi`).

* **Scan Button:**
    * **Action:** Triggers `WiFi.scanNetworks()`.
    * **UI:** Popups a list (`scan_list_ui`) of available networks. Clicking a network auto-fills the SSID field.

* **Join Button:**
    * **Action:** Saves credentials to NVS and initiates connection (`WiFi.begin()`).


* **Saved Networks List:**
    * **Logic:** Displays up to 5 previously successful networks stored in NVS. Clicking one auto-fills credentials.

---

## 2. Home Assistant

**Function:** `create_ha_screen()`
**Storage Namespace:** `sys_config`

Configures the connection to the Home Assistant MQTT Broker.

### Components

* **Enable Toggle (`sw_mqtt_enable`):**
    * **Logic:** If WiFi is disconnected, this automatically turns off with an error popup.

* **Connection Details:**
    * **Host:** IP address or hostname of the broker.
    * **Port:** Default is `1883`.
    * **User/Pass:** Optional authentication.
    * **Notify Topic:** Topic to listen for text notifications (default: `ha/panel/notify`).

* **Save Settings Button:**
    * **Action:** Saves details to NVS and immediately attempts to connect/reconnect to the broker.

* **Reset Layout Button (New):**
    * **Action:** Calls `reset_grid_to_defaults()`.
    * **Logic:** Wipes the `grid_cfg` namespace in NVS and resets the main screen buttons to their default "Unset" state.

---

## 3. Display

**Function:** `create_display_screen()`
**Storage Namespace:** `disp_config`

Manages screen brightness and power-saving features.

### Components

* **Brightness Slider:**
    * **Range:** 5% to 100%.
    * **Logic:** Directly controls the PWM duty cycle on `LCD_BL_PIN` (Pin 4) via `ledcWrite`.

* **Screensaver Timeout:**
    * **Options:** 15s, 30s, 1m, 2m, 5m, 10m, Never.
    * **Logic:** Time before the screen dims to 0% brightness (but remains active).

* **Deep Sleep Timeout:**
    * **Options:** Same as above.
    * **Logic:** Time before the ESP32 enters deep sleep or turns off the backlight completely to save battery/power.

* **Logic Validation:**
    * When saving, the code ensures **Deep Sleep Time > Screensaver Time**. If invalid (e.g., Sleep is 15s but Saver is 30s), it auto-corrects and shows a warning popup.

---

## 4. Time & Date

**Function:** `create_time_date_screen()`
**Storage Namespace:** `sys_config` (NTP setting)

Manages the system clock, which syncs with the hardware RTC (`PCF85063`).

### Components

* **Auto Sync Toggle (`sw_ntp_auto`):**
    * **ON:** Hides manual inputs. Fetches time from NTP (`pool.ntp.org`) or `timeapi.io` based on location.
    * **OFF:** Shows manual input fields.

* **Manual Inputs:**
    * Day, Month, Year, Hour, Minute.
    * **Save Action:** Writes the specific values directly to the I2C RTC via `rtc.setDateTime()`.

---

## 5. Location

**Function:** `create_location_screen()`
**Storage Namespace:** `loc_config`

Determines location for Weather and Timezone data.

### Components

* **Auto (IP) Toggle:**
    * **Action:** Uses `http://ip-api.com/json` to detect location based on public IP.

* **Manual Search:**
    * **Input:** City name.
    * **Action:** Queries `geocoding-api.open-meteo.com`.
    * **Logic:** returns the first match's Latitude/Longitude and saves it to NVS.

* **Save Location Button:**
    * **Action:** Commits the Lat/Lon and City Name to memory and triggers a weather refresh (`trigger_weather_update`).

---

## 6. System Status

**Function:** `create_power_screen()`
**Logic Source:** `update_power_screen_ui()` loop

A read-only information screen useful for debugging.

### Displayed Information

* **Power:**
    * Source: Battery or USB (VBUS > 4V).
    * Battery Percentage & Voltage (via `AXP2101` PMU).
    * Charging Status.

* **Network:**
    * WiFi SSID & Local IP.
    * Signal Strength (RSSI) converted to %.


* **MQTT:**
    * Connection State (Connected/Connecting/Disabled).
    * Broker IP.

---

## 7. About Device

**Function:** `create_about_screen()`
**Storage Namespace:** `sys_config` (`dev_name`)

### Components

* **Device Name Input:**
    * Allows renaming the device (e.g., "Kitchen Panel").
    * **Logic:** Sets `WiFi.setHostname()` and includes this name in MQTT Discovery payloads.

* **Hardware Info:**
    * Displays static info about the board components (Touch: GT911, PMU: AXP2101, etc.).

---

### General Logic Notes

* **Persistence:** All settings use the `Preferences.h` library (NVS) to survive reboots.
* **Navigation:** The "Back" button on all sub-screens triggers `back_event_cb`, which loads the parent settings menu animation (`LV_SCR_LOAD_ANIM_MOVE_RIGHT`).
* **Overlays:** A global `loader_overlay` is used to block UI interaction during network operations (like Scanning WiFi or Connecting MQTT).