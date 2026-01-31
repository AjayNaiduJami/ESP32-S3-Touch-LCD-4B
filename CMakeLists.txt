SET(SOURCES ui_uiScreenSleep.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_scenes_clear_day_png.c
    ui_img_weather_day_png.c
    ui_img_scenes_clear_night_png.c
    ui_img_scenes_cloud_day_png.c
    ui_img_scenes_cloud_night_png.c
    ui_img_scenes_rain_day_png.c
    ui_img_scenes_rain_night_png.c
    ui_img_scenes_snow_day_png.c
    ui_img_scenes_snow_night_png.c
    ui_img_scenes_thunderstorm_png.c
    ui_img_weather_day_cloud_png.c
    ui_img_weather_day_fog_png.c
    ui_img_weather_day_rain_png.c
    ui_img_weather_day_snow_png.c
    ui_img_weather_night_png.c
    ui_img_weather_night_cloud_png.c
    ui_img_weather_night_fog_png.c
    ui_img_weather_night_rain_png.c
    ui_img_weather_night_snow_png.c
    ui_img_weather_thunder_storm_png.c)

add_library(ui ${SOURCES})
