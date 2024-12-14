/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include <stdio.h>
#include "lv_example_pub.h"
#include "lv_example_image.h"
#include "bsp/esp-bsp.h"

// Headers needed for audio to play
#include "settings.h"
#include "app_audio.h"

// FreeRTOS Headers needed to operate Event Groups and Threads
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

// Global Variable for Mutex (Lock/Unlock Mechanism)
SemaphoreHandle_t xMutex = NULL; 

// Task Handler for each light announcement's xTaskCreate
TaskHandle_t xtaskHandle = NULL; 
TaskHandle_t xtaskHandle2 = NULL;
TaskHandle_t xtaskHandle3 = NULL;
TaskHandle_t xtaskHandle4 = NULL;
TaskHandle_t xtaskHandle5 = NULL;

// Event Group declared for use in concurrency control
EventGroupHandle_t AnnounceGroup; 

// Assign bit IDs for light announcement functions
const uint32_t Light_0_ID = (1 << 0); // 0x01 (In Hex); 0 (In Bits); 1 (In Decimal)
const uint32_t Light_25_ID = (1 << 1); // 0x02 (In Hex); 1 (In Bits); 2 (In Decimal)
const uint32_t Light_50_ID = (1 << 2); // 0x03
const uint32_t Light_75_ID = (1 << 3); // 0x04
const uint32_t Light_100_ID = (1 << 4); // 0x05
const uint32_t All_Task_Bits = (Light_0_ID | Light_25_ID | Light_50_ID | Light_75_ID | Light_100_ID); // 0x06

// Callback functions for layer management
static bool light_2color_layer_enter_cb(void *layer);
static bool light_2color_layer_exit_cb(void *layer);
static void light_2color_layer_timer_cb(lv_timer_t *tmr);

// Enum to represent different light color temperatures
typedef enum {
    LIGHT_CCK_WARM,
    LIGHT_CCK_COOL,
    LIGHT_CCK_MAX,
} LIGHT_CCK_TYPE;

// Struct to represent light attributes
typedef struct {
    uint8_t light_pwm;         // Light PWM setting (0-100)
    LIGHT_CCK_TYPE light_cck;  // Light color temperature setting
} light_set_attribute_t;

// Struct to represent UI image resources for light settings
typedef struct {
    const lv_img_dsc_t *img_bg[2];
    const lv_img_dsc_t *img_pwm_25[2];
    const lv_img_dsc_t *img_pwm_50[2];
    const lv_img_dsc_t *img_pwm_75[2];
    const lv_img_dsc_t *img_pwm_100[2];
} ui_light_img_t;

// UI elements and time-out counters
static lv_obj_t *page;
static time_out_count time_20ms, time_500ms;
static lv_obj_t *img_light_bg, *label_pwm_set;
static lv_obj_t *img_light_pwm_25, *img_light_pwm_50, *img_light_pwm_75, *img_light_pwm_100, *img_light_pwm_0;

// Light setting attributes
static light_set_attribute_t light_set_conf, light_xor;

// UI image resources for light settings
static const ui_light_img_t light_image = {
    {&light_warm_bg, &light_cool_bg},
    {&light_warm_25, &light_cool_25},
    {&light_warm_50, &light_cool_50},
    {&light_warm_75, &light_cool_75},
    {&light_warm_100, &light_cool_100},
};


// Light control layer initialization structure
lv_layer_t light_2color_Layer = {
    .lv_obj_name    = "light_2color_Layer",
    .lv_obj_parent  = NULL,
    .lv_obj_layer   = NULL,
    .lv_show_layer  = NULL,
    .enter_cb       = light_2color_layer_enter_cb,
    .exit_cb        = light_2color_layer_exit_cb,
    .timer_cb       = light_2color_layer_timer_cb,
};

// Event callback function to handle various UI events
static void light_2color_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (LV_EVENT_FOCUSED == code) {
        lv_group_set_editing(lv_group_get_default(), true);
    } else if (LV_EVENT_KEY == code) {
        uint32_t key = lv_event_get_key(e);
        if (is_time_out(&time_500ms)) {
            if (LV_KEY_RIGHT == key && light_set_conf.light_pwm < 100) {
                light_set_conf.light_pwm += 25;

            } else if (LV_KEY_LEFT == key && light_set_conf.light_pwm > 0) {
                light_set_conf.light_pwm -= 25;
            }
        }
    } else if (LV_EVENT_CLICKED == code) {
        // Toggle light color temperature between warm and cool
        light_set_conf.light_cck = (light_set_conf.light_cck == LIGHT_CCK_WARM) ? LIGHT_CCK_COOL : LIGHT_CCK_WARM;
    } else if (LV_EVENT_LONG_PRESSED == code) {
        // Return to menu layer on long press
        lv_indev_wait_release(lv_indev_get_next(NULL));
        ui_remove_all_objs_from_encoder_group();
        lv_func_goto_layer(&menu_layer);
    }
}

// Initialize UI elements for light control
void ui_light_2color_init(lv_obj_t *parent) {
    // Initial XOR values to force initial update
    light_xor.light_pwm = 0xFF;
    light_xor.light_cck = LIGHT_CCK_MAX;

    // Initial light settings
    light_set_conf.light_pwm = 100;
    light_set_conf.light_cck = LIGHT_CCK_WARM;

    // Create main page object
    page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(page);

    // Background image for light
    img_light_bg = lv_img_create(page);
    lv_img_set_src(img_light_bg, &light_warm_bg);
    lv_obj_align(img_light_bg, LV_ALIGN_CENTER, 0, 0);

    // Label to display PWM setting
    label_pwm_set = lv_label_create(page);
    lv_obj_set_style_text_font(label_pwm_set, &HelveticaNeue_Regular_24, 0);
    lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
    lv_obj_align(label_pwm_set, LV_ALIGN_CENTER, 0, 65);

    // Create image objects for different PWM levels
    img_light_pwm_0 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_0, &light_close_status);
    lv_obj_add_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_0, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_25 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_25, &light_warm_25);
    lv_obj_align(img_light_pwm_25, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_50 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_50, &light_warm_50);
    lv_obj_align(img_light_pwm_50, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_75 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_75, &light_warm_75);
    lv_obj_add_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_75, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_100 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_100, &light_warm_100);
    lv_obj_add_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_100, LV_ALIGN_TOP_MID, 0, 0);

    // Add event callbacks to the page object
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_CLICKED, NULL);

    // Add page to encoder group for input handling
    ui_add_obj_to_encoder_group(page);

    // Mutex Initialization for use in task functions
    xMutex = xSemaphoreCreateMutex();

    // Event Group Initialization
    AnnounceGroup = xEventGroupCreate();
}

// Enter callback for the light control layer
static bool light_2color_layer_enter_cb(void *layer) {
    bool ret = false;

    lv_layer_t *create_layer = layer;
    if (NULL == create_layer->lv_obj_layer) {
        ret = true;
        create_layer->lv_obj_layer = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(create_layer->lv_obj_layer);
        lv_obj_set_size(create_layer->lv_obj_layer, LV_HOR_RES, LV_VER_RES);


        ui_light_2color_init(create_layer->lv_obj_layer);
        set_time_out(&time_20ms, 20);
        set_time_out(&time_500ms, 200);
    }
    return ret;
}

// Exit callback for the light control layer
static bool light_2color_layer_exit_cb(void *layer) {
    bsp_led_rgb_set(0x00, 0x00, 0x00);  // Turn off LEDs when exiting
    return true;
}

// Announcement Tasks (Project Implementation)
// Plays respective sound file based on current light level
void Light_0 (void *Parameters) {
    while(1) {
        if(xSemaphoreTake(xMutex, portMAX_DELAY)) { // Lock Mutex (Indefinitely)
            // Critical Section 
            audio_handle_info(SOUND_TYPE_LIGHT0); // Announce current brightness level
            xSemaphoreGive(xMutex); // Release Mutex
        }
        break;
    }
    vTaskDelete(xtaskHandle); // Delete task after announcing light level
}

void Light_25 (void *Parameters) { 
    while(1) {
        if(xSemaphoreTake(xMutex, portMAX_DELAY)) { // Lock Mutex (Indefinitely)
            // Critical Section
            audio_handle_info(SOUND_TYPE_LIGHT25); // Announce current brightness level
            xSemaphoreGive(xMutex); // Release Mutex
            }
        break;
    }
    vTaskDelete(xtaskHandle2);
}

void Light_50 (void *Parameters) {
    while(1) {
        if(xSemaphoreTake(xMutex, portMAX_DELAY)) { // Lock Mutex (Indefinitely)
            // Critical Section 
            audio_handle_info(SOUND_TYPE_LIGHT50); // Announce current brightness level
            xSemaphoreGive(xMutex); // Release Mutex
            }
        break;
    }
    vTaskDelete(xtaskHandle3);
}

void Light_75 (void *Parameters) {
    while(1) {
        if(xSemaphoreTake(xMutex, portMAX_DELAY)) { // Lock Mutex (Indefinitely)
            // Critical Section 
            audio_handle_info(SOUND_TYPE_LIGHT75); // Announce current brightness level
            xSemaphoreGive(xMutex); // Release Mutex
            }
        break;
    }
    vTaskDelete(xtaskHandle4);
}

void Light_100 (void *Parameters) {
    while(1) {
        if(xSemaphoreTake(xMutex, portMAX_DELAY)) { // Lock Mutex (Indefinitely)
            // Critical Section 
            audio_handle_info(SOUND_TYPE_LIGHT100); // Announce current brightness level
            xSemaphoreGive(xMutex); // Release Mutex
            }
        break;
    }
    vTaskDelete(xtaskHandle5);
}


// Handle which audio file to play based on current light setting
void Announce_Light_Lvl(void *Parameters) {
    while(1) {
        if(xSemaphoreTake(xMutex, portMAX_DELAY)) { // Lock Mutex (Indefinitely)
            EventBits_t xbits;
            const TickType_t xTicksToWait = 100 / portTICK_PERIOD_MS;
            
            xbits = xEventGroupWaitBits(
                AnnounceGroup, // Event group used to determine task operation
                All_Task_Bits, // Represents corresponding light levels [0% --> 100%]
                pdTRUE, // Clear bits before returning
                pdFALSE, // Accept any bit that gets set
                xTicksToWait); // Wait indefinitely for any bit to set
            

            // Call respective audio functions based on set bit
            if ((xbits & Light_0_ID) != 0) {
                xTaskCreate(Light_0, "Announce 0", 2048, NULL, 3, &xtaskHandle);
            }

            if ((xbits & Light_25_ID) != 0) {
                xTaskCreate(Light_25, "Announce 25", 2048, NULL, 3, &xtaskHandle2);
            }

            if ((xbits & Light_50_ID) != 0) {
                xTaskCreate(Light_50, "Announce 50", 2048, NULL, 3, &xtaskHandle3);
            }

            if ((xbits & Light_75_ID) != 0) {
                xTaskCreate(Light_75, "Announce 75", 2048, NULL, 3, &xtaskHandle4);
            }

            if ((xbits & Light_100_ID) != 0) {
                xTaskCreate(Light_100, "Announce 100", 2048, NULL, 3, &xtaskHandle5);
            }

            xSemaphoreGive(xMutex); // Release Mutex
        }
    }
}

// Timer callback to periodically update UI and LED state
static void light_2color_layer_timer_cb(lv_timer_t *tmr) {
    uint32_t RGB_color = 0xFF;

    feed_clock_time();  // Function to update internal clock time (implementation not provided)

    if (is_time_out(&time_20ms)) {
        // Check if light settings have changed
        if ((light_set_conf.light_pwm ^ light_xor.light_pwm) || (light_set_conf.light_cck ^ light_xor.light_cck)) {
            light_xor.light_pwm = light_set_conf.light_pwm;
            light_xor.light_cck = light_set_conf.light_cck;

            // Calculate RGB value based on current settings
            if (LIGHT_CCK_COOL == light_xor.light_cck) {
                RGB_color = (0xFF * light_xor.light_pwm / 100) << 16 | (0xFF * light_xor.light_pwm / 100) << 8 | (0xFF * light_xor.light_pwm / 100);
            } else {
                RGB_color = (0xFF * light_xor.light_pwm / 100) << 16 | (0xFF * light_xor.light_pwm / 100) << 8 | (0x33 * light_xor.light_pwm / 100);
            }
            bsp_led_rgb_set((RGB_color >> 16) & 0xFF, (RGB_color >> 8) & 0xFF, (RGB_color));


            // Update UI elements based on current settings
            lv_obj_add_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_50, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_25, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);


            // Displays current light percentage on device screen
            if (light_set_conf.light_pwm) {
                lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
            } else {
                lv_label_set_text(label_pwm_set, "--");
            }

            uint8_t cck_set = (uint8_t)light_xor.light_cck;

            // Update image sources based on current light settings
            switch (light_xor.light_pwm) {
            case 100:
                lv_obj_clear_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_100, light_image.img_pwm_100[cck_set]);
                xEventGroupSetBits(AnnounceGroup, Light_100_ID); // Signal event group current light level
                xTaskCreate(Light_100, "Announce 100", 2048, NULL, 3, &xtaskHandle5);
                break;

            case 75:
                lv_obj_clear_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_75, light_image.img_pwm_75[cck_set]);
                xEventGroupSetBits(AnnounceGroup, Light_75_ID);
                xTaskCreate(Light_75, "Announce 75", 2048, NULL, 3, &xtaskHandle4);
                break;

            case 50:
                lv_obj_clear_flag(img_light_pwm_50, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_50, light_image.img_pwm_50[cck_set]);
                xEventGroupSetBits(AnnounceGroup, Light_50_ID);
                xTaskCreate(Light_50, "Announce 50", 2048, NULL, 3, &xtaskHandle3);
                break;

            case 25:
                lv_obj_clear_flag(img_light_pwm_25, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_25, light_image.img_pwm_25[cck_set]);
                lv_img_set_src(img_light_bg, light_image.img_bg[cck_set]);
                xEventGroupSetBits(AnnounceGroup, Light_25_ID);
                xTaskCreate(Light_25, "Announce 25", 2048, NULL, 3, &xtaskHandle2);
                break;

            case 0:
                lv_obj_clear_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_bg, &light_close_bg);
                xEventGroupSetBits(AnnounceGroup, Light_0_ID);
                xTaskCreate(Light_0, "Announce 0", 2048, NULL, 3, &xtaskHandle);
                break;

            default:
              break;
            }
        }
    }
}