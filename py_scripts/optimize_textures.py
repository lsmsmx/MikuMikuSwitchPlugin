#!/usr/bin/env python3
import os
import sys
import struct
import gzip
import shutil

def log(tag: str, msg: str):
    print(f"[{tag.upper():^10}] {msg}")

# ==============================================================================
# 1. FARC UNPACK / PACK
# ==============================================================================
def unpack_farc(farc_path: str) -> dict[str, bytes]:
    with open(farc_path, "rb") as f:
        data = f.read()

    magic = data[:4]
    if magic not in (b"FArC", b"FArc"):
        return {}

    header_len, align = struct.unpack_from(">2I", data, 4)
    entries = {}
    pos = 12

    while pos < 8 + header_len:
        null_pos = data.find(b"\x00", pos)
        if null_pos == -1 or null_pos >= 8 + header_len:
            break
        name = data[pos:null_pos].decode("utf-8", errors="ignore")
        pos = null_pos + 1

        offset, comp_len, uncomp_len = struct.unpack_from(">3I", data, pos)
        pos += 12

        raw_chunk = data[offset:offset + comp_len]
        decomp = gzip.decompress(raw_chunk) if raw_chunk.startswith(b"\x1f\x8b") else raw_chunk
        entries[name] = decomp

    return entries

def pack_farc(out_path: str, files: dict[str, bytes], alignment: int = 16):
    file_list = []
    for name, raw in files.items():
        comp = gzip.compress(raw, compresslevel=9)
        file_list.append({
            "name": name.encode("utf-8") + b"\x00",
            "comp": comp,
            "raw_len": len(raw),
            "comp_len": len(comp),
            "offset": 0
        })

    table_size = sum(len(f["name"]) + 12 for f in file_list)
    header_end = 12 + table_size
    header_size = header_end - 8

    cur_offset = (header_end + (alignment - 1)) & ~(alignment - 1)
    for f in file_list:
        f["offset"] = cur_offset
        padded_size = (f["comp_len"] + (alignment - 1)) & ~(alignment - 1)
        cur_offset += padded_size

    with open(out_path, "wb") as f:
        f.write(b"FArC")
        f.write(struct.pack(">I", header_size))
        f.write(struct.pack(">I", alignment))

        for item in file_list:
            f.write(item["name"])
            f.write(struct.pack(">3I", item["offset"], item["comp_len"], item["raw_len"]))

        cur_pos = f.tell()
        first_off = file_list[0]["offset"]
        if first_off > cur_pos:
            f.write(b"x" * (first_off - cur_pos))

        for item in file_list:
            f.write(item["comp"])
            pad = (alignment - (f.tell() % alignment)) % alignment
            if pad > 0:
                f.write(b"x" * pad)

# ==============================================================================
# 2. TEXTURE OPTIMIZATION (DOWN TO 1024x1024)
# ==============================================================================
def optimize_single_txp(chunk: bytes) -> bytes:
    if not chunk.startswith(b"TXP\x04"):
        return chunk

    mip_count = struct.unpack_from("<I", chunk, 4)[0]
    if mip_count <= 1:
        return chunk

    m_offsets = list(struct.unpack_from(f"<{mip_count}I", chunk, 12))
    
    cur_offsets = m_offsets[:]
    cur_count = mip_count

    # The 1.25 MB threshold strictly separates 1024x1024 (<= 1.0 MB) from 2048x2048 (2.0 - 4.0 MB)
    while cur_count > 1:
        first_mip_len = cur_offsets[1] - cur_offsets[0]
        if first_mip_len < 1.25 * 1024 * 1024:
            break
        # Strip top mipmap level (downscaling 4K/2K textures to the next mip level)
        cur_offsets = cur_offsets[1:]
        cur_count -= 1

    if cur_count == mip_count:
        return chunk  # Texture is already 1024x1024 or smaller, leave untouched!

    new_header_len = 12 + cur_count * 4
    new_first_off = (new_header_len + 15) & ~15
    delta = cur_offsets[0] - new_first_off

    new_offsets = [off - delta for off in cur_offsets]
    new_hdr = bytearray(new_first_off)
    new_hdr[0:4] = b"TXP\x04"
    struct.pack_into("<I", new_hdr, 4, cur_count)
    new_hdr[8:12] = chunk[8:12]

    for idx, off in enumerate(new_offsets):
        struct.pack_into("<I", new_hdr, 12 + idx * 4, off)

    res = bytes(new_hdr) + chunk[cur_offsets[0]:]
    log("TEX", f"    Compressed texture: {len(chunk)/1024/1024:.1f} MB -> {len(res)/1024/1024:.1f} MB (capped at 1024x1024)")
    return res

def process_tex_bin(raw: bytes) -> tuple[bytes, int]:
    if len(raw) < 16 or struct.unpack_from("<I", raw, 0)[0] != 0x03505854:
        return raw, 0

    count, flags = struct.unpack_from("<2I", raw, 4)
    if count == 0:
        return raw, 0

    offsets = list(struct.unpack_from(f"<{count}I", raw, 12))
    optimized_textures = []
    shrunk_count = 0

    for i in range(count):
        start = offsets[i]
        end = offsets[i + 1] if (i + 1 < count) else len(raw)
        chunk = raw[start:end]
        opt_chunk = optimize_single_txp(chunk)
        if len(opt_chunk) < len(chunk):
            shrunk_count += 1
        optimized_textures.append(opt_chunk)

    if shrunk_count == 0:
        return raw, 0

    table_size = 12 + count * 4
    first_tex_off = (table_size + 15) & ~15

    new_bin = bytearray(first_tex_off)
    struct.pack_into("<3I", new_bin, 0, 0x03505854, count, flags)

    cur_pos = first_tex_off
    for idx, tex_bytes in enumerate(optimized_textures):
        struct.pack_into("<I", new_bin, 12 + idx * 4, cur_pos)
        new_bin.extend(tex_bytes)
        cur_pos += len(tex_bytes)

    return bytes(new_bin), shrunk_count

# ==============================================================================
# 3. MAIN WORKFLOW
# ==============================================================================
def process_mod_folder(mod_root: str, out_root: str):
    log("INIT", f"Original mod folder: {mod_root}")
    log("INIT", f"Output folder:        {out_root}")

    if os.path.exists(out_root):
        shutil.rmtree(out_root)
    shutil.copytree(mod_root, out_root)

    total_farcs = 0
    total_textures_optimized = 0
    saved_bytes = 0

    for dirpath, _, filenames in os.walk(out_root):
        for f in filenames:
            if not f.lower().endswith(".farc"):
                continue

            farc_path = os.path.join(dirpath, f)
            entries = unpack_farc(farc_path)
            if not entries:
                continue

            updated = False
            for inner_name, inner_data in entries.items():
                if inner_name.lower().endswith("_tex.bin"):
                    orig_len = len(inner_data)
                    new_data, count = process_tex_bin(inner_data)
                    if count > 0:
                        entries[inner_name] = new_data
                        updated = True
                        total_textures_optimized += count
                        saved_bytes += (orig_len - len(new_data))
                        log("FARC", f"[{f}] Optimized {count} texture(s)")

            if updated:
                pack_farc(farc_path, entries)
                total_farcs += 1

    log("DONE", "==================================================")
    log("DONE", f"Processed FARC archives: {total_farcs}")
    log("DONE", f"High-res (2K/4K) textures optimized: {total_textures_optimized}")
    log("DONE", f"Memory saved: {saved_bytes / 1024 / 1024:.1f} MB")
    log("DONE", "ALL 3D MODELS AND RIGGING ARE FULLY PRESERVED!")

if __name__ == "__main__":
    current_dir = os.path.dirname(os.path.abspath(__file__))

    folders = [
        d for d in os.listdir(current_dir)
        if os.path.isdir(os.path.join(current_dir, d))
        and not d.startswith(".")
        and "optimized" not in d.lower()
    ]
    if not folders:
        print("[!] Place the script next to the mod folder!")
        sys.exit(1)

    input_dir = os.path.join(current_dir, folders[0])
    output_dir = os.path.join(current_dir, f"{folders[0]}_Optimized")

    process_mod_folder(input_dir, output_dir)