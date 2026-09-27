#include "MemoryScannerUi.hpp"
#include "imgui/imgui_nvn.h"
#include <lib.hpp>
#include <hid.hpp>
#include <vector>
#include <algorithm>

namespace MemoryScannerUi {

    bool g_showWindow = false;

    static std::vector<MemoryGap> s_gaps;
    static float s_totalFreeBssMB = 0.0f;

    static constexpr size_t LIST_FILTER_BYTES = 32 * 1024 * 1024;
    static constexpr size_t SUM_FILTER_BYTES  = 8  * 1024 * 1024;

    static float s_clickFlashTimer = 0.0f;
    static bool s_lastL3State = false;

    static inline uintptr_t GetMainBase() {
        return exl::util::GetMainModuleInfo().m_Total.m_Start;
    }

    struct SvcMemoryInfo {
        uint64_t addr;
        uint64_t size;
        uint32_t type;
        uint32_t attr;
        uint32_t perm; // Bit 0: Read permission
        uint32_t ipc_ref;
        uint32_t device_ref;
        uint32_t pad;
    };

    // Fast kernel memory query with full clobber protection
    __attribute__((noinline))
    static bool QueryMemorySafe(uintptr_t addr, SvcMemoryInfo* outInfo) {
        uint32_t pageInfo = 0;
        register uintptr_t x0 asm("x0") = reinterpret_cast<uintptr_t>(outInfo);
        register uintptr_t x1 asm("x1") = reinterpret_cast<uintptr_t>(&pageInfo);
        register uintptr_t x2 asm("x2") = addr;

        asm volatile(
            "svc 0x06"
            : "+r"(x0), "+r"(x1), "+r"(x2)
            :
            : "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory"
        );
        return (uint32_t)x0 == 0;
    }

    void ScanGaps() {
        s_gaps.clear();
        s_totalFreeBssMB = 0.0f;

        uintptr_t base = GetMainBase();
        uintptr_t scanStart = base;
        uintptr_t scanEnd   = base + 0xBEA00000;

        // 256KB chunks: 4x fewer iterations, identical accuracy for 32MB+ gaps
        const size_t CHUNK_SIZE = 256 * 1024;
        constexpr size_t WORDS_PER_CHUNK = CHUNK_SIZE / sizeof(uint64_t); // 32768 words

        uintptr_t curStart = 0;
        size_t curLen = 0;

        uintptr_t addr = scanStart;
        while (addr < scanEnd) {
            SvcMemoryInfo mem = {};
            if (!QueryMemorySafe(addr, &mem)) {
                addr += CHUNK_SIZE;
                continue;
            }

            // Skip unmapped holes
            if ((mem.perm & 1) == 0) {
                if (curLen >= SUM_FILTER_BYTES) {
                    s_totalFreeBssMB += (float)curLen / (1024.0f * 1024.0f);
                }
                if (curLen >= LIST_FILTER_BYTES) {
                    s_gaps.push_back({ curStart - base, (curStart + curLen) - base, curLen, (float)curLen / (1024.0f * 1024.0f) });
                }
                curStart = 0;
                curLen = 0;

                uintptr_t nextAddr = mem.addr + mem.size;
                addr = (nextAddr > addr) ? nextAddr : (addr + CHUNK_SIZE);
                continue;
            }

            uintptr_t blockEnd = std::min(mem.addr + mem.size, scanEnd);

            while (addr + CHUNK_SIZE <= blockEnd) {
                const uint64_t* p = reinterpret_cast<const uint64_t*>(addr);
                bool isZero = true;

                // 1. Ultra-fast probe: check 8 widely spaced points across 256KB in 1 CPU cycle
                if ((p[0] | p[4096] | p[8192] | p[12288] | p[16384] | p[20480] | p[24576] | p[WORDS_PER_CHUNK - 1]) != 0ULL) {
                    isZero = false;
                } else {
                    // 2. Unrolled 64-byte batch read (processes 8 words per loop with 0 branch stalls)
                    for (size_t i = 0; i < WORDS_PER_CHUNK; i += 8) {
                        uint64_t batch = p[i] | p[i+1] | p[i+2] | p[i+3] | p[i+4] | p[i+5] | p[i+6] | p[i+7];
                        if (batch != 0ULL) {
                            isZero = false;
                            break;
                        }
                    }
                }

                if (isZero) {
                    if (curStart == 0) curStart = addr;
                    curLen += CHUNK_SIZE;
                } else {
                    if (curLen >= SUM_FILTER_BYTES) {
                        s_totalFreeBssMB += (float)curLen / (1024.0f * 1024.0f);
                    }
                    if (curLen >= LIST_FILTER_BYTES) {
                        s_gaps.push_back({ curStart - base, (curStart + curLen) - base, curLen, (float)curLen / (1024.0f * 1024.0f) });
                    }
                    curStart = 0;
                    curLen = 0;
                }

                addr += CHUNK_SIZE;
            }

            if (addr < blockEnd) {
                addr = blockEnd;
            }
        }

        if (curLen >= SUM_FILTER_BYTES) {
            s_totalFreeBssMB += (float)curLen / (1024.0f * 1024.0f);
        }
        if (curLen >= LIST_FILTER_BYTES) {
            s_gaps.push_back({ curStart - base, (curStart + curLen) - base, curLen, (float)curLen / (1024.0f * 1024.0f) });
        }
    }

    void Init() {
        s_gaps.clear();
        s_clickFlashTimer = 0.0f;
    }

    void DrawWindow() {
        if (!g_showWindow) return;

        nn::hid::NpadHandheldState npad = nn::hid::GetMergedNpadState();
        bool l3Down = (npad.buttons & nn::hid::Button::LStick) != 0;
        bool r3Down = (npad.buttons & nn::hid::Button::RStick) != 0;

        if (l3Down && !s_lastL3State && !r3Down) {
            ScanGaps();
            s_clickFlashTimer = 0.20f;
        }
        s_lastL3State = l3Down;

        // Anchor window to top-right corner; it grows downwards automatically
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(screenSize.x - 12.0f, 12.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("###BSSGapsHUDOverlay", nullptr, flags)) {
            ImGui::GetWindowDrawList()->PushClipRectFullScreen();

            // Miku Teal (#47DFD3) Title
            ImGui::TextColored(ImVec4(0.28f, 0.88f, 0.83f, 1.0f), "BSS RAM (32MB+ Gaps)");
            ImGui::SameLine(160.0f);

            bool isFlashing = (s_clickFlashTimer > 0.0f);
            if (isFlashing) {
                s_clickFlashTimer -= ImGui::GetIO().DeltaTime;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.43f, 0.95f));
            }

            if (ImGui::Button("Update", ImVec2(80, 22))) {
                ScanGaps();
                s_clickFlashTimer = 0.20f;
            }

            if (isFlashing) {
                ImGui::PopStyleColor(1);
            }

            ImGui::Text("Free: %.2f MB | Total: 3050 MB", s_totalFreeBssMB);
            ImGui::Separator();
            ImGui::Spacing();

            // No scroll area: window dynamically adjusts its height to fit all gaps
            if (s_gaps.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Press L3 or click to scan.");
            } else {
                for (size_t i = 0; i < s_gaps.size(); i++) {
                    const auto& g = s_gaps[i];
                    ImGui::Text("[%02zu] +0x%08lX", i, g.startOffset);
                    ImGui::SameLine(165.0f);
                    ImGui::TextColored(ImVec4(0.28f, 0.88f, 0.83f, 1.0f), "%.1f MB", g.sizeMB);
                }
            }

            ImGui::GetWindowDrawList()->PopClipRect();
        }
        ImGui::End();
    }

} // namespace MemoryScannerUi
