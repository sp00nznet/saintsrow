# Saints Row (Xbox 360) - Binary Analysis

## XEX2 Header
- File size: 9,293,824 bytes (8.9 MB)
- Module flags: 0x00000001
- PE data offset: 0x5000
- Encryption: AES-128 (Normal)
- Compression: Basic block (3 blocks)
- Image size: 0x2160000 (33.4 MB decompressed)
- Image base: 0x82000000
- Entry point: 0x827178B0
- Imports: xboxkrnl.exe (266 records), xam.xex (204 records)

## PE Sections (12)
| Section  | VA Start     | Virtual Size | Type        |
|----------|-------------|-------------|-------------|
| .rdata   | 0x82000400  | 0x08A6BC    | Data, R     |
| .pdata   | 0x8208AC00  | 0x01FBB8    | Data, R     |
| BINKBSS  | 0x820AA800  | 0x004908    | Data, R     |
| .text    | 0x820B0000  | 0x6D9544    | Code, X, R  |
| BINK     | 0x82789600  | 0x010124    | Code, X, R  |
| .data    | 0x827A0000  | 0x191C70C   | Data, R, W  |
| BINKDATA | 0x840BC800  | 0x003D08    | Data, R, W  |
| .tls     | 0x840C0600  | 0x001E45    | Data, R, W  |
| .XEXID   | 0x840C2600  | 0x000004    | Data, R, W  |
| .idata   | 0x840D0000  | 0x000436    | Data, R, W  |
| .XBLD    | 0x840E0000  | 0x0000C0    | Data, R     |
| .reloc   | 0x840E0200  | 0x098F28    | Data, R     |

## Code Sections
- **.text**: 0x820B0000 - 0x82789544 (6.84 MB, main game code)
- **BINK**: 0x82789600 - 0x82799724 (64 KB, Bink video middleware)

## ABI Helper Functions
| Function         | Address      |
|-----------------|-------------|
| __savegprlr_14  | 0x82702850  |
| __restgprlr_14  | 0x827028A0  |
| __savefpr_14    | 0x82702F10  |
| __restfpr_14    | 0x82702F5C  |
| __savevmx_14    | 0x8275B150  |
| __restvmx_14    | 0x8275B3E8  |
| __savevmx_64    | 0x8275B1E4  |
| __restvmx_64    | 0x8275B47C  |

setjmp/longjmp: Not found (game doesn't use them)

## XenonRecomp Results (First Pass)
- **33,725 functions** recompiled
- **135 output files**, 122 MB total C++
- **Unrecognized instructions**: vandc (0x825E02D8), mulhdu (2 sites), frsqrte (2 sites)
- **Missing switch tables**: ~30 detected by XenonRecomp (game uses lwzx-based direct address tables)

## Switch Table Pattern
Saints Row uses a **different switch pattern** from typical XBLA games:
- Standard XBLA pattern: `lhzx/lbzx` offset table + `add r12,r12,r0; mtctr r12; bctr`
- Saints Row pattern: `lwzx r0, rA, rB; mtctr r0; bctr` (direct 32-bit address table)

658 out of 1029 bctr instructions use the `lwzx + mtctr r0` pattern.
These are handled at runtime by `PPC_CALL_INDIRECT_FUNC`.

## Notable Middleware
- **Bink Video** (RAD Game Tools) - Dedicated code and data sections (BINK, BINKBSS, BINKDATA)
- **Havok Physics** - Likely present given the open-world nature
- Audio: WMA files for soundtrack, likely XMA for in-game audio

## Game Data Structure
- 375 files extracted from ISO
- Large streaming world data in .vpp_xbox2 pack files
- sr_city_stream.vpp_xbox2: 1.3 GB (main city data)
- Extensive video content (attract mode, cutscenes) in .bik format
- Audio: MP3 player WMA tracks, voice packs, foley audio packs
