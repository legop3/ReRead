# Native stock camera image processing

ReRead's implementation is [camera_processing.cpp](../antix/camera_processing.cpp), with the public interface in [camera_processing.hpp](../antix/camera_processing.hpp). It reconstructs the stock **complete-frame color and document pipelines**, rather than retaining the initial Python percentile/square-root diagnostic rendering.

This report combines static recovery with an executed **offline stock-library oracle**. It does not claim that image processing validation establishes autofocus accuracy, lens motion, exposure quality, or live preview frame rate on the Reader.

## Evidence and recovered stages

Stock artifact: `analysis/stock-root/opt/colbert/lib/libcolbert-camera.so`, SHA-256 `b8cf21e44aad76ea7874cd7812a65a916ff0be20b06c65cd7cd9b596b5262b6b`.

Saved REA Bayer recovery: `analysis/inventory/camera-color-rea.json`, Evidence ID `ev_2935d9596e7025ba0badb99ac69c53717772f19544c5bf70215cf484bc5d00ae`, function VA `0x17200`. Signatures and numerical operations were corroborated with GNU `nm -D -C`, `readelf`, and `objdump -d -C -Mintel` against the same artifact. In particular, the C++ symbol says three **double** coefficients; the decompiler's approximate signature incorrectly labels them float.

| Stage | Stock address | Recovered behavior |
| --- | --- | --- |
| Still pipeline | `0x18760` | Bayer input 2592×1920; shading, AWB, demosaic, sharpen, histogram gain; final RGB 2588×1916. |
| Stream pipeline | `0x18e80` | Bayer input 648×480; frame preparation, AWB, demosaic, histogram gain; final packed RGB 646×478. |
| Frame preparation | `0x17520` | Finds frame/line flags; complete rows mask pixels to 12 bits. Also contains damaged-line/frame reconstruction outside the current aligned-frame API. |
| Lens shading | `0x16dc0` | Mask `0x0fff`; still only: multiply by `1 + 2.43523e-7 × ((x−width/2)² + (y−height/2)²)`, truncate, saturate at 4095; accumulate the four Bayer parity sums. Stream masks/accumulates without radial multiplication. Coefficient is stock rodata double at `0x21788`. |
| Automatic white balance | `0x16f20` | Gray world: form R and B sums plus average of the two G sums; their mean divided by each channel sum produces the three gains. |
| Bayer conversion | `0x17200` | Still pattern index 1 and stream pattern index 3, constants at `0x21740/44`; the exact four-case interpolation table was recovered. These indices alone do not establish the physical sensor color-filter naming. Interpolate then shift 12-bit values right by 4; apply gains as `((sample >> 4) × trunc(gain × 256)) >> 8`; saturate to bytes. Crop one pixel on each edge. |
| Sharpen | `0x16f90` | Still only: 3×3 kernel `[-1,-2,-1; -2,20,-2; -1,-2,-1]`, divide by 8 with signed right shift, clip 0…255. Crop another pixel on each edge and accumulate 64-bin RGB-byte histogram. Kernel rodata at `0x21760`. |
| Stream histogram | `0x17090` | Count every RGB byte in bin `byte >> 2`, 64 bins. |
| Histogram gain | `0x170d0` | Let `unit=floor(RGB_byte_count/100)`. Low bin reaches `2×unit` samples; high bin reaches `99×unit`. Black=`4×low_bin`, scale=`255/(4×high_bin−black−1)`. Subtract black, clip at zero, multiply using float32 precision, truncate, saturate at 255. |

The stock demosaic differs from ordinary bilinear conversion at green photosites: its reconstructed green is the average of the center sample and the average of the four diagonal green neighbors. Red/blue photosites use direct color, cross-neighbor green, and diagonal opposite color. This is why replacing it with a generic libdc1394 bilinear call does not reproduce stock RGB exactly. The implementation therefore uses the recovered native loops. JPEG encoding uses ordinary **libjpeg**.

There is **no extra square-root gamma curve** in these recovered pipelines. The Python experiment's 1%/99% raw histogram stretch and square-root transform were diagnostic choices and are not carried into the native implementation.

### Photo versus document rendering

`detail::ProcessPicture` at `0x11de0` branches only on the image-type enum being zero:

- At `0x11e28`, type **zero** supplies float `−0.6f` (`0xbf19999a`) as the still pipeline saturation argument.
- Other values supply `0.0f` at `0x11df9`.

“Document” and “photo” are ReRead's readable names for these zero/nonzero branches; no original enum-name table was recovered. In the Bayer routine, `trunc(saturation × 256)` is `−153` for document rendering. It applies `R += ((R−G) × −153) >> 8` and the corresponding B expression, leaving G unchanged, before clipping. This reduces color saturation; it is not a separate grayscale/OCR implementation.

The API adds an optional final argument:

```cpp
Image process_frame(const std::vector<uint16_t>& frame,
                    int width, int height, bool still,
                    bool document = false);
```

Existing four-argument callers receive color/photo behavior. Live view uses stock saturation zero, regardless of the document setting. There is no additional vertical flip in either recovered RGB pipeline; complete-frame oracle comparisons use the same row order as input.

## Offline stock oracle and repeatable comparison

The maintained runner is [compare-camera-processing.py](compare-camera-processing.py). It builds both a native test executable and an executable that calls the stock exported image functions. Stock libraries and generated frames/results stay in ignored `build/antix/camera-oracle/`; none are added to the ReRead package.

Prerequisites: the extracted `analysis/partitions/root.img` plus the optional stock camera/common library extraction, `debugfs`, host Python Pillow, and a running workspace-mounted Docker builder named `intelreader-validator` (override with `--container`). The builder needs i386 C++ development/runtime files, `libjpeg62-turbo-dev:i386`, and the `en_US.UTF-8` locale used by a stock common-library constructor. These are oracle-development requirements, not Reader runtime dependencies.

The stock harness uses `_GLIBCXX_USE_CXX11_ABI=0`, loads the recovered Boost 1.34.1 and common-library dependency closure, and overrides `usb_find_busses`, `usb_find_devices`, and `usb_busses` to expose **no USB devices**. It calls only image entry points, never starts a camera or loads firmware. Stock firmware, EEPROM and FPGA programming tools are not invoked.

Run from the repository root:

```sh
python3 analysis/compare-camera-processing.py \
  --still build/antix/native-baseline.raw \
  --stream build/antix/camera-stream-calibrated.raw
```

The optional real fixtures are locally captured evidence and intentionally ignored. Without arguments, the runner creates deterministic synthetic still/stream frames and tests document rendering too. A stream argument can be a USB burst: the runner selects its first complete marked 648×480 frame. It compiles the native source with `-m32 -O2 -Wall -Wextra -Werror`.

Observed results on 2026-11-08:

| Fixture | Output | Comparison with executed stock function |
| --- | --- | --- |
| Real printed-page baseline still | 2588×1916 RGB | Every RGB byte identical. |
| Real calibrated stream frame | 646×478 RGB | Every RGB byte identical. |
| Deterministic varying-color still | 2588×1916 RGB | Every RGB byte identical. |
| Deterministic varying-color stream | 646×478 RGB | Every RGB byte identical. |
| Real printed-page baseline, document saturation | 2588×1916 RGB | Every RGB byte identical. |

The oracle's stream vector contains trailing allocation/padding bytes beyond the logical packed RGB extent; the comparison reads exactly `width×height×3` useful bytes. ReRead's `Image.rgb` contains exactly those useful bytes. Float32 rounding in histogram gain matters: forcing the stock float32 scale/product precision eliminated initial one-level differences and made all five comparisons byte-exact.

Retained evidence:

- `build/antix/camera-oracle/validation.json`: input/source/library SHA-256 digests, dimensions, RGB hashes, and comparison outcomes.
- `build/antix/camera-oracle-comparison.log`: reproducible runner output.
- `build/antix/camera-oracle/*-stock.ppm`, `*-native.ppm`: uncompressed oracle/native RGB results.
- `build/antix/camera-oracle/*-native.ppm.jpg`: libjpeg output, including aspect-preserving preview scaling.
- `build/antix/camera-oracle/validation.cpp`: focused malformed-frame/invalid-image rejection check. It passed for missing row flag, short frame, and invalid RGB input. JPEG outputs were decoded with Pillow as 960×710 still preview and 646×478 live preview.

JPEG byte identity to stock is not claimed: the oracle comparison concerns RGB processing, while JPEG format/quality and preview area scaling are ReRead output choices. The stock application also has BMP-oriented output functions.

## Coverage and remaining boundaries

The complete aligned-frame photo and document pipelines are recovered and validated against executed stock functions on the fixtures above. Dimensions, parity, shading, AWB, unusual green interpolation, sharpening, contrast gain and saturation are included.

`process_frame` expects an already-selected complete frame with the first-frame and every-row marker in the correct location. It rejects corrupt/missing rows. It **does not recreate** `PrepForRgbConversion`'s partial-frame recovery, row insertion or frame-phase repair. Burst acquisition, alignment, continuous streaming, exposure/autofocus sequencing, flash, sleep/resume and device lifecycle belong to the camera engine and require separate transport/hardware validation. Stock damaged-frame recovery is therefore a known compatibility boundary, not a tested feature.

For an all-zero Bayer color sum, ReRead uses a finite fallback AWB gain rather than propagate division-by-zero into integer conversion; this defensive degenerate-input behavior is not asserted byte-identical to stock. Normal real and synthetic color fixtures do not take that fallback.

## BMP display orientation versus packed image bytes

A later offline pass inspected `SaveBmp(unsigned char const*, unsigned int, unsigned int, std::string)` at **0x14ae0**. At **0x14c61–0x14c7c** it copies the supplied positive height unchanged into the bitmap header; **0x14e26–0x14e3d** writes the original pixel buffer as one contiguous block, with no row reversal. The file has a 14-byte file header, 44 bytes written for its information-header region, and pixel offset **58**.

This was corroborated by executing stock `SaveBmp` offline with a 4×2 fixture. Raw first-row pixels `(10,20,30)` and second-row pixels `(150,160,170)` remain in that order on disk. Decoding the resulting standard BMP with Pillow displays top-row `(170,160,150)` and bottom-row `(30,20,10)`: positive-height BMP displays **bottom-up rows and BGR channel order**. The executable and artifact are `build/antix/camera-oracle/orientation-oracle.cpp` and `orientation-stock.bmp`.

Therefore “the processing function does not flip rows” above describes the packed-buffer boundary, not the orientation of stock's final displayed BMP. JPEG has no corresponding implicit bottom-up interpretation. To match that display orientation, JPEG encoding must reverse **still** rows, while live-view rows remain in their stream order. The optional `jpeg(image, quality, max_width, flip_vertical=false, swap_rb=false)` flag performs row reversal at the encoding boundary, preserving the byte-exact processing oracle. A 32×32 two-tone native fixture verified default ordering, flipped full-resolution JPEG, and flipped 16×16 preview JPEG; its source/artifacts are `build/antix/camera-oracle/orientation-native.cpp` and `orientation-{0,1,2}.jpg`.

The same BMP observation establishes that direct stock BMP decoding swaps the buffer's first/third channels. This is separate from vertical orientation. The optional `swap_rb` output flag sets libjpeg-turbo's `JCS_EXT_BGR` input format, preserving the processing function's byte-exact buffer without an extra full-image channel-swap allocation. Saved still JPEGs and thumbnails use both flags; live JPEGs use neither. The native colored two-tone fixture tested both row and channel order at full size and after preview scaling; decoded colors agreed with expected values to within two JPEG quantization levels.


### Actual application save path and live-view channel boundary

The standalone camera `SaveBmp` helper above is **corroborating evidence**, not the application's known file writer: a bounded dynamic-import/call-site search found no external `SaveBmp` caller in the extracted Colbert DSOs or C3. `camera::TakePicture(std::string, ...)` at `0x1fd80` ultimately calls **SaveRawFile**, not SaveBmp. Claims about the real application boundary use the following additional trace:

- C3 `BookInProcess::AddPageToBook` forwards the vector/dimensions unchanged to `cb::ColbertBook::AddPageToBook` at **0x8149d64**.
- In `libcolbert-colbertbook.so`, `AddPageToBook` passes the original vector/dimensions to **`cb::SaveBitmapToFile`** at **0x213d5**.
- `SaveBitmapToFile` at **0x2bae0** creates an 8-bit three-channel OpenCV image. It starts the source at `(height−1)×width×3` (**0x2bb47–0x2bb61**), copies byte positions 0, 1 and 2 unchanged into consecutive destination pixels (**0x2bba0–0x2bbd0**), steps the source backward by `width×3` each row (**0x2bbe6–0x2bbec**), then calls **cvSaveImage at 0x2bc52**. Thus the actual stock save path reverses still rows and passes the preserved triplets to OpenCV's BGR file-writing interface.

This validates the same still output normalization as the directly executed camera SaveBmp helper: **reverse rows and interpret triplets as BGR**. An additional full real-fixture oracle ran stock `ProcessStillImage` then stock `SaveBmp`; Pillow's decoded 2588×1916 BMP was **byte-for-byte equal** to the native processing result with rows reversed and first/third channels exchanged. Artifacts: `build/antix/camera-oracle/real-file-oracle.cpp`, `baseline-stock-file.ppm.bmp`, and `baseline-stock-file-display.jpg`. The separate cb writer was statically traced, not executed in this test.

Live view has a separately proven **RGB** boundary; its order must not be inferred from still files:

- Camera `StreamServer::Dispatch` at **0x1934c–0x19359** passes the processed WorkType byte vector directly to the registered callback.
- `libcolbert-ui.so` `ViewFinder::CameraCallback` constructs `Image(vector,width,height,24)` at **0xd96c5**.
- The image constructor copies the bytes unchanged with `memmove` at **0x540a6**, then calls `SDLBridge::LoadImageFromRGBBuffer` at **0x540f2**.
- That bridge at **0x92630** calls `SDL_CreateRGBSurfaceFrom` at **0x926bc** with **R mask `0x0000ff`**, **G mask `0x00ff00`**, **B mask `0xff0000`**, alpha mask zero (**0x9268e–0x926a5**). On this little-endian target that consumes triplets as RGB. It does not perform a vertical flip.

Additional artifact identities: UI DSO SHA-256 `1842a19d5a6916cbd707b1fa7f384daead837802b60997d0d8d6b631939ca893`; ColbertBook DSO SHA-256 `95b23dfd0758ec516716a67ff0d85cd1e6ba3ad0ae831b90813a94a19b74bbed`.

The maintained comparison runner also includes the colored two-tone JPEG layout check: default RGB/live ordering, full-size flipped BGR/still ordering, and scaled flipped BGR/still ordering. Flat-patch decoded channels must match their expected values within two JPEG quantization levels.
