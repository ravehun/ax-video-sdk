#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "ax_cmdline_utils.h"
#include "common/ax_image.h"
#include "common/ax_system.h"
#include "pipeline/ax_pipeline.h"

namespace {

using namespace axvsdk;

std::size_t PlaneCount(common::PixelFormat format) noexcept {
    switch (format) {
    case common::PixelFormat::kNv12:
        return 2;
    case common::PixelFormat::kRgb24:
    case common::PixelFormat::kBgr24:
        return 1;
    case common::PixelFormat::kUnknown:
    default:
        return 0;
    }
}

std::size_t PlaneRows(const common::ImageDescriptor& descriptor, std::size_t plane_index) noexcept {
    switch (descriptor.format) {
    case common::PixelFormat::kNv12:
        return plane_index == 0 ? descriptor.height : descriptor.height / 2U;
    case common::PixelFormat::kRgb24:
    case common::PixelFormat::kBgr24:
        return descriptor.height;
    case common::PixelFormat::kUnknown:
    default:
        return 0;
    }
}

std::size_t PlaneRowBytes(const common::ImageDescriptor& descriptor, std::size_t plane_index) noexcept {
    switch (descriptor.format) {
    case common::PixelFormat::kNv12:
        return plane_index < 2 ? descriptor.width : 0;
    case common::PixelFormat::kRgb24:
    case common::PixelFormat::kBgr24:
        return plane_index == 0 ? static_cast<std::size_t>(descriptor.width) * 3U : 0;
    case common::PixelFormat::kUnknown:
    default:
        return 0;
    }
}

std::uint64_t Checksum(const common::AxImage& image) {
    auto& mutable_image = const_cast<common::AxImage&>(image);
    if (!mutable_image.InvalidateCache()) {
        return 0;
    }

    std::uint64_t checksum = 0;
    const auto plane_count = PlaneCount(image.format());
    for (std::size_t plane = 0; plane < plane_count; ++plane) {
        const auto* data = image.plane_data(plane);
        if (data == nullptr) {
            return 0;
        }

        const auto rows = PlaneRows(image.descriptor(), plane);
        const auto row_bytes = PlaneRowBytes(image.descriptor(), plane);
        const auto stride = image.stride(plane);
        for (std::size_t row = 0; row < rows; ++row) {
            const auto* row_ptr = data + row * stride;
            for (std::size_t column = 0; column < row_bytes; ++column) {
                checksum += row_ptr[column];
            }
        }
    }

    return checksum;
}

int Run(const char* input_path, const char* output_path, int expected_frames, int timeout_seconds) {
    common::SystemOptions system_options{};
    system_options.enable_vdec = true;
    system_options.enable_venc = true;
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

    auto pipeline = pipeline::CreatePipeline();
    if (!pipeline) {
        std::cerr << "CreatePipeline failed\n";
        return 3;
    }

    std::atomic<int> callback_frames{0};
    std::atomic<int> callback_width{0};
    std::atomic<int> callback_height{0};
    std::atomic<int> callback_format{0};
    std::atomic<std::uint64_t> callback_checksum{0};
    std::atomic<int> callback_packets{0};

    pipeline::PipelineConfig config{};
    config.input.uri = input_path;
    config.input.realtime_playback = false;
    config.frame_output.output_image.format = common::PixelFormat::kBgr24;
    config.frame_output.output_image.width = 640;
    config.frame_output.output_image.height = 640;
    config.frame_output.resize.mode = common::ResizeMode::kKeepAspectRatio;

    pipeline::PipelineOutputConfig output{};
    output.codec = codec::VideoCodecType::kH264;
    output.width = 640;
    output.height = 360;
    output.uris.push_back(output_path);
    output.packet_callback = [&](codec::EncodedPacket) {
        callback_packets.fetch_add(1, std::memory_order_relaxed);
    };
    config.outputs.push_back(output);

    if (!pipeline->Open(config)) {
        std::cerr << "pipeline Open failed\n";
        return 4;
    }

    pipeline->SetFrameCallback([&](common::AxImage::Ptr frame) {
        if (!frame) {
            return;
        }
        callback_width.store(static_cast<int>(frame->width()), std::memory_order_relaxed);
        callback_height.store(static_cast<int>(frame->height()), std::memory_order_relaxed);
        callback_format.store(static_cast<int>(frame->format()), std::memory_order_relaxed);
        callback_checksum.store(Checksum(*frame), std::memory_order_relaxed);
        callback_frames.fetch_add(1, std::memory_order_relaxed);
    });

    if (!pipeline->Start()) {
        std::cerr << "pipeline Start failed\n";
        pipeline->Close();
        return 5;
    }

    auto user_output = common::AxImage::Create(common::PixelFormat::kNv12, 320, 320);
    if (!user_output) {
        pipeline->Stop();
        pipeline->Close();
        std::cerr << "user output alloc failed\n";
        return 6;
    }

    std::atomic<int> latest_hits{0};
    std::atomic<int> latest_width{0};
    std::atomic<int> latest_height{0};
    std::atomic<int> latest_format{0};
    std::atomic<std::uint64_t> latest_checksum{0};

    std::atomic<int> user_hits{0};
    std::atomic<std::uint64_t> user_checksum{0};

    const auto start = std::chrono::steady_clock::now();
    while (true) {
        auto latest = pipeline->GetLatestFrame();
        if (latest) {
            latest_width.store(static_cast<int>(latest->width()), std::memory_order_relaxed);
            latest_height.store(static_cast<int>(latest->height()), std::memory_order_relaxed);
            latest_format.store(static_cast<int>(latest->format()), std::memory_order_relaxed);
            latest_checksum.store(Checksum(*latest), std::memory_order_relaxed);
            latest_hits.fetch_add(1, std::memory_order_relaxed);
        }

        if (pipeline->GetLatestFrame(*user_output)) {
            user_checksum.store(Checksum(*user_output), std::memory_order_relaxed);
            user_hits.fetch_add(1, std::memory_order_relaxed);
        }

        if (callback_frames.load(std::memory_order_relaxed) >= expected_frames &&
            latest_hits.load(std::memory_order_relaxed) > 0 &&
            user_hits.load(std::memory_order_relaxed) > 0 &&
            callback_packets.load(std::memory_order_relaxed) > 0) {
            break;
        }

        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds(timeout_seconds)) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipeline->Stop();
    pipeline->Close();

    const auto cb = callback_frames.load(std::memory_order_relaxed);
    const auto cb_w = callback_width.load(std::memory_order_relaxed);
    const auto cb_h = callback_height.load(std::memory_order_relaxed);
    const auto cb_fmt = callback_format.load(std::memory_order_relaxed);
    const auto cb_sum = callback_checksum.load(std::memory_order_relaxed);
    const auto poll_hits = latest_hits.load(std::memory_order_relaxed);
    const auto poll_w = latest_width.load(std::memory_order_relaxed);
    const auto poll_h = latest_height.load(std::memory_order_relaxed);
    const auto poll_fmt = latest_format.load(std::memory_order_relaxed);
    const auto poll_sum = latest_checksum.load(std::memory_order_relaxed);
    const auto user_ok = user_hits.load(std::memory_order_relaxed);
    const auto user_sum = user_checksum.load(std::memory_order_relaxed);
    const auto packets = callback_packets.load(std::memory_order_relaxed);

    std::cout << "callback_frames=" << cb << "\n";
    std::cout << "callback_width=" << cb_w << "\n";
    std::cout << "callback_height=" << cb_h << "\n";
    std::cout << "callback_format=" << cb_fmt << "\n";
    std::cout << "callback_checksum=" << cb_sum << "\n";
    std::cout << "latest_hits=" << poll_hits << "\n";
    std::cout << "latest_width=" << poll_w << "\n";
    std::cout << "latest_height=" << poll_h << "\n";
    std::cout << "latest_format=" << poll_fmt << "\n";
    std::cout << "latest_checksum=" << poll_sum << "\n";
    std::cout << "user_hits=" << user_ok << "\n";
    std::cout << "user_width=" << user_output->width() << "\n";
    std::cout << "user_height=" << user_output->height() << "\n";
    std::cout << "user_format=" << static_cast<int>(user_output->format()) << "\n";
    std::cout << "user_checksum=" << user_sum << "\n";
    std::cout << "encoded_packets=" << packets << "\n";

    if (cb <= 0 || cb_w != 640 || cb_h != 640 ||
        cb_fmt != static_cast<int>(common::PixelFormat::kBgr24) || cb_sum == 0) {
        return 7;
    }
    if (poll_hits <= 0 || poll_w != 640 || poll_h != 640 ||
        poll_fmt != static_cast<int>(common::PixelFormat::kBgr24) || poll_sum == 0) {
        return 8;
    }
    if (user_ok <= 0 || user_output->width() != 320 || user_output->height() != 320 || user_sum == 0 || packets <= 0) {
        return 9;
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    cmdline::parser parser;
    parser.set_program_name("ax_pipeline_frame_smoke");
    parser.add<std::string>("input", 'i', "input URI/path", false, "");
    parser.add<std::string>("output", 'o', "output URI/path", false, "");
    parser.add<int>("expected-frames", 'n', "expected frame callbacks", false, 10);
    parser.add<int>("timeout", 't', "timeout seconds", false, 25);

    const auto cli_result = tooling::ParseCommandLine(parser, argc, argv);
    if (cli_result != tooling::CliParseResult::kOk) {
        return tooling::CliParseExitCode(cli_result);
    }

    std::string input_uri;
    std::string output_uri;
    int expected_frames = 10;
    int timeout_seconds = 25;
    if (!tooling::GetRequiredArgument(parser, "input", 0, "input", &input_uri, std::cerr) ||
        !tooling::GetRequiredArgument(parser, "output", 1, "output", &output_uri, std::cerr) ||
        !tooling::GetOptionalArgument(parser, "expected-frames", 2, 10, &expected_frames, std::cerr) ||
        !tooling::GetOptionalArgument(parser, "timeout", 3, 25, &timeout_seconds, std::cerr)) {
        std::cerr << parser.usage();
        return 1;
    }

    return Run(input_uri.c_str(), output_uri.c_str(), expected_frames, timeout_seconds);
}
