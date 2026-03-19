#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "ax_cmdline_utils.h"
#include "common/ax_system.h"
#include "pipeline/ax_pipeline_manager.h"

namespace {

using namespace axvsdk;

pipeline::PipelineConfig MakePipelineConfig(const std::string& input_path,
                                            const std::string& output_path,
                                            codec::VideoCodecType codec,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            std::atomic<int>* packet_counter,
                                            std::atomic<int>* key_counter) {
    pipeline::PipelineConfig config{};
    config.input.uri = input_path;
    config.input.realtime_playback = false;

    pipeline::PipelineOutputConfig output{};
    output.codec = codec;
    output.width = width;
    output.height = height;
    output.uris.push_back(output_path);
    output.packet_callback = [packet_counter, key_counter](codec::EncodedPacket packet) {
        if (packet_counter != nullptr) {
            packet_counter->fetch_add(1, std::memory_order_relaxed);
        }
        if (packet.key_frame && key_counter != nullptr) {
            key_counter->fetch_add(1, std::memory_order_relaxed);
        }
    };

    config.outputs.push_back(std::move(output));
    return config;
}

int Run(const char* input_path, const char* output_dir, int expected_packets, int timeout_seconds) {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
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

    auto manager = pipeline::CreatePipelineManager();
    if (!manager) {
        std::cerr << "CreatePipelineManager failed\n";
        return 4;
    }

    std::atomic<int> main_packets{0};
    std::atomic<int> main_keys{0};
    std::atomic<int> sub_packets{0};
    std::atomic<int> sub_keys{0};

    const auto main_config = MakePipelineConfig(std::string(input_path),
                                                (std::filesystem::path(output_dir) / "main.h264").string(),
                                                codec::VideoCodecType::kH264,
                                                1280,
                                                720,
                                                &main_packets,
                                                &main_keys);
    const auto sub_config = MakePipelineConfig(std::string(input_path),
                                               (std::filesystem::path(output_dir) / "sub.h265").string(),
                                               codec::VideoCodecType::kH265,
                                               640,
                                               360,
                                               &sub_packets,
                                               &sub_keys);

    if (!manager->AddPipeline("main", main_config)) {
        std::cerr << "AddPipeline main failed\n";
        return 5;
    }
    if (!manager->AddPipeline("sub", sub_config)) {
        manager->Clear();
        std::cerr << "AddPipeline sub failed\n";
        return 6;
    }

    if (!manager->StartAll()) {
        manager->Clear();
        std::cerr << "StartAll failed\n";
        return 7;
    }

    const auto start = std::chrono::steady_clock::now();
    while (true) {
        pipeline::ManagedPipelineInfo main_info{};
        pipeline::ManagedPipelineInfo sub_info{};
        const bool has_main = manager->GetPipelineInfo("main", &main_info);
        const bool has_sub = manager->GetPipelineInfo("sub", &sub_info);
        if (!has_main || !has_sub) {
            manager->Clear();
            std::cerr << "GetPipelineInfo failed\n";
            return 8;
        }

        const auto main_encoded = main_info.stats.output_stats.empty() ? 0ULL : main_info.stats.output_stats.front().encoded_packets;
        const auto sub_encoded = sub_info.stats.output_stats.empty() ? 0ULL : sub_info.stats.output_stats.front().encoded_packets;
        if (static_cast<int>(main_encoded) >= expected_packets && static_cast<int>(sub_encoded) >= expected_packets) {
            break;
        }

        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds(timeout_seconds)) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipeline::ManagedPipelineInfo main_info{};
    pipeline::ManagedPipelineInfo sub_info{};
    const bool has_main = manager->GetPipelineInfo("main", &main_info);
    const bool has_sub = manager->GetPipelineInfo("sub", &sub_info);

    manager->StopPipeline("main");
    manager->StopPipeline("sub");
    const bool removed_main = manager->RemovePipeline("main");
    const bool removed_sub = manager->RemovePipeline("sub");

    std::cout << "pipeline_count_after_remove=" << manager->ListPipelines().size() << "\n";

    if (!has_main || !has_sub) {
        return 9;
    }

    const auto main_encoded = main_info.stats.output_stats.empty() ? 0ULL : main_info.stats.output_stats.front().encoded_packets;
    const auto sub_encoded = sub_info.stats.output_stats.empty() ? 0ULL : sub_info.stats.output_stats.front().encoded_packets;
    const auto main_dropped = main_info.stats.output_stats.empty() ? 0ULL : main_info.stats.output_stats.front().dropped_frames;
    const auto sub_dropped = sub_info.stats.output_stats.empty() ? 0ULL : sub_info.stats.output_stats.front().dropped_frames;

    std::cout << "main_decoded_frames=" << main_info.stats.decoded_frames << "\n";
    std::cout << "main_encoded_packets=" << main_encoded << "\n";
    std::cout << "main_dropped_frames=" << main_dropped << "\n";
    std::cout << "main_callback_packets=" << main_packets.load(std::memory_order_relaxed) << "\n";
    std::cout << "main_key_packets=" << main_keys.load(std::memory_order_relaxed) << "\n";
    std::cout << "sub_decoded_frames=" << sub_info.stats.decoded_frames << "\n";
    std::cout << "sub_encoded_packets=" << sub_encoded << "\n";
    std::cout << "sub_dropped_frames=" << sub_dropped << "\n";
    std::cout << "sub_callback_packets=" << sub_packets.load(std::memory_order_relaxed) << "\n";
    std::cout << "sub_key_packets=" << sub_keys.load(std::memory_order_relaxed) << "\n";
    std::cout << "removed_main=" << static_cast<int>(removed_main) << "\n";
    std::cout << "removed_sub=" << static_cast<int>(removed_sub) << "\n";

    if (!removed_main || !removed_sub) {
        return 10;
    }
    if (main_info.stats.decoded_frames == 0 || sub_info.stats.decoded_frames == 0) {
        return 11;
    }
    if (main_encoded == 0 || sub_encoded == 0) {
        return 12;
    }
    if (main_packets.load(std::memory_order_relaxed) == 0 || sub_packets.load(std::memory_order_relaxed) == 0) {
        return 13;
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    cmdline::parser parser;
    parser.set_program_name("ax_pipeline_manager_smoke");
    parser.add<std::string>("input", 'i', "input MP4 path", false, "");
    parser.add<std::string>("output", 'o', "output directory", false, "");
    parser.add<int>("expected-packets", 'n', "expected packets per pipeline", false, 10);
    parser.add<int>("timeout", 't', "timeout seconds", false, 25);

    const auto cli_result = tooling::ParseCommandLine(parser, argc, argv);
    if (cli_result != tooling::CliParseResult::kOk) {
        return tooling::CliParseExitCode(cli_result);
    }

    std::string input_path;
    std::string output_dir;
    int expected_packets = 10;
    int timeout_seconds = 25;
    if (!tooling::GetRequiredArgument(parser, "input", 0, "input", &input_path, std::cerr) ||
        !tooling::GetRequiredArgument(parser, "output", 1, "output", &output_dir, std::cerr) ||
        !tooling::GetOptionalArgument(parser, "expected-packets", 2, 10, &expected_packets, std::cerr) ||
        !tooling::GetOptionalArgument(parser, "timeout", 3, 25, &timeout_seconds, std::cerr)) {
        std::cerr << parser.usage();
        return 1;
    }

    return Run(input_path.c_str(), output_dir.c_str(), expected_packets, timeout_seconds);
}
