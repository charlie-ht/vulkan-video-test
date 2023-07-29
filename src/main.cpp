
#include "util.hpp"
#include "vk.hpp"

#include <string>

#include "vulkan_video_bootstrap.cpp"

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
    avc_profile_info.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
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

    const int AVC_MAX_DPB_REF_SLOTS = 16;
    VkVideoSessionCreateInfoKHR session_create_info = {};
    session_create_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
    session_create_info.pNext = nullptr;
    session_create_info.queueFamilyIndex = sys_vk->queue_family_decode_index;
    session_create_info.flags = 0;
    session_create_info.pVideoProfile = &avc_video_profile;
    session_create_info.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
    session_create_info.maxCodedExtent = VkExtent2D{ 3840, 2160 };
    session_create_info.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
    session_create_info.maxDpbSlots = AVC_MAX_DPB_REF_SLOTS + 1; // From the H.264 spec, + 1 for the setup slot.
    session_create_info.maxActiveReferencePictures = AVC_MAX_DPB_REF_SLOTS;
    session_create_info.pStdHeaderVersion = &avc_ext_version;

    VkVideoSessionKHR video_session = VK_NULL_HANDLE;
    VK_CHECK(vk.CreateVideoSessionKHR(sys_vk->_active_dev,
        &session_create_info,
        nullptr,
        &video_session));

    std::vector<VkVideoSessionMemoryRequirementsKHR> memory_requirements;
    u32 num_reqs = 0;
    VK_CHECK(vk.GetVideoSessionMemoryRequirementsKHR(sys_vk->_active_dev, video_session, &num_reqs, nullptr));
    memory_requirements.resize(num_reqs);
    for (u32 i = 0; i < num_reqs; i++)
        memory_requirements[i].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
    VK_CHECK(vk.GetVideoSessionMemoryRequirementsKHR(sys_vk->_active_dev, video_session, &num_reqs, memory_requirements.data()));
    
    std::vector<VmaAllocation> session_memory_allocations(memory_requirements.size());
    std::vector<VmaAllocationInfo> session_memory_allocation_infos(memory_requirements.size());
    VmaAllocationCreateInfo session_memory_alloc_create_info = {};
    for (u32 i = 0; i < memory_requirements.size(); i++) {
        auto& mem_req = memory_requirements[i];
        auto& allocation = session_memory_allocations[i];
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

        VK_CHECK(vk.BindVideoSessionMemoryKHR(sys_vk->_active_dev, video_session, 1, &bind_session_memory_info));
    }
    //;;;;;;;;;; Session is now setup

    //;;;;;;;;;; Create the session parameters
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
    avc_std_sps.flags.vui_parameters_present_flag = 0;
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
    avc_std_sps_vui.flags.timing_info_present_flag = 0;
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
    session_params_create_info.videoSession = video_session;
    VkVideoSessionParametersKHR video_session_params = VK_NULL_HANDLE;
    VK_CHECK(vk.CreateVideoSessionParametersKHR(sys_vk->_active_dev, &session_params_create_info,
        nullptr, &video_session_params));
	
    // parse bitstream
    // allocate picture buffers
    // perform decode algorithm for each AU (**)
    // for each frame, submit decode command
    // wait for frames to decode and present to the screen


    vk.DestroyVideoSessionParametersKHR(sys_vk->_active_dev, video_session_params, nullptr);

    for (auto& alloc : session_memory_allocations)
        vmaFreeMemory(sys_vk->_allocator, alloc);

    vk.DestroyVideoSessionKHR(sys_vk->_active_dev, video_session, nullptr);


	delete sys_vk;
	return 0;
}
