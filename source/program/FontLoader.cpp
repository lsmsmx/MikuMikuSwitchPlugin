#include "FontLoader.hpp"
#include <cstddef>
#include <cmath>
#include <algorithm>
#include "lib.hpp"

namespace nn::pl {
    enum SharedFontType {
        SharedFontType_Standard,
        SharedFontType_ChineseSimple,
        SharedFontType_ChineseSimpleExtension,
        SharedFontType_ChineseTraditional,
        SharedFontType_Korean,
        SharedFontType_NintendoExtension,
        SharedFontType_Max,
    };

    enum SharedFontLoadState {
        SharedFontLoadState_Loading,
        SharedFontLoadState_Loaded,
    };

    void RequestSharedFontLoad(SharedFontType sharedFontType) noexcept;
    auto GetSharedFontLoadState(SharedFontType sharedFontType) noexcept -> SharedFontLoadState;
    auto GetSharedFontAddress(SharedFontType sharedFontType) noexcept -> const void *;
    auto GetSharedFontSize(SharedFontType sharedFontType) noexcept -> size_t;
}  // namespace nn::pl

namespace FontLoader {

    void LoadFonts(ImGuiIO& io) {
        nn::pl::RequestSharedFontLoad(nn::pl::SharedFontType_Standard);

        const void* fontData = nullptr;
        size_t fontSize = 0;

        if (nn::pl::GetSharedFontLoadState(nn::pl::SharedFontType_Standard) == nn::pl::SharedFontLoadState_Loaded) {
            fontData = nn::pl::GetSharedFontAddress(nn::pl::SharedFontType_Standard);
            fontSize = nn::pl::GetSharedFontSize(nn::pl::SharedFontType_Standard);
        }

        io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

        if (fontData != nullptr && fontSize > 0) {
            // Compact glyph range reducing texture atlas memory footprint (~0.7 MB vs ~16 MB)
            static const ImWchar s_ranges[] = {
                0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
                0x0400, 0x052F, // Cyrillic
                0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
                0,
            };

            // Dynamically resolve target scale from engine height (720p = 1.0, 1080p = 1.5, 1440p = 2.0, 4K = 3.0)
            uintptr_t mainBase = exl::util::GetMainModuleInfo().m_Total.m_Start;
            uint32_t engineHeight = *(uint32_t*)(mainBase + 0x00CA8970);
            float scale = (engineHeight >= 360 && engineHeight <= 4320) ? ((float)engineHeight / 720.0f) : 1.0f;

            // Base font scaled directly from original 11.0f design
            float baseFontSize = std::round(11.0f * scale);
            if (baseFontSize < 11.0f) baseFontSize = 11.0f;

            ImFontConfig cfg {};
            cfg.FontDataOwnedByAtlas = false;
            cfg.SizePixels           = baseFontSize;

            // Subpixel anti-aliasing without memory overhead
            cfg.OversampleH          = 2;
            cfg.OversampleV          = 1;
            cfg.PixelSnapH           = false;
            cfg.GlyphRanges          = s_ranges;

            io.Fonts->AddFontFromMemoryTTF(
                const_cast<void*>(fontData),
                static_cast<int>(fontSize),
                cfg.SizePixels,
                &cfg
            );
        } else {
            io.Fonts->AddFontDefault();
        }

        io.Fonts->Build();
    }

} // namespace FontLoader
