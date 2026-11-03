# foo_deltarec — foobar2000 1-bit DSD Converter Component

[日本語の README はこちら](README.ja.md)

`foo_deltarec` is a 64-bit foobar2000 v2.x component based on [Imaha486/deltarec](https://github.com/Imaha486/deltarec) (and the CPU-optimized fork [yasudaz/deltarec](https://github.com/yasudaz/deltarec)) that converts any audio format decoded by foobar2000 into a 1-bit DSD stream (**DSD64 / DSD128 / DSD256 / DSD512**) and saves it as a **DSF** (`.dsf`) or **DSDIFF** (`.dff`) file with full **ID3v2 metadata tag and front cover art copying**.

## Features

- **Universal Input Support**: Decodes any track supported by foobar2000 (FLAC, WAV, ALAC, WavPack, Monkey's Audio, MP3, AAC, Ogg Vorbis, Opus, CUE sheet subsongs, etc.) via foobar2000's native `input_entry::g_open_for_decoding` pipeline.
- **Right-Click Context Menu UI**: Select one or more tracks in any playlist → Right-click → **Convert to DSD (deltarec)...**.
- **Interactive Configuration Dialog** (settings are persisted across sessions via `cfg_var`):
  - **DSD Rate**: `DSD64` (2.8224 / 3.072 MHz), `DSD128` (5.6448 / 6.144 MHz), `DSD256` (11.2896 / 12.288 MHz), `DSD512` (22.5792 / 24.576 MHz)
  - **Base Clock Family**: `Auto` (matches 44.1 kHz or 48 kHz source family), `44100 Hz family`, or `48000 Hz family`
  - **Output Format**: Sony `DSF (.dsf)` or Philips `DSDIFF (.dff)`
  - **Modulator Order**: `2nd` to `8th` order (default `7th` order, Gauss–Legendre NTF zeros, Butterworth poles bisected to $H_\infty = 1.4$)
  - **Input Gain (dB)** & **TPDF Dither** toggle
  - **Output Destination**: Save in the same folder as each source track, or choose a custom target folder, optional property-based subfolder pattern (e.g. `[%artist%] %album%-(%encoder%)`), automatic `Disc1`, `Disc2`... subfolder creation for multi-disc albums, and output filename pattern (`Source filename`, `%tracknumber% - %title%`, or `%artist% - %title%`).
- **Metadata & Album Art Copying**: Automatically reads source track metadata and embedded/external front cover artwork via `album_art_manager_v2` and embeds a compliant **ID3v2.3 (UTF-16)** tag chunk into both `.dsf` and `.dff` files.
- **AVX2 & Multi-Threaded DSP Engine**: Uses the transposed polyphase Kaiser-windowed-sinc FIR upsampler (160 dB stopband attenuation), SIMD register-resident ΔΣ modulator state update, L1/L2 cache tiling, and per-channel lock-free SPSC producer-consumer threading.

## Changes and Additions Relative to Original ([Imaha486/deltarec](https://github.com/Imaha486/deltarec))

This component builds upon the original [Imaha486/deltarec](https://github.com/Imaha486/deltarec) CLI converter with the following additions and optimizations (see [CHANGELOG.md](CHANGELOG.md) for details):

1. **foobar2000 v2.x (x64) Component Integration (`src/main.cpp`, `src/foo_deltarec.rc.in`)**:
   - Replaced standalone WAV/FLAC CLI decoding with foobar2000's `input_entry::g_open_for_decoding` service so any format or subsong playable in foobar2000 can be converted directly.
   - Added `DeltarecContextMenu` (`contextmenu_item_simple`), a Win32 settings dialog (`IDD_DELTAREC_DIALOG`) with `IFileOpenDialog` folder picker, persistent `cfg_var` configuration, and modeless progress/cancellation reporting via `threaded_process_callback`.
2. **ID3v2.3 Metadata & Cover Art Embedding (`src/id3v2_builder.hpp`, `src/dsdwriter.hpp`)**:
   - Implemented a standalone ID3v2.3 (UTF-16LE with BOM) tag builder supporting standard frames (`TIT2`, `TPE1`, `TALB`, `TPE2`, `TYER`, `TRCK`, `TPOS`, `TCON`, `TCOM`, `TPE3`, `TPE4`, `TCOP`, `TPUB`, `TSRC`, `TBPM`, `COMM`, `USLT`), arbitrary custom user-defined tags (`TXXX`), and front cover art (`APIC` via `album_art_manager_v2`).
   - Extended `DsdWriter` with UTF-16 Windows file paths (`_wfopen`), 64-bit seeking (`_fseeki64` / `_ftelli64` for >4 GB DSD files), and ID3v2 chunk appending for both Sony DSF v1.01 and Philips DSDIFF v1.5.
3. **CPU Performance Optimizations (~3.2× Faster, Bit-Exact DSP)**:
   - **AVX2 Transposed Polyphase FIR (`src/resampler.hpp`)**: Computes 4 polyphase branches in parallel using `_mm256_fmadd_pd` for power-of-2 interpolation (`M == 1`).
   - **SIMD Register-Resident ΔΣ Modulator (`src/modulator.hpp`)**: Keeps the 7th-order error-feedback state vector and NTF coefficients in XMM registers and separates input limiting + xorshift64* TPDF dither into a vectorizable pre-pass.
   - **L1/L2 Cache Tiling & Lock-Free SPSC Pipeline (`src/main.cpp`)**: Processes 16,384-frame batches in 512-frame cache-resident sub-blocks and overlaps FIR upsampling (`PrepWorker`) with 1-bit ΔΣ modulation (`ModWorker`) via a 2-slot lock-free ring buffer and persistent thread pool.

## Building

Requires Visual Studio 2022 (x64) and CMake 3.24+.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

Outputs:
- `build-msvc/Release/foo_deltarec.dll`
- `build-msvc/foo_deltarec.fb2k-component`

## Installation

Double-click `build-msvc/foo_deltarec.fb2k-component`, or in foobar2000 open **File → Preferences → Components → Install...**, select `foo_deltarec.fb2k-component`, and click **Apply**.

## License

MIT License. Based on [deltarec](https://github.com/Imaha486/deltarec).

## Acknowledgements

- **[Imaha486](https://github.com/Imaha486)** — Original author and creator of [deltarec](https://github.com/Imaha486/deltarec). Special thanks for designing and releasing the high-precision ΔΣ modulator and polyphase FIR upsampler under the MIT License.
- **Peter Pawlowski** — Creator of [foobar2000](https://www.foobar2000.org/) and the foobar2000 SDK.
- **R. Schreier, G. C. Temes** — *Understanding Delta-Sigma Data Converters*.
