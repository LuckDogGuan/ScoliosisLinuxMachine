# Original 100-backlight Python matcher in C (desktop only)

2026-10-20: supersedes the earlier 65 candidate's accuracy gate for this
strict Python reproduction. Production 65 reader/model remain byte-identical.
C/Python parity, pixel/ROI and fixed input adaptation are the acceptance gates;
this does not claim accuracy on independently acquired 1280x1080 images.

The 100 runtime trains from the original 21 images (-10..10) at 640x480.
ROI is top-origin (0,0,640,260), equivalent to bottom-origin y=220..480.
A 1280x1080 input is converted by fixed nearest-neighbour indexing:
  gray640[y,x] = input1080[floor(y*1080/480), floor(x*1280/640)]
Native 640x480 is also accepted for Python parity checks.
No scene-residual cutoff, sign heuristic or other measurement algorithm is added.
Constant inputs still reject rather than returning a fabricated zero.

Public ABI: existing rs_* for context lifecycle and reading; ruler_template.h
adds rt_gray_w (query dimensions, then copy gray pixels), rt_roi_w (bottom-origin
rectangle) and rt_pixel_w (single pixel, bottom-origin coordinates). The C CLI
provides train/predict/gray/inspect. Runtime has no Python/OpenCV dependency.
Windows orchestration: tool/build_template_reader.ps1 and tool/prepare_ruler100.py
in ScoliosisFlutter; the latter invokes C training and records source/image hashes.

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
Constant frames (variance <4) reject with NaN.
Status/reason semantics use the existing ABI. Contexts are serially owned by a
single worker; free only after outstanding calls finish.

Portable memory check:
  gcc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer ruler_template.c test_template.c -lm -o /tmp/test_template
  /tmp/test_template /path/to/model.bin

This is a desktop tool. Never include it in a board update.
