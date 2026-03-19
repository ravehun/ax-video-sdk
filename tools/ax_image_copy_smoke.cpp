#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ax_cmdline_utils.h"
#include "ax_mp4_decode_util.h"
#include "ax_image_copy.h"
#include "ax_image_internal.h"
#include "codec/ax_video_decoder.h"
#include "common/ax_image.h"
#include "common/ax_image_processor.h"
#include "common/ax_system.h"

namespace {

using namespace axvsdk;

std::uint64_t Checksum(common::AxImage& image) {
    if (!image.InvalidateCache()) {
        return 0;
    }

    std::uint64_t sum = 0;
    for (std::size_t plane = 0; plane < image.plane_count(); ++plane) {
        const auto* data = image.plane_data(plane);
        if (data == nullptr) {
            return 0;
        }

        const std::size_t row_bytes =
            image.format() == common::PixelFormat::kNv12 ? image.width() :
            static_cast<std::size_t>(image.width()) * 3U;
        const std::size_t rows =
            image.format() == common::PixelFormat::kNv12 && plane == 1 ? image.height() / 2U : image.height();
        const auto stride = image.stride(plane);
        if (stride < row_bytes) {
            return 0;
        }

        for (std::size_t row = 0; row < rows; ++row) {
            const auto* row_ptr = data + row * stride;
            for (std::size_t col = 0; col < row_bytes; ++col) {
                sum += row_ptr[col];
            }
        }
    }

    return sum;
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

    auto nv12_source = WaitForFrame(*decoder, timeout_seconds);
    stop_feed.store(true, std::memory_order_relaxed);
    decoder->Stop();
    if (feed_thread.joinable()) {
        feed_thread.join();
    }
    decoder->Close();
    if (!nv12_source) {
        std::cerr << "no source frame\n";
        return 7;
    }

    common::ImageProcessRequest bgr_request{};
    bgr_request.output_image.format = common::PixelFormat::kBgr24;
    bgr_request.output_image.width = nv12_source->width();
    bgr_request.output_image.height = nv12_source->height();
    auto bgr_source = processor->Process(*nv12_source, bgr_request);
    if (!bgr_source) {
        std::cerr << "bgr conversion failed\n";
        return 8;
    }

    auto nv12_copy = common::AxImage::Create(nv12_source->descriptor());
    auto nv12_frame_copy = common::AxImage::Create(nv12_source->descriptor());
    auto bgr_copy = common::AxImage::Create(bgr_source->descriptor());
    if (!nv12_copy || !nv12_frame_copy || !bgr_copy) {
        std::cerr << "AxImage::Create failed\n";
        return 9;
    }

    if (!common::internal::CopyImage(*nv12_source, nv12_copy.get())) {
        std::cerr << "CopyImage nv12 failed\n";
        return 10;
    }
    if (!common::internal::CopyVideoFrameToImage(common::internal::AxImageAccess::GetAxFrameInfo(*nv12_source),
                                                 nv12_frame_copy.get())) {
        std::cerr << "CopyVideoFrameToImage nv12 failed\n";
        return 11;
    }
    if (!common::internal::CopyImage(*bgr_source, bgr_copy.get())) {
        std::cerr << "CopyImage bgr failed\n";
        return 12;
    }

    const auto nv12_source_sum = Checksum(*nv12_source);
    const auto nv12_copy_sum = Checksum(*nv12_copy);
    const auto nv12_frame_copy_sum = Checksum(*nv12_frame_copy);
    const auto bgr_source_sum = Checksum(*bgr_source);
    const auto bgr_copy_sum = Checksum(*bgr_copy);

    std::cout << "nv12_source_checksum=" << nv12_source_sum << "\n";
    std::cout << "nv12_copy_checksum=" << nv12_copy_sum << "\n";
    std::cout << "nv12_frame_copy_checksum=" << nv12_frame_copy_sum << "\n";
    std::cout << "bgr_source_checksum=" << bgr_source_sum << "\n";
    std::cout << "bgr_copy_checksum=" << bgr_copy_sum << "\n";

    if (nv12_copy_sum == 0 || nv12_frame_copy_sum == 0 || bgr_source_sum == 0) {
        return 12;
    }
    if (nv12_copy_sum != nv12_frame_copy_sum) {
        return 13;
    }
    if (bgr_source_sum != bgr_copy_sum) {
        return 14;
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    cmdline::parser parser;
    parser.set_program_name("ax_image_copy_smoke");
    parser.add<std::string>("input", 'i', "input MP4 path", false, "");
    parser.add<int>("timeout", 't', "timeout seconds", false, 15);

    const auto cli_result = tooling::ParseCommandLine(parser, argc, argv);
    if (cli_result != tooling::CliParseResult::kOk) {
        return tooling::CliParseExitCode(cli_result);
    }

    std::string input_path;
    int timeout_seconds = 15;
    if (!tooling::GetRequiredArgument(parser, "input", 0, "input", &input_path, std::cerr) ||
        !tooling::GetOptionalArgument(parser, "timeout", 1, 15, &timeout_seconds, std::cerr)) {
        std::cerr << parser.usage();
        return 1;
    }

    return Run(input_path.c_str(), timeout_seconds);
}
