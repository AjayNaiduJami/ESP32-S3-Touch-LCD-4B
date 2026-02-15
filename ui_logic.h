#ifndef UI_LOGIC_H
#define UI_LOGIC_H

#include "ui.h"
#include "ui_comp.h"
#include <ArduinoJson.h>

extern PubSubClient mqtt;
extern void show_notification_popup(const char* text, int index);

// --- VISUAL CONSTANTS ---
#define COLOR_ACTIVE_YELLOW  0xFEC106 
#define COLOR_INACTIVE_GREY  0xD6D6D6 
#define COLOR_BG_BLACK       0x000000
#define COLOR_TEXT_WHITE     0xFFFFFF
#define COLOR_BLUE_ACTIVE    0x28A0FB

// Dimensions based on your snippet
#define SW_WIDTH   205 
#define SW_HEIGHT  70

String current_room_filter = "All"; 
bool is_first_ui_update = true;

// --- ICON MAPPING ---
const void* get_icon_by_name(const char* icon_name) {
    if (icon_name == NULL) return &ui_img_252433816; 
    if (strcmp(icon_name, "light") == 0)   return &ui_img_253037777;   
    if (strcmp(icon_name, "fan") == 0)     return &ui_img_1332138075; 
    if (strcmp(icon_name, "ac") == 0)      return &ui_img_133537992;  
    if (strcmp(icon_name, "radiator") == 0) return &ui_img_979047894; 
    if (strcmp(icon_name, "tv") == 0)      return &ui_img_435491338;  
    if (strcmp(icon_name, "dryer") == 0)   return &ui_img_827440816;  
    if (strcmp(icon_name, "garage") == 0)  return &ui_img_175529197;  
    if (strcmp(icon_name, "washer") == 0)  return &ui_img_1965837174; 
    if (strcmp(icon_name, "speaker") == 0) return &ui_img_285837919;  
    if (strcmp(icon_name, "socket") == 0)  return &ui_img_450969811;  
    if (strcmp(icon_name, "power") == 0)   return &ui_img_252433816;  
    return &ui_img_252433816; 
}

// --- UPDATE STYLES (Toggle Logic) ---
void update_manual_switch_visuals(lv_obj_t* btn, bool is_on) {
    // Structure:
    // Child 0: Icon Container (cui_icc1)
    //    `-> Child 0: Icon Object (cui_icp1)
    
    if (lv_obj_get_child_cnt(btn) < 1) return;

    lv_obj_t* icon_cont = lv_obj_get_child(btn, 0);
    if(lv_obj_get_child_cnt(icon_cont) < 1) return;
    lv_obj_t* icon_obj = lv_obj_get_child(icon_cont, 0);

    if (is_on) {
        // ACTIVE: Icon BG Yellow, Icon Black
        lv_obj_set_style_bg_color(icon_cont, lv_color_hex(COLOR_ACTIVE_YELLOW), LV_PART_MAIN);
        
        // Recolor the Background Image of the inner object
        lv_obj_set_style_bg_image_recolor(icon_obj, lv_color_hex(COLOR_ACTIVE_YELLOW), LV_PART_MAIN);
        lv_obj_set_style_bg_image_recolor_opa(icon_obj, 255, LV_PART_MAIN);
    } else {
        // INACTIVE: Icon BG Black, Icon Grey
        lv_obj_set_style_bg_color(icon_cont, lv_color_hex(COLOR_BG_BLACK), LV_PART_MAIN);
        
        lv_obj_set_style_bg_image_recolor(icon_obj, lv_color_hex(COLOR_INACTIVE_GREY), LV_PART_MAIN);
        lv_obj_set_style_bg_image_recolor_opa(icon_obj, 255, LV_PART_MAIN);
    }
}

// --- CLICK EVENT ---
void on_manual_switch_toggle(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    const char* entity_id = (const char*)lv_event_get_user_data(e);
    
    bool was_on = lv_obj_has_state(btn, LV_STATE_CHECKED);
    if(was_on) lv_obj_clear_state(btn, LV_STATE_CHECKED);
    else lv_obj_add_state(btn, LV_STATE_CHECKED);
    
    bool is_on = !was_on;
    update_manual_switch_visuals(btn, is_on);

    if (entity_id) {
        JsonDocument* doc = new JsonDocument();
        (*doc)["entity_id"] = entity_id;
        (*doc)["action"] = is_on ? "turn_on" : "turn_off";
        if (strstr(entity_id, "cover.")) (*doc)["action"] = is_on ? "open_cover" : "close_cover";
        if (strstr(entity_id, "scene.")) (*doc)["action"] = "turn_on"; 
        
        char buffer[128];
        serializeJson(*doc, buffer);
        if (mqtt.connected()) mqtt.publish("ha/panel/command", buffer);
        delete doc;
    }
}

// --- FILTERING ---
void apply_switch_filter() {
    if (!ui_haswC) return;
    uint32_t count = lv_obj_get_child_cnt(ui_haswC);
    for(uint32_t i=0; i<count; i++) {
        lv_obj_t* btn = lv_obj_get_child(ui_haswC, i);
        // Room Label is Child index 2
        if (lv_obj_get_child_cnt(btn) < 3) continue; 
        
        lv_obj_t* room_lbl = lv_obj_get_child(btn, 2); 
        if(!room_lbl) continue;
        
        String room = String(lv_label_get_text(room_lbl));
        
        if (current_room_filter == "All" || room == current_room_filter) {
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// --- ROOM CHIP EVENTS ---
void on_room_click(lv_event_t* e) {
    lv_obj_t* clicked_chip = (lv_obj_t*)lv_event_get_target(e);
    if (!ui_rmC) return;
    
    uint32_t count = lv_obj_get_child_cnt(ui_rmC);
    for(uint32_t i=0; i<count; i++) {
        lv_obj_t* chip = lv_obj_get_child(ui_rmC, i);
        lv_obj_t* label = lv_obj_get_child(chip, 0); 
        
        if (chip == clicked_chip) {
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BLUE_ACTIVE), LV_PART_MAIN);
            if(label) {
                lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT_WHITE), LV_PART_MAIN);
                current_room_filter = String(lv_label_get_text(label)); 
            }
        } else {
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BG_BLACK), LV_PART_MAIN);
            if(label) lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT_WHITE), LV_PART_MAIN);
        }
    }
    apply_switch_filter();
}

void on_arrow_click(lv_event_t* e) {
    if (!ui_rmC) return;
    if (lv_obj_get_scroll_right(ui_rmC) < 5) lv_obj_scroll_to_x(ui_rmC, 0, LV_ANIM_ON);
    else lv_obj_scroll_by(ui_rmC, 100, 0, LV_ANIM_ON);
}

// --- MAIN BUILD FUNCTION ---
void refresh_ui_data(const char* json_payload) {
    Serial.print("UI: Build Start. Heap: "); Serial.println(ESP.getFreeHeap());
    if (!ui_haswC || !ui_rmC) return;

    JsonDocument* doc = new JsonDocument();
    DeserializationError error = deserializeJson(*doc, json_payload);
    if (error) { Serial.print("JSON Error"); delete doc; return; }

    JsonArray buttons = (*doc)["buttons"];
    if (buttons.isNull()) buttons = (*doc)["switches"];
    
    // Cleanup
    if (is_first_ui_update) {
        lv_obj_clean(ui_haswC); 
        is_first_ui_update = false;
    } else {
        uint32_t count = lv_obj_get_child_cnt(ui_haswC);
        for(uint32_t i=0; i<count; i++) {
            lv_obj_t* btn = lv_obj_get_child(ui_haswC, i);
            void* ud = lv_obj_get_user_data(btn); 
            if(ud) free(ud);
        }
        lv_obj_clean(ui_haswC);
    }
    lv_obj_clean(ui_rmC);

    if (buttons.isNull() || buttons.size() == 0) {
        lv_obj_clear_flag(ui_haswCnd, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_haswC, LV_OBJ_FLAG_HIDDEN);
        delete doc; 
        return;
    } else {
        lv_obj_add_flag(ui_haswCnd, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_haswC, LV_OBJ_FLAG_HIDDEN);
        
        // --- LAYOUT CONFIGURATION ---
        // Configure Grid 2x3
        lv_obj_set_flex_flow(ui_haswC, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(ui_haswC, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(ui_haswC, 10, 0);
        lv_obj_set_style_pad_column(ui_haswC, 10, 0); // Gap between columns
        lv_obj_set_style_pad_all(ui_haswC, 0, 0);     // Container padding
    }

    const int MAX_ROOMS = 10;
    char room_list[MAX_ROOMS][32]; 
    strcpy(room_list[0], "All");
    int room_count = 1;

    // --- MANUAL SWITCH CREATION ---
    Serial.println("UI: Creating Switches...");
    int idx = 0;
    for (JsonObject btn : buttons) {
        if (idx++ >= 6) break; // Limit 6 items for 2x3 Grid

        const char* name = btn["name"] | "Dev"; 
        const char* entity = btn["entity"] | ""; 
        const char* icon = btn["icon"] | "power";   
        const char* room = btn["room"] | "Home"; 
        const char* state = btn["state"] | "OFF";

        // A. Root Button (CompSwitch)
        lv_obj_t* sw_btn = lv_btn_create(ui_haswC);
        lv_obj_remove_style_all(sw_btn); // Clean slate
        lv_obj_set_width(sw_btn, SW_WIDTH);
        lv_obj_set_height(sw_btn, SW_HEIGHT);
        lv_obj_add_flag(sw_btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_clear_flag(sw_btn, LV_OBJ_FLAG_SCROLLABLE);
        
        // Base Style
        lv_obj_set_style_radius(sw_btn, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw_btn, lv_color_hex(COLOR_BG_BLACK), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sw_btn, 80, LV_PART_MAIN); // 80/255 opacity (~30%) or 80%? Snippet says 80. LVGL opa 80 is transparent. 
        // Note: SquareLine Opa 80 usually means 80/255. 
        
        // Store Entity ID
        char* entity_store = strdup(entity); 
        lv_obj_set_user_data(sw_btn, (void*)entity_store);

        // B. Icon Container (cui_icc1)
        lv_obj_t* icon_cont = lv_obj_create(sw_btn);
        lv_obj_remove_style_all(icon_cont);
        lv_obj_set_size(icon_cont, 50, 50);
        lv_obj_set_pos(icon_cont, 10, 0);
        lv_obj_set_align(icon_cont, LV_ALIGN_LEFT_MID);
        lv_obj_set_style_radius(icon_cont, 50, LV_PART_MAIN); // Circle
        lv_obj_set_style_bg_color(icon_cont, lv_color_hex(COLOR_BG_BLACK), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(icon_cont, 80, LV_PART_MAIN); // Matches base
        lv_obj_clear_flag(icon_cont, LV_OBJ_FLAG_SCROLLABLE);
        
        // C. Icon Image Object (cui_icp1)
        // Using lv_obj_create to match snippet structure which uses bg_image
        lv_obj_t* icon_obj = lv_obj_create(icon_cont);
        lv_obj_set_size(icon_obj, 32, 32);
        lv_obj_set_align(icon_obj, LV_ALIGN_CENTER);
        lv_obj_clear_flag(icon_obj, LV_OBJ_FLAG_SCROLLABLE);
        // Set Image Source as Background Image
        lv_obj_set_style_bg_image_src(icon_obj, get_icon_by_name(icon), LV_PART_MAIN);
        lv_obj_set_style_bg_color(icon_obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(icon_obj, 0, LV_PART_MAIN); // Transparent BG
        // Initial Recolor (Grey)
        lv_obj_set_style_bg_image_recolor(icon_obj, lv_color_hex(COLOR_INACTIVE_GREY), LV_PART_MAIN);
        lv_obj_set_style_bg_image_recolor_opa(icon_obj, 255, LV_PART_MAIN);

        // D. Name Label (cui_swn1)
        lv_obj_t* lbl_n = lv_label_create(sw_btn);
        lv_obj_set_width(lbl_n, 125);
        lv_obj_set_height(lbl_n, 20);
        lv_obj_set_pos(lbl_n, -10, -5);
        lv_obj_set_align(lbl_n, LV_ALIGN_RIGHT_MID);
        lv_label_set_text(lbl_n, name);
        lv_obj_set_style_text_color(lbl_n, lv_color_hex(COLOR_TEXT_WHITE), LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl_n, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_n, &lv_font_montserrat_16, LV_PART_MAIN);

        // E. Room Label (cui_rm1)
        lv_obj_t* lbl_r = lv_label_create(sw_btn);
        lv_obj_set_width(lbl_r, 125);
        lv_obj_set_height(lbl_r, 16);
        lv_obj_set_pos(lbl_r, -10, -14);
        lv_obj_set_align(lbl_r, LV_ALIGN_BOTTOM_RIGHT);
        lv_label_set_text(lbl_r, room);
        lv_obj_set_style_text_color(lbl_r, lv_color_hex(COLOR_TEXT_WHITE), LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl_r, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_r, &lv_font_montserrat_12, LV_PART_MAIN);

        // State Init
        bool is_on = (strcasecmp(state, "ON") == 0);
        if (is_on) lv_obj_add_state(sw_btn, LV_STATE_CHECKED);
        update_manual_switch_visuals(sw_btn, is_on);

        // Click Event
        lv_obj_add_event_cb(sw_btn, on_manual_switch_toggle, LV_EVENT_CLICKED, (void*)entity_store);

        // Collect Room
        bool exists = false;
        for(int k=0; k<room_count; k++) {
            if(strcmp(room_list[k], room) == 0) { exists = true; break; }
        }
        if(!exists && room_count < MAX_ROOMS) {
            strncpy(room_list[room_count], room, 31);
            room_list[room_count][31] = '\0'; 
            room_count++;
        }
        delay(5); 
    }

    // 5. Manual Chips
    Serial.println("UI: Creating Chips...");
    for (int i = 0; i < room_count; i++) {
        lv_obj_t* chip = lv_btn_create(ui_rmC);
        
        lv_obj_set_height(chip, 30);
        lv_obj_set_width(chip, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(chip, 14, LV_PART_MAIN);
        lv_obj_set_style_radius(chip, 24, LV_PART_MAIN);
        // Explicitly remove borders/shadows
        lv_obj_set_style_border_width(chip, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(chip, 0, LV_PART_MAIN);

        lv_obj_t* lbl = lv_label_create(chip);
        lv_label_set_text(lbl, room_list[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

        if (String(room_list[i]) == current_room_filter) {
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BLUE_ACTIVE), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(chip, 255, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT_WHITE), 0);
        } else {
            lv_obj_set_style_bg_color(chip, lv_color_hex(COLOR_BG_BLACK), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(chip, 80, LV_PART_MAIN); 
            lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT_WHITE), 0);
        }

        lv_obj_add_event_cb(chip, on_room_click, LV_EVENT_CLICKED, NULL);
        delay(5);
    }
    
    lv_obj_update_layout(ui_rmC);
    if (ui_rmPe) {
        if (lv_obj_get_scroll_right(ui_rmC) > 5) lv_obj_clear_flag(ui_rmPe, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ui_rmPe, LV_OBJ_FLAG_HIDDEN);
    }

    Serial.println("UI: Build Complete.");
    delete doc; 
}

// --- UPDATE FROM MQTT ---
void update_device_state(const char* entity_id, bool is_on) {
    if (!ui_haswC) return;
    uint32_t count = lv_obj_get_child_cnt(ui_haswC);
    for(uint32_t i=0; i<count; i++) {
        lv_obj_t* btn = lv_obj_get_child(ui_haswC, i);
        const char* id = (const char*)lv_obj_get_user_data(btn);
        if (id && strcmp(id, entity_id) == 0) {
            if (is_on) lv_obj_add_state(btn, LV_STATE_CHECKED);
            else lv_obj_clear_state(btn, LV_STATE_CHECKED);
            update_manual_switch_visuals(btn, is_on);
            return; 
        }
    }
}

void setup_ui_logic() {
    if(ui_rmPe) lv_obj_add_event_cb(ui_rmPe, on_arrow_click, LV_EVENT_CLICKED, NULL);
}

#endif