# Reference-template C candidate (not released)

Source of truth: tools/ruler-template in the authoritative WSL Scoliosis repository.
Windows build: ScoliosisFlutter/tool/build_template_reader.ps1.
The resulting ruler_signed.dll uses the existing ABI 1 header in tools/ruler-signed.
The old signed reader and firmware are unchanged.

This ports test.py's grayscale ROI, brightness least squares, coarse reference
comparison and adjacent-angle 0.01 mixture search. Duplicate-angle references are
retained separately. Centered dot products and a precomputed Gram matrix eliminate
repeated per-pixel least-squares solves without changing the search. scoliosis.py's
gray data and look.py's bottom-origin coordinates are available from the gray and
inspect commands. WIC JPEG/PNG decoding and RGB-to-gray integers run in C. No
Python, NumPy, OpenCV or external runtime is needed for training or reading.
The gray command exports lossless PGM pixels (no Excel dependency or GUI).
Python is used only by the separate comparison/audit harness.

Commands (Windows, Unicode paths supported):
  ruler_template.exe train inputs.tsv model.bin W H Xtop Ytop ROIW ROIH block exclude|- backlight65|100
  ruler_template.exe predict model.bin image.png
  ruler_template.exe gray image.png output.pgm
  ruler_template.exe inspect image.png x y_bottom

TSV: one line per reference, angle TAB video_group TAB image_path, UTF-8.
Do not include tabs/newlines in paths/groups. Outputs refuse to overwrite an
existing readable model or PGM. Exclusion happens before image decoding/feature
extraction. Keep labels and source images unchanged. Use different directories
for the four folds and full candidate. Never use a validation frame as its own
reference. Keep TRAIN.json/BUILD.json, input hashes and prediction records together.

100-backlight reproduction: 640x480, top-origin ROI 0,0,640,260, block 1.
65-backlight candidate: 1280x1080, ROI 100,80,1000,600, block 4. This avoids the
timestamp. The 4x4 arithmetic block mean is computed directly in C.

Model format RSRTBIN1: version u32=1, eight u32 geometry/reference fields, sorted
float64 labels, then centered float64 reference vectors, explicitly little-endian.
The loader validates geometry/allocation bounds, reference variance, finite
values, sorted labels and exact length. Wrong format/version cannot load.
Constant frames (variance <4) and scene residual ratio >0.5 reject with NaN.
Status/reason semantics use the existing ABI. Contexts are serially owned by a
single worker; free only after outstanding calls finish.

Acceptance on the 62 reviewed images failed. Do not deploy this candidate:
the original adjacent-template method cannot extrapolate past the training
range, and image similarity can confuse left/right shadows. The V04 holdout
training minimum is -8.65, so a convex mixture cannot reach its -10 label.
No per-image offsets, rounding, label changes or validation-frame references
were used to conceal these failures. Flutter keeps the released reader default.

Portable memory check (model previously trained by the C Windows tool):
  gcc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer ruler_template.c test_template.c -lm -o /tmp/test_template
  /tmp/test_template /path/to/full/model.bin

This is a desktop tool. Do not package it into a board update.
