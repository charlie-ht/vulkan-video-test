#pragma once
/*
* Copyright 2023 Igalia S.L.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#define VK_NO_PROTOTYPES
#define VK_ENABLE_BETA_EXTENSIONS

#define VK_MAKE_VIDEO_STD_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))

extern "C" {
#include "vulkan.h"
#include "vk_video/vulkan_video_codec_h264std_decode.h"
}

#include <vector>

// Helper for robustly executing the two-call pattern
template <typename T, typename F, typename... Ts>
auto get_vector(std::vector<T>& out, F&& f, Ts&&... ts) -> VkResult
{
    uint32_t count = 0;
    VkResult err;
    do {
        err = f(ts..., &count, nullptr);
        if (err != VK_SUCCESS) {
            return err;
        }
        out.resize(count);
        err = f(ts..., &count, out.data());
        out.resize(count);
    } while (err == VK_INCOMPLETE);
    return err;
}

template <typename T, typename F, typename... Ts>
auto get_vector_noerror(F&& f, Ts&&... ts) -> std::vector<T>
{
    uint32_t count = 0;
    std::vector<T> results;
    f(ts..., &count, nullptr);
    results.resize(count);
    f(ts..., &count, results.data());
    results.resize(count);
    return results;
}