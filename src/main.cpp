#include "util.hpp"

#include "vk.h"
#include "vulkan_video_bootstrap.cpp"

#include <string>

int main(int argc, char** argv)
{
    bool detect_env = false, enable_validation = true;
	const char* requested_device_name = nullptr;
	int device_major = -1, device_minor = -1;

    for (int arg = 1; arg < argc; arg++) {
        if (util::StrEqual(argv[arg], "--help")) {
            printf("Usage: %s [options] <input>\n", argv[0]);
            printf("Options:\n");
            printf("  --help: print this message\n");
            printf("  --detect: detect devices and capabilities\n");
            printf("  --validate-api-calls: enable vulkan validation layer (EXTRA SLOW!)\n");
			printf("  --device-major-minor=<major>.<minor> : select device by major and minor version (hex)\n");
            printf("  --device-name=<name> : case-insentive substring search of the reported device name to select (e.g. nvidia or amd)\n");
            printf("The rest is boilerplate\n");
            printf("  --version: print version\n");
            printf("  --verbose: print verbose messages\n");
            printf("  --debug: print debug messages\n");
            printf("  --trace: print trace messages\n");
            printf("  --quiet: print no messages\n");
            printf("  --output <file>: output file\n");
            printf("  --format <format>: output format\n");
            printf("  --width <width>: output width\n");
            printf("  --height <height>: output height\n");
            printf("  --fps <fps>: output fps\n");
            printf("  --bitrate <bitrate>: output bitrate\n");
            printf("  --gop <gop>: output gop\n");
            printf("  --preset <preset>: output preset\n");
            printf("  --profile <profile>: output profile\n");
            printf("  --level <level>: output level\n");
            printf("  --input <file>: input file\n");
            printf("  --input-format <format>: input format\n");
            printf("  --input-width <width>: input width\n");
            printf("  --input-height <height>: input height\n");
            printf("  --input-fps <fps>: input fps\n");
            printf("  --input-bitrate <bitrate>: input bitrate\n");
            printf("  --input-gop <gop>: input gop\n");
            printf("  --input-preset <preset>: input preset\n");
            printf("  --input-profile <profile>: input profile\n");
            printf("  --input-level <level>: input level\n");
            printf("  --input-threads <threads>: input threads\n");
            printf("  --input-queue <queue>: input queue\n");
            printf("  --input-buffers <buffers>: input buffers\n");
            printf("  --input-delay\n");
			exit(0);
        } else if (util::StrHasPrefix(argv[arg], "--device-name=")) {
            requested_device_name = util::StrRemovePrefix(argv[arg], "--device-name=");
		} else if (util::StrHasPrefix(argv[arg], "--device-major-minor=")) {
            std::string major_minor_pair = util::StrRemovePrefix(argv[arg], "--device-major-minor=");

			int period_offset = util::StrFindIndex(major_minor_pair.c_str(), ".");
			ASSERT(period_offset != -1);
			// convert major and minor to hex
			major_minor_pair[period_offset] = '\0';
			ASSERT(util::StrToInt(major_minor_pair.c_str(), 16, device_major));
			ASSERT(util::StrToInt(major_minor_pair.c_str() + period_offset + 1, 16, device_minor));

        } else if (util::StrEqual(argv[arg], "--validate-api-calls")) {
            enable_validation = true;
        } else if (util::StrEqual(argv[arg], "--detect")) {
            detect_env = true;
        } else {
			XERROR(0, "Unknown flag: %s\n", argv[arg]);
			exit(1);
		}
    }
	vvb::SysVulkan::UserOptions opts;
	opts.detect_env = detect_env;
	opts.enable_validation = enable_validation;
	opts.requested_device_name = requested_device_name;
	opts.requested_device_major = device_major;
	opts.requested_device_minor = device_minor;

	vvb::SysVulkan* sys_vk = new vvb::SysVulkan(opts);
	ASSERT(sys_vk);
    vvb::init_vulkan(*sys_vk);

    auto& vk = sys_vk->_vfn;

    // The following profile structs are for reference, they would need to be sniffed from
    // the input files in reality.
    VkVideoDecodeAV1ProfileInfoMESA av1_profile_info = {};
    av1_profile_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_MESA;
    av1_profile_info.pNext = nullptr;
    av1_profile_info.stdProfileIdc = STD_VIDEO_AV1_MESA_PROFILE_MAIN;

    VkVideoProfileInfoKHR av1_video_profile = {};
    av1_video_profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    av1_video_profile.pNext = &av1_profile_info;
    av1_video_profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_MESA;
    av1_video_profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    av1_video_profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    av1_video_profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

    VkVideoDecodeH264ProfileInfoKHR avc_profile_info = {};
    avc_profile_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
    avc_profile_info.pNext = nullptr;
    avc_profile_info.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN;
    avc_profile_info.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;
    
    VkVideoProfileInfoKHR avc_video_profile = {};
    avc_video_profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    avc_video_profile.pNext = &avc_profile_info;
    avc_video_profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
    avc_video_profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    avc_video_profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    avc_video_profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

    const VkExtensionProperties avc_ext_version = {
        VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
        VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION
    };

    const VkExtensionProperties av1_ext_version = {
        VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME,
        VK_MAKE_VERSION(0, 0, 1),
    };

    VkVideoSessionCreateInfoKHR session_create_info = {};
    session_create_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
    session_create_info.pNext = nullptr;
    session_create_info.queueFamilyIndex = sys_vk->queue_family_decode_index;
    session_create_info.flags = 0;
    session_create_info.pVideoProfile = &avc_video_profile;
    session_create_info.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
    session_create_info.maxCodedExtent = VkExtent2D{ 3840, 2160 };
    session_create_info.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
    session_create_info.maxDpbSlots = 9;
    session_create_info.maxActiveReferencePictures = 9;
    session_create_info.pStdHeaderVersion = &avc_ext_version;

    VkVideoSessionKHR video_session = VK_NULL_HANDLE;
    VK_CHECK(vk.CreateVideoSessionKHR(sys_vk->_active_dev,
        &session_create_info,
        nullptr,
        &video_session));
    
    vk.DestroyVideoSessionKHR(sys_vk->_active_dev, video_session, nullptr);
	
    // parse bitstream
    // allocate picture buffers
    // perform decode algorithm for each AU (**)
    // for each frame, submit decode command
    // wait for frames to decode and present to the screen

	delete sys_vk;
	return 0;
}
