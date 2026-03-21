# Saints Row (Xbox 360, 2006) - Static Recompilation

**Status: Early Analysis**

Static recompilation of Saints Row for Xbox 360 to native x86-64 PC executable using [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

Saints Row (2006) was an Xbox 360 exclusive and has never been ported to PC, making it a compelling preservation target.

## Background

This project uses the same toolchain proven across multiple Xbox 360 titles:
- [360tools](https://github.com/sp00nznet/360tools) -- Extraction, analysis, and project templates
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) -- PowerPC to C++ static recompiler
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) -- Xbox 360 runtime (kernel, D3D12 GPU, audio, input)

## Project Complexity

Saints Row is a large open-world game -- significantly more complex than the arcade ports and smaller titles that have been successfully recompiled so far. Key challenges:

- **Scale**: Large PE image with tens of thousands of functions
- **Open world**: Streaming world data, LOD systems, AI systems
- **Physics**: Havok physics engine integration
- **Audio**: Complex multi-channel audio with environmental effects
- **Networking**: Xbox Live stubs needed for save/profile systems

## Pipeline

```
Saints Row.iso
    |
    v
[ extract_iso.py ]          -- Extract game files from XDVDFS disc image
    |
    v
[ extract_pe.py ]           -- Decrypt XEX2 + decompress PE image
    |
    v
[ find_abi_addrs.py ]       -- Locate PPC ABI helpers
[ extract_switch_tables.py ] -- Map jump tables
[ xex_info.py ]             -- Analyze XEX2 headers
[ parse_xex_imports.py ]    -- Identify kernel/XAM imports
    |
    v
[ XenonRecomp ]             -- PowerPC -> C++ static recompilation
    |
    v
[ ReXGlue SDK ]             -- Xbox 360 runtime environment
    |
    v
Native x86-64 .exe          -- Saints Row on PC
```

## Progress

- [x] Acquire game ISO
- [x] Extract ISO contents (375 files from XGD2 disc)
- [x] Extract and analyze XEX2 binary (33.4 MB PE, 12 sections, Bink middleware)
- [x] Find ABI helper addresses (8/10 found, no setjmp/longjmp)
- [x] Analyze kernel imports (266 kernel + 204 XAM)
- [x] Run XenonRecomp first pass (33,725 functions -> 122 MB C++)
- [ ] Resolve unrecognized instructions (vandc, mulhdu, frsqrte)
- [ ] Add missing switch table entries (~30 tables)
- [ ] Create project scaffold (CMake + ReXGlue SDK)
- [ ] First compilation
- [ ] First boot
- [ ] Menu navigation
- [ ] In-game rendering
- [ ] Gameplay

## Related Projects

- [OpenRow2](https://github.com/KairiFey/OpenRow2) -- Decompilation of the Saints Row 2 PC port (different target, but useful for understanding game internals)
- [Xenia](https://github.com/xenia-project/xenia) -- Xbox 360 emulator with Saints Row compatibility info

## Prerequisites

- Python 3.8+ with `pycryptodome`
- CMake 3.25+, Ninja, Clang 18+ (clang-cl on Windows)
- MSVC 2022 (for Windows SDK headers)
- [360tools](https://github.com/sp00nznet/360tools)

## License

Tools and scripts in this repo are provided under the MIT License.
