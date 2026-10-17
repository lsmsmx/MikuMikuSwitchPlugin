#include "FontLoader.hpp"
#include <cstddef>

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

            ImFontConfig cfg {};
            cfg.FontDataOwnedByAtlas = false;
            cfg.SizePixels           = 11.0f;

            // Oversample must be set to 1 to avoid running out of font atlas memory and crashing
            cfg.OversampleH          = 1;
            cfg.OversampleV          = 1;
            cfg.PixelSnapH           = true;
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
