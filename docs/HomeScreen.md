
# Home Screen & UI Logic Documentation

## 1. Overview

The **Home Screen** is the primary dashboard for the ESP32-S3 Touch Panel. It is designed to be a **dynamic, data-driven interface** that renders smart home controls based on configuration data received remotely via MQTT.

Unlike static screens, this dashboard clears and rebuilds itself whenever a new configuration is pushed, allowing for complete flexibility without changing firmware code.

---

## 2. Dynamic Grid System

The core of the Home Screen is a **2x3 Grid Layout** that hosts switch widgets.

### **2.1. Generation Logic**

* **Input Source:** The system listens for a JSON payload on the MQTT topic `ha/panel/config/set`.
* **Parsing:** It extracts a list of devices (buttons/switches).
* **Limit:** The grid accommodates up to **6 items** efficiently on the screen.
* **Rendering:**
    * If the list is empty, a "No Data" placeholder is shown.
    * If data exists, the placeholder is hidden, and the grid is populated.

### **2.2. Switch Anatomy**

Each switch is manually constructed using code (bypassing generated wrappers for stability) and consists of three layers:

1. **Card Container:**
    * A rounded rectangle serving as the touch target.
    * **Visual State:**
        * **OFF:** Dark Grey/Black background with a subtle border.
        * **ON:** Highlighted with a Yellow border to indicate activity.

2. **Icon Group (Top-Left):**
    * A circular container holding the device icon (e.g., Light, Fan, AC).
    * **Visual State:**
        * **OFF:** Black circle with a Grey icon.
        * **ON:** Yellow circle with a Yellow icon (high contrast).

3. **Labels:**
    * **Device Name:** Large, legible text at the bottom-left (e.g., "Living Room").
    * **Room Name:** Smaller, secondary text at the top-right (e.g., "Main Lights").


### **2.3. Interaction & Control**

* **Touch Action:** Tapping anywhere on the card toggles the state.
* **Command Logic:**
    * The system determines the `entity_id` associated with the button.
    * It sends an MQTT message to `ha/panel/command` with the payload `{"action": "turn_on"}` or `{"action": "turn_off"}`.
    * *Special handling exists for Covers (Blinds) and Scenes.*

---

## 3. Room Filtering (Chip Navigation)

To manage multiple devices, the system automatically categorizes them into "Rooms" displayed as a horizontal scrollable list at the top.

### **3.1. Auto-Categorization**

* The system scans the incoming configuration for unique "Room" names.
* It automatically creates a **"My Home"** (or "All") filter as the default first item.
* It then creates a "Chip" (pill-shaped button) for every unique room found.

### **3.2. Filtering Logic**

* **Selection:** Tapping a room chip highlights it in **Blue**.
* **Visibility:**
    * If "My Home" is selected, **all** switches are shown.
    * If a specific room is selected, only switches matching that room name remain visible; others are hidden instantly.



### **3.3. Smart Navigation (Arrow Indicator)**

* **Overflow Detection:** The system calculates if the total width of the room chips exceeds the screen width.
* **Arrow Visibility:** A small arrow appears on the right side *only* if there are hidden items off-screen.
* **Toggle Behavior:**
    * **Click 1:** Scrolls the list to the far **End** and rotates the arrow 180° (pointing left).
    * **Click 2:** Scrolls the list back to the **Start** and resets the arrow rotation.

---

## 4. State Synchronization

The UI maintains synchronization with the real world using a two-layer approach.

### **4.1. Initial State (Anti-Flicker)**

* When the grid is first built, it reads the `"state": "ON/OFF"` field from the configuration JSON.
* The switches are rendered in their correct color immediately. This prevents the UI from appearing "OFF" for a split second before the live data arrives.

### **4.2. Live Updates**

* The system listens to a separate MQTT topic: `ha/panel/state/update`.
* When a state change is received (e.g., a light is turned on via a physical switch or phone app):
1. The system scans the current grid for the matching `entity_id`.
2. It updates the visual style (Icon color, Border color) of that specific button **without** reloading the entire screen.


---

## 5. Visual Customization

The interface adapts to the environment using sensor data.

### **5.1. Day/Night Cycle**

* **Trigger:** Weather data updates (via API).
* **Behavior:**
    * **Day:** The Home Screen background uses a brighter, daylight-themed image.
    * **Night:** The background switches to a darker, night-themed image to reduce glare.


### **5.2. Styling Standards**

* **Colors:**
* **Active:** `0xFEC106` (Yellow)
* **Inactive:** `0xD6D6D6` (Light Grey) / `0x000000` (Black)
* **Focus/Select:** `0x28A0FB` (Blue)
* **Fonts:** Uses Montserrat 14 (Standard) and Montserrat 16 (Headers) for readability.

---

## 6. Stability & Safety

The logic includes specific protections for the ESP32 hardware.

* **Heap Protection:** Large JSON configuration files are allocated on the Heap (RAM) rather than the Stack to prevent system crashes (Stack Overflow) during parsing.
* **Memory Cleanup:** Before building a new grid, the system runs a "Garbage Collection" routine to explicitly free the memory used by the previous buttons' text and IDs.
* **Watchdog Prevention:** During the creation of heavy UI elements (like a loop creating 10 buttons), the system intentionally pauses (`yield`) for a few milliseconds to let the processor handle background tasks (WiFi/Touch), preventing freezes.