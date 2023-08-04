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

    // !(video_caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR) -> image arrays for dpb
    // This test uses an image array for the DPB in any case, since it's simpler, but potentially less efficient.

    printf("Buffer size alignment: %lu\n", video_caps.minBitstreamBufferSizeAlignment);
    printf("Buffer offset alignment: %lu %lu\n", video_caps.minBitstreamBufferOffsetAlignment, util::AlignUp((VkDeviceSize)0, video_caps.minBitstreamBufferOffsetAlignment));

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
    VkImageUsageFlags dst_usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (dpb_and_dst_coincide)
    {
        dpb_usage = dst_usage | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
        dst_usage &= ~VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
    }
    auto supported_dpb_formats = get_supported_formats(dpb_usage);
    // Think of what to do for > 1 supported formats
    ASSERT(supported_dpb_formats.size() == 1);
    VkVideoFormatPropertiesKHR selected_dpb_format = supported_dpb_formats[0];
    VkVideoFormatPropertiesKHR selected_dst_format = {};
    selected_dst_format.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
    selected_dst_format.format = selected_dpb_format.format;
    if (!dpb_and_dst_coincide) {
        auto supported_output_formats = get_supported_formats(dst_usage);
        ASSERT(supported_output_formats.size() == 1);
        selected_dst_format = supported_output_formats[0];
    };

    printf("Coincide: %d\n", dpb_and_dst_coincide);
    printf("dpb_usage: "); vk_print(dpb_usage); printf("\n");
    printf("out_usage: "); vk_print(dst_usage); printf("\n");
    
    //;;;;;;;;;; End of cap queries

    auto coding_session = vvb::CreateVideoSession(sys_vk, &avc_profile, selected_dst_format.format, selected_dpb_format.format, &video_caps,
        1, 1);
    vvb::AddSessionParameters(sys_vk, &coding_session);

    u64 bitstream_size = util::AlignUp((VkDeviceSize)58, video_caps.minBitstreamBufferSizeAlignment);
    auto bitstream = vvb::CreateBufferResource(sys_vk, bitstream_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        &avc_session_profile_list);

    u8 slice_bytes[] = {0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0xfe, 0xe6, 0xc0, 0xf9, 0x96, 0x55, 0x0d, 0x57, 0x7f, 0xfd, 0x69, 0x3d, 0x2b, 0xf8, 0xcd, 0x22, 0xe5, 0x25, 0xe2, 0x93, 0x0c, 0xad, 0xe0, 0xfa, 0x71, 0x00, 0xcb, 0x99, 0xd1, 0xd6, 0xb5, 0x9b, 0xed, 0x6f, 0x8b, 0x38, 0xa7, 0xdc, 0xe1, 0x90, 0x01, 0x6e, 0x00, 0x10, 0xd0, 0x9a, 0xf3, 0x63, 0x3f, 0x81, 0x00};
    void* bitstream_mapped = nullptr;
    VK_CHECK(vmaMapMemory(sys_vk->_allocator, bitstream._allocation, &bitstream_mapped));
    memcpy(bitstream_mapped, slice_bytes, sizeof(slice_bytes));
    vmaUnmapMemory(sys_vk->_allocator, bitstream._allocation);

    VkBufferMemoryBarrier2 bitstream_barrier = bitstream.Barrier(vvb::TRANSITION_BUFFER_FOR_READING);

    auto dpb = vvb::CreateDpbResource(sys_vk, 176, 144, 3,
        dpb_and_dst_coincide,
        dpb_usage, selected_dpb_format.format, selected_dpb_format.componentMapping,
        dst_usage, selected_dst_format.format, selected_dst_format.componentMapping,
        &avc_session_profile_list);

    if (sys_vk->DecodeQueriesAreSupported())
    {
        VkQueryPoolCreateInfo query_pool_info = {};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.pNext = &avc_profile._profile_info;
        query_pool_info.flags = 0;
        query_pool_info.queryType = VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR;
        query_pool_info.queryCount = 1; // this is fixed by the spec, per region decoding will change this
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

    //auto out_image_resource = dpb_and_dst_coincide ? dpb._slot_picture_resource_infos[0] : dst_image_resource;

    //;;;;;;;;;;; Video coding scope begin
    VkVideoBeginCodingInfoKHR begin_coding_info = {};
    begin_coding_info.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
    begin_coding_info.pNext = nullptr;
    begin_coding_info.flags = 0;
    begin_coding_info.videoSession = coding_session._handle;
    begin_coding_info.videoSessionParameters = coding_session._parameters;
    VkVideoReferenceSlotInfoKHR reference_slot = {};
    reference_slot.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
    reference_slot.pNext = nullptr;
    reference_slot.slotIndex = -1;
    reference_slot.pPictureResource = &dpb._dpb_slot_picture_resource_infos[0];
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
    std::vector<VkImageMemoryBarrier2> image_barriers = dpb.SlotBarriers(vvb::TRANSITION_IMAGE_INITIALIZE, 0u);
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
    decode_info.srcBuffer = bitstream._buffer;
    decode_info.srcBufferOffset = 0;
    decode_info.srcBufferRange = bitstream._create_info.size;
    decode_info.dstPictureResource = dpb.SlotDstPictureResource(0u);
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

    auto luma_buf = vvb::CreateBufferResource(sys_vk, 32768,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        &avc_session_profile_list);
    auto chroma_buf = vvb::CreateBufferResource(sys_vk, 16384,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        &avc_session_profile_list);

    vk.BeginCommandBuffer(tx_cmd_buf, &cmd_buf_begin_info);
        auto out_image_barrier = dpb.SlotBarriers(vvb::TRANSITION_IMAGE_TRANSFER_TO_HOST, 0u);

        out_dep_info = {};
        out_dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
        out_dep_info.pNext = nullptr;
        out_dep_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        out_dep_info.memoryBarrierCount = 0;
        out_dep_info.pMemoryBarriers = nullptr;
        out_dep_info.bufferMemoryBarrierCount = 0;
        out_dep_info.pBufferMemoryBarriers = nullptr;
        out_dep_info.imageMemoryBarrierCount = out_image_barrier.size();
        out_dep_info.pImageMemoryBarriers = out_image_barrier.data();
        vk.CmdPipelineBarrier2KHR(tx_cmd_buf, &out_dep_info);

        u32 luma_width_samples = 176;
        u32 luma_buf_pitch = 192;
        u32 luma_buf_height = 144;
        dpb.CopySlotToBuffer(sys_vk, tx_cmd_buf, 0u, luma_width_samples, luma_buf_pitch, luma_buf_height,
            VK_IMAGE_ASPECT_PLANE_0_BIT, luma_buf._buffer);
        
        u32 chroma_width_samples = luma_width_samples / 2;
        u32 chroma_buf_pitch = luma_buf_pitch / 2;
        u32 chroma_buf_height = luma_buf_height / 2;
        dpb.CopySlotToBuffer(sys_vk, tx_cmd_buf, 0u, chroma_width_samples, chroma_buf_pitch, chroma_buf_height,
            VK_IMAGE_ASPECT_PLANE_1_BIT, chroma_buf._buffer);
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
    vmaMapMemory(sys_vk->_allocator, luma_buf._allocation, &luma_buf_data);
    u8* luma_buf_bytes = (u8*)luma_buf_data;
    u32 bytes_written = 0;
    // Output the frame data in NV12 format
    for (int line = 0; line < luma_buf_height; line++)
    {
        bytes_written += fwrite(luma_buf_bytes + line * luma_buf_pitch, 1, luma_width_samples, out_file);
    }
    ASSERT(bytes_written == luma_width_samples * luma_buf_height);
    vmaUnmapMemory(sys_vk->_allocator, luma_buf._allocation);
    vmaMapMemory(sys_vk->_allocator, chroma_buf._allocation, &chroma_buf_data);
    u8* chroma_buf_bytes = (u8*)chroma_buf_data;
    for (int line = 0; line < chroma_buf_height; line++)
    {
        bytes_written += fwrite(chroma_buf_bytes + line * chroma_buf_pitch * sizeof(u16), 1, chroma_width_samples * sizeof(u16), out_file);
    }
    ASSERT(bytes_written == luma_width_samples * luma_buf_height + 2 * (chroma_width_samples * chroma_buf_height));
    vmaUnmapMemory(sys_vk->_allocator, chroma_buf._allocation);
    fclose(out_file);

    vk.DestroyFence(sys_vk->_active_dev, fence, nullptr);

    vvb::DestroyBufferResource(sys_vk, &luma_buf);
    vvb::DestroyBufferResource(sys_vk, &chroma_buf);
    vvb::DestroyBufferResource(sys_vk, &bitstream);

    if (sys_vk->_query_pool != VK_NULL_HANDLE)
    {
        vk.DestroyQueryPool(sys_vk->_active_dev, sys_vk->_query_pool, nullptr);
    }
    vk.DestroyCommandPool(sys_vk->_active_dev, tx_cmd_pool, nullptr);
    vk.DestroyCommandPool(sys_vk->_active_dev, decode_cmd_pool, nullptr);

    vvb::DestroyDpbResource(sys_vk, &dpb);

    vvb::DestroyVideoSession(sys_vk, &coding_session);
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
