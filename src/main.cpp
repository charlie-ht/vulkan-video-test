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
        }
    }
	vvb::SysVulkan::UserOptions opts;
	opts.detect_env = detect_env;
	opts.enable_validation = enable_validation;
	opts.requested_device_name = requested_device_name;
	opts.requested_device_major = device_major;
	opts.requested_device_minor = device_minor;

	vvb::SysVulkan sys_vk(opts);
    vvb::init_vulkan(sys_vk);
    // parse bitstream
    // allocate picture buffers
    // perform decode algorithm for each AU (**)
    // for each frame, submit decode command
    // wait for frames to decode and present to the screen
}
