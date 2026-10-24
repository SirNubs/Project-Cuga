#!/usr/bin/env python3
"""Wrap an MWo3 overlay image in a minimal ELF32 MIPS executable.

The complete overlay remains mapped at its original PSP address, but only the
code region described by the MWo3 header is exposed as executable .text.
The 64-byte MWo3 header and following data region remain loadable data.

Usage: wrap_overlay.py <overlay.bin> <base_address> <output.elf>
"""

import struct
import sys

EHDR_SIZE = 52
PHDR_SIZE = 32
SHDR_SIZE = 40
HEADER_BYTES = 64

# section names:
# 0  ""
# 1  ".header"
# 9  ".text"
# 15 ".data"
# 21 ".shstrtab"
SHSTRTAB = b"\0.header\0.text\0.data\0.shstrtab\0"


def main(argv):
    if len(argv) != 4:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    with open(argv[1], "rb") as source:
        data = source.read()

    base = int(argv[2], 0)
    if base % 4:
        print("base address must be 4-byte aligned", file=sys.stderr)
        return 1

    if len(data) < HEADER_BYTES or data[:4] != b"MWo3":
        print("not an MWo3 overlay image", file=sys.stderr)
        return 1

    _id, load, code_size, data_size = struct.unpack("<4I", data[4:20])

    if load != base:
        print(
            f"overlay loads at {load:#010x}, not requested base {base:#010x}",
            file=sys.stderr,
        )
        return 1

    expected_size = HEADER_BYTES + code_size + data_size
    if expected_size > len(data):
        print(
            f"truncated overlay: header requires {expected_size} bytes, "
            f"file has {len(data)}",
            file=sys.stderr,
        )
        return 1

    # Ignore any trailing dump bytes outside the declared overlay image.
    data = data[:expected_size]

    image_offset = EHDR_SIZE + PHDR_SIZE

    header_offset = image_offset
    text_offset = image_offset + HEADER_BYTES
    data_section_offset = text_offset + code_size

    header_addr = base
    text_addr = base + HEADER_BYTES
    data_addr = text_addr + code_size

    shstrtab_offset = image_offset + len(data)
    shdr_offset = shstrtab_offset + len(SHSTRTAB)

    # One PT_LOAD keeps the complete overlay image mapped exactly as the PSP
    # sees it. Section flags tell the static analyzer which bytes are code.
    ehdr = struct.pack(
        "<16sHHIIIIIHHHHHH",
        b"\x7fELF\x01\x01\x01" + b"\0" * 9,
        2,             # ET_EXEC
        8,             # EM_MIPS
        1,             # version
        text_addr,     # entry: first byte after MWo3 header
        EHDR_SIZE,     # phoff
        shdr_offset,   # shoff
        0x10A23001,    # PSP MIPS ELF flags
        EHDR_SIZE,
        PHDR_SIZE,
        1,             # one program header
        SHDR_SIZE,
        5,             # null + header + text + data + shstrtab
        4,             # .shstrtab index
    )

    # R|W|X on the load segment keeps all overlay bytes available to the ELF
    # loader. Executable discovery should follow the .text section's AX flags.
    phdr = struct.pack(
        "<8I",
        1,             # PT_LOAD
        image_offset,
        base,
        base,
        len(data),
        len(data),
        7,             # PF_R | PF_W | PF_X
        0x10,
    )

    null_shdr = b"\0" * SHDR_SIZE

    header_shdr = struct.pack(
        "<10I",
        1,             # ".header"
        1,             # SHT_PROGBITS
        0x2,           # SHF_ALLOC
        header_addr,
        header_offset,
        HEADER_BYTES,
        0, 0, 4, 0,
    )

    text_shdr = struct.pack(
        "<10I",
        9,             # ".text"
        1,             # SHT_PROGBITS
        0x6,           # SHF_ALLOC | SHF_EXECINSTR
        text_addr,
        text_offset,
        code_size,
        0, 0, 4, 0,
    )

    data_shdr = struct.pack(
        "<10I",
        15,            # ".data"
        1,             # SHT_PROGBITS
        0x3,           # SHF_WRITE | SHF_ALLOC
        data_addr,
        data_section_offset,
        data_size,
        0, 0, 4, 0,
    )

    shstrtab_shdr = struct.pack(
        "<10I",
        21,            # ".shstrtab"
        3,             # SHT_STRTAB
        0,
        0,
        shstrtab_offset,
        len(SHSTRTAB),
        0, 0, 1, 0,
    )

    with open(argv[3], "wb") as out:
        out.write(
            ehdr
            + phdr
            + data
            + SHSTRTAB
            + null_shdr
            + header_shdr
            + text_shdr
            + data_shdr
            + shstrtab_shdr
        )

    print(
        f"wrote {argv[3]}: "
        f"header={HEADER_BYTES} code={code_size} data={data_size} "
        f"base={base:#010x} text={text_addr:#010x}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
