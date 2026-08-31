import os
import re
import struct
import sys

"""
Auth3D & StageData UID Compact Remapper for Project DIVA (Nintendo Switch / PC)

Remaps large mod UIDs in mod_auth_3d_db.bin and patches mod_stage_data.bin in-place
to prevent Out-Of-Memory (OOM) crashes on Nintendo Switch.

Binary format specifications and offsets referenced from ReDIVA (by korenkonder):
- auth_3d_db text parsing: ReDIVA/src/KKdLib/database/auth_3d.cpp
- stage_data binary layout: ReDIVA/src/KKdLib/database/stage.cpp
"""

class Auth3dRemapper:
    def __init__(self, threshold=5100):
        # UIDs above this threshold will be compacted
        self.threshold = threshold
        self.uid_map = {}  # Mapping dictionary: { old_uid : new_uid }

    def process_auth_3d_db(self, db_path):
        """
        Parses and overwrites mod_auth_3d_db.bin, compacting large UIDs
        and updating uid.max while populating the mapping dictionary.
        
        Format based on ReDIVA A3DA text specification.
        """
        print(f"[Auth3D] Reading file: {db_path}")
        
        with open(db_path, "rb") as f:
            data = f.read()

        # A3DA header signature (#A3DA__________ is 16 bytes)
        header = data[:16]
        text_content = data[16:].decode("utf-8", errors="ignore")
        lines = text_content.splitlines()

        # Find the highest vanilla UID to start contiguous remapping after it
        max_vanilla_uid = 0
        for line in lines:
            match = re.match(r"^uid\.\d+\.org_uid\s*=\s*(\d+)", line.strip())
            if match:
                uid = int(match.group(1))
                if uid <= self.threshold:
                    max_vanilla_uid = max(max_vanilla_uid, uid)

        next_free_uid = max(self.threshold, max_vanilla_uid) + 1
        highest_assigned_uid = max_vanilla_uid

        processed_lines = []
        for line in lines:
            strip_line = line.strip()
            # Match lines with format: uid.X.org_uid=YYYYYY
            match = re.match(r"^(uid\.\d+\.org_uid)\s*=\s*(\d+)", strip_line)
            
            if match:
                prefix = match.group(1)
                old_uid = int(match.group(2))

                if old_uid > self.threshold:
                    if old_uid not in self.uid_map:
                        self.uid_map[old_uid] = next_free_uid
                        next_free_uid += 1
                    
                    new_uid = self.uid_map[old_uid]
                    highest_assigned_uid = max(highest_assigned_uid, new_uid)
                    processed_lines.append(f"{prefix}={new_uid}")
                    print(f"  -> Compacted UID: {old_uid} -> {new_uid}")
                else:
                    highest_assigned_uid = max(highest_assigned_uid, old_uid)
                    processed_lines.append(line)
            elif strip_line.startswith("uid.max="):
                # Skip old uid.max, we'll append the updated max at the end
                pass 
            else:
                processed_lines.append(line)

        # Append corrected uid.max to the end of the text block
        processed_lines.append(f"uid.max={highest_assigned_uid}")
        print(f"  -> Set new uid.max={highest_assigned_uid}")

        # Rebuild and overwrite the binary file
        new_text = "\n".join(processed_lines) + "\n"
        with open(db_path, "wb") as f:
            f.write(header)
            f.write(new_text.encode("utf-8"))
            
        print("[Auth3D] Database successfully updated.\n")

    def patch_stage_data_bin(self, stage_path):
        """
        Patches stage_data.bin IN-PLACE (overwrites bytes directly without changing file size).
        
        Binary layout offsets referenced from ReDIVA (stage.cpp).
        """
        if not self.uid_map:
            print("[StageData] No UIDs mapped for replacement, skipping patch.")
            return

        print(f"[StageData] Patching file: {stage_path}")
        
        # 'rb+' mode allows reading and writing directly over existing bytes
        with open(stage_path, "rb+") as f:
            # Read header structure (as defined in ReDIVA stage.cpp):
            # count (uint32), stages_off (uint32), effects_off (uint32),
            # counts_off (uint32), offsets_off (uint32)
            header_data = f.read(20)
            stage_count, _, _, auth3d_counts_off, auth3d_offsets_off = struct.unpack("<5I", header_data)

            # 1. Read array of Auth3D ID counts per stage
            f.seek(auth3d_counts_off)
            auth3d_counts = struct.unpack(f"<{stage_count}I", f.read(4 * stage_count))

            # 2. Go to the array of Auth3D ID offset pointers
            f.seek(auth3d_offsets_off)
            
            # Store array offsets for each stage
            array_offsets = []
            for i in range(stage_count):
                stage_index, array_offset = struct.unpack("<2I", f.read(8))
                array_offsets.append(array_offset)

            # 3. Iterate through each stage and patch Auth3dIds in-place
            for i in range(stage_count):
                count = auth3d_counts[i]
                offset = array_offsets[i]

                # Skip if stage has no Auth3D effects
                if count == 0 or offset == 0:
                    continue

                f.seek(offset)
                
                # Read and evaluate each Auth3D ID
                for j in range(count):
                    pos = f.tell()  # Save current position
                    auth_id = struct.unpack("<I", f.read(4))[0]

                    # If this ID was compacted during Auth3D processing
                    if auth_id in self.uid_map:
                        new_id = self.uid_map[auth_id]
                        print(f"  -> Stage [{i}] replacing effect ID: {auth_id} -> {new_id}")
                        
                        # Seek back 4 bytes and overwrite in-place
                        f.seek(pos)
                        f.write(struct.pack("<I", new_id))
                        
                        # f.write advanced the cursor by 4 bytes, ready for next ID

        print("[StageData] Stage binary successfully patched!\n")


if __name__ == "__main__":
    AUTH_DB_PATH = "dlc10_auth_3d_db.bin"
    STAGE_DB_PATH = "dlc10_stage_data.bin"

    if os.path.exists(AUTH_DB_PATH) and os.path.exists(STAGE_DB_PATH):
        remapper = Auth3dRemapper()
        # Parse auth_3d first to build mapping dictionary
        remapper.process_auth_3d_db(AUTH_DB_PATH)
        # Patch stage_data using the map
        remapper.patch_stage_data_bin(STAGE_DB_PATH)
    else:
        print("Please place _auth_3d_db.bin and _stage_data.bin in the script directory!")