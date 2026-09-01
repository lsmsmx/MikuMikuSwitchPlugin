#include "lib.hpp"
#include "Types.hpp"
#include "AetDB.hpp"
#include "logger.hpp"

// Size and alignment match ARM64 (56 bytes)
struct AetSetEntry
{
	struct AetSetInfo
	{
		uint32_t id;
		const char* name;
		INSERT_PADDING(0x20);
	};

	INSERT_PADDING(0x8);
	AetSetInfo info;
};

struct AetSetComparer
{
	bool operator()(const AetSetEntry& entry, uint32_t id) const { return entry.info.id < id; }
	bool operator()(uint32_t id, const AetSetEntry& entry) const { return id < entry.info.id; }
};

// On Switch, the original function loads a pointer from this address.
// Dereferencing double pointer to get default entry.
static AetSetEntry::AetSetInfo** s_defaultAetSetEntry = nullptr;

// Hook GetAetSetEntry to perform a binary search on the std::vector instead of indexing a sparse array.
// This prevents allocating massive contiguous pointer tables for high/arbitrary IDs (e.g., ID 9841).
HOOK_DEFINE_TRAMPOLINE(GetAetSetEntry) {
	static AetSetEntry::AetSetInfo* Callback(void* a1, uint32_t id) {
		// Vector is located at offset +0x18 inside a1 (based on disassembly: ldp x8, x9, [x19, #0x18])
		auto aetSetEntries = reinterpret_cast<prj::vector<AetSetEntry>*>(reinterpret_cast<uint8_t*>(a1) + 0x18);

		auto it = std::equal_range(aetSetEntries->begin(), aetSetEntries->end(), id, AetSetComparer()).first;
		if (it == aetSetEntries->end() || it->info.id != id) {
			if (s_defaultAetSetEntry && *s_defaultAetSetEntry) {
				return *s_defaultAetSetEntry;
			}
			return nullptr;
		}
		return &it->info;
	}
};

void AetDB::init()
{
	uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
	s_defaultAetSetEntry = reinterpret_cast<AetSetEntry::AetSetInfo**>(base + 0x00CDF720);

	// 1. Skip allocating the large sparse lookup table in memory (B 0x11C: 0x001E1188 -> 0x001E12A4).
	// Saves substantial RAM by avoiding huge allocations for custom songs with high AetSet IDs.
	exl::patch::CodePatcher(0x001E1188).Write<uint32_t>(0x14000047);

	// 2. Install hook on GetAetSetEntry
	GetAetSetEntry::InstallAtOffset(0x001E2540);

	// 3. Patch inlined lookup table access inside FUN_001e2910 (starting at 0x001E292C).
	exl::patch::CodePatcher(0x001E292C).Write<uint32_t>(0xB9000BE2); // str  w2, [sp, #8]
	exl::patch::CodePatcher(0x001E2930).Write<uint32_t>(0xAA1303E0); // mov  x0, x19
	exl::patch::CodePatcher(0x001E2934).Write<uint32_t>(0x97FFFF03); // bl   0x001E2540
	exl::patch::CodePatcher(0x001E2938).Write<uint32_t>(0xB9400BE2); // ldr  w2, [sp, #8]
	exl::patch::CodePatcher(0x001E293C).Write<uint32_t>(0xAA0003E8); // mov  x8, x0
	exl::patch::CodePatcher(0x001E2940).Write<uint32_t>(0x14000008); // b    0x001E2960
}
