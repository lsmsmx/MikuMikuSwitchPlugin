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
static AetSetEntry::AetSetInfo** defaultAetSetEntry = reinterpret_cast<AetSetEntry::AetSetInfo**>(0x00CDF720);

// Trampoline hook to replace function logic
HOOK_DEFINE_TRAMPOLINE(GetAetSetEntry) {
	static AetSetEntry::AetSetInfo* Callback(void* a1, uint32_t id) {
		// Vector is located at offset +0x18 inside a1 (based on disassembly: ldp x8, x9, [x19, #0x18])
		auto aetSetEntries = reinterpret_cast<prj::vector<AetSetEntry>*>(reinterpret_cast<uint8_t*>(a1) + 0x18);

		auto it = std::equal_range(aetSetEntries->begin(), aetSetEntries->end(), id, AetSetComparer()).first;
		if (it == aetSetEntries->end() || it->info.id != id)
			return *defaultAetSetEntry;

		return &it->info;
	}
};

void AetDB::init()
{
	// Skip nnSdk init and loop after saving max_id.
	// Branch from 0x001E1188 to 0x001E12A4 (+0x11C bytes: B 0x11C -> 0x14000047).
	exl::patch::CodePatcher(0x001E1188).Write<uint32_t>(0x14000047);

	// Install hook
	GetAetSetEntry::InstallAtOffset(0x001E2540);
}
