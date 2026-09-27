#include "imgui_alloc.hpp"
#include "Allocator.hpp"
#include "imgui/imgui.h"

namespace nvn_backend {

    static void* ImGuiAlloc(size_t size, void* /*user_data*/) {
        void* ptr = GameOperatorNew(size);
        return ptr;
    }

    static void ImGuiFree(void* ptr, void* /*user_data*/) {
        if (ptr) {
            GameOperatorDelete(ptr);
        }
    }

    bool TryConfigureImGuiAllocators() {
        static bool s_configured = false;
        if (!s_configured) {
            ImGui::SetAllocatorFunctions(ImGuiAlloc, ImGuiFree, nullptr);
            s_configured = true;
        }
        return true;
    }

}
