# foo_deltarec — PCM to 1-bit DSD (DSF/DFF) Converter for foobar2000

`foo_deltarec` は、[deltarec](https://github.com/yasudaz/deltarec) の高精度 2段ポリフェーズ FIR アップサンプラ＋7次 ΔΣ（デルタシグマ）変調器を foobar2000 コンポーネント化したプラグインです。

## 特徴

- **foobar2000 がデコード可能なすべてのフォーマットに対応**（WAV, FLAC, ALAC, APE, WavPack, MP3, AAC, Ogg Vorbis, Opus, CUEシート等）
- **右クリックから簡単変換**: トラック（単一・複数）を選択して右クリック → `Convert to DSD (deltarec)...`
- **設定ダイアログ**:
  - **DSD レート**: `DSD64` / `DSD128` / `DSD256`（既定） / `DSD512`
  - **ベースクロック系列**: `Auto`（ソースに応じて 44.1kHz / 48kHz 系を自動選択）、`44100 Hz 系`、`48000 Hz 系`
  - **出力フォーマット**: `DSF (.dsf)` または `DSDIFF (.dff)`
  - **変調器次数・ゲイン・TPDF ディザ設定**
  - **出力先指定**: ソースと同じフォルダ、または任意のフォルダ＋ファイル命名規則を選択可能
- **タグ＆アルバムアート（フロントカバー）の自動コピー**:
  - ソースのメタデータ（Title, Artist, Album, Album Artist, Date, Tracknumber, Discnumber, Genre, Composer, Comment, Lyrics, ReplayGain 等のカスタムタグ）およびフロントカバー画像を **ID3v2.3（UTF-16）チャンク**として `.dsf` / `.dff` に直接埋め込みます。
- **AVX2 + マルチスレッドパイプライン最適化済み**

## ビルド方法

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

ビルド完了後、以下のファイルが生成されます：
- `build-msvc/Release/foo_deltarec.dll`
- `build-msvc/foo_deltarec.fb2k-component`（foobar2000 にダブルクリックまたはドラッグ＆ドロップでインストール可能）
