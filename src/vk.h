#pragma once
#define VK_NO_PROTOTYPES
#define VK_ENABLE_BETA_EXTENSIONS

#define VK_MAKE_VIDEO_STD_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))

#include "vulkan/vulkan.h"
#include "vk_video/vulkan_video_codec_h264std_decode.h"