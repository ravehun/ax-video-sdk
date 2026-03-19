#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ax_cmdline_utils.h"
#include "ax_mp4_decode_util.h"
#include "codec/ax_video_decoder.h"
#include "common/ax_image_processor.h"
#include "common/ax_system.h"

namespace {

using namespace axvsdk;

std::uint64_t PlaneChecksum(common::AxImage& image, std::size_t plane_index) {
    if (!image.InvalidateCache()) {
        return 0;
    }

    const auto* data = image.plane_data(plane_index);
    const auto bytes = image.plane_size(plane_index);
    if (data == nullptr || bytes == 0) {
        return 0;
    }

    const auto sample_count = std::min<std::size_t>(bytes, 1024U);
    const auto step = std::max<std::size_t>(bytes / sample_count, 1U);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < bytes && (i / step) < sample_count; i += step) {
        sum += data[i];
    }
    return sum;
}

int Run(const char* input_path, int timeout_seconds) {
    common::SystemOptions system_options{};
    system_options.enable_vdec = true;
    system_options.enable_venc = false;
    system_options.enable_ivps = true;
    if (!common::InitializeSystem(system_options)) {
        std::cerr << "InitializeSystem failed\n";
        return 2;
    }

    struct Guard {
        ~Guard() {
            common::ShutdownSystem();
        }
    } guard;

    auto decoder = codec::CreateVideoDecoder();
    auto processor = common::CreateImageProcessor();
    if (!decoder || !processor) {
        std::cerr << "Create instances failed\n";
        return 3;
    }

    auto demuxer = tooling::OpenDemuxer(input_path, false);
    if (!demuxer) {
        std::cerr << "OpenDemuxer failed\n";
        return 4;
    }

    const auto decoder_config = tooling::MakeDecoderConfigFromStreamInfo(demuxer->GetVideoStreamInfo());
    if (!decoder->Open(decoder_config)) {
        std::cerr << "decoder Open failed\n";
        return 5;
    }

    if (!decoder->Start()) {
        std::cerr << "decoder Start failed\n";
        decoder->Close();
        return 6;
    }

    std::atomic<bool> stop_feed{false};
    std::thread feed_thread([&] {
        (void)tooling::FeedDecoderFromDemuxer(*demuxer, *decoder, &stop_feed);
    });

    common::AxImage::Ptr source;
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        source = decoder->GetLatestFrame();
        if (source) {
            break;
        }

        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(timeout_seconds)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stop_feed.store(true, std::memory_order_relaxed);
    decoder->Stop();
    if (feed_thread.joinable()) {
        feed_thread.join();
    }
    decoder->Close();

    if (!source) {
        std::cerr << "no source frame\n";
        return 7;
    }

    common::ImageProcessRequest resize_request{};
    resize_request.output_image.format = common::PixelFormat::kNv12;
    resize_request.output_image.width = 640;
    resize_request.output_image.height = 360;

    common::ImageDescriptor resize_output_desc{};
    resize_output_desc.format = common::PixelFormat::kNv12;
    resize_output_desc.width = 640;
    resize_output_desc.height = 360;
    resize_output_desc.strides[0] = 640;
    resize_output_desc.strides[1] = 640;
    auto resize_output = common::AxImage::Create(resize_output_desc);
    if (!resize_output) {
        std::cerr << "resize output allocation failed\n";
        return 8;
    }

    if (!processor->Process(*source, resize_request, *resize_output)) {
        std::cerr << "resize process failed\n";
        return 9;
    }

    common::ImageProcessRequest rgb_request{};
    rgb_request.output_image.format = common::PixelFormat::kRgb24;
    rgb_request.output_image.width = source->width();
    rgb_request.output_image.height = source->height();
    auto rgb_output = processor->Process(*source, rgb_request);
    if (!rgb_output) {
        std::cerr << "rgb process failed\n";
        return 10;
    }

    common::ImageProcessRequest crop_request{};
    crop_request.enable_crop = true;
    crop_request.crop.x = 320;
    crop_request.crop.y = 180;
    crop_request.crop.width = 640;
    crop_request.crop.height = 360;
    crop_request.output_image.format = common::PixelFormat::kNv12;
    crop_request.output_image.width = 320;
    crop_request.output_image.height = 180;
    auto crop_output = processor->Process(*source, crop_request);
    if (!crop_output) {
        std::cerr << "crop process failed\n";
        return 11;
    }

    common::ImageProcessRequest align_resize_request{};
    align_resize_request.output_image.format = common::PixelFormat::kNv12;
    align_resize_request.output_image.width = 640;
    align_resize_request.output_image.height = 640;
    align_resize_request.resize.mode = common::ResizeMode::kKeepAspectRatio;
    auto align_resize_output = processor->Process(*source, align_resize_request);
    if (!align_resize_output) {
        std::cerr << "align resize process failed\n";
        return 11;
    }

    std::cout << "source_width=" << source->width() << "\n";
    std::cout << "source_height=" << source->height() << "\n";
    std::cout << "resize_width=" << resize_output->width() << "\n";
    std::cout << "resize_height=" << resize_output->height() << "\n";
    std::cout << "resize_checksum_y=" << PlaneChecksum(*resize_output, 0) << "\n";
    std::cout << "resize_checksum_uv=" << PlaneChecksum(*resize_output, 1) << "\n";
    std::cout << "rgb_width=" << rgb_output->width() << "\n";
    std::cout << "rgb_height=" << rgb_output->height() << "\n";
    std::cout << "rgb_memory_type=" << static_cast<int>(rgb_output->memory_type()) << "\n";
    std::cout << "rgb_block_id=" << rgb_output->block_id(0) << "\n";
    std::cout << "rgb_checksum=" << PlaneChecksum(*rgb_output, 0) << "\n";
    std::cout << "crop_width=" << crop_output->width() << "\n";
    std::cout << "crop_height=" << crop_output->height() << "\n";
    std::cout << "crop_memory_type=" << static_cast<int>(crop_output->memory_type()) << "\n";
    std::cout << "crop_block_id=" << crop_output->block_id(0) << "\n";
    std::cout << "crop_checksum_y=" << PlaneChecksum(*crop_output, 0) << "\n";
    std::cout << "crop_checksum_uv=" << PlaneChecksum(*crop_output, 1) << "\n";
    std::cout << "align_resize_width=" << align_resize_output->width() << "\n";
    std::cout << "align_resize_height=" << align_resize_output->height() << "\n";
    std::cout << "align_resize_checksum_y=" << PlaneChecksum(*align_resize_output, 0) << "\n";
    std::cout << "align_resize_checksum_uv=" << PlaneChecksum(*align_resize_output, 1) << "\n";

    if (PlaneChecksum(*resize_output, 0) == 0 || PlaneChecksum(*rgb_output, 0) == 0 ||
        PlaneChecksum(*crop_output, 0) == 0 || PlaneChecksum(*align_resize_output, 0) == 0) {
        return 12;
    }

    if (rgb_output->memory_type() != common::MemoryType::kPool || crop_output->memory_type() != common::MemoryType::kPool ||
        rgb_output->block_id(0) == 0 || crop_output->block_id(0) == 0) {
        return 13;
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    cmdline::parser parser;
    parser.set_program_name("ax_image_processor_smoke");
    parser.add<std::string>("input", 'i', "input MP4 path", false, "");
    parser.add<int>("timeout", 't', "timeout seconds", false, 10);

    const auto cli_result = tooling::ParseCommandLine(parser, argc, argv);
    if (cli_result != tooling::CliParseResult::kOk) {
        return tooling::CliParseExitCode(cli_result);
    }

    std::string input_path;
    int timeout_seconds = 10;
    if (!tooling::GetRequiredArgument(parser, "input", 0, "input", &input_path, std::cerr) ||
        !tooling::GetOptionalArgument(parser, "timeout", 1, 10, &timeout_seconds, std::cerr)) {
        std::cerr << parser.usage();
        return 1;
    }

    return Run(input_path.c_str(), timeout_seconds);
}
