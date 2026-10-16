# Round-four native signed ruler reader

This independent C11 core implements the frozen signed-reference algorithm for the Windows formal video-analysis DLL. It does not replace the old ruler-vision preview core and is not deployed to a board.

Runtime ABI 1: rs_load_w / rs_analyze_gray8 / rs_analyze_png_w / rs_free. Result layout is four int32 fields followed by four doubles (48 bytes). A context is owned and used serially by one background analysis worker; concurrent calls on the same context are not supported. Gray buffers must contain height rows with the supplied stride. Status 0 is valid, 1 is a normal rejected frame, and negative codes are system errors. Invalid readings/absent points are NaN, never a fabricated zero.

Parameters use RSR4BIN1, version 1, explicit little-endian scalar data. The header fixes the calibrated 1280x1080 image geometry; model shape, finite values, ranges, reserved bytes and exact file length are validated. The Windows caller validates model/DLL SHA256 against its versioned manifest before loading. Development export tools live in the Windows Luckfox debug-tools repository, not in the installed application.

The right branch uses 2x2 INTER_AREA, a learned white-band mask, 3x3 opening, 8-connected area selection and rightmost-edge interpolation. The left branch uses area-resized 96x112 normalized shadow features, float32 pairwise RMS and two-reference weighted interpolation. It preserves the reference rejection gates. Fixed resize weights are precomputed during model loading; morphology uses equivalent branch-free interior operations.

Windows PNG decoding uses system WIC RGB8 only. Each call initializes/releases COM in the calling thread, never in DllMain and never sharing cached COM objects between Dart isolate calls. Gray conversion is (9797*R+19234*G+3737*B)>>15, matching the frozen OpenCV/libpng reference on all 62 source PNGs. No Python/OpenCV/NumPy runtime is linked or packaged. Compile with /fp:strict to preserve arithmetic ordering.

Windows build: ScoliosisFlutter/tool/build_signed_reader.ps1 reads these authoritative WSL files directly and records source/binary hashes. Linux memory checks: make asan, then build/test_signed_asan <exported-model.bin>. Windows-only PNG input has no Linux fallback decoder.

Validation evidence: Windows Luckfox/调试/记录/ruler-native-r4-20261016. Full-model self matches and leave-one-video-out accuracy are separate. Migration is required to match all 124 reference predictions with angle tolerance 0.001 degree and exact status/reason/branch; the model itself is not independently validated across instruments.
