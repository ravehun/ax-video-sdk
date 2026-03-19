#include <cstdio>
#include <cstring>
#include <string>

#include "ax_cmdline_utils.h"
#include "ax_ivps_api.h"
#include "ax_sys_api.h"
#include "ax_vdec_api.h"
#include "ax_venc_api.h"
#include "ax_venc_comm.h"

namespace {

void PrintRet(const char* name, AX_S32 ret) {
    std::printf("%s ret=0x%x\n", name, ret);
    std::fflush(stdout);
}

AX_VENC_MOD_ATTR_T MakeVencModAttr() {
    AX_VENC_MOD_ATTR_T mod_attr{};
    mod_attr.enVencType = AX_VENC_MULTI_ENCODER;
    mod_attr.stModThdAttr.u32TotalThreadNum = 3;
    mod_attr.stModThdAttr.bExplicitSched = AX_FALSE;
    return mod_attr;
}

}  // namespace

int main(int argc, char** argv) {
    cmdline::parser parser;
    parser.set_program_name("ax_system_probe");
    parser.add("vdec", 0, "probe vdec");
    parser.add("venc", 0, "probe venc");
    parser.add("ivps", 0, "probe ivps");
    parser.add("vdec-null", 0, "use AX_VDEC_Init(NULL)");

    const auto cli_result = axvsdk::tooling::ParseCommandLine(parser, argc, argv);
    if (cli_result != axvsdk::tooling::CliParseResult::kOk) {
        return axvsdk::tooling::CliParseExitCode(cli_result);
    }

    const bool probe_vdec = parser.exist("vdec");
    const bool probe_venc = parser.exist("venc");
    const bool probe_ivps = parser.exist("ivps");
    const bool use_vdec_null = parser.exist("vdec-null");

    std::printf("probe_vdec=%d probe_venc=%d probe_ivps=%d use_vdec_null=%d\n",
                probe_vdec ? 1 : 0,
                probe_venc ? 1 : 0,
                probe_ivps ? 1 : 0,
                use_vdec_null ? 1 : 0);
    std::fflush(stdout);

    AX_S32 ret = AX_SYS_Init();
    PrintRet("AX_SYS_Init", ret);
    if (ret != AX_SUCCESS) {
        return 2;
    }

    bool vdec_inited = false;
    bool venc_inited = false;
    bool ivps_inited = false;

    if (probe_vdec) {
        AX_VDEC_MOD_ATTR_T mod_attr{};
        mod_attr.u32MaxGroupCount = 16;
        ret = use_vdec_null ? AX_VDEC_Init(nullptr) : AX_VDEC_Init(&mod_attr);
        PrintRet(use_vdec_null ? "AX_VDEC_Init(NULL)" : "AX_VDEC_Init(attr)", ret);
        if (ret != AX_SUCCESS) {
            goto done;
        }
        vdec_inited = true;
    }

    if (probe_venc) {
        const auto mod_attr = MakeVencModAttr();
        ret = AX_VENC_Init(&mod_attr);
        PrintRet("AX_VENC_Init", ret);
        if (ret != AX_SUCCESS) {
            goto done;
        }
        venc_inited = true;
    }

    if (probe_ivps) {
        ret = AX_IVPS_Init();
        PrintRet("AX_IVPS_Init", ret);
        if (ret != AX_SUCCESS) {
            goto done;
        }
        ivps_inited = true;
    }

    ret = AX_SUCCESS;

done:
    if (ivps_inited) {
        PrintRet("AX_IVPS_Deinit", AX_IVPS_Deinit());
    }
    if (venc_inited) {
        PrintRet("AX_VENC_Deinit", AX_VENC_Deinit());
    }
    if (vdec_inited) {
        PrintRet("AX_VDEC_Deinit", AX_VDEC_Deinit());
    }
    PrintRet("AX_SYS_Deinit", AX_SYS_Deinit());
    return ret == AX_SUCCESS ? 0 : 1;
}
