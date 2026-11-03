# Changelog

All notable changes to `foo_deltarec` are documented here.
This project is a foobar2000 x64 component derived from [Imaha486/deltarec](https://github.com/Imaha486/deltarec) and [yasudaz/deltarec](https://github.com/yasudaz/deltarec).

## [0.2.0] - 2026-11-03

### Added
- **Property-Based Subfolder Creation (`src/main.cpp`, `src/foo_deltarec.rc.in`, `src/resource.h`)**:
  - Added **Create subfolder using track/album properties** checkbox and customizable **Subfolder Name** pattern field (e.g., `[%artist%] %album%-(%encoder%)` or `[%Arthist%]%album%_(%encoder%)`).
  - Supports `%encoder%` (`DSD64` / `DSD128` / `DSD256` / `DSD512`), `%format%` (`DSF` / `DFF`), `%artist%` (and `%Arthist%`), `%album%`, and any foobar2000 metadata tag token, including multi-level relative paths (`\`).
- **Automatic Multi-Disc Subfolder Option (`Disc1`, `Disc2`, ...)**:
  - Added **Create Disc subfolder (Disc1, Disc2...) for multi-disc albums** checkbox.
  - Automatically detects multi-disc albums via `TOTALDISCS` / `DISCTOTAL`, `DISCNUMBER` (`N/M`), or batch track pre-scanning, creating `%Output Folder%\%Sub Folder%\DiscN` when disc count is 2 or more, while omitting `DiscN` for single-disc albums.
- **Output Directory Verification & Error Handling**:
  - Added `ensureDirectoryExists` to validate and recursively create missing target folders and subfolders.
  - Displays a modal error dialog (`MessageBoxW`) and immediately aborts encoding if the target folder cannot be created.

## [0.1.0] - 2026-11-02

### Added (foobar2000 Component Features over [Imaha486/deltarec](https://github.com/Imaha486/deltarec))
- **foobar2000 Universal Decoder Integration (`src/main.cpp`)**: Replaced standalone WAV/FLAC file readers with `input_entry::g_open_for_decoding`, enabling conversion from any audio format or CUE subsong decoded by foobar2000.
- **Context Menu & Configuration Dialog (`src/main.cpp`, `src/foo_deltarec.rc.in`, `src/resource.h`)**:
  - Added right-click context menu command **Convert to DSD (deltarec)...**.
  - Added Win32 configuration dialog supporting DSD rate (`64` / `128` / `256` / `512`), base clock family (`Auto` / `44100 Hz` / `48000 Hz`), output format (`DSF` / `DFF`), modulator order (`2..8`), input gain (`dB`), TPDF dither toggle, tag copying toggle, output folder picker (`IFileOpenDialog`), and output filename formatting rules.
  - Persisted all user settings across foobar2000 sessions via `cfg_var_modern`.
  - Integrated modeless conversion progress window (`threaded_process_callback`) displaying per-track progress and real-time conversion speed (`x realtime`), with safe cancellation cleanup.
- **ID3v2.3 Metadata & Album Art Copying (`src/id3v2_builder.hpp`, `src/dsdwriter.hpp`)**:
  - Built UTF-16LE ID3v2.3 tag generator mapping standard foobar2000 metadata fields (`TIT2`, `TPE1`, `TALB`, `TPE2`, `TYER`, `TRCK`, `TPOS`, `TCON`, `TCOM`, `TPE3`, `TPE4`, `TCOP`, `TPUB`, `TSRC`, `TBPM`, `COMM`, `USLT`), arbitrary custom fields (`TXXX`), and front cover art (`APIC` via `album_art_manager_v2`).
  - Extended `DsdWriter` to embed ID3v2 metadata chunks in both Sony DSF v1.01 and Philips DSDIFF v1.5 files, with Windows UTF-16 path support (`_wfopen`) and 64-bit file offsets (`_fseeki64` / `_ftelli64`).

### Optimized (DSP Engine Improvements from [yasudaz/deltarec](https://github.com/yasudaz/deltarec))
- **AVX2 Transposed Polyphase FIR (`src/resampler.hpp`)**: 4-way SIMD FMA (`_mm256_fmadd_pd`) across polyphase branches for power-of-2 interpolation (`M == 1`).
- **SIMD Register-Resident ΔΣ Modulator (`src/modulator.hpp`)**: XMM register-resident 7th-order state update and separated pre-pass for input limiting and xorshift64* TPDF dither generation.
- **L1/L2 Cache Tiling & Lock-Free SPSC Pipeline (`src/main.cpp`)**: 512-frame sub-block cache tiling, 2-slot lock-free producer-consumer ring buffer per channel (`PrepWorker` + `ModWorker`), and persistent worker thread pool (~3.2× faster CPU conversion with 100% bit-exact output).
