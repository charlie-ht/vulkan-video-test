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
#include "util.hpp"
#include "vk.hpp"

#include <string>
#include <numeric>

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
    vvb::VideoProfile av1_profile = vvb::Av1Progressive420Profile();
    vvb::VideoProfile avc_profile = vvb::AvcProgressive420Profile();

    VkVideoProfileListInfoKHR avc_session_profile_list = {};
    avc_session_profile_list.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
    avc_session_profile_list.pNext = nullptr;
    avc_session_profile_list.profileCount = 1;
    avc_session_profile_list.pProfiles = &avc_profile._profile_info;

    //;;;;;;;;;; Cap queries
    VkVideoCapabilitiesKHR video_caps = {};
    video_caps.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    VkVideoDecodeCapabilitiesKHR decode_caps = {};
    decode_caps.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
    VkVideoDecodeH264CapabilitiesKHR avc_caps = {};
    avc_caps.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR;
    decode_caps.pNext = &avc_caps;
    video_caps.pNext = &decode_caps;
    VK_CHECK(vk.GetPhysicalDeviceVideoCapabilitiesKHR(sys_vk->SelectedPhysicalDevice(),
        &avc_profile._profile_info, &video_caps));

    // !(video_caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR) -> image arrays for dpb, not implemeted / tested
    ASSERT(video_caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR);
    bool dpb_and_dst_coincide = decode_caps.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;
    VkFormat dpb_image_format = VK_FORMAT_UNDEFINED;
    VkFormat out_image_format = VK_FORMAT_UNDEFINED;
    
    auto get_supported_formats = [=](VkImageUsageFlags usage_flags){
        u32 num_supported_formats = 0;
        VkPhysicalDeviceVideoFormatInfoKHR video_format_info = {};
        video_format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR;
        video_format_info.pNext = &avc_session_profile_list;
        video_format_info.imageUsage = usage_flags;
        vk.GetPhysicalDeviceVideoFormatPropertiesKHR(sys_vk->SelectedPhysicalDevice(),
            &video_format_info,
            &num_supported_formats,
            nullptr);
        
        std::vector<VkVideoFormatPropertiesKHR> supported_formats(num_supported_formats);
        for (auto& sf : supported_formats)
            sf.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
        vk.GetPhysicalDeviceVideoFormatPropertiesKHR(sys_vk->SelectedPhysicalDevice(),
            &video_format_info,
            &num_supported_formats,
            supported_formats.data());
        return supported_formats;
    };
    VkImageUsageFlags dpb_usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    VkImageUsageFlags out_usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    //out_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR|VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    if (dpb_and_dst_coincide)
    {
        dpb_usage = out_usage | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
        out_usage &= ~VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
    }
    auto supported_dpb_formats = get_supported_formats(dpb_usage);
    // Think of what to do for > 1 supported formats
    ASSERT(supported_dpb_formats.size() == 1);
    VkVideoFormatPropertiesKHR selected_dpb_format = supported_dpb_formats[0];
    VkVideoFormatPropertiesKHR selected_out_format = {};
    selected_out_format.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
    selected_out_format.format = VK_FORMAT_UNDEFINED;
    if (!dpb_and_dst_coincide) {
        auto supported_output_formats = get_supported_formats(out_usage);
        ASSERT(supported_output_formats.size() == 1);
        selected_out_format = supported_output_formats[0];
    };

    printf("Coincide: %d\n", dpb_and_dst_coincide);
    printf("dpb_usage: "); vk_print(dpb_usage); printf("\n");
    printf("out_usage: "); vk_print(out_usage); printf("\n");
    
    //;;;;;;;;;; End of cap queries

    const VkExtensionProperties avc_ext_version = {
        VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
        VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION
    };

    const VkExtensionProperties av1_ext_version = {
        VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME,
        VK_MAKE_VERSION(0, 0, 1),
    };

    const int AVC_MAX_DPB_REF_SLOTS = 16u;
    VkVideoSessionCreateInfoKHR session_create_info = {};
    session_create_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
    session_create_info.pNext = nullptr;
    session_create_info.queueFamilyIndex = sys_vk->queue_family_decode_index;
    session_create_info.flags = 0;
    session_create_info.pVideoProfile = &avc_profile._profile_info;
    session_create_info.pictureFormat = selected_dpb_format.format;
    session_create_info.maxCodedExtent = VkExtent2D{176,144}; // video_caps.maxCodedExtent;
    session_create_info.referencePictureFormat = selected_dpb_format.format;
    session_create_info.maxDpbSlots = 10; // std::min(video_caps.maxDpbSlots, AVC_MAX_DPB_REF_SLOTS + 1u); // From the H.264 spec, + 1 for the setup slot.
    session_create_info.maxActiveReferencePictures = 10; // std::min(video_caps.maxActiveReferencePictures, (u32)AVC_MAX_DPB_REF_SLOTS);
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
    session_params_create_info.videoSession = video_session;
    VkVideoSessionParametersKHR video_session_params = VK_NULL_HANDLE;
    VK_CHECK(vk.CreateVideoSessionParametersKHR(sys_vk->_active_dev, &session_params_create_info,
        nullptr, &video_session_params));

    // Used for concurrent usage resources
    u32 queue_family_indices[2] = {(u32)sys_vk->queue_family_decode_index, (u32)sys_vk->queue_family_tx_index};

    // parse bitstream
    u64 bitstream_size = util::AlignUp((VkDeviceSize)58, video_caps.minBitstreamBufferSizeAlignment);
    VkBufferCreateInfo bitstream_buffer_info = {};
    bitstream_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bitstream_buffer_info.pNext = &avc_session_profile_list;
    bitstream_buffer_info.size = bitstream_size;
    bitstream_buffer_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
    bitstream_buffer_info.queueFamilyIndexCount = 2;
    bitstream_buffer_info.pQueueFamilyIndices = queue_family_indices;
    bitstream_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR;
    VmaAllocationCreateInfo bitstream_alloc_info = {};
    bitstream_alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    // Note! This is rather subtle. Since I don't intend to read the bitstream back to the CPU, it seems
    // I can use uncached combined memory for extra performance.
    bitstream_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    VkBuffer bitstream_buffer = VK_NULL_HANDLE;
    VmaAllocation bitstream_alloc = VK_NULL_HANDLE;
    vmaCreateBuffer(sys_vk->_allocator,
        &bitstream_buffer_info,
        &bitstream_alloc_info,
        &bitstream_buffer,
        &bitstream_alloc,
        nullptr);
    u8 slice_bytes[] = {0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0xfe, 0xe6, 0xc0, 0xf9, 0x96, 0x55, 0x0d, 0x57, 0x7f, 0xfd, 0x69, 0x3d, 0x2b, 0xf8, 0xcd, 0x22, 0xe5, 0x25, 0xe2, 0x93, 0x0c, 0xad, 0xe0, 0xfa, 0x71, 0x00, 0xcb, 0x99, 0xd1, 0xd6, 0xb5, 0x9b, 0xed, 0x6f, 0x8b, 0x38, 0xa7, 0xdc, 0xe1, 0x90, 0x01, 0x6e, 0x00, 0x10, 0xd0, 0x9a, 0xf3, 0x63, 0x3f, 0x81, 0x00};
    void* bitstream_mapped = nullptr;
    VK_CHECK(vmaMapMemory(sys_vk->_allocator, bitstream_alloc, &bitstream_mapped));
    memcpy(bitstream_mapped, slice_bytes, sizeof(slice_bytes));
    vmaUnmapMemory(sys_vk->_allocator, bitstream_alloc);
    VkBufferMemoryBarrier2 bitstream_barrier = {};
    bitstream_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    bitstream_barrier.pNext = nullptr;
    bitstream_barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    bitstream_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    bitstream_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
    bitstream_barrier.dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR;
    bitstream_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bitstream_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bitstream_barrier.buffer = bitstream_buffer;
    bitstream_barrier.offset = 0;
    bitstream_barrier.size = bitstream_size;

    // allocate picture buffers
    // dpb picture
    vvb::ImageResource dpb_image_resource = vvb::CreateImageResource(sys_vk, selected_dpb_format.format, 176, 144, dpb_usage, &avc_session_profile_list);
    vvb::AttachImageView(sys_vk, &dpb_image_resource, selected_out_format.componentMapping);
    VkVideoPictureResourceInfoKHR dpb_picture_resource = dpb_image_resource.PictureResourceInfo();
    VkImageMemoryBarrier2 dpb_image_barrier = dpb_image_resource.Barrier(vvb::DpbImageInitialize);
    
    // output picture (not neccesary for coincident case)
    vvb::ImageResource dst_image_resource = {};
    VkVideoPictureResourceInfoKHR dst_picture_resource = {};
    VkImageMemoryBarrier2 dst_image_barrier = {};
    if (!dpb_and_dst_coincide)
    {
        dst_image_resource = vvb::CreateImageResource(sys_vk, selected_out_format.format, 176, 144, out_usage, &avc_session_profile_list);
        vvb::AttachImageView(sys_vk, &dst_image_resource, selected_out_format.componentMapping);
        dst_picture_resource = dst_image_resource.PictureResourceInfo();
        dst_image_barrier = dst_image_resource.Barrier(vvb::DstImageInitialize);
    }

    // perform decode algorithm for each AU (**)iop
    // for each frame, submit decode command
    // wait for frames to decode and present to the screen

    // Queries
    if (sys_vk->DecodeQueriesAreSupported())
    {
        VkQueryPoolCreateInfo query_pool_info = {};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.pNext = &avc_profile._profile_info;
        query_pool_info.flags = 0;
        query_pool_info.queryType = VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR;
        query_pool_info.queryCount = 32; // numSlots
        query_pool_info.pipelineStatistics = 0;
        VK_CHECK(vk.CreateQueryPool(sys_vk->_active_dev, &query_pool_info, nullptr, &sys_vk->_query_pool));
    }
    VkCommandPool decode_cmd_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.pNext = nullptr;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmd_pool_info.queueFamilyIndex = sys_vk->queue_family_decode_index;
    VK_CHECK(vk.CreateCommandPool(sys_vk->_active_dev, &cmd_pool_info, nullptr, &decode_cmd_pool));
    VkCommandPool tx_cmd_pool = VK_NULL_HANDLE;
    cmd_pool_info.queueFamilyIndex = sys_vk->queue_family_tx_index;
    VK_CHECK(vk.CreateCommandPool(sys_vk->_active_dev, &cmd_pool_info, nullptr, &tx_cmd_pool));

    VkCommandBuffer decode_cmd_buf = VK_NULL_HANDLE;
    VkCommandBuffer tx_cmd_buf = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cmd_buf_alloc_info = {};
    cmd_buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buf_alloc_info.pNext = nullptr;
    cmd_buf_alloc_info.commandPool = decode_cmd_pool;
    cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buf_alloc_info.commandBufferCount = 1;
    VK_CHECK(vk.AllocateCommandBuffers(sys_vk->_active_dev, &cmd_buf_alloc_info, &decode_cmd_buf));
    cmd_buf_alloc_info.commandPool = tx_cmd_pool;
    VK_CHECK(vk.AllocateCommandBuffers(sys_vk->_active_dev, &cmd_buf_alloc_info, &tx_cmd_buf));
    VkCommandBufferBeginInfo cmd_buf_begin_info = {};
    cmd_buf_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmd_buf_begin_info.pNext = nullptr;
    cmd_buf_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmd_buf_begin_info.pInheritanceInfo = nullptr;

    vk.BeginCommandBuffer(decode_cmd_buf, &cmd_buf_begin_info);


    // Queries
    if (sys_vk->DecodeQueriesAreSupported())
    {
        vk.CmdResetQueryPool(decode_cmd_buf, sys_vk->_query_pool, 0, 1);
    }

    auto out_image_resource = dpb_and_dst_coincide ? dpb_image_resource : dst_image_resource;

    //;;;;;;;;;;; Video coding scope begin
    VkVideoBeginCodingInfoKHR begin_coding_info = {};
    begin_coding_info.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
    begin_coding_info.pNext = nullptr;
    begin_coding_info.flags = 0;
    begin_coding_info.videoSession = video_session;
    begin_coding_info.videoSessionParameters = video_session_params;
    VkVideoReferenceSlotInfoKHR reference_slot = {};
    reference_slot.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
    reference_slot.pNext = nullptr;
    reference_slot.slotIndex = -1;
    reference_slot.pPictureResource = &dpb_picture_resource;
    begin_coding_info.referenceSlotCount = 1;
    begin_coding_info.pReferenceSlots = &reference_slot;
    vk.CmdBeginVideoCodingKHR(decode_cmd_buf, &begin_coding_info);

    VkVideoCodingControlInfoKHR coding_ctrl_info = {};
    coding_ctrl_info.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
    coding_ctrl_info.pNext = nullptr;
    coding_ctrl_info.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
    vk.CmdControlVideoCodingKHR(decode_cmd_buf, &coding_ctrl_info);

    VkDependencyInfoKHR out_dep_info = {};
    out_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
    out_dep_info.pNext = nullptr;
    out_dep_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    out_dep_info.memoryBarrierCount = 0;
    out_dep_info.pMemoryBarriers = nullptr;
    out_dep_info.bufferMemoryBarrierCount = 1;
    out_dep_info.pBufferMemoryBarriers = &bitstream_barrier;
    std::vector<VkImageMemoryBarrier2> image_barriers;
    image_barriers.push_back(dpb_image_barrier);
    if (!dpb_and_dst_coincide)
    {
        image_barriers.push_back(dst_image_barrier);
    }
    out_dep_info.imageMemoryBarrierCount = image_barriers.size();
    out_dep_info.pImageMemoryBarriers = image_barriers.data();
    vk.CmdPipelineBarrier2KHR(decode_cmd_buf, &out_dep_info);

    if (sys_vk->DecodeQueriesAreSupported())
    {
        vk.CmdBeginQuery(decode_cmd_buf, sys_vk->_query_pool, 0, VkQueryControlFlags());
    }

    StdVideoDecodeH264PictureInfo avc_picture_info = {};
    avc_picture_info.flags.field_pic_flag = 0;
    avc_picture_info.flags.is_intra = 1;
    avc_picture_info.flags.IdrPicFlag = 0;
    avc_picture_info.flags.bottom_field_flag = 0;
    avc_picture_info.flags.is_reference = 1;
    avc_picture_info.flags.complementary_field_pair = 0;
    avc_picture_info.seq_parameter_set_id = 0;
    avc_picture_info.pic_parameter_set_id = 0;
    avc_picture_info.frame_num = 0;
    avc_picture_info.idr_pic_id = 0;
    avc_picture_info.PicOrderCnt[0] = 0;
    avc_picture_info.PicOrderCnt[1] = 0;
    VkVideoDecodeH264PictureInfoKHR avc_decode_info = {};
    avc_decode_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR;
    avc_decode_info.pNext = nullptr;
    avc_decode_info.pStdPictureInfo = &avc_picture_info;
    avc_decode_info.sliceCount = 1;
    u32 slice_offsets[] = {0};
    avc_decode_info.pSliceOffsets = slice_offsets;

    VkVideoDecodeInfoKHR decode_info = {};
    decode_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
    decode_info.pNext = &avc_decode_info;
    decode_info.flags = 0;
    decode_info.srcBuffer = bitstream_buffer;
    decode_info.srcBufferOffset = 0;
    decode_info.srcBufferRange = bitstream_buffer_info.size;
    decode_info.dstPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
    decode_info.dstPictureResource.pNext = nullptr;
    decode_info.dstPictureResource.codedExtent = VkExtent2D{ 176, 144 };
    decode_info.dstPictureResource.codedOffset = VkOffset2D{ 0, 0 };
    decode_info.dstPictureResource.baseArrayLayer = 0;
    decode_info.dstPictureResource.imageViewBinding = out_image_resource._view;
    VkVideoDecodeH264DpbSlotInfoKHR dpb_slot_info = {};
    dpb_slot_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
    dpb_slot_info.pNext = nullptr;
    StdVideoDecodeH264ReferenceInfo ref_info = {};
    ref_info.flags.top_field_flag = 0;
    ref_info.flags.bottom_field_flag = 0;
    ref_info.flags.used_for_long_term_reference = 0;
    ref_info.flags.is_non_existing = 0;
    ref_info.FrameNum = 0;
    ref_info.PicOrderCnt[0] = 0;
    ref_info.PicOrderCnt[1] = 0;
    dpb_slot_info.pStdReferenceInfo = &ref_info;
    reference_slot.pNext = &dpb_slot_info;
    reference_slot.slotIndex = 0;
    decode_info.pSetupReferenceSlot = &reference_slot;
    decode_info.referenceSlotCount = 0;
    decode_info.pReferenceSlots = nullptr;
    vk.CmdDecodeVideoKHR(decode_cmd_buf, &decode_info);

    if (sys_vk->DecodeQueriesAreSupported())
    {
        vk.CmdEndQuery(decode_cmd_buf, sys_vk->_query_pool, 0);
    }

    VkVideoEndCodingInfoKHR end_coding_info = {};
    end_coding_info.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
    end_coding_info.pNext = nullptr;
    end_coding_info.flags = 0;    
    vk.CmdEndVideoCodingKHR(decode_cmd_buf, &end_coding_info);
    //;;;;;;;;;;; Video coding scope end

    vk.EndCommandBuffer(decode_cmd_buf);

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = nullptr;
    fence_info.flags = 0;
    VkFence fence;
    vk.CreateFence(sys_vk->_active_dev, &fence_info, nullptr, &fence);
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = 0;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &decode_cmd_buf;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;
    VK_CHECK(vk.QueueSubmit(sys_vk->_decode_queue0, 1, &submit_info, fence));
    VK_CHECK(vk.WaitForFences(sys_vk->_active_dev, 1, &fence, VK_TRUE, UINT64_MAX));

    if (sys_vk->DecodeQueriesAreSupported())
    {
        VkQueryResultStatusKHR decode_status;
        VK_CHECK(vk.GetQueryPoolResults(sys_vk->_active_dev,
            sys_vk->_query_pool,
            0,
            1,
            sizeof(decode_status),
            &decode_status,
            sizeof(decode_status),
            VK_QUERY_RESULT_WITH_STATUS_BIT_KHR | VK_QUERY_RESULT_WAIT_BIT));
        ASSERT(decode_status == VK_QUERY_RESULT_STATUS_COMPLETE_KHR);
    }

    vk.ResetFences(sys_vk->_active_dev, 1, &fence);
    VkBufferCreateInfo luma_buf_create_info = {};
    luma_buf_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    luma_buf_create_info.pNext = &avc_session_profile_list;
    luma_buf_create_info.size = 32768; // todo
    luma_buf_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    luma_buf_create_info.queueFamilyIndexCount = 0;
    luma_buf_create_info.pQueueFamilyIndices = nullptr;
    luma_buf_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo luma_buf_alloc_info = {};
    luma_buf_alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    // Note! This is rather subtle. Since I don't intend to read the bitstream back to the CPU, it seems
    // I can use uncached combined memory for extra performance.
    luma_buf_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    VkBuffer luma_buf = VK_NULL_HANDLE;
    VmaAllocation luma_buf_alloc = VK_NULL_HANDLE;
    vmaCreateBuffer(sys_vk->_allocator,
        &luma_buf_create_info,
        &luma_buf_alloc_info,
        &luma_buf,
        &luma_buf_alloc,
        nullptr);
    VkBufferCreateInfo chroma_buf_create_info = {};
    chroma_buf_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    chroma_buf_create_info.pNext = &avc_session_profile_list;
    chroma_buf_create_info.size = 16384; // todo
    chroma_buf_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    chroma_buf_create_info.queueFamilyIndexCount = 0;
    chroma_buf_create_info.pQueueFamilyIndices = nullptr;
    chroma_buf_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo chroma_buf_alloc_info = {};
    chroma_buf_alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    // Note! This is rather subtle. Since I don't intend to read the bitstream back to the CPU, it seems
    // I can use uncached combined memory for extra performance.
    chroma_buf_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    VkBuffer chroma_buf = VK_NULL_HANDLE;
    VmaAllocation chroma_buf_alloc = VK_NULL_HANDLE;
    vmaCreateBuffer(sys_vk->_allocator,
        &chroma_buf_create_info,
        &chroma_buf_alloc_info,
        &chroma_buf,
        &chroma_buf_alloc,
        nullptr);

    vk.BeginCommandBuffer(tx_cmd_buf, &cmd_buf_begin_info);
    VkImageMemoryBarrier2 out_image_barrier = {};
    out_image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    out_image_barrier.pNext = nullptr;
    out_image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    out_image_barrier.srcAccessMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    out_image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
    out_image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    out_image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    out_image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // concurrent usage is enabled
    out_image_barrier.image = out_image_resource._image;
    out_image_barrier.oldLayout = dpb_and_dst_coincide ? VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR : VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
    out_image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    out_image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    out_image_barrier.subresourceRange.baseMipLevel = 0;
    out_image_barrier.subresourceRange.levelCount = 1;
    out_image_barrier.subresourceRange.baseArrayLayer = 0;
    out_image_barrier.subresourceRange.layerCount = 1;
    out_dep_info = {};
    out_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
    out_dep_info.pNext = nullptr;
    out_dep_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    out_dep_info.memoryBarrierCount = 0;
    out_dep_info.pMemoryBarriers = nullptr;
    out_dep_info.bufferMemoryBarrierCount = 0;
    out_dep_info.pBufferMemoryBarriers = nullptr;
    out_dep_info.imageMemoryBarrierCount = 1;
    out_dep_info.pImageMemoryBarriers = &out_image_barrier;
    vk.CmdPipelineBarrier2KHR(tx_cmd_buf, &out_dep_info);

    u32 luma_width_samples = 176;
    u32 luma_buf_pitch = 192;
    u32 luma_buf_height = 144;
    VkBufferImageCopy luma_copy_region = {};
    luma_copy_region.bufferOffset = 0;
    luma_copy_region.bufferRowLength = luma_buf_pitch;
    luma_copy_region.bufferImageHeight = luma_buf_height;
    luma_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
    luma_copy_region.imageSubresource.mipLevel = 0;
    luma_copy_region.imageSubresource.baseArrayLayer = 0;
    luma_copy_region.imageSubresource.layerCount = 1;
    luma_copy_region.imageOffset = { 0, 0, 0 };
    luma_copy_region.imageExtent = { luma_width_samples, luma_buf_height, 1 };
    vk.CmdCopyImageToBuffer(tx_cmd_buf, out_image_resource._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        luma_buf, 1, &luma_copy_region);
    
    u32 chroma_width_samples = 176 / 2;
    u32 chroma_buf_pitch = 96;
    u32 chroma_buf_height = 144 / 2;
    VkBufferImageCopy chroma_copy_region = {};
    chroma_copy_region.bufferOffset = 0;
    chroma_copy_region.bufferRowLength = chroma_buf_pitch;
    chroma_copy_region.bufferImageHeight = chroma_buf_height;
    chroma_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
    chroma_copy_region.imageSubresource.mipLevel = 0;
    chroma_copy_region.imageSubresource.baseArrayLayer = 0;
    chroma_copy_region.imageSubresource.layerCount = 1;
    chroma_copy_region.imageOffset = { 0, 0, 0 };
    chroma_copy_region.imageExtent = { chroma_width_samples, chroma_buf_height, 1 };
    vk.CmdCopyImageToBuffer(tx_cmd_buf, out_image_resource._image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        chroma_buf, 1, &chroma_copy_region);

    vk.EndCommandBuffer(tx_cmd_buf);

    submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = 0;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &tx_cmd_buf;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;
    VK_CHECK(vk.QueueSubmit(sys_vk->_tx_queue0, 1, &submit_info, fence));
    VK_CHECK(vk.WaitForFences(sys_vk->_active_dev, 1, &fence, VK_TRUE, UINT64_MAX));

    FILE* out_file = fopen("/tmp/vd.yuv", "wb");

    void* luma_buf_data = nullptr;
    void* chroma_buf_data = nullptr;
    vmaMapMemory(sys_vk->_allocator, luma_buf_alloc, &luma_buf_data);
    u8* luma_buf_bytes = (u8*)luma_buf_data;
    u32 bytes_written = 0;
    // Output the frame data in NV12 format
    for (int line = 0; line < luma_buf_height; line++)
    {
        bytes_written += fwrite(luma_buf_bytes + line * luma_buf_pitch, 1, luma_width_samples, out_file);
    }
    ASSERT(bytes_written == luma_width_samples * luma_buf_height);
    vmaUnmapMemory(sys_vk->_allocator, luma_buf_alloc);
    vmaMapMemory(sys_vk->_allocator, chroma_buf_alloc, &chroma_buf_data);
    u8* chroma_buf_bytes = (u8*)chroma_buf_data;
    for (int line = 0; line < chroma_buf_height; line++)
    {
        bytes_written += fwrite(chroma_buf_bytes + line * chroma_buf_pitch * sizeof(u16), 1, chroma_width_samples * sizeof(u16), out_file);
    }
    ASSERT(bytes_written == luma_width_samples * luma_buf_height + 2 * (chroma_width_samples * chroma_buf_height));
    vmaUnmapMemory(sys_vk->_allocator, chroma_buf_alloc);
    fclose(out_file);

    vk.DestroyFence(sys_vk->_active_dev, fence, nullptr);
    vvb::DestroyImageResource(sys_vk, &dpb_image_resource);
    if (!dpb_and_dst_coincide)
        vvb::DestroyImageResource(sys_vk, &dst_image_resource);

    vmaDestroyBuffer(sys_vk->_allocator, luma_buf, luma_buf_alloc);
    vmaDestroyBuffer(sys_vk->_allocator, chroma_buf, chroma_buf_alloc);
    vmaDestroyBuffer(sys_vk->_allocator, bitstream_buffer, bitstream_alloc);
    if (sys_vk->_query_pool != VK_NULL_HANDLE)
    {
        vk.DestroyQueryPool(sys_vk->_active_dev, sys_vk->_query_pool, nullptr);
    }
    vk.DestroyCommandPool(sys_vk->_active_dev, tx_cmd_pool, nullptr);
    vk.DestroyCommandPool(sys_vk->_active_dev, decode_cmd_pool, nullptr);
    vk.DestroyVideoSessionParametersKHR(sys_vk->_active_dev, video_session_params, nullptr);
    for (auto& alloc : session_memory_allocations)
        vmaFreeMemory(sys_vk->_allocator, alloc);
    vk.DestroyVideoSessionKHR(sys_vk->_active_dev, video_session, nullptr);
    if (false)
    {
        char *str = new char[1024*1024*1024];
        vmaBuildStatsString(sys_vk->_allocator, &str, VK_TRUE);
        printf("%s\n", str);
        delete[] str;
    }
	delete sys_vk;
	return 0;
}
