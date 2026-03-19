#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ax_cmdline_utils.h"
#include "ax_mp4_decode_util.h"
#include "codec/ax_jpeg_codec.h"
#include "codec/ax_video_decoder.h"
#include "common/ax_image_processor.h"
#include "common/ax_system.h"

namespace {

using namespace axvsdk;

bool WriteJpeg(const common::AxImage& image, const std::filesystem::path& path) {
    codec::JpegEncodeOptions options{};
    options.quality = 90;
    return codec::EncodeJpegToFile(image, path.string(), options);
}

bool WriteMetadata(const std::filesystem::path& path,
                   const common::AxImage& source,
                   const common::AxImage& resize_stretch,
                   const common::AxImage& resize_align,
                   const common::AxImage& crop,
                   const common::AxImage& crop_resize,
                   const common::AxImage& crop_resize_align,
                   const common::AxImage& rgb) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }

    output << "source_width=" << source.width() << "\n";
    output << "source_height=" << source.height() << "\n";
    output << "resize_stretch_width=" << resize_stretch.width() << "\n";
    output << "resize_stretch_height=" << resize_stretch.height() << "\n";
    output << "resize_align_width=" << resize_align.width() << "\n";
    output << "resize_align_height=" << resize_align.height() << "\n";
    output << "crop_width=" << crop.width() << "\n";
    output << "crop_height=" << crop.height() << "\n";
    output << "crop_resize_width=" << crop_resize.width() << "\n";
    output << "crop_resize_height=" << crop_resize.height() << "\n";
    output << "crop_resize_align_width=" << crop_resize_align.width() << "\n";
    output << "crop_resize_align_height=" << crop_resize_align.height() << "\n";
    output << "rgb_width=" << rgb.width() << "\n";
    output << "rgb_height=" << rgb.height() << "\n";
    output << "rgb_memory_type=" << static_cast<int>(rgb.memory_type()) << "\n";
    output << "rgb_block_id=" << rgb.block_id(0) << "\n";
    output << "crop_memory_type=" << static_cast<int>(crop.memory_type()) << "\n";
    output << "crop_block_id=" << crop.block_id(0) << "\n";
    output << "crop_resize_memory_type=" << static_cast<int>(crop_resize.memory_type()) << "\n";
    output << "crop_resize_block_id=" << crop_resize.block_id(0) << "\n";
    output << "crop_resize_align_memory_type=" << static_cast<int>(crop_resize_align.memory_type()) << "\n";
    output << "crop_resize_align_block_id=" << crop_resize_align.block_id(0) << "\n";
    return output.good();
}

common::AxImage::Ptr WaitForFrame(codec::VideoDecoder& decoder, int timeout_seconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        auto frame = decoder.GetLatestFrame();
        if (frame) {
            return frame;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return nullptr;
}

int Run(const char* input_path, const char* output_dir, int timeout_seconds) {
    const std::filesystem::path output_root(output_dir);
    std::error_code ec;
    std::filesystem::create_directories(output_root, ec);
    if (ec) {
        std::cerr << "create_directories failed: " << ec.message() << "\n";
        return 2;
    }

    common::SystemOptions system_options{};
    system_options.enable_vdec = true;
    system_options.enable_venc = true;
    system_options.enable_ivps = true;
    if (!common::InitializeSystem(system_options)) {
        std::cerr << "InitializeSystem failed\n";
        return 3;
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
        return 4;
    }

    auto demuxer = tooling::OpenDemuxer(input_path, false);
    if (!demuxer) {
        std::cerr << "OpenDemuxer failed\n";
        return 5;
    }

    const auto decoder_config = tooling::MakeDecoderConfigFromStreamInfo(demuxer->GetVideoStreamInfo());
    if (!decoder->Open(decoder_config)) {
        std::cerr << "decoder Open failed\n";
        return 6;
    }

    if (!decoder->Start()) {
        std::cerr << "decoder Start failed\n";
        decoder->Close();
        return 7;
    }

    std::atomic<bool> stop_feed{false};
    std::thread feed_thread([&] {
        (void)tooling::FeedDecoderFromDemuxer(*demuxer, *decoder, &stop_feed);
    });

    auto source = WaitForFrame(*decoder, timeout_seconds);
    stop_feed.store(true, std::memory_order_relaxed);
    decoder->Stop();
    if (feed_thread.joinable()) {
        feed_thread.join();
    }
    decoder->Close();

    if (!source) {
        std::cerr << "no source frame\n";
        return 8;
    }

    common::ImageProcessRequest resize_request{};
    resize_request.output_image.format = common::PixelFormat::kNv12;
    resize_request.output_image.width = 640;
    resize_request.output_image.height = 640;
    auto resize_stretch_output = processor->Process(*source, resize_request);
    if (!resize_stretch_output) {
        std::cerr << "resize stretch process failed\n";
        return 8;
    }

    common::ImageProcessRequest resize_align_request = resize_request;
    resize_align_request.resize.mode = common::ResizeMode::kKeepAspectRatio;
    auto resize_align_output = processor->Process(*source, resize_align_request);
    if (!resize_align_output) {
        std::cerr << "resize align process failed\n";
        return 9;
    }

    common::ImageProcessRequest crop_request{};
    crop_request.enable_crop = true;
    crop_request.crop.x = 320;
    crop_request.crop.y = 180;
    crop_request.crop.width = 640;
    crop_request.crop.height = 360;
    crop_request.output_image.format = common::PixelFormat::kNv12;
    crop_request.output_image.width = 640;
    crop_request.output_image.height = 360;
    auto crop_output = processor->Process(*source, crop_request);
    if (!crop_output) {
        std::cerr << "crop process failed\n";
        return 10;
    }

    common::ImageProcessRequest crop_resize_request = crop_request;
    crop_resize_request.output_image.width = 320;
    crop_resize_request.output_image.height = 320;
    auto crop_resize_output = processor->Process(*source, crop_resize_request);
    if (!crop_resize_output) {
        std::cerr << "crop resize process failed\n";
        return 11;
    }

    common::ImageProcessRequest crop_resize_align_request = crop_resize_request;
    crop_resize_align_request.resize.mode = common::ResizeMode::kKeepAspectRatio;
    auto crop_resize_align_output = processor->Process(*source, crop_resize_align_request);
    if (!crop_resize_align_output) {
        std::cerr << "crop resize align process failed\n";
        return 12;
    }

    common::ImageProcessRequest rgb_request{};
    rgb_request.output_image.format = common::PixelFormat::kRgb24;
    rgb_request.output_image.width = source->width();
    rgb_request.output_image.height = source->height();
    auto rgb_output = processor->Process(*source, rgb_request);
    if (!rgb_output) {
        std::cerr << "rgb process failed\n";
        return 13;
    }

    if (!WriteJpeg(*source, output_root / "source.jpg") ||
        !WriteJpeg(*resize_stretch_output, output_root / "resize_stretch_640x640.jpg") ||
        !WriteJpeg(*resize_align_output, output_root / "resize_align_640x640.jpg") ||
        !WriteJpeg(*crop_output, output_root / "crop_640x360.jpg") ||
        !WriteJpeg(*crop_resize_output, output_root / "crop_resize_320x320.jpg") ||
        !WriteJpeg(*crop_resize_align_output, output_root / "crop_resize_align_320x320.jpg") ||
        !WriteJpeg(*rgb_output, output_root / "csc_rgb24.jpg") ||
        !WriteMetadata(output_root / "metadata.txt", *source, *resize_stretch_output, *resize_align_output,
                       *crop_output, *crop_resize_output, *crop_resize_align_output, *rgb_output)) {
        std::cerr << "write outputs failed\n";
        return 14;
    }

    std::cout << "output_dir=" << output_root << "\n";
    std::cout << "source_width=" << source->width() << "\n";
    std::cout << "source_height=" << source->height() << "\n";
    std::cout << "resize_stretch_width=" << resize_stretch_output->width() << "\n";
    std::cout << "resize_stretch_height=" << resize_stretch_output->height() << "\n";
    std::cout << "resize_align_width=" << resize_align_output->width() << "\n";
    std::cout << "resize_align_height=" << resize_align_output->height() << "\n";
    std::cout << "crop_width=" << crop_output->width() << "\n";
    std::cout << "crop_height=" << crop_output->height() << "\n";
    std::cout << "crop_resize_width=" << crop_resize_output->width() << "\n";
    std::cout << "crop_resize_height=" << crop_resize_output->height() << "\n";
    std::cout << "crop_resize_align_width=" << crop_resize_align_output->width() << "\n";
    std::cout << "crop_resize_align_height=" << crop_resize_align_output->height() << "\n";
    std::cout << "rgb_width=" << rgb_output->width() << "\n";
    std::cout << "rgb_height=" << rgb_output->height() << "\n";

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    cmdline::parser parser;
    parser.set_program_name("ax_image_processor_dump");
    parser.add<std::string>("input", 'i', "input MP4 path", false, "");
    parser.add<std::string>("output", 'o', "output directory", false, "");
    parser.add<int>("timeout", 't', "timeout seconds", false, 10);

    const auto cli_result = tooling::ParseCommandLine(parser, argc, argv);
    if (cli_result != tooling::CliParseResult::kOk) {
        return tooling::CliParseExitCode(cli_result);
    }

    std::string input_path;
    std::string output_dir;
    int timeout_seconds = 10;
    if (!tooling::GetRequiredArgument(parser, "input", 0, "input", &input_path, std::cerr) ||
        !tooling::GetRequiredArgument(parser, "output", 1, "output", &output_dir, std::cerr) ||
        !tooling::GetOptionalArgument(parser, "timeout", 2, 10, &timeout_seconds, std::cerr)) {
        std::cerr << parser.usage();
        return 1;
    }

    return Run(input_path.c_str(), output_dir.c_str(), timeout_seconds);
}
