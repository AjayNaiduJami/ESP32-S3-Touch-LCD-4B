#ifndef UI_LOGIC_H
#define UI_LOGIC_H

#include "ui.h"
#include "ui_comp.h"
#include <ArduinoJson.h>
#include <vector>

// --- EXTERNS (Variables from main.ino we need access to) ---
extern PubSubClient mqtt;
extern void show_notification_popup(const char* text, int index);

// --- CONFIGURATION ---
#define COLOR_YELLOW      0xFEC106
#define COLOR_GREY_ICON   0x323232
#define COLOR_BLUE_ACTIVE 0x28A0FB
#define COLOR_BLACK_BG    0x000000

// --- STATE ---
String current_room_filter = "My Home"; 

// --- HELPER: Map String to Image Asset ---
const void* get_icon_by_name(const char* icon_name) {
    if (strcmp(icon_name, "light") == 0) return &ui_img_253037777; // lightbulb.png
    if (strcmp(icon_name, "fan") == 0)   return &ui_img_1332138075; // fan.png
    if (strcmp(icon_name, "ac") == 0)    return &ui_img_133537992;  // ac.png
    if (strcmp(icon_name, "tv") == 0)    return &ui_img_435491338;  // tv.png
    // Add more mappings as needed from your assets
    return &ui_img_253037777; // Default
}

// --- HELPER: Visual Switch Styling ---
void update_switch_visuals(lv_obj_t* sw_comp) {
    bool is_checked = lv_obj_has_state(sw_comp, LV_STATE_CHECKED);
    lv_obj_t* icon_cont = ui_comp_get_child(sw_comp, UI_COMP_COMPSWITCH_ICC1);
    lv_obj_t* icon_img  = ui_comp_get_child(sw_comp, UI_COMP_COMPSWITCH_ICC1_ICP1);

    if (is_checked) {
        // ON STATE: Yellow Theme
        lv_obj_set_style_border_color(sw_comp, lv_color_hex(COLOR_YELLOW), LV_PART_MAIN);
        lv_obj_set_style_border_width(sw_comp, 2, LV_PART_MAIN);
        if(icon_cont) lv_obj_set_style_bg_color(icon_cont, lv_color_hex(COLOR_YELLOW), LV_PART_MAIN);
        if(icon_img) {
            lv_obj_set_style_img_recolor(icon_img, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_img_recolor_opa(icon_img, 255, LV_PART_MAIN);
        }
    } else {
        // OFF STATE: Dark Theme
        lv_obj_set_style_border_color(sw_comp, lv_color_hex(0x323232), LV_PART_MAIN);
        lv_obj_set_style_border_width(sw_comp, 1, LV_PART_MAIN);
        if(icon_cont) lv_obj_set_style_bg_color(icon_cont, lv_color_hex(COLOR_GREY_ICON), LV_PART_MAIN);
        if(icon_img) {
            lv_obj_set_style_img_recolor(icon_img, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            lv_obj_set_style_img_recolor_opa(icon_img, 255, LV_PART_MAIN);
        }
    }
}

// --- EVENT: Switch Toggled ---
void on_switch_toggle(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    bool is_on = lv_obj_has_state(btn, LV_STATE_CHECKED);
    update_switch_visuals(btn);

    // Retrieve Entity ID stored in user_data
    const char* entity_id = (const char*)lv_obj_get_user_data(btn);
    if (entity_id) {
        JsonDocument doc;
        doc["entity_id"] = entity_id;
        doc["action"] = is_on ? "turn_on" : "turn_off";
        char buffer[128];
        serializeJson(doc, buffer);
        mqtt.publish("ha/panel/command", buffer);
    }
}

// --- LOGIC: Apply Room Filter ---
void apply_switch_filter() {
    uint32_t count = lv_obj_get_child_cnt(ui_haswC);
    for(uint32_t i=0; i<count; i++) {
        lv_obj_t* sw = lv_obj_get_child(ui_haswC, i);
        lv_obj_t* room_lbl = ui_comp_get_child(sw, UI_COMP_COMPSWITCH_RM1);
        if(!room_lbl) continue;
        
        String room = String(lv_label_get_text(room_lbl));
        if (current_room_filter == "My Home" || room == current_room_filter) {
            lv_obj_clear_flag(sw, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sw, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// --- EVENT: Room Chip Clicked ---
void on_room_click(lv_event_t* e) {
    lv_obj_t* clicked_chip = lv_event_get_target(e);
    uint32_t count = lv_obj_get_child_cnt(ui_rmC);
    
    for(uint32_t i=0; i<count; i++) {
        lv_obj_t* chip = lv_obj_get_child(ui_rmC, i);
        if (chip == clicked_chip) {
            lv_obj_add_state(chip, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BLUE_ACTIVE), LV_PART_MAIN);
            lv_obj_t* label = ui_comp_get_child(chip, UI_COMP_COMPROOM_RMT1);
            current_room_filter = String(lv_label_get_text(label));
        } else {
            lv_obj_clear_state(chip, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BLACK_BG), LV_PART_MAIN);
        }
    }
    apply_switch_filter();
}

// --- EVENT: Arrow Scroll ---
void on_arrow_click(lv_event_t* e) {
    lv_coord_t max_x = lv_obj_get_scroll_right(ui_rmC);
    if (max_x < 5) lv_obj_scroll_to_x(ui_rmC, 0, LV_ANIM_ON);
    else lv_obj_scroll_by(ui_rmC, 100, 0, LV_ANIM_ON);
}

// --- MAIN: Refresh UI from JSON ---
void refresh_ui_data(const char* json_payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json_payload);
    if (error) { Serial.println("JSON Error"); return; }

    JsonArray switches = doc["switches"]; // Expecting new format
    if (switches.isNull()) { 
        // Fallback for old format if necessary, or show error
        return; 
    }

    lv_obj_clean(ui_haswC);
    std::vector<String> rooms;
    rooms.push_back("My Home");

    for (JsonObject sw : switches) {
        const char* name = sw["name"];
        const char* room = sw["room"];
        const char* entity = sw["entity_id"];
        const char* icon = sw["icon"];
        const char* state = sw["state"]; // "ON" or "OFF"

        // 1. Create Switch
        lv_obj_t* sw_btn = ui_CompSwitch_create(ui_haswC);
        
        // 2. Set User Data (Entity ID for callbacks)
        // Note: Allocating memory for string. Ideally clear this when cleaning list.
        char* entity_store = strdup(entity); 
        lv_obj_set_user_data(sw_btn, (void*)entity_store);

        // 3. Set Text
        lv_label_set_text(ui_comp_get_child(sw_btn, UI_COMP_COMPSWITCH_SWN1), name);
        lv_label_set_text(ui_comp_get_child(sw_btn, UI_COMP_COMPSWITCH_RM1), room);

        // 4. Set Icon
        lv_obj_t* img = ui_comp_get_child(sw_btn, UI_COMP_COMPSWITCH_ICC1_ICP1);
        if(img) lv_img_set_src(img, get_icon_by_name(icon));

        // 5. Set State
        if (strcasecmp(state, "ON") == 0) lv_obj_add_state(sw_btn, LV_STATE_CHECKED);
        update_switch_visuals(sw_btn);

        // 6. Events
        lv_obj_add_event_cb(sw_btn, on_switch_toggle, LV_EVENT_CLICKED, NULL);

        // 7. Collect Room
        bool exists = false;
        for(String r : rooms) if(r == room) exists = true;
        if(!exists) rooms.push_back(String(room));
    }

    // Populate Rooms
    lv_obj_clean(ui_rmC);
    for (String r : rooms) {
        lv_obj_t* chip = ui_CompRoom_create(ui_rmC);
        lv_label_set_text(ui_comp_get_child(chip, UI_COMP_COMPROOM_RMT1), r.c_str());
        
        if (r == current_room_filter) {
            lv_obj_add_state(chip, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BLUE_ACTIVE), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BLACK_BG), LV_PART_MAIN);
        }
        lv_obj_add_event_cb(chip, on_room_click, LV_EVENT_CLICKED, NULL);
    }
    
    // Check Arrow Visibility (delayed check usually better, but simple check here)
    lv_obj_update_layout(ui_rmC);
    if (lv_obj_get_scroll_width(ui_rmC) > lv_obj_get_width(ui_rmC)) {
        lv_obj_clear_flag(ui_rmPe, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui_rmPe, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- UPDATE STATE ONLY (Optimization) ---
void update_device_state(const char* entity_id, bool is_on) {
    uint32_t count = lv_obj_get_child_cnt(ui_haswC);
    for(uint32_t i=0; i<count; i++) {
        lv_obj_t* sw = lv_obj_get_child(ui_haswC, i);
        const char* id = (const char*)lv_obj_get_user_data(sw);
        if (id && strcmp(id, entity_id) == 0) {
            if (is_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
            else lv_obj_clear_state(sw, LV_STATE_CHECKED);
            update_switch_visuals(sw);
            return; 
        }
    }
}

void setup_ui_logic() {
    if(ui_rmPe) lv_obj_add_event_cb(ui_rmPe, on_arrow_click, LV_EVENT_CLICKED, NULL);
}

#endif