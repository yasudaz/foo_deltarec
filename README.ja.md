# foo_deltarec — foobar2000 用 1-bit DSD 変換コンポーネント

[English README](README.md)

`foo_deltarec` は、[Imaha486/deltarec](https://github.com/Imaha486/deltarec)（および CPU 最適化フォーク [yasudaz/deltarec](https://github.com/yasudaz/deltarec)）をベースに作成された 64-bit foobar2000 v2.x 用コンポーネントです。foobar2000 がデコードできるすべての音源フォーマットを 1-bit DSD ストリーム（**DSD64 / DSD128 / DSD256 / DSD512**）へ変換し、**ID3v2 タグおよびフロントカバー画像（アルバムアート）を引き継いだ DSF (`.dsf`) または DSDIFF (`.dff`) ファイル**として出力します。

## 主な機能

- **foobar2000 がデコード可能な全フォーマットに対応**
  - foobar2000 の `input_entry::g_open_for_decoding` を介してデコードするため、FLAC / WAV / ALAC / WavPack / Monkey's Audio / MP3 / AAC / Ogg Vorbis / Opus や CUE シートの各トラックなど、foobar2000 で再生可能なすべての音源を直接 DSD に変換できます。
- **右クリック コンテキストメニューからの簡単操作**
  - プレイリスト上のトラック（複数選択可）を右クリック → **Convert to DSD (deltarec)...** から設定ダイアログを起動します。
- **設定ダイアログ**（設定値は `cfg_var` により次回以降も記憶されます）
  - **DSD Rate**: `DSD64` (2.8224 / 3.072 MHz)、`DSD128` (5.6448 / 6.144 MHz)、`DSD256` (11.2896 / 12.288 MHz・既定)、`DSD512` (22.5792 / 24.576 MHz)
  - **Base Clock Family**: `Auto`（44.1 kHz 系 / 48 kHz 系を入力から自動判定）、`44100 Hz family`、`48000 Hz family`
  - **Output Format**: Sony `DSF (.dsf)` または Philips `DSDIFF (.dff)`
  - **Modulator Order**: `2nd` 〜 `8th`（既定 `7th`、Gauss–Legendre 零点配置・Butterworth 極配置 $H_\infty = 1.4$）
  - **Input Gain (dB)** および **TPDF Dither** の有効/無効
  - **Output Destination**: 変換元トラックと同じフォルダに出力、または任意のフォルダ（`Browse...`）とファイル名規則（元ファイル名 / `%tracknumber% - %title%` / `%artist% - %title%`）を指定可能。
- **メタデータ（タグ）＆カバーアートの完全コピー**
  - 変換元の標準タグ・カスタムタグ（`TXXX`）・フロントカバー画像（`album_art_manager_v2` による `APIC` フレーム）を ID3v2.3 (UTF-16) チャンクとして構築し、`.dsf` および `.dff` に直接埋め込みます。
- **AVX2 & マルチスレッド高速 DSP エンジン**
  - 転置ポリフェーズ FIR の AVX2 ベクトル化、7 次 ΔΣ 変調器の XMM レジスタ常駐化、L1/L2 キャッシュタイリング、およびチャンネル内ロックフリー SPSC パイプラインにより高速に変換を行います。

## オリジナル（[Imaha486/deltarec](https://github.com/Imaha486/deltarec)）からの変更・追加点

本コンポーネントはオリジナルの CLI ツール [Imaha486/deltarec](https://github.com/Imaha486/deltarec) をベースに、以下の機能追加と最適化を行っています（詳細は [CHANGELOG.md](CHANGELOG.md) を参照）：

1. **foobar2000 v2.x (x64) コンポーネント化 (`src/main.cpp`, `src/foo_deltarec.rc.in`)**
   - 単体 WAV/FLAC デコーダに代わり foobar2000 の `input_entry::g_open_for_decoding` を採用し、全対応フォーマットおよびサブソングのデコードに対応。
   - 右クリックメニュー（`DeltarecContextMenu`）、フォルダ選択ダイアログ付きの Win32 設定ダイアログ（`IDD_DELTAREC_DIALOG`）、`cfg_var` による設定永続化、`threaded_process_callback` によるプログレスバー・リアルタイム倍率表示・キャンセル処理を実装。
2. **ID3v2.3 タグ＆アルバムアート埋め込み機能 (`src/id3v2_builder.hpp`, `src/dsdwriter.hpp`)**
   - 標準フレーム（`TIT2`, `TPE1`, `TALB`, `TPE2`, `TYER`, `TRCK`, `TPOS`, `TCON`, `TCOM`, `TPE3`, `TPE4`, `TCOP`, `TPUB`, `TSRC`, `TBPM`, `COMM`, `USLT`）、すべてのカスタムタグ（`TXXX`）、およびフロントカバー画像（`APIC`）を出力する ID3v2.3 (UTF-16LE with BOM) ビルダを新規実装。
   - `DsdWriter` を拡張し、Windows UTF-16 パス対応（`_wfopen`）、4 GB 超ファイル対応の 64-bit シーク（`_fseeki64` / `_ftelli64`）、および DSF v1.01 メタデータポインタ／DSDIFF v1.5 `ID3 ` チャンクの書き出しに対応。
3. **CPU パフォーマンス最適化（出力ビット完全一致・約 3.2 倍高速化）**
   - **ポリフェーズ FIR の転置レイアウトと AVX2 ベクトル化 (`src/resampler.hpp`)**: 2 のべき乗補間時（`M == 1`）に `_mm256_fmadd_pd` で 4 位相を同時計算。
   - **ΔΣ 変調器の状態更新 SIMD レジスタ常駐化と 2 パス分離 (`src/modulator.hpp`)**: 7 次誤差帰還ループの状態ベクトルと係数を XMM レジスタに常駐化し、リミッタと TPDF ディザ生成を前段パスに分離。
   - **L1/L2 キャッシュタイリングとロックフリー SPSC パイプライン (`src/main.cpp`)**: 512 フレーム単位のキャッシュ常駐サブブロック処理と、2 スロットリングバッファによる FIR 補間（`PrepWorker`）・1-bit 変調（`ModWorker`）の並列パイプラインおよび常駐スレッドプールを導入。

## ビルド方法

Visual Studio 2022 (x64) および CMake 3.24 以降が必要です。

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

ビルド成果物：
- `build-msvc/Release/foo_deltarec.dll`
- `build-msvc/foo_deltarec.fb2k-component`

## インストール方法

`build-msvc/foo_deltarec.fb2k-component` をダブルクリックするか、foobar2000 の **File → Preferences → Components → Install...** から選択し、**Apply** を押して再起動してください。

## ライセンス

MIT License（[Imaha486/deltarec](https://github.com/Imaha486/deltarec) に準拠）。

## 謝辞

- **[Imaha486](https://github.com/Imaha486)** — オリジナルの [deltarec](https://github.com/Imaha486/deltarec) の作者。明快で美しいアーキテクチャと高精度な NTF・ポリフェーズ FIR 設計を実装し、MIT ライセンスのオープンソースとして公開されたことに深く感謝いたします。
- **Peter Pawlowski** — [foobar2000](https://www.foobar2000.org/) および foobar2000 SDK の作者。
- **R. Schreier, G. C. Temes** — *Understanding Delta-Sigma Data Converters*
