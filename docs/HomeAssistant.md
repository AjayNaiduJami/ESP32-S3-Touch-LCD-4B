# Home Assistant Integration Guide

This guide explains how to link your ESP32 Control Panel to Home Assistant.

## Prerequisites
1.  **MQTT Broker:** You must have an MQTT broker (like Mosquitto) running in Home Assistant.
2.  **The Panel:** Your ESP32 device must be flashed with version v1.2.0 or higher, connected to WiFi, and connected to your MQTT broker (Status: Connected on the Settings screen).

---

## Step 1: Define Your Layout (One-Time Setup)

You do not need to code C++ to change buttons. You send a "Configuration Payload" via MQTT.

1.  Go to **Home Assistant** > **Developer Tools** > **Actions**.
2.  Select Service: `MQTT: Publish`.
3.  Enter the Topic: `ha/panel/config/set`.
4.  Check the **Retain** box (Important! This ensures the panel loads the config after a reboot).
5.  Paste the JSON below into the **Payload** section and modify names/entities as needed.

### Sample Configuration Payload
```json
{
  "buttons": [
    {"name": "Kitchen", "entity": "light.kitchen_main", "icon": "light", "room": "Kitchen", "state": "OFF"},
    {"name": "Garage",  "entity": "cover.garage_door",  "icon": "power", "room": "Garage", "state": "OFF"},
    {"name": "AC",      "entity": "climate.living_room","icon": "fan", "room": "Living Room", "state": "OFF"},
    {"name": "Movie",   "entity": "scene.movie_night",  "icon": "light", "room": "Master BedRoom", "state": "OFF"},
    {"name": "Table",   "entity": "light.dining_table", "icon": "light", "room": "Dining Room", "state": "OFF"},
    {"name": "Plug",    "entity": "switch.smart_plug",  "icon": "power", "room": "Work Room", "state": "OFF"}
  ]
}
```

- **Icons:** You can use "light", "fan", "ac", "radiator", "tv", "dryer", "garage", "washer", "speaker", "socket", "power"
- **Max Buttons:** 6

*Note*: **The reason we added state in config json:** When the screen layout is rebuilt, all buttons are destroyed and recreated. If we didn't read the state from the config JSON immediately, all switches would default to OFF. They would stay OFF for a split second (or longer) until the separate state/update messages arrive via MQTT. This would cause your dashboard to "flicker" (buttons flipping from Off -> On) every time the config refreshes.

## Step 2: Create the Automations

You need two automations in Home Assistant to handle the logic. Copy these into your automations.yaml or create them via the UI.

### Automation A: The Controller (Panel -> HA)

This listens for button presses on the panel and toggles the device in HA.

```yaml
alias: "ESP32 Panel Controller"
description: "Receives commands from ESP32 Panel and toggles entities"
mode: queued
trigger:
  - platform: mqtt
    topic: "ha/panel/command"
action:
  - service: homeassistant.toggle
    target:
      entity_id: "{{ trigger.payload_json.entity_id }}"
```

### Automation B: The Feedback Loop (HA -> Panel)

This watches your devices. If you turn on a light via the HA App or Alexa, this automation tells the Panel to turn the button Orange.

Note: You must list every entity you used in Step 1 in the entity_id section below.

```yaml
alias: "ESP32 Panel Feedback"
description: "Pushes real-time state changes back to the panel"
mode: queued
trigger:
  - platform: state
    entity_id:
      - light.kitchen_main
      - cover.garage_door
      - climate.living_room
      - scene.movie_night
      - light.dining_table
      - switch.smart_plug
      # Add all your panel entities here
action:
  - service: mqtt.publish
    data:
      topic: "ha/panel/state/update"
      payload: >
        {
          "entity_id": "{{ trigger.entity_id }}",
          "state": "{{ trigger.to_state.state }}"
        }
```

### Automation C. The Boot Sync (Panel Requests States)

Ensures the panel gets the correct color/state immediately after a reboot.

```yaml
alias: "ESP32 Panel Sync"
description: "Sends current states of all devices when panel boots up"
mode: queued
trigger:
  - platform: mqtt
    topic: "ha/panel/sync"
action:
  # Loop through your entities and send their current state
  - repeat:
      for_each:
        - light.kitchen_main
        - cover.garage_door
        - climate.living_room
        - scene.movie_night
        - light.dining_table
        - switch.smart_plug
        # Add all your panel entities here
      sequence:
        - service: mqtt.publish
          data:
            topic: "ha/panel/state/update"
            payload: |
              {
                "entity_id": "{{ repeat.item }}",
                "state": "{{ states(repeat.item) }}"
              }
```

### Step 3: Sending Notifications (Optional)

To send a popup notification to the panel, simply publish to the notify topic.

* **Topic:** ha/panel/notify
* **Payload:** (Raw Text) Front Door is Open!

**Example Automation:**
```yaml
alias: Notify Panel on Doorbell
trigger:
  - platform: state
    entity_id: binary_sensor.doorbell
    to: 'on'
action:
  - service: mqtt.publish
    data:
      topic: "ha/panel/notify"
      payload: "Ding Dong! Someone is at the door."
```