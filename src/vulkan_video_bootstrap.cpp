#include <array>
#include <bit>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#define LINUX 1

#include <cstring>

#ifdef LINUX
#include <dlfcn.h>
#endif

#include "vk_mem_alloc.h"

namespace vvb {
/*
NV12: VK_FORMAT_G8_B8R8_2PLANE_420_UNORM = 1000156003
P010: VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 = 1000156013
P012: VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16 = 1000156023
P016: VK_FORMAT_G16_B16R16_2PLANE_420_UNORM = 1000156030
P210: VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16 = 1000156015
P212: VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16 = 1000156025
P216: VK_FORMAT_G16_B16R16_2PLANE_422_UNORM = 1000156032
YUY2: VK_FORMAT_G8B8G8R8_422_UNORM = 1000156000
Y210: VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16 = 1000156010
Y212: VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16 = 1000156020
Y216: VK_FORMAT_G16B16G16R16_422_UNORM = 1000156027
AYUV: **VK_FORMAT_A8G8B8R8_UNORM_PACK32**
nY410: VK_FORMAT_A2R10G10B10_UNORM_PACK32 = 58
Y412: **VK_FORMAT_A12x4R12X4G12X4B12X4_UNORM_4PACK16**
Y416: **VK_FORMAT_A16R16G16B16_UNORM_4PACK16**
*/
#define ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

enum VideoFormat {
    YUV_420_8BIT = 0,
    YUV_420_10BIT = 1,
    YUV_420_12BIT = 2,
    YUV_422_8BIT = 3,
    YUV_422_10BIT = 4,
    YUV_422_12BIT = 5,
    YUV_444_8BIT = 6,
    YUV_444_10BIT = 7,
    YUV_444_12BIT = 8,

    NV12 = YUV_420_8BIT,
    P010 = YUV_420_10BIT,
    AYUV = YUV_444_8BIT,
    Y412 = YUV_444_12BIT,

    LastFormat
};

struct VideoFormatInfo {
    VkFormat format;
};

#if 0
static VkFormat _vk_format(VideoFormat fmt)
{
    switch (fmt) {
    case NV12:
        return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    case P010:
        return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
    case AYUV:
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    case Y412:
        return VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16;
    default:
        XERROR(1, "Bad format");
    }
}
#endif

static const char* vk_dev_type(enum VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "software";
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        return "other";
    case VK_PHYSICAL_DEVICE_TYPE_MAX_ENUM:
    default:
        return "unknown";
    }
}

/* Converts return values to strings */
const char* vk_ret2str(VkResult res)
{
#define CASE(VAL) \
    case VAL:     \
        return #VAL
    switch (res) {
        CASE(VK_SUCCESS);
        CASE(VK_NOT_READY);
        CASE(VK_TIMEOUT);
        CASE(VK_EVENT_SET);
        CASE(VK_EVENT_RESET);
        CASE(VK_INCOMPLETE);
        CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
        CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        CASE(VK_ERROR_INITIALIZATION_FAILED);
        CASE(VK_ERROR_DEVICE_LOST);
        CASE(VK_ERROR_MEMORY_MAP_FAILED);
        CASE(VK_ERROR_LAYER_NOT_PRESENT);
        CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
        CASE(VK_ERROR_FEATURE_NOT_PRESENT);
        CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
        CASE(VK_ERROR_TOO_MANY_OBJECTS);
        CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
        CASE(VK_ERROR_FRAGMENTED_POOL);
        CASE(VK_ERROR_SURFACE_LOST_KHR);
        CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        CASE(VK_SUBOPTIMAL_KHR);
        CASE(VK_ERROR_OUT_OF_DATE_KHR);
        CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
        CASE(VK_ERROR_VALIDATION_FAILED_EXT);
        CASE(VK_ERROR_INVALID_SHADER_NV);
        CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
        CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
        CASE(VK_ERROR_NOT_PERMITTED_EXT);
        CASE(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
        CASE(VK_ERROR_INVALID_DEVICE_ADDRESS_EXT);
        CASE(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
    default:
        return "Unknown error";
    }
#undef CASE
}

#define VK_CHECK(x) \
    do { \
        VkResult err = x; \
        if (err) { \
            printf("Detected Vulkan error: %s (%d)\n", vvb::vk_ret2str(err), err); \
            assert(!err); \
            exit(1); \
        } \
    } while (0)

// {{{ platform code

// Instance loading ------------------
/* An enum of bitflags for every optional extension we need */
enum FFVulkanExtensions {
    FF_VK_EXT_EXTERNAL_DMABUF_MEMORY = 1ULL << 0, /* VK_EXT_external_memory_dma_buf */
    FF_VK_EXT_DRM_MODIFIER_FLAGS = 1ULL << 1, /* VK_EXT_image_drm_format_modifier */
    FF_VK_EXT_EXTERNAL_FD_MEMORY = 1ULL << 2, /* VK_KHR_external_memory_fd */
    FF_VK_EXT_EXTERNAL_FD_SEM = 1ULL << 3, /* VK_KHR_external_semaphore_fd */
    FF_VK_EXT_EXTERNAL_HOST_MEMORY = 1ULL << 4, /* VK_EXT_external_memory_host */
    FF_VK_EXT_DEBUG_UTILS = 1ULL << 5, /* VK_EXT_debug_utils */
#ifdef _WIN32
    FF_VK_EXT_EXTERNAL_WIN32_MEMORY = 1ULL << 6, /* VK_KHR_external_memory_win32 */
    FF_VK_EXT_EXTERNAL_WIN32_SEM = 1ULL << 7, /* VK_KHR_external_semaphore_win32 */
#endif
    FF_VK_EXT_SYNC2 = 1ULL << 8, /* VK_KHR_synchronization2 */
    FF_VK_EXT_DESCRIPTOR_BUFFER = 1ULL << 9, /* VK_EXT_descriptor_buffer */
    FF_VK_EXT_DEVICE_DRM = 1ULL << 10, /* VK_EXT_physical_device_drm */
    FF_VK_EXT_ATOMIC_FLOAT = 1ULL << 11, /* VK_EXT_shader_atomic_float */
    FF_VK_EXT_VIDEO_QUEUE = 1ULL << 12, /* VK_KHR_video_queue */
    FF_VK_EXT_VIDEO_DECODE_QUEUE = 1ULL << 13, /* VK_KHR_video_decode_queue */
    FF_VK_EXT_VIDEO_DECODE_H264 = 1ULL << 14, /* VK_EXT_video_decode_h264 */
    FF_VK_EXT_VIDEO_DECODE_H265 = 1ULL << 15, /* VK_EXT_video_decode_h265 */
    FF_VK_EXT_VIDEO_ENCODE_QUEUE = 1ULL << 16, /* VK_KHR_video_encode_queue */
    FF_VK_EXT_VIDEO_ENCODE_H264 = 1ULL << 17, /* VK_EXT_video_encode_h264 */
    FF_VK_EXT_VIDEO_ENCODE_H265 = 1ULL << 18, /* VK_EXT_video_encode_h265 */
    FF_VK_EXT_VIDEO_DECODE_AV1 = 1ULL << 19, /* VK_MESA_video_decode_av1 */

    FF_VK_EXT_NO_FLAG = 1ULL << 31,
};

// }}}

/* Macro containing every function that we utilize in our codebase */
#define FN_LIST(MACRO)                                                                \
    /* Instance */                                                                    \
    MACRO(0, 0, FF_VK_EXT_NO_FLAG, EnumerateInstanceExtensionProperties)              \
    MACRO(0, 0, FF_VK_EXT_NO_FLAG, EnumerateInstanceLayerProperties)                  \
    MACRO(0, 0, FF_VK_EXT_NO_FLAG, CreateInstance)                                    \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, DestroyInstance)                                   \
                                                                                      \
    /* Debug */                                                                       \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, CreateDebugUtilsMessengerEXT)                      \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, DestroyDebugUtilsMessengerEXT)                     \
                                                                                      \
    /* Device */                                                                      \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetDeviceProcAddr)                                 \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, CreateDevice)                                      \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceFeatures2)                        \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceProperties)                       \
    MACRO(1, 0, FF_VK_EXT_VIDEO_QUEUE, GetPhysicalDeviceVideoCapabilitiesKHR)         \
    MACRO(1, 0, FF_VK_EXT_VIDEO_QUEUE, GetPhysicalDeviceVideoFormatPropertiesKHR)     \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, DeviceWaitIdle)                                    \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, DestroyDevice)                                     \
                                                                                      \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, EnumeratePhysicalDevices)                          \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, EnumerateDeviceExtensionProperties)                \
                                                                                      \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceProperties2)                      \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceMemoryProperties)                 \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceFormatProperties2)                \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceImageFormatProperties2)           \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceQueueFamilyProperties)            \
    MACRO(1, 0, FF_VK_EXT_NO_FLAG, GetPhysicalDeviceQueueFamilyProperties2)           \
                                                                                      \
    /* Command pool */                                                                \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateCommandPool)                                 \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyCommandPool)                                \
                                                                                      \
    /* Command buffer */                                                              \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, AllocateCommandBuffers)                            \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, BeginCommandBuffer)                                \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, EndCommandBuffer)                                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, FreeCommandBuffers)                                \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdDispatch)                                       \
                                                                                      \
    /* Queue */                                                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, GetDeviceQueue)                                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, QueueSubmit)                                       \
                                                                                      \
    /* Fences */                                                                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateFence)                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, WaitForFences)                                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, ResetFences)                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyFence)                                      \
                                                                                      \
    /* Semaphores */                                                                  \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_FD_SEM, GetSemaphoreFdKHR)                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateSemaphore)                                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, WaitSemaphores)                                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroySemaphore)                                  \
                                                                                      \
    /* Memory */                                                                      \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_FD_MEMORY, GetMemoryFdKHR)                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, GetMemoryFdPropertiesKHR)                          \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_HOST_MEMORY, GetMemoryHostPointerPropertiesEXT)    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, AllocateMemory)                                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, MapMemory)                                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, FlushMappedMemoryRanges)                           \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, InvalidateMappedMemoryRanges)                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, UnmapMemory)                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, FreeMemory)                                        \
                                                                                      \
    /* Commands */                                                                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdBindDescriptorSets)                             \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdPushConstants)                                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdBindPipeline)                                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdPipelineBarrier)                                \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdCopyBufferToImage)                              \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdCopyImageToBuffer)                              \
                                                                                      \
    /* Buffer */                                                                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, GetBufferMemoryRequirements2)                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateBuffer)                                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, BindBufferMemory)                                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, GetBufferDeviceAddress)                            \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdFillBuffer)                                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyBuffer)                                     \
                                                                                      \
    /* Image */                                                                       \
    MACRO(1, 1, FF_VK_EXT_DRM_MODIFIER_FLAGS, GetImageDrmFormatModifierPropertiesEXT) \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, GetImageMemoryRequirements2)                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateImage)                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, BindImageMemory2)                                  \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, GetImageSubresourceLayout)                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyImage)                                      \
                                                                                      \
    /* ImageView */                                                                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateImageView)                                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyImageView)                                  \
                                                                                      \
    /* DescriptorSet */                                                               \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateDescriptorSetLayout)                         \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, AllocateDescriptorSets)                            \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateDescriptorPool)                              \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyDescriptorPool)                             \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyDescriptorSetLayout)                        \
                                                                                      \
    /* Descriptor buffers */                                                          \
    MACRO(1, 1, FF_VK_EXT_DESCRIPTOR_BUFFER, GetDescriptorSetLayoutBindingOffsetEXT)  \
    MACRO(1, 1, FF_VK_EXT_DESCRIPTOR_BUFFER, GetDescriptorEXT)                        \
    MACRO(1, 1, FF_VK_EXT_DESCRIPTOR_BUFFER, CmdBindDescriptorBuffersEXT)             \
    MACRO(1, 1, FF_VK_EXT_DESCRIPTOR_BUFFER, CmdSetDescriptorBufferOffsetsEXT)        \
                                                                                      \
    /* DescriptorUpdateTemplate */                                                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, UpdateDescriptorSetWithTemplate)                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateDescriptorUpdateTemplate)                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyDescriptorUpdateTemplate)                   \
                                                                                      \
    /* Queries */                                                                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateQueryPool)                                   \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, GetQueryPoolResults)                               \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, ResetQueryPool)                                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdBeginQuery)                                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdEndQuery)                                       \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CmdResetQueryPool)                                 \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyQueryPool)                                  \
                                                                                      \
    /* sync2 */                                                                       \
    MACRO(1, 1, FF_VK_EXT_SYNC2, CmdPipelineBarrier2KHR)                              \
                                                                                      \
    /* Video queue */                                                                 \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, CreateVideoSessionKHR)                         \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, CreateVideoSessionParametersKHR)               \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, GetVideoSessionMemoryRequirementsKHR)          \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, BindVideoSessionMemoryKHR)                     \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, CmdBeginVideoCodingKHR)                        \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, CmdControlVideoCodingKHR)                      \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, CmdEndVideoCodingKHR)                          \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, DestroyVideoSessionParametersKHR)              \
    MACRO(1, 1, FF_VK_EXT_VIDEO_QUEUE, DestroyVideoSessionKHR)                        \
                                                                                      \
    /* Video decoding */                                                              \
    MACRO(1, 1, FF_VK_EXT_VIDEO_DECODE_QUEUE, CmdDecodeVideoKHR)                      \
                                                                                      \
    /* Video encoding */                                                              \
    /*MACRO(1, 1, FF_VK_EXT_VIDEO_ENCODE_QUEUE,   CmdEncodeVideoKHR)*/                \
                                                                                      \
    /* Pipeline */                                                                    \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreatePipelineLayout)                              \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyPipelineLayout)                             \
                                                                                      \
    /* PipelineLayout */                                                              \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateComputePipelines)                            \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyPipeline)                                   \
                                                                                      \
    /* Sampler */                                                                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateSamplerYcbcrConversion)                      \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroySamplerYcbcrConversion)                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateSampler)                                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroySampler)                                    \
                                                                                      \
    /* Shaders */                                                                     \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, CreateShaderModule)                                \
    MACRO(1, 1, FF_VK_EXT_NO_FLAG, DestroyShaderModule)

/* Macro containing every win32 specific function that we utilize in our codebase */
#define FN_LIST_WIN32(MACRO)                                              \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_WIN32_SEM, GetSemaphoreWin32HandleKHR) \
    MACRO(1, 1, FF_VK_EXT_EXTERNAL_WIN32_MEMORY, GetMemoryWin32HandleKHR)

/* Macro to turn a function name into a definition */
#define PFN_DEF(req_inst, req_dev, ext_flag, name) \
    PFN_vk##name name;

/* Structure with the definition of all listed functions */
typedef struct VulkanFunctions {
    FN_LIST(PFN_DEF)
#ifdef _WIN32
    FN_LIST_WIN32(PFN_DEF)
#endif
} VulkanFunctions;

/* Macro to turn a function name into a loader struct */
#define PFN_LOAD_INFO(req_inst, req_dev, ext_flag, name)   \
    {                                                      \
        req_inst,                                          \
        req_dev,                                           \
        offsetof(VulkanFunctions, name),                   \
        ext_flag,                                          \
        { "vk" #name, "vk" #name "EXT", "vk" #name "KHR" } \
    },

#define ASPECT_2PLANE (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT)
#define ASPECT_3PLANE (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT | VK_IMAGE_ASPECT_PLANE_2_BIT)

struct VulkanPhysicalDevicePriv {
    VkPhysicalDeviceProperties2 props;
    VkPhysicalDeviceMemoryProperties memory_props;
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT external_memory_host_props;
    VkPhysicalDeviceVulkan11Features features_1_1;
    VkPhysicalDeviceVulkan12Features features_1_2;
    VkPhysicalDeviceVulkan13Features features_1_3;
    VkPhysicalDeviceDescriptorBufferFeaturesEXT desc_buf_features;
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomic_float_features;
    VkPhysicalDeviceFeatures2 features;
};

class SysVulkan {
public:
    VmaAllocator _allocator;

    PFN_vkGetInstanceProcAddr _get_proc_addr { nullptr };
    VulkanFunctions _vfn;
    
    void* _libvulkan { nullptr }; // Library and loader functions
    unsigned int extensions;

    std::vector<VkLayerProperties> _available_instance_layers;
    VkInstance _instance { VK_NULL_HANDLE };

    VkDevice _active_dev { VK_NULL_HANDLE };
    
    std::vector<VkPhysicalDevice> _physical_devices;
    std::vector<VkPhysicalDeviceProperties2> _physical_device_props;
    std::vector<VkPhysicalDeviceIDProperties> _physical_device_id_props;
    std::vector<VkPhysicalDeviceDrmPropertiesEXT> _physical_device_drm_props;

    size_t _selected_physical_dev_idx { 0 };
    VulkanPhysicalDevicePriv _selected_physical_device_priv;
    std::vector<VkExtensionProperties> _selected_device_all_available_extensions;
    VkPhysicalDevice SelectedPhysicalDevice() const
    {
        ASSERT(_physical_devices.size() > 0);
        return _physical_devices[_selected_physical_dev_idx];
    }

    /* Settings */
    int dev_is_nvidia;
    int use_linear_images;
    /* Debug callback */
    VkDebugUtilsMessengerEXT _dev_debug_ctx;
    // -- end of physical device settings

    /* Queues */
    std::vector<VkQueueFamilyProperties2> _qf_properties;
    std::vector<VkQueueFamilyVideoPropertiesKHR> _qf_video_properties;
    std::vector<VkQueueFamilyQueryResultStatusPropertiesKHR> _qf_query_support;
    std::vector<std::vector<pthread_mutex_t>> _qf_mutexs;


    /**
     * Queue family index for graphics operations, and the number of queues
     * enabled for it. If unavaiable, will be set to -1. Not required.
     * av_hwdevice_create() will attempt to find a dedicated queue for each
     * queue family, or pick the one with the least unrelated flags set.
     * Queue indices here may overlap if a queue has to share capabilities.
     */
    int queue_family_index;
    int nb_graphics_queues;

    /**
     * Queue family index for transfer operations and the number of queues
     * enabled. Required.
     */
    int queue_family_tx_index;
    int nb_tx_queues;

    /**
     * Queue family index for compute operations and the number of queues
     * enabled. Required.
     */
    int queue_family_comp_index;
    int nb_comp_queues;

    /**
     * Queue family index for video encode ops, and the amount of queues enabled.
     * If the device doesn't support such, queue_family_encode_index will be -1.
     * Not required.
     */
    int queue_family_encode_index;
    int nb_encode_queues;

    /**
     * Queue family index for video decode ops, and the amount of queues enabled.
     * If the device doesn't support such, queue_family_decode_index will be -1.
     * Not required.
     */
    int queue_family_decode_index;
    int nb_decode_queues;

    VkQueue _decode_queue0{VK_NULL_HANDLE};
    VkQueue _tx_queue0{VK_NULL_HANDLE};

    bool EncodeQueriesAreSupported() const
    {
        return _qf_query_support[queue_family_encode_index].queryResultStatusSupport;
    }

    bool DecodeQueriesAreSupported() const
    {
        return _qf_query_support[queue_family_decode_index].queryResultStatusSupport;
    }

    VkQueryPool _query_pool { VK_NULL_HANDLE };
    
    std::vector<const char*> _active_dev_enabled_exts;

    struct UserOptions {
        bool detect_env { false };
        bool enable_validation { true };
        const char* requested_device_name { nullptr };
        int requested_device_major { -1 };
        int requested_device_minor { -1 };
        int requested_driver_version_major { -1 };
        int requested_driver_version_minor { -1 };
        int requested_driver_version_patch { -1 };
    } _options;

    SysVulkan(const UserOptions& options)
        : _options(options)
    {
    }
    SysVulkan(SysVulkan& other) = delete;
    SysVulkan(SysVulkan&& other) = delete;
    ~SysVulkan()
    {
        auto& vk = _vfn;
        vmaDestroyAllocator(_allocator);

        if (_dev_debug_ctx)
            vk.DestroyDebugUtilsMessengerEXT(_instance, _dev_debug_ctx, nullptr);

        if (_active_dev != VK_NULL_HANDLE)
            vk.DestroyDevice(_active_dev, nullptr);

        if (_instance != VK_NULL_HANDLE)
            vk.DestroyInstance(_instance, nullptr);
        if (_libvulkan)
#if LINUX
            dlclose(_libvulkan);
#else
#error "Platform unsupported"
            ;
#endif
    }
};

static const char default_layer[] = { "VK_LAYER_KHRONOS_validation" };

typedef struct VulkanOptExtension {
    const char* name;
    FFVulkanExtensions flag;
    u32 padding { 0 };
} VulkanOptExtension;

static const VulkanOptExtension optional_device_exts[] = {
    /* Misc or required by other extensions */
    { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, FF_VK_EXT_NO_FLAG },
    { VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, FF_VK_EXT_NO_FLAG },
    { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, FF_VK_EXT_SYNC2 },
    {
        VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
        FF_VK_EXT_DESCRIPTOR_BUFFER,
    },
    { VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME, FF_VK_EXT_DEVICE_DRM },
    { VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME, FF_VK_EXT_ATOMIC_FLOAT },

    /* Imports/exports */
    { VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, FF_VK_EXT_EXTERNAL_FD_MEMORY },
    { VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME, FF_VK_EXT_EXTERNAL_DMABUF_MEMORY },
    { VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, FF_VK_EXT_DRM_MODIFIER_FLAGS },
    { VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, FF_VK_EXT_EXTERNAL_FD_SEM },
    { VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME, FF_VK_EXT_EXTERNAL_HOST_MEMORY },
#ifdef _WIN32
    { VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, FF_VK_EXT_EXTERNAL_WIN32_MEMORY },
    { VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME, FF_VK_EXT_EXTERNAL_WIN32_SEM },
#endif

    /* Video encoding/decoding */
    { VK_KHR_VIDEO_QUEUE_EXTENSION_NAME, FF_VK_EXT_VIDEO_QUEUE },
    { VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME, FF_VK_EXT_VIDEO_ENCODE_QUEUE },
    { VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME, FF_VK_EXT_VIDEO_DECODE_QUEUE },
#if 0
    { VK_EXT_VIDEO_ENCODE_H264_EXTENSION_NAME, FF_VK_EXT_VIDEO_ENCODE_H264 },
#endif
    { VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME, FF_VK_EXT_VIDEO_DECODE_H264 },
#if 0
    { VK_EXT_VIDEO_ENCODE_H265_EXTENSION_NAME, FF_VK_EXT_VIDEO_ENCODE_H265 },
#endif
    { VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME, FF_VK_EXT_VIDEO_DECODE_H265 },
    { "VK_MESA_video_decode_av1", FF_VK_EXT_VIDEO_DECODE_AV1 },
};

static void check_device_extensions(SysVulkan& sys_vk, std::vector<const char*>& enabled_extensions)
{
    auto& vk = sys_vk._vfn;

    enabled_extensions.clear();

    u64 start = util::RDTSC();
    util::Timer t;
    ASSERT(sys_vk._selected_device_all_available_extensions.empty());

    t.GetCurrentTime();
    get_vector(sys_vk._selected_device_all_available_extensions, vk.EnumerateDeviceExtensionProperties, sys_vk.SelectedPhysicalDevice(), nullptr);
    u64 end = util::RDTSC();
    u64 ms = t.ElapsedMilliseconds();
    fprintf(stderr, "EnumerateDeviceExtensionProperties took %" PRIu64 " ms (%" PRIu64 " cycles)\n", ms, end - start);
    fprintf(stderr, "device extensions:\n");
    int optional_exts_num;
    optional_exts_num = ARRAY_ELEMS(optional_device_exts);

    for (const auto& prop : sys_vk._selected_device_all_available_extensions) {
        bool found = false;
        for (int i = 0; i < optional_exts_num; i++) {
            const char* tstr = optional_device_exts[i].name;
            if (!strcmp(prop.extensionName, tstr)) {
                enabled_extensions.push_back(tstr);
                found = true;
                break;
            }
        }
        if (found)
            printf("[ENABLED] %s\n", prop.extensionName);
        else
            printf("[       ] %s\n", prop.extensionName);
    }
}

static void check_instance_extensions(SysVulkan& sys_vk, std::vector<const char*>& enabled_extensions)
{
    auto& vk = sys_vk._vfn;

    enabled_extensions.clear();

    std::vector<VkExtensionProperties> properties;
    get_vector(properties, vk.EnumerateInstanceExtensionProperties, default_layer);
    if (sys_vk._options.enable_validation) {
        fprintf(stderr, "extensions provided by layer %s:\n", default_layer);
        for (const auto& prop : properties) {
            if (!strcmp(prop.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
                enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                putchar('*');
            }
            printf("\t%s\n", prop.extensionName);
        }
    }

    std::vector<VkExtensionProperties> default_extension_properties;
    get_vector(default_extension_properties, vk.EnumerateInstanceExtensionProperties, nullptr);
    fprintf(stderr, "implementation available instance extensions:\n");
    for (const auto& prop : default_extension_properties) {
        putchar('\t');
        puts(prop.extensionName);
    }
}

static void check_validation_layers(SysVulkan& sys_vk, std::vector<const char*>& enabled_layers)
{
    auto& vk = sys_vk._vfn;

    auto& layers = sys_vk._available_instance_layers;
    layers.clear();
    get_vector(layers, vk.EnumerateInstanceLayerProperties);

    enabled_layers.clear();

    if (sys_vk._options.enable_validation) {
        bool found_default = false;

        for (const auto& layerProperties : layers) {
            fprintf(stderr, "Instance layer: %s\n", layerProperties.layerName);

            if (!strcmp(default_layer, layerProperties.layerName)) {
                found_default = 1;
                break;
            }
        }
        if (!found_default)
            XERROR(1, "Failed to find required validation layer in debug mode\n");

        enabled_layers.push_back(default_layer);
    }
}

void load_instance(SysVulkan& sys_vk)
{
    auto& vk = sys_vk._vfn;

    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "vvb",
        .applicationVersion = VK_MAKE_VERSION(0,
            0,
            1),
        .pEngineName = "vvb",
        .engineVersion = VK_MAKE_VERSION(0,
            0,
            1),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkValidationFeaturesEXT validation_features = {};
    validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;

    VkInstanceCreateInfo inst_props = {};
    inst_props.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_props.pApplicationInfo = &application_info;

    assert(sys_vk._get_proc_addr);

    std::vector<const char*> enabled_validation_layers;
    check_validation_layers(sys_vk, enabled_validation_layers);
    inst_props.enabledLayerCount = enabled_validation_layers.size();
    inst_props.ppEnabledLayerNames = enabled_validation_layers.data();

    VkValidationFeatureEnableEXT feat_list[] = {
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };

    if (sys_vk._options.enable_validation) {
        validation_features.pEnabledValidationFeatures = feat_list;
        validation_features.enabledValidationFeatureCount = ARRAY_ELEMS(feat_list);
        inst_props.pNext = &validation_features;
    }

    std::vector<const char*> enabled_instance_extensions;
    check_instance_extensions(sys_vk, enabled_instance_extensions);
    inst_props.enabledExtensionCount = enabled_instance_extensions.size();
    inst_props.ppEnabledExtensionNames = enabled_instance_extensions.data();

    /* Try to create the instance */
    VkResult ret = vk.CreateInstance(&inst_props, nullptr, &sys_vk._instance);
    if (ret != VK_SUCCESS)
        XERROR(1, "Failed to create instance: %s\n", vk_ret2str(ret));
}

class VideoPictureResource {
};
using DPBSlotIdx = int8_t;

class BoundReferencePictureResources {
    struct Mapping {
        VideoPictureResource resource;
        DPBSlotIdx slotIdx;
    };
    using SlotMapping = std::vector<Mapping>;
    enum { SlotUnbound = -1 };
    SlotMapping _slots;

    BoundReferencePictureResources()
        : _slots(16)
    {
        for (Mapping& m : _slots) {
            m.slotIdx = SlotUnbound;
        }
    }

    SlotMapping::size_type num_bound() const { return _slots.size(); }
    bool is_bound(DPBSlotIdx idx) const
    {
        assert(idx != -1);
        assert(static_cast<SlotMapping::size_type>(idx) <= num_bound());
        return _slots[static_cast<u32>(idx)].slotIdx != SlotUnbound;
    }
};

class DPB {
    std::array<VideoPictureResource, 32> slots;
};

class FrameContext {
};

// Decoded (raw) video data.
class Frame {
public:
    // Resources backing this frame.
    VkImage img; // Images to which memory is bound
    VkDeviceMemory mem; // Memory backing frame resources
    ptrdiff_t offset { 0 }; // Optional offset into the mem for img.

    // The coded dimensions in pixels.
    int width, height;

    VkFormat format;

    i64 pts; // Time when frame should be shown to the user.

    // Picture number in bitstream order
    int coded_picture_number;

    // Picture number in display order
    int display_picture_number;

    // The content is interlaced
    int interlaced_frame;
    int top_field_first;

    // Updated per barrier
    VkAccessFlagBits access;
    VkImageLayout layout;

    // Timeline semaphore for img. Signal before and after (with increment) at every submission
    VkSemaphore sem;
    u64 sem_value;

    // Queue family for the img
    u32 queue_family { VK_QUEUE_FAMILY_IGNORED };
};

enum Codec { H264 };

struct video_device_preferences {
    std::vector<Codec> required_codecs;
};

void load_vk_functions(SysVulkan& sys_vk, const uint64_t extensions_mask = FF_VK_EXT_NO_FLAG, bool has_inst = false, bool has_dev = false)
{
    static const struct FunctionLoadInfo {
        int req_inst { 0 };
        int req_dev { 0 };
        size_t struct_offset { 0 };
        FFVulkanExtensions ext_flag { FF_VK_EXT_NO_FLAG };
        const char* names[3];
    } vk_load_info[] = {
        FN_LIST(PFN_LOAD_INFO)
#ifdef _WIN32
            FN_LIST_WIN32(PFN_LOAD_INFO)
#endif
    };

    for (u32 i = 0; i < ARRAY_ELEMS(vk_load_info); i++) {
        const struct FunctionLoadInfo* load = &vk_load_info[i];
        PFN_vkVoidFunction fn = nullptr;

        if (load->req_dev && !has_dev)
            continue;
        if (load->req_inst && !has_inst)
            continue;

        for (u32 j = 0; j < ARRAY_ELEMS(load->names); j++) {
            const char* name = load->names[j];

            if (load->req_dev)
                fn = sys_vk._vfn.GetDeviceProcAddr(sys_vk._active_dev, name);
            else if (load->req_inst) {
                fn = sys_vk._get_proc_addr(sys_vk._instance, name);
            } else
                fn = sys_vk._get_proc_addr(nullptr, name);

            if (fn)
                break;
        }

        if (false && ((extensions_mask & ~FF_VK_EXT_NO_FLAG) & load->ext_flag)) {
            abort();
        }

        *(PFN_vkVoidFunction*)((uint8_t*)&sys_vk._vfn + load->struct_offset) = fn;
    }
}

static void choose_and_load_device(SysVulkan& sys_vk)
{
    auto& vk = sys_vk._vfn;

    // Find a device on the machine
    auto& prop = sys_vk._physical_device_props;
    auto& idp = sys_vk._physical_device_id_props;
    auto& drm_prop = sys_vk._physical_device_drm_props;

    get_vector(sys_vk._physical_devices, vk.EnumeratePhysicalDevices, sys_vk._instance);
    if (sys_vk._physical_devices.empty())
        XERROR(1, "No physical device found!");

    const auto num_devices = sys_vk._physical_devices.size();
    if (num_devices > 1)
        printf("%ld physical devices discovered\n", num_devices);
    else
        printf("single physical device discovered\n");
    prop.resize(num_devices);
    idp.resize(num_devices);
    drm_prop.resize(num_devices);

    for (u32 i = 0; i < num_devices; i++) {
        drm_prop[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
        idp[i].pNext = &drm_prop[i];
        idp[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        prop[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        prop[i].pNext = &idp[i];
        vk.GetPhysicalDeviceProperties2(sys_vk._physical_devices[i], &prop[i]);
        printf("    %d: %s (driverUUID=%s version=%u.%u.%u.%u) (%s) (deviceID=0x%x) (primary major/minor 0x%lx/0x%lx render major/minor 0x%lx/0x%lx\n",
            i,
            prop[i].properties.deviceName,
            idp[i].driverUUID,
            VK_API_VERSION_MAJOR(prop[i].properties.driverVersion),
            VK_API_VERSION_MINOR(prop[i].properties.driverVersion),
            VK_API_VERSION_PATCH(prop[i].properties.driverVersion),
            VK_API_VERSION_VARIANT(prop[i].properties.driverVersion),
            vk_dev_type(prop[i].properties.deviceType),
            prop[i].properties.deviceID,
            drm_prop[i].primaryMajor,
            drm_prop[i].primaryMinor,
            drm_prop[i].renderMajor,
            drm_prop[i].renderMinor);
    }

    i32 choice = -1;

    for (u32 i = 0; i < num_devices; i++) {
        auto& opts = sys_vk._options;       
        if (opts.requested_driver_version_major != -1 && opts.requested_driver_version_minor != -1 && opts.requested_driver_version_patch != -1) {
            if (VK_API_VERSION_MAJOR(prop[i].properties.driverVersion) == opts.requested_driver_version_major &&
                VK_API_VERSION_MINOR(prop[i].properties.driverVersion) == opts.requested_driver_version_minor &&
                VK_API_VERSION_PATCH(prop[i].properties.driverVersion) == opts.requested_driver_version_patch) {
                choice = i;
                printf("Device %s picked based on driver version selection\n", prop[i].properties.deviceName);
                break;
            }
            if (drm_prop[i].primaryMajor == opts.requested_device_major && drm_prop[i].primaryMinor == opts.requested_device_minor) {
                choice = i;
                printf("Device %s picked based on major/minor number selection\n", prop[i].properties.deviceName);
                break;
            }
        }
        if (opts.requested_device_major != -1 || opts.requested_device_minor != -1) {
            if (drm_prop[i].primaryMajor == opts.requested_device_major && drm_prop[i].primaryMinor == opts.requested_device_minor) {
                choice = i;
                printf("Device %s picked based on major/minor number selection\n", prop[i].properties.deviceName);
                break;
            }
        }

        const char* this_device_name = prop[i].properties.deviceName;
        if (opts.requested_device_name && util::StrCaseInsensitiveSubstring(this_device_name, opts.requested_device_name)) {
            choice = i;
            printf("Device %s picked based on device name selection (%s)\n", prop[i].properties.deviceName, opts.requested_device_name);
            break;
        }
    }

    if (choice == -1)
    {
        printf("ERROR: No physical device selected\n");
        printf("Consult the above list of available devices, and then select one using either:\n");
        printf("    --device-name=<name> (e.g. --device-name=amd) (insensitive substring search) OR\n");
        printf("    --device-major-minor=<major>.<minor> : select device by major and minor version (hex)\n");
        printf("    --driver-version=<major>.<minor>.<patch> : select device by available driver version\n");
        exit(1);
    }
 
    sys_vk._selected_physical_dev_idx = choice;

    printf("Selected device %s (driver version: %d)\n", prop[choice].properties.deviceName, VK_API_VERSION_MAJOR(prop[choice].properties.driverVersion));

    sys_vk.dev_is_nvidia = drm_prop[choice].primaryMajor == 0xe2;

    // Device selected, now query its features for decoding.
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features = {};
    timeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomic_float_features = {};
    atomic_float_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
    atomic_float_features.pNext = &timeline_features;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT desc_buf_features = {};
    desc_buf_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    desc_buf_features.pNext = &atomic_float_features;

    VkPhysicalDeviceVulkan13Features dev_features_1_3 = {};
    dev_features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    dev_features_1_3.pNext = &desc_buf_features;

    VkPhysicalDeviceVulkan12Features dev_features_1_2 = {};
    dev_features_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    dev_features_1_2.pNext = &dev_features_1_3;
    VkPhysicalDeviceVulkan11Features dev_features_1_1 = {};
    dev_features_1_1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    dev_features_1_1.pNext = &dev_features_1_2;
    VkPhysicalDeviceFeatures2 dev_features = {};
    dev_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    dev_features.pNext = &dev_features_1_1;

    util::Timer t;
    u64 us;
    t.GetCurrentTime();
    vk.GetPhysicalDeviceFeatures2(sys_vk.SelectedPhysicalDevice(), &dev_features);
    us = t.ElapsedMicroseconds();
    printf("GetPhysicalDeviceFeatures2 took %" PRIu64 " us\n", us);

    VkDeviceCreateInfo dev_info = {};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    auto priv = sys_vk._selected_physical_device_priv;
    memset(&priv, 0, sizeof(priv));
    priv.features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    priv.features.pNext = &priv.features_1_1;
    priv.features_1_1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    priv.features_1_1.pNext = &priv.features_1_2;
    priv.features_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    priv.features_1_2.pNext = &priv.features_1_3;
    priv.features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    priv.features_1_3.pNext = &priv.desc_buf_features;
    priv.desc_buf_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    priv.desc_buf_features.pNext = &priv.atomic_float_features;
    priv.atomic_float_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
    priv.atomic_float_features.pNext = nullptr;

#define COPY_FEATURE(DST, NAME) (DST).features.NAME = dev_features.features.NAME;
    COPY_FEATURE(priv.features, shaderImageGatherExtended)
    COPY_FEATURE(priv.features, shaderStorageImageReadWithoutFormat)
    COPY_FEATURE(priv.features, shaderStorageImageWriteWithoutFormat)
    COPY_FEATURE(priv.features, fragmentStoresAndAtomics)
    COPY_FEATURE(priv.features, vertexPipelineStoresAndAtomics)
    COPY_FEATURE(priv.features, shaderInt64)
    COPY_FEATURE(priv.features, shaderInt16)
    COPY_FEATURE(priv.features, shaderFloat64)
#undef COPY_FEATURE

    priv.features_1_1.samplerYcbcrConversion = dev_features_1_1.samplerYcbcrConversion;
    priv.features_1_1.storagePushConstant16 = dev_features_1_1.storagePushConstant16;

    priv.features_1_2.timelineSemaphore = 1;
    priv.features_1_2.bufferDeviceAddress = dev_features_1_2.bufferDeviceAddress;
    priv.features_1_2.storagePushConstant8 = dev_features_1_2.storagePushConstant8;
    priv.features_1_2.shaderInt8 = dev_features_1_2.shaderInt8;
    priv.features_1_2.storageBuffer8BitAccess = dev_features_1_2.storageBuffer8BitAccess;
    priv.features_1_2.uniformAndStorageBuffer8BitAccess = dev_features_1_2.uniformAndStorageBuffer8BitAccess;
    priv.features_1_2.shaderFloat16 = dev_features_1_2.shaderFloat16;
    priv.features_1_2.shaderSharedInt64Atomics = dev_features_1_2.shaderSharedInt64Atomics;
    priv.features_1_2.vulkanMemoryModel = dev_features_1_2.vulkanMemoryModel;
    priv.features_1_2.vulkanMemoryModelDeviceScope = dev_features_1_2.vulkanMemoryModelDeviceScope;

    priv.features_1_3.synchronization2 = dev_features_1_3.synchronization2;
    priv.features_1_3.computeFullSubgroups = dev_features_1_3.computeFullSubgroups;
    priv.features_1_3.shaderZeroInitializeWorkgroupMemory = dev_features_1_3.shaderZeroInitializeWorkgroupMemory;
    priv.features_1_3.maintenance4 = dev_features_1_3.maintenance4;

    priv.desc_buf_features.descriptorBuffer = desc_buf_features.descriptorBuffer;
    priv.desc_buf_features.descriptorBufferPushDescriptors = desc_buf_features.descriptorBufferPushDescriptors;

    priv.atomic_float_features.shaderBufferFloat32Atomics = atomic_float_features.shaderBufferFloat32Atomics;
    priv.atomic_float_features.shaderBufferFloat32AtomicAdd = atomic_float_features.shaderBufferFloat32AtomicAdd;

    dev_info.pNext = &priv.features;

    /// Now setup the queue families on the chosen physical device
    u32 qf_properties_count;
    vk.GetPhysicalDeviceQueueFamilyProperties2(sys_vk.SelectedPhysicalDevice(), &qf_properties_count, nullptr);
    assert(qf_properties_count);
    sys_vk._qf_properties.resize(qf_properties_count);
    sys_vk._qf_video_properties.resize(qf_properties_count);
    sys_vk._qf_query_support.resize(qf_properties_count);
    for (u32 i = 0; i < qf_properties_count; i++)
    {
        sys_vk._qf_properties[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        sys_vk._qf_video_properties[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
        sys_vk._qf_query_support[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR;
        sys_vk._qf_video_properties[i].pNext = &sys_vk._qf_query_support[i];
        sys_vk._qf_properties[i].pNext = &sys_vk._qf_video_properties[i];
    }
    vk.GetPhysicalDeviceQueueFamilyProperties2(sys_vk.SelectedPhysicalDevice(), &qf_properties_count, sys_vk._qf_properties.data());

    sys_vk._qf_mutexs.resize(qf_properties_count);
    for (u32 i = 0; i < qf_properties_count; i++) {
        auto& qf_mutex = sys_vk._qf_mutexs[i];
        qf_mutex.resize(sys_vk._qf_properties[i].queueFamilyProperties.queueCount);
        for (auto& mutex : qf_mutex) {
            pthread_mutex_init(&mutex, nullptr);
        }
    }

    printf("Queue families:\n");
    for (u32 i = 0; i < sys_vk._qf_properties.size(); i++) {
        auto flags = sys_vk._qf_properties[i].queueFamilyProperties.queueFlags;
        printf("    %i:%s%s%s%s%s%s%s (queues: %i)", i,
            ((flags) & VK_QUEUE_GRAPHICS_BIT) ? " graphics" : "",
            ((flags) & VK_QUEUE_COMPUTE_BIT) ? " compute" : "",
            ((flags) & VK_QUEUE_TRANSFER_BIT) ? " transfer" : "",
            ((flags) & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) ? " encode" : "",
            ((flags) & VK_QUEUE_VIDEO_DECODE_BIT_KHR) ? " decode" : "",
            ((flags) & VK_QUEUE_SPARSE_BINDING_BIT) ? " sparse" : "",
            ((flags) & VK_QUEUE_PROTECTED_BIT) ? " protected" : "",
            sys_vk._qf_properties[i].queueFamilyProperties.queueCount);
        
        if (sys_vk._qf_video_properties[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
            printf(" (decode H264)");
        if (sys_vk._qf_video_properties[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)
            printf(" (decode H265)");
        if (sys_vk._qf_video_properties[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_MESA)
            printf(" (decode AV1)");
        if (sys_vk._qf_video_properties[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_EXT)
            printf(" (encode H264)");
        if (sys_vk._qf_video_properties[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_EXT)
            printf(" (encode H265)");
        printf("\n");

        /* We use this field to keep a score of how many times we've used that
         * queue family in order to make better choices. */
        sys_vk._qf_properties[i].queueFamilyProperties.timestampValidBits = 0;
    }

    auto pick_queue_family = [&sys_vk](VkQueueFlagBits flags) {
        int index = -1;
        uint32_t min_score = UINT32_MAX;

        for (u32 i = 0; i < sys_vk._qf_properties.size(); i++) {
            const VkQueueFlags qflags = sys_vk._qf_properties[i].queueFamilyProperties.queueFlags;
            if (qflags & flags) {
                uint32_t score = std::__popcount(qflags) + sys_vk._qf_properties[i].queueFamilyProperties.timestampValidBits;
                if (score < min_score) {
                    index = i;
                    min_score = score;
                }
            }
        }

        if (index > -1)
            sys_vk._qf_properties[index].queueFamilyProperties.timestampValidBits++;

        return index;
    };

    /* Pick each queue family to use */
    int graph_index, comp_index, tx_index, enc_index, dec_index;
    graph_index = pick_queue_family(VK_QUEUE_GRAPHICS_BIT);
    comp_index = pick_queue_family(VK_QUEUE_COMPUTE_BIT);
    tx_index = pick_queue_family(VK_QUEUE_TRANSFER_BIT);
    enc_index = pick_queue_family(VK_QUEUE_VIDEO_ENCODE_BIT_KHR);
    dec_index = pick_queue_family(VK_QUEUE_VIDEO_DECODE_BIT_KHR);

    /* Signalling the transfer capabilities on a queue family is optional */
    if (tx_index < 0) {
        tx_index = pick_queue_family(VK_QUEUE_COMPUTE_BIT);
        if (tx_index < 0)
            tx_index = pick_queue_family(VK_QUEUE_GRAPHICS_BIT);
    }

    sys_vk.queue_family_index = -1;
    sys_vk.queue_family_comp_index = -1;
    sys_vk.queue_family_tx_index = -1;
    sys_vk.queue_family_encode_index = -1;
    sys_vk.queue_family_decode_index = -1;

    float* weights;

#define SETUP_QUEUE(qf_idx)                                                       \
    if (qf_idx > -1) {                                                            \
        int fidx = qf_idx;                                                        \
        u32 qc = sys_vk._qf_properties[fidx].queueFamilyProperties.queueCount;    \
        VkDeviceQueueCreateInfo* pc;                                              \
                                                                                  \
        if (fidx == graph_index) {                                                \
            sys_vk.queue_family_index = fidx;                                     \
            sys_vk.nb_graphics_queues = qc;                                       \
            graph_index = -1;                                                     \
        }                                                                         \
        if (fidx == comp_index) {                                                 \
            sys_vk.queue_family_comp_index = fidx;                                \
            sys_vk.nb_comp_queues = qc;                                           \
            comp_index = -1;                                                      \
        }                                                                         \
        if (fidx == tx_index) {                                                   \
            sys_vk.queue_family_tx_index = fidx;                                  \
            sys_vk.nb_tx_queues = qc;                                             \
            tx_index = -1;                                                        \
        }                                                                         \
        if (fidx == enc_index) {                                                  \
            sys_vk.queue_family_encode_index = fidx;                              \
            sys_vk.nb_encode_queues = qc;                                         \
            enc_index = -1;                                                       \
        }                                                                         \
        if (fidx == dec_index) {                                                  \
            sys_vk.queue_family_decode_index = fidx;                              \
            sys_vk.nb_decode_queues = qc;                                         \
            dec_index = -1;                                                       \
        }                                                                         \
                                                                                  \
        pc = (VkDeviceQueueCreateInfo*)realloc((void*)dev_info.pQueueCreateInfos, \
            sizeof(*pc) * (dev_info.queueCreateInfoCount + 1));                   \
        if (!pc) {                                                                \
            XERROR(1, "Bad memory");                                              \
        }                                                                         \
        dev_info.pQueueCreateInfos = pc;                                          \
        pc = &pc[dev_info.queueCreateInfoCount];                                  \
                                                                                  \
        weights = (float*)malloc(qc * sizeof(float));                             \
        if (!weights) {                                                           \
            XERROR(1, "Bad memory");                                              \
        }                                                                         \
                                                                                  \
        memset(pc, 0, sizeof(*pc));                                               \
        pc->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;                   \
        pc->queueFamilyIndex = fidx;                                              \
        pc->queueCount = qc;                                                      \
        pc->pQueuePriorities = weights;                                           \
                                                                                  \
        for (u32 i = 0; i < qc; i++)                                              \
            weights[i] = 1.0f / static_cast<float>(qc);                           \
                                                                                  \
        dev_info.queueCreateInfoCount++;                                          \
    }

    SETUP_QUEUE(graph_index)
    SETUP_QUEUE(comp_index)
    SETUP_QUEUE(tx_index)
    SETUP_QUEUE(enc_index)
    SETUP_QUEUE(dec_index)

    ASSERT(sys_vk.queue_family_decode_index > -1);

#undef SETUP_QUEUE

    {
    } // Emacs sucks
    // Now check the device has the supported extensions for video

    std::vector<const char*> enabled_device_extensions;
    check_device_extensions(sys_vk, enabled_device_extensions);

    dev_info.ppEnabledExtensionNames = enabled_device_extensions.data();
    dev_info.enabledExtensionCount = enabled_device_extensions.size();

    VkResult res = vk.CreateDevice(sys_vk.SelectedPhysicalDevice(), &dev_info, nullptr,
        &sys_vk._active_dev);
    for (u32 i = 0; i < dev_info.queueCreateInfoCount; i++)
        free((void*)dev_info.pQueueCreateInfos[i].pQueuePriorities);
    free((void*)dev_info.pQueueCreateInfos);

    if (res != VK_SUCCESS) {
        for (u32 i = 0; i < dev_info.enabledExtensionCount; i++)
            free((void*)dev_info.ppEnabledExtensionNames[i]);
        free((void*)dev_info.ppEnabledExtensionNames);
        XERROR(1, "Device creation failure: %s\n",
            vk_ret2str(res));
    }

    sys_vk._active_dev_enabled_exts.assign(dev_info.ppEnabledExtensionNames, dev_info.ppEnabledExtensionNames + dev_info.enabledExtensionCount);

    /* Set device extension flags */
    for (u32 i = 0; i < sys_vk._active_dev_enabled_exts.size(); i++) {
        for (u32 j = 0; j < ARRAY_ELEMS(optional_device_exts); j++) {
            if (!strcmp(sys_vk._active_dev_enabled_exts[i],
                    optional_device_exts[j].name)) {
                sys_vk.extensions |= optional_device_exts[j].flag;
                break;
            }
        }
    }
}

static VkBool32 VKAPI_CALL vk_dbg_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
        fprintf(stderr, "%s\n", data->pMessage);
        for (u32 i = 0; i < data->cmdBufLabelCount; i++)
            fprintf(stderr, "\t%i: %s\n", i, data->pCmdBufLabels[i].pLabelName);
        break;
    }
    default:
        break;
    }

    return 0;
}

bool init_vulkan(SysVulkan& sys_vk)
{
    auto& vk = sys_vk._vfn;

    // Find the Vulkan loader
    static const std::array<const char*, 2> libnames = {
        "libvulkan.so.1",
        "libvulkan.so"
    };

    for (const char* libname : libnames) {

#ifdef LINUX
        sys_vk._libvulkan = dlopen(libname, RTLD_NOW | RTLD_LOCAL);
#else
#error "Unsupported platform"
#endif
        if (sys_vk._libvulkan)
            break;
    }

    if (!sys_vk._libvulkan) {
#ifdef LINUX
        fprintf(stderr, "%s\n", dlerror());
#else
#error "Unsupported platform"
#endif
        return false;
    }

#ifdef LINUX
    dlerror(); // clear any existing error
#endif

#ifdef LINUX
    sys_vk._get_proc_addr = (PFN_vkGetInstanceProcAddr)dlsym(sys_vk._libvulkan, "vkGetInstanceProcAddr");
#else
#error "Unsupported platform"
#endif

    util::Timer t;
    u64 ms;

    t.GetCurrentTime();
    load_vk_functions(sys_vk);
    ms = t.ElapsedMicroseconds();
    printf("DEBUG: core load_vk_functions took %" PRIu64 " us\n", ms);
    
    t.GetCurrentTime();
    load_instance(sys_vk);
    ms = t.ElapsedMilliseconds();
    printf("DEBUG: load_instance took %" PRIu64 " ms\n", ms);

    t.GetCurrentTime();
    load_vk_functions(sys_vk, FF_VK_EXT_NO_FLAG, true, false);
    ms = t.ElapsedMicroseconds();
    printf("DEBUG: full load_vk_functions took %" PRIu64 " us\n", ms);

    if (sys_vk._options.enable_validation) {
        VkDebugUtilsMessengerCreateInfoEXT dbg = {};
        dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = vk_dbg_callback;
        dbg.pUserData = nullptr;

        vk.CreateDebugUtilsMessengerEXT(sys_vk._instance, &dbg, nullptr, &sys_vk._dev_debug_ctx);
    }

    choose_and_load_device(sys_vk);

    // Fill in everything else needed now that an instance and a physical device are available.
    load_vk_functions(sys_vk, sys_vk.extensions, true, true);

    vk.GetDeviceQueue(sys_vk._active_dev, sys_vk.queue_family_decode_index, 0, &sys_vk._decode_queue0);
    vk.GetDeviceQueue(sys_vk._active_dev, sys_vk.queue_family_tx_index, 0, &sys_vk._tx_queue0);
    ASSERT(sys_vk._decode_queue0 != VK_NULL_HANDLE && sys_vk._tx_queue0 != VK_NULL_HANDLE);

    auto& device_priv = sys_vk._selected_physical_device_priv;

    device_priv.props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    device_priv.props.pNext = &device_priv.external_memory_host_props;
    device_priv.external_memory_host_props.sType = (VkStructureType)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
    device_priv.external_memory_host_props.pNext = nullptr;

    vk.GetPhysicalDeviceProperties2(sys_vk.SelectedPhysicalDevice(), &device_priv.props);
    printf("Using device: %s\n",
        device_priv.props.properties.deviceName);
    printf("Physical device alignments:\n");
    printf("    optimalBufferCopyRowPitchAlignment: %" PRIu64 "\n",
        device_priv.props.properties.limits.optimalBufferCopyRowPitchAlignment);
    printf("    minMemoryMapAlignment:              %ld\n",
        device_priv.props.properties.limits.minMemoryMapAlignment);
    printf("    nonCoherentAtomSize:                %" PRIu64 "\n",
        device_priv.props.properties.limits.nonCoherentAtomSize);
    if (sys_vk.extensions & FF_VK_EXT_EXTERNAL_HOST_MEMORY)
        printf("    minImportedHostPointerAlignment:    %" PRIu64 "\n",
            device_priv.external_memory_host_props.minImportedHostPointerAlignment);

    // Create the GPU memory allocator
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = sys_vk._get_proc_addr;
    vulkanFunctions.vkGetDeviceProcAddr = vk.GetDeviceProcAddr;
 
    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorCreateInfo.physicalDevice = sys_vk.SelectedPhysicalDevice();
    allocatorCreateInfo.device = sys_vk._active_dev;
    allocatorCreateInfo.instance = sys_vk._instance;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
 
    VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &sys_vk._allocator));
    return true;
}

struct VideoProfile
{
    VkVideoDecodeUsageInfoKHR _decode_usage_info;
    VkVideoProfileInfoKHR _profile_info;
    union {
        VkVideoDecodeAV1ProfileInfoMESA av1;
        VkVideoDecodeH264ProfileInfoKHR avc;
    } _decode_codec_profile;
};
bool VideoProfilesDiffer(const VideoProfile& a, const VideoProfile& b)
{
    if (a._profile_info.videoCodecOperation != b._profile_info.videoCodecOperation)
        return true;

    if (!(a._decode_usage_info.videoUsageHints == b._decode_usage_info.videoUsageHints &&
            a._profile_info.chromaBitDepth == b._profile_info.chromaBitDepth &&
            a._profile_info.lumaBitDepth == b._profile_info.lumaBitDepth &&
            a._profile_info.chromaSubsampling == b._profile_info.chromaSubsampling))
        return true;
       

    switch(a._profile_info.videoCodecOperation)
    {
        case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_MESA:
            return a._decode_codec_profile.av1.stdProfileIdc == b._decode_codec_profile.av1.stdProfileIdc;
        case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
            return a._decode_codec_profile.avc.stdProfileIdc == b._decode_codec_profile.avc.stdProfileIdc && \
                a._decode_codec_profile.avc.pictureLayout == b._decode_codec_profile.avc.pictureLayout;
        default: ASSERT(false);
    }
    return false;
}
VideoProfile AvcProgressive420Profile()
{
    VideoProfile avc_profile = {};
    avc_profile._decode_usage_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR;
    avc_profile._decode_usage_info.pNext = nullptr;
    avc_profile._decode_usage_info.videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR;
    avc_profile._decode_codec_profile.avc.sType =  VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
    avc_profile._decode_codec_profile.avc.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;
    avc_profile._decode_codec_profile.avc.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
    avc_profile._decode_codec_profile.avc.pNext = nullptr;
    avc_profile._profile_info.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    avc_profile._profile_info.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    avc_profile._profile_info.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    avc_profile._profile_info.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    avc_profile._profile_info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
    avc_profile._profile_info.pNext = &avc_profile._decode_codec_profile.avc;
    return avc_profile;
}
VideoProfile Av1Progressive420Profile()
{
    VideoProfile av1_profile = {};
    av1_profile._decode_usage_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR;
    av1_profile._decode_usage_info.pNext = nullptr;
    av1_profile._decode_usage_info.videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR;
    av1_profile._decode_codec_profile.av1.sType =  VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_MESA;
    av1_profile._decode_codec_profile.av1.stdProfileIdc = STD_VIDEO_AV1_MESA_PROFILE_MAIN;
    av1_profile._decode_codec_profile.av1.pNext = nullptr;
    av1_profile._profile_info.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    av1_profile._profile_info.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    av1_profile._profile_info.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    av1_profile._profile_info.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    av1_profile._profile_info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_MESA;
    av1_profile._profile_info.pNext = &av1_profile._decode_codec_profile.av1;
    return av1_profile;
}

enum TransitionType
{
    TRANSITION_IMAGE_INITIALIZE,
    TRANSITION_IMAGE_TRANSFER_TO_HOST,
    TRANSITION_IMAGE_DPB_TO_DST,
    TRANSITION_BUFFER_FOR_READING,
};

struct Dpb
{
    VkImageCreateInfo _dpb_image_info;
    VmaAllocationCreateInfo _dpb_alloc_create_info;
    VmaAllocation _dpb_allocation;
    VkImage _dpb_images;
    VkImageView _dpb_slot_views[16]; // check min / max caps
    VkVideoPictureResourceInfoKHR _dpb_slot_picture_resource_infos[16];

    VkImageCreateInfo _dst_image_info;
    VmaAllocationCreateInfo _dst_alloc_create_info;
    VmaAllocation _dst_allocation;
    VkImage _dst_images; // One used for non-coincident cases (AMD only currently)
    VkImageView _dst_slot_views[16]; // check min / max caps
    VkVideoPictureResourceInfoKHR _dst_slot_picture_resource_infos[16];

    bool _coincident_image_resources = true;

    std::vector<VkImageMemoryBarrier2> SlotBarriers(TransitionType trans_type, u32 slot_idx)
    {
        std::vector<VkImageMemoryBarrier2> r;
        VkImageMemoryBarrier2 dpb_barrier = {};
        VkImageMemoryBarrier2 dst_barrier = {};
        dpb_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        dpb_barrier.pNext = nullptr;
        dpb_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dpb_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // concurrent usage is enabled
        dpb_barrier.image = _dpb_images;
        dpb_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        dpb_barrier.subresourceRange.baseMipLevel = 0;
        dpb_barrier.subresourceRange.levelCount = 1;
        dpb_barrier.subresourceRange.baseArrayLayer = slot_idx;
        dpb_barrier.subresourceRange.layerCount = 1;
        switch(trans_type)
        {
            case TRANSITION_IMAGE_INITIALIZE:
                dpb_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE_KHR;
                dpb_barrier.srcAccessMask = VK_ACCESS_2_NONE_KHR;
                dpb_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
                dpb_barrier.dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
                dpb_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                dpb_barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
                dst_barrier = dpb_barrier;
                dst_barrier.image = _dst_images;
                dst_barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
                r.push_back(dpb_barrier);
                if (!_coincident_image_resources)
                    r.push_back(dst_barrier);
                return r;
            case TRANSITION_IMAGE_DPB_TO_DST:
                dpb_barrier.srcStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
                dpb_barrier.srcAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
                dpb_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
                dpb_barrier.dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
                dpb_barrier.oldLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
                dpb_barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
                r.push_back(dpb_barrier);
                ASSERT(_coincident_image_resources);
                return r;
            case TRANSITION_IMAGE_TRANSFER_TO_HOST:
                dpb_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                dpb_barrier.srcAccessMask = VK_ACCESS_2_NONE_KHR;
                dpb_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
                dpb_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT_KHR;
                dpb_barrier.oldLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
                dpb_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                dst_barrier = dpb_barrier;
                dst_barrier.image = _dst_images;
                dst_barrier.oldLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
                if (!_coincident_image_resources)
                    r.push_back(dst_barrier);
                else
                    r.push_back(dpb_barrier);
                return r;
                break;
        }

        return r;
    }

    void CopySlotToBuffer(vvb::SysVulkan* sys_vk, VkCommandBuffer cmd_buf, u32 slot_idx, u32 width_samples,
        u32 buf_pitch, u32 buf_height, VkImageAspectFlags aspect_mask, VkBuffer buffer)
    {
        auto& vk = sys_vk->_vfn;
        VkBufferImageCopy copy_region = {};
        copy_region.bufferOffset = 0;
        copy_region.bufferRowLength = buf_pitch;
        copy_region.bufferImageHeight = buf_height;
        copy_region.imageSubresource.aspectMask = aspect_mask;
        copy_region.imageSubresource.mipLevel = 0;
        copy_region.imageSubresource.baseArrayLayer = slot_idx;
        copy_region.imageSubresource.layerCount = 1;
        copy_region.imageOffset = { 0, 0, 0 };
        copy_region.imageExtent = { width_samples, buf_height, 1 };
        VkImage dst_image = _coincident_image_resources ? _dpb_images : _dst_images;
        vk.CmdCopyImageToBuffer(cmd_buf, dst_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            buffer, 1, &copy_region);
    }

    VkVideoPictureResourceInfoKHR SlotDstPictureResource(u32 slot_idx)
    {
        if (_coincident_image_resources)
        {
            return _dpb_slot_picture_resource_infos[slot_idx];
        }
        else
        {
            return _dst_slot_picture_resource_infos[slot_idx];
        }
    }
};

Dpb CreateDpbResource(vvb::SysVulkan* sys_vk, u32 width, u32 height, u32 num_slots,
    bool coincident_image_resources,
    VkImageUsageFlags dpb_usage, VkFormat dpb_format, VkComponentMapping dpb_view_component_map,
    VkImageUsageFlags dst_usage, VkFormat dst_format, VkComponentMapping dst_view_component_map,
    VkVideoProfileListInfoKHR* profile_list = nullptr)
{
    auto& vk = sys_vk->_vfn;
    Dpb r = {};
    printf("Dpb is %lu bytes\n", sizeof(r));
    static_assert(sizeof(r) < 4000, "Dpb is too big");
    ASSERT(num_slots < 16);

    r._coincident_image_resources = coincident_image_resources;

    r._dpb_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    r._dpb_image_info.pNext = profile_list;
    r._dpb_image_info.flags = 0;
    r._dpb_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    r._dpb_image_info.imageType = VK_IMAGE_TYPE_2D;
    r._dpb_image_info.format = dpb_format;
    r._dpb_image_info.extent = VkExtent3D{width, height, 1};
    r._dpb_image_info.mipLevels = 1;
    r._dpb_image_info.arrayLayers = num_slots;
    r._dpb_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    r._dpb_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    r._dpb_image_info.usage = dpb_usage;
    r._dpb_image_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
    u32 queue_family_indices[2] = {(u32)sys_vk->queue_family_decode_index, (u32)sys_vk->queue_family_tx_index};
    r._dpb_image_info.queueFamilyIndexCount = 2;
    r._dpb_image_info.pQueueFamilyIndices = queue_family_indices;
    r._dpb_alloc_create_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    r._dpb_alloc_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(sys_vk->_allocator,
        &r._dpb_image_info,
        &r._dpb_alloc_create_info,
        &r._dpb_images,
        &r._dpb_allocation,
        nullptr));
    
    // Now reuse the above to create the dst images. This are required for non-coincident implementations like AMD
    // A little heavy handed to always allocate them, but makes life simpler.
    r._dst_image_info = r._dpb_image_info;
    r._dst_image_info.format = dst_format;
    r._dst_image_info.usage = dst_usage;
    VK_CHECK(vmaCreateImage(sys_vk->_allocator,
        &r._dst_image_info,
        &r._dst_alloc_create_info,
        &r._dst_images,
        &r._dst_allocation,
        nullptr));

    for (u32 slot_idx = 0; slot_idx < num_slots; slot_idx++)
    {
        VkImageViewUsageCreateInfo dpb_view_usage_info = {};
        dpb_view_usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        dpb_view_usage_info.usage = dpb_usage;
        VkImageViewUsageCreateInfo dst_view_usage_info = {};
        dst_view_usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        dst_view_usage_info.usage = dst_usage;
        VkImageViewCreateInfo image_view_info = {};
        image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_info.pNext = &dpb_view_usage_info;
        image_view_info.flags = 0;
        image_view_info.image = r._dpb_images;
        image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D; // todo: 2d arrays are also supported but not tested
        image_view_info.format = dpb_format;
        image_view_info.components = dpb_view_component_map;
        image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_info.subresourceRange.baseMipLevel = 0;
        image_view_info.subresourceRange.levelCount = 1;
        image_view_info.subresourceRange.baseArrayLayer = slot_idx;
        image_view_info.subresourceRange.layerCount = 1;
        VK_CHECK(vk.CreateImageView(sys_vk->_active_dev, &image_view_info, nullptr, &r._dpb_slot_views[slot_idx]));
        if (!coincident_image_resources)
        {
            image_view_info.pNext = &dst_view_usage_info;
            image_view_info.image = r._dst_images;
            image_view_info.format = dst_format;
            image_view_info.components = dst_view_component_map;
            VK_CHECK(vk.CreateImageView(sys_vk->_active_dev, &image_view_info, nullptr, &r._dst_slot_views[slot_idx]));
        }
        r._dpb_slot_picture_resource_infos[slot_idx].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
        r._dpb_slot_picture_resource_infos[slot_idx].pNext = nullptr;
        r._dpb_slot_picture_resource_infos[slot_idx].codedOffset = VkOffset2D{0, 0};
        r._dpb_slot_picture_resource_infos[slot_idx].codedExtent = VkExtent2D{width, height};
        r._dpb_slot_picture_resource_infos[slot_idx].baseArrayLayer = 0;
        r._dpb_slot_picture_resource_infos[slot_idx].imageViewBinding = r._dpb_slot_views[slot_idx];
        if (!coincident_image_resources)
        {
            r._dst_slot_picture_resource_infos[slot_idx].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
            r._dst_slot_picture_resource_infos[slot_idx].pNext = nullptr;
            r._dst_slot_picture_resource_infos[slot_idx].codedOffset = VkOffset2D{0, 0};
            r._dst_slot_picture_resource_infos[slot_idx].codedExtent = VkExtent2D{width, height};
            r._dst_slot_picture_resource_infos[slot_idx].baseArrayLayer = 0;
            r._dst_slot_picture_resource_infos[slot_idx].imageViewBinding = r._dst_slot_views[slot_idx];
        }
    }
    return r;
}
void DestroyDpbResource(vvb::SysVulkan* sys_vk, Dpb* r)
{
    auto& vk = sys_vk->_vfn;
    for (u32 slot_idx = 0; slot_idx < r->_dpb_image_info.arrayLayers; slot_idx++)
    {
        vk.DestroyImageView(sys_vk->_active_dev, r->_dpb_slot_views[slot_idx], nullptr);
        vk.DestroyImageView(sys_vk->_active_dev, r->_dst_slot_views[slot_idx], nullptr);
    }
    vmaDestroyImage(sys_vk->_allocator, r->_dpb_images, r->_dpb_allocation);
    vmaDestroyImage(sys_vk->_allocator, r->_dst_images, r->_dst_allocation);
}

struct BufferResource
{
    VkBuffer _buffer;
    VmaAllocation _allocation;
    VkBufferCreateInfo _create_info;
    VmaAllocationCreateInfo _alloc_info;

    VkBufferMemoryBarrier2 Barrier(TransitionType trans_type)
    {
        VkBufferMemoryBarrier2 r = {};
        switch (trans_type)
        {
        case TRANSITION_BUFFER_FOR_READING:
            r.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            r.pNext = nullptr;
            r.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            r.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            r.dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
            r.dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR;
            r.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            r.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            r.buffer = _buffer;
            r.offset = 0;
            r.size = _create_info.size;
            return r;
        default:
            ASSERT(false);
        }
    }
};
BufferResource CreateBufferResource(vvb::SysVulkan* sys_vk, VkDeviceSize size,
    VkBufferUsageFlags usage, VmaMemoryUsage mem_usage, VmaAllocationCreateFlags alloc_flags = 0,
    VkVideoProfileListInfoKHR* profile_list = nullptr)
{
    BufferResource r = {};

    r._create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    r._create_info.pNext = profile_list;
    r._create_info.size = size;
    r._create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r._create_info.queueFamilyIndexCount = 0;
    r._create_info.pQueueFamilyIndices = nullptr;
    r._create_info.usage = usage;
    r._alloc_info.usage = mem_usage;
    // Note! This is rather subtle. Since I don't intend to read the bitstream back to the CPU, it seems
    // I can use uncached combined memory for extra performance.
    r._alloc_info.flags = alloc_flags;
    vmaCreateBuffer(sys_vk->_allocator,
        &r._create_info,
        &r._alloc_info,
        &r._buffer,
        &r._allocation,
        nullptr);
    return r;
}
void DestroyBufferResource(vvb::SysVulkan* sys_vk, BufferResource* r)
{
    vmaDestroyBuffer(sys_vk->_allocator, r->_buffer, r->_allocation);
}
struct VideoSession
{
    VkVideoSessionKHR _handle;
    VkVideoSessionParametersKHR _parameters{VK_NULL_HANDLE};
    std::vector<VmaAllocation> _memory_allocations;
    std::vector<VmaAllocationInfo> _memory_allocation_infos;
    std::vector<VkVideoSessionMemoryRequirementsKHR> _memory_requirements;

    VkVideoSessionCreateInfoKHR _create_info;
    VkExtensionProperties _avc_ext_version{
        VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
        VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION
    };
     const VkExtensionProperties _av1_ext_version = {
        VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME,
        VK_MAKE_VERSION(0, 0, 1),
    };
};
VideoSession CreateVideoSession(SysVulkan* sys_vk, const vvb::VideoProfile* avc_profile, VkFormat selected_output_picture_format,
    VkFormat selected_reference_picture_format, const VkVideoCapabilitiesKHR* video_caps, u32 max_dpb_slots, u32 max_reference_slots)
{
    VideoSession session = {};
    const int AVC_MAX_DPB_REF_SLOTS = 16u;
    session._create_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
    session._create_info.pNext = nullptr;
    session._create_info.queueFamilyIndex = sys_vk->queue_family_decode_index;
    session._create_info.flags = 0;
    session._create_info.pVideoProfile = &avc_profile->_profile_info;
    session._create_info.pictureFormat = selected_output_picture_format;
    session._create_info.maxCodedExtent = video_caps->maxCodedExtent;
    session._create_info.referencePictureFormat = selected_reference_picture_format;
    session._create_info.maxDpbSlots = max_dpb_slots; // std::min(video_caps.maxDpbSlots, AVC_MAX_DPB_REF_SLOTS + 1u); // From the H.264 spec, + 1 for the setup slot.
    session._create_info.maxActiveReferencePictures = max_reference_slots; // std::min(video_caps.maxActiveReferencePictures, (u32)AVC_MAX_DPB_REF_SLOTS);
    session._create_info.pStdHeaderVersion = &session._avc_ext_version;

    auto& vk = sys_vk->_vfn;

    VK_CHECK(vk.CreateVideoSessionKHR(sys_vk->_active_dev,
        &session._create_info,
        nullptr,
        &session._handle));

    session._memory_requirements;
    u32 num_reqs = 0;
    VK_CHECK(vk.GetVideoSessionMemoryRequirementsKHR(sys_vk->_active_dev, session._handle, &num_reqs, nullptr));
    session._memory_requirements.resize(num_reqs);
    for (u32 i = 0; i < num_reqs; i++)
        session._memory_requirements[i].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
    VK_CHECK(vk.GetVideoSessionMemoryRequirementsKHR(sys_vk->_active_dev, session._handle, &num_reqs, session._memory_requirements.data()));
    
    session._memory_allocations.resize(session._memory_requirements.size());
    std::vector<VmaAllocationInfo> session_memory_allocation_infos(session._memory_requirements.size());
    VmaAllocationCreateInfo session_memory_alloc_create_info = {};
    for (u32 i = 0; i < session._memory_requirements.size(); i++) {
        auto& mem_req = session._memory_requirements[i];
        auto& allocation = session._memory_allocations[i];
        auto& allocation_info = session_memory_allocation_infos[i];
        session_memory_alloc_create_info.memoryTypeBits = mem_req.memoryRequirements.memoryTypeBits;
        VK_CHECK(vmaAllocateMemory(sys_vk->_allocator, &mem_req.memoryRequirements, &session_memory_alloc_create_info,
                &allocation, &allocation_info));

        if (allocation_info.size > 1024*1024)
            printf("Allocated %lu MB of session memory for index %d\n", allocation_info.size / 1024 / 1024, i);
        else
            printf("Allocated %lu KB of session memory for index %d\n", allocation_info.size / 1024, i);

        VkBindVideoSessionMemoryInfoKHR bind_session_memory_info = {};
        bind_session_memory_info.sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
        bind_session_memory_info.pNext = nullptr;
        bind_session_memory_info.memoryBindIndex = i;
        bind_session_memory_info.memory = allocation_info.deviceMemory;
        bind_session_memory_info.memoryOffset = allocation_info.offset;
        bind_session_memory_info.memorySize = allocation_info.size;

        VK_CHECK(vk.BindVideoSessionMemoryKHR(sys_vk->_active_dev, session._handle, 1, &bind_session_memory_info));
    }
    return session;
}
void DestroyVideoSession(SysVulkan* sys_vk, VideoSession* session)
{
    auto& vk = sys_vk->_vfn;
    vk.DestroyVideoSessionKHR(sys_vk->_active_dev, session->_handle, nullptr);
    for (auto& alloc : session->_memory_allocations)
        vmaFreeMemory(sys_vk->_allocator, alloc);
    if (session->_parameters != VK_NULL_HANDLE)
        vk.DestroyVideoSessionParametersKHR(sys_vk->_active_dev, session->_parameters, nullptr);
}

void AddSessionParameters(SysVulkan* sys_vk, vvb::VideoSession* session)
{
    auto& vk = sys_vk->_vfn;
    StdVideoH264SequenceParameterSet avc_std_sps = {};
    avc_std_sps.flags.direct_8x8_inference_flag = 1;
    avc_std_sps.flags.mb_adaptive_frame_field_flag = 0;
    avc_std_sps.flags.frame_mbs_only_flag = 1;
    avc_std_sps.flags.delta_pic_order_always_zero_flag = 0;
    avc_std_sps.flags.separate_colour_plane_flag = 0;
    avc_std_sps.flags.gaps_in_frame_num_value_allowed_flag = 0;
    avc_std_sps.flags.qpprime_y_zero_transform_bypass_flag = 0;
    avc_std_sps.flags.frame_cropping_flag = 0;
    avc_std_sps.flags.seq_scaling_matrix_present_flag = 0;
    avc_std_sps.flags.vui_parameters_present_flag = 1;
    avc_std_sps.profile_idc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
    avc_std_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_1;
    avc_std_sps.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
    avc_std_sps.seq_parameter_set_id = 0;
    avc_std_sps.bit_depth_luma_minus8 = 0;
    avc_std_sps.bit_depth_chroma_minus8 = 0;
    avc_std_sps.log2_max_frame_num_minus4 = 0;
    avc_std_sps.pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_2;
    avc_std_sps.offset_for_non_ref_pic = 0;
    avc_std_sps.offset_for_top_to_bottom_field = 0;
    avc_std_sps.log2_max_pic_order_cnt_lsb_minus4 = 0;
    avc_std_sps.num_ref_frames_in_pic_order_cnt_cycle = 0;
    avc_std_sps.max_num_ref_frames = 3;
    avc_std_sps.pic_width_in_mbs_minus1 = 10;
    avc_std_sps.pic_height_in_map_units_minus1 = 8;
    avc_std_sps.frame_crop_left_offset = 0;
    avc_std_sps.frame_crop_right_offset = 0;
    avc_std_sps.frame_crop_top_offset = 0;
    avc_std_sps.frame_crop_bottom_offset = 0;
    int32_t offset_for_ref_frame[] = {0};
    avc_std_sps.pOffsetForRefFrame = offset_for_ref_frame;
    avc_std_sps.pScalingLists = nullptr;
    StdVideoH264SequenceParameterSetVui avc_std_sps_vui = {};
    avc_std_sps_vui.flags.aspect_ratio_info_present_flag = 0;
    avc_std_sps_vui.flags.overscan_info_present_flag = 0;
    avc_std_sps_vui.flags.overscan_appropriate_flag = 0;
    avc_std_sps_vui.flags.video_signal_type_present_flag = 0;
    avc_std_sps_vui.flags.video_full_range_flag = 0;
    avc_std_sps_vui.flags.color_description_present_flag = 0;
    avc_std_sps_vui.flags.timing_info_present_flag = 1;
    avc_std_sps_vui.flags.fixed_frame_rate_flag = 0;
    avc_std_sps_vui.flags.bitstream_restriction_flag = 1;
    avc_std_sps_vui.flags.nal_hrd_parameters_present_flag = 0;
    avc_std_sps_vui.flags.vcl_hrd_parameters_present_flag = 0;
    avc_std_sps_vui.aspect_ratio_idc = STD_VIDEO_H264_ASPECT_RATIO_IDC_UNSPECIFIED;
    avc_std_sps_vui.sar_width = 1;
    avc_std_sps_vui.sar_height = 1;
    avc_std_sps_vui.video_format = 0;
    avc_std_sps_vui.colour_primaries = 0;
    avc_std_sps_vui.transfer_characteristics = 0;
    avc_std_sps_vui.num_units_in_tick = 1;
    avc_std_sps_vui.time_scale = 60;
    avc_std_sps_vui.max_num_reorder_frames = 0;
    avc_std_sps_vui.max_dec_frame_buffering = 3;
    avc_std_sps_vui.chroma_sample_loc_type_top_field = 0;
    avc_std_sps_vui.chroma_sample_loc_type_bottom_field = 0;
    StdVideoH264HrdParameters avc_std_sps_vui_hrd = {};
    avc_std_sps_vui_hrd.cpb_cnt_minus1 = 0;
    avc_std_sps_vui_hrd.bit_rate_scale = 0;
    avc_std_sps_vui_hrd.cpb_size_scale = 0;
    avc_std_sps_vui_hrd.initial_cpb_removal_delay_length_minus1 = 23;
    avc_std_sps_vui_hrd.cpb_removal_delay_length_minus1 = 0;
    avc_std_sps_vui_hrd.dpb_output_delay_length_minus1 = 0;
    avc_std_sps_vui_hrd.time_offset_length = 0;
    avc_std_sps_vui.pHrdParameters = &avc_std_sps_vui_hrd;
    avc_std_sps.pSequenceParameterSetVui = &avc_std_sps_vui;
    
    StdVideoH264PictureParameterSet avc_std_pps = {};
    avc_std_pps.flags.transform_8x8_mode_flag = 1;
    avc_std_pps.flags.redundant_pic_cnt_present_flag = 0;
    avc_std_pps.flags.constrained_intra_pred_flag = 0;
    avc_std_pps.flags.deblocking_filter_control_present_flag = 1;
    avc_std_pps.flags.weighted_pred_flag = 1;
    avc_std_pps.flags.bottom_field_pic_order_in_frame_present_flag = 0;
    avc_std_pps.flags.entropy_coding_mode_flag = 1;
    avc_std_pps.flags.pic_scaling_matrix_present_flag = 0;
    avc_std_pps.seq_parameter_set_id = 0;
    avc_std_pps.pic_parameter_set_id = 0;
    avc_std_pps.num_ref_idx_l0_default_active_minus1 = 2;
    avc_std_pps.num_ref_idx_l1_default_active_minus1 = 0;
    avc_std_pps.weighted_bipred_idc = STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_DEFAULT;
    avc_std_pps.pic_init_qp_minus26 = -16;
    avc_std_pps.pic_init_qs_minus26 = 0;
    avc_std_pps.chroma_qp_index_offset = -2;
    avc_std_pps.second_chroma_qp_index_offset = -2;
    avc_std_pps.pScalingLists = nullptr;

    VkVideoDecodeH264SessionParametersAddInfoKHR avc_params_add_info = {};
    avc_params_add_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR;
    avc_params_add_info.pNext = nullptr;
    avc_params_add_info.stdSPSCount = 1;
    avc_params_add_info.pStdSPSs = &avc_std_sps;
    avc_params_add_info.stdPPSCount = 1;
    avc_params_add_info.pStdPPSs = &avc_std_pps;

    VkVideoDecodeH264SessionParametersCreateInfoKHR avc_params = {};
    avc_params.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
    avc_params.pNext = nullptr;
    avc_params.maxStdPPSCount = 1;
    avc_params.maxStdSPSCount = 1;
    avc_params.pParametersAddInfo = &avc_params_add_info;

    VkVideoSessionParametersCreateInfoKHR session_params_create_info = {};
    session_params_create_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
    session_params_create_info.pNext = &avc_params;
    session_params_create_info.flags = 0;
    session_params_create_info.videoSessionParametersTemplate = VK_NULL_HANDLE;
    session_params_create_info.videoSession = session->_handle;
    VkVideoSessionParametersKHR video_session_params = VK_NULL_HANDLE;
    VK_CHECK(vk.CreateVideoSessionParametersKHR(sys_vk->_active_dev, &session_params_create_info,
        nullptr, &session->_parameters));
}
} // namespace vvb
