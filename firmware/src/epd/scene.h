#pragma once

typedef enum
{
    EPD_SCENE_TIME = 0, // optional legacy clock scene
    EPD_SCENE_LIST,     // optional legacy text-list scene
    EPD_SCENE_IMAGE,    // static DSL-rendered image

    EPD_SCENE_DEFAULT = EPD_SCENE_IMAGE,
} EPD_SCENE;

unsigned char epd_scene_is_supported(EPD_SCENE scene);
void change_scene(EPD_SCENE scene);
