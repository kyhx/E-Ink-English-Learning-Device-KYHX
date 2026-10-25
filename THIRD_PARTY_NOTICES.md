# Third-Party Notices

This file records third-party code/data used by the firmware and the PC-side
toolchain. It is a notice, not a replacement for the full license texts.

## Firmware

### `components/epd_ssd1619a/include/epd_lut_partial.h`

The 70-byte partial LUT and the partial init/update sequences are derived from
[HalfSweetStudio/EPaperDrive](https://github.com/HalfSweetStudio/EPaperDrive),
which is licensed under **AGPL-3.0**.

The rest of `main/` and `components/epd_ssd1619a/` is MIT. Linking the AGPL
LUT into the firmware means the combined firmware cannot be distributed as
MIT-only. Before public release / commercial use, choose one of:

1. release the combined firmware under AGPL-3.0;
2. obtain permission from the upstream author;
3. rewrite the partial LUT/sequences independently from the SSD1619A
   datasheet.

For local academic experiments, keep this notice and cite the upstream source
in the thesis.

### `components/epd_ssd1619a/epd_font_5x7.h`

5x7 font data derived from Adafruit-GFX (`glcdfont.c`), **BSD-2-Clause**.

### Managed ESP-IDF components

Downloaded into `managed_components/` by the ESP-IDF component manager, each
under its own license:

- `joltwallet/littlefs` (MIT)
- `espressif/esp-tflite-micro` (Apache-2.0)
- `espressif/esp-dsp` (Apache-2.0)
- `espressif/esp-nn` (Apache-2.0)

## PC-side toolchain (not flashed to the device)

- PyTorch (BSD-3-Clause), TensorFlow (Apache-2.0), ONNX/ONNX Runtime (MIT),
  onnx2tf (MIT), librosa (ISC), soundfile (BSD-3-Clause), NumPy (BSD-3-Clause),
  SciPy (BSD-3-Clause), matplotlib (PSF-based), Pillow (HPND), pyserial
  (BSD-3-Clause), fontTools (MIT).
- **DejaVuSans.ttf** (bundled with matplotlib) is under the Bitstream Vera /
  DejaVu license. Used for IPA glyph generation.
- **NotoSansSC-VF.ttf** (Windows system font) is under the SIL Open Font
  License 1.1. Used for Chinese glyph generation; subset only the glyphs that
  appear in the word bank and keep the OFL notice with any redistributed
  subset.

## Audio / dataset

Reference pronunciations and the recorded dataset are **not** covered by the
code license. Record their source, speaker consent, and usage terms in
`docs/M0测试记录.md` / the dataset README before publishing.
