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

    VkDevice _active_dev { VK_NULL_HANDLE };
    std::vector<const char*> _active_dev_enabled_exts;

    bool _enable_validation { true };
    u32 _debug_level { 1 };

    struct UserOptions {
        bool detect_env { false };
        bool enable_validation { true };
        const char* requested_device_name { nullptr };
        int requested_device_major { -1 };
        int requested_device_minor { -1 };
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
    if (sys_vk._debug_level > 0) {
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

    if (sys_vk._enable_validation) {
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

    if (sys_vk._enable_validation) {
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
    printf("There's %ld physical devices\n", num_devices);
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
        if (opts.requested_device_major != -1 || opts.requested_device_minor != -1) {
            if (drm_prop[i].primaryMajor == opts.requested_device_major && drm_prop[i].primaryMinor == opts.requested_device_minor) {
                choice = i;
                printf("Device %s picked based on major/minor number selection\n", prop[i].properties.deviceName);
                break;
            }
        }

        const char* this_device_name = prop[i].properties.deviceName;
        if (opts.requested_device_name && util::StrCaseInsensitiveSubstring(this_device_name, opts.requested_device_name)) {
            if (util::StrEqual(opts.requested_device_name, "amd") && VK_API_VERSION_MAJOR(prop[i].properties.driverVersion) < 23)
                continue; // HACK to pick up the Mesa Git driver
            choice = i;
            printf("Device %s picked based on device name selection (%s)\n", prop[i].properties.deviceName, opts.requested_device_name);
            break;
        }
    }

    if (choice == -1)
        XERROR(1, "Could not find a physical device to use\n");

    sys_vk._selected_physical_dev_idx = choice;

    printf("Selected device %s (driver version: %d)\n", prop[choice].properties.deviceName, VK_API_VERSION_MAJOR(prop[choice].properties.driverVersion));

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
    u64 ms;
    t.GetCurrentTime();
    vk.GetPhysicalDeviceFeatures2(sys_vk.SelectedPhysicalDevice(), &dev_features);
    ms = t.ElapsedMilliseconds();
    printf("GetPhysicalDeviceFeatures2 took %" PRIu64 " ms\n", ms);

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
    for (u32 i = 0; i < qf_properties_count; i++)
    {
        sys_vk._qf_properties[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        sys_vk._qf_video_properties[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
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
    ms = t.ElapsedMilliseconds();
    printf("load_vk_functions took %" PRIu64 " ms", ms);
    
    t.GetCurrentTime();
    load_instance(sys_vk);
    ms = t.ElapsedMilliseconds();
    printf("load_instance took %" PRIu64 " ms", ms);

    t.GetCurrentTime();
    load_vk_functions(sys_vk, FF_VK_EXT_NO_FLAG, true, false);
    ms = t.ElapsedMilliseconds();
    printf("load_vk_functions took %" PRIu64 " ms", ms);

    if (sys_vk._debug_level > 0) {
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

}
