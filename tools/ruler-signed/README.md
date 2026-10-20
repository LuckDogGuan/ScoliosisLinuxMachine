# Round-four native signed ruler reader

This independent C11 core implements the frozen signed-reference algorithm for the Windows formal video-analysis DLL. It does not replace the old ruler-vision preview core and is not deployed to a board.

Runtime ABI 1: rs_load_w / rs_analyze_gray8 / rs_analyze_png_w / rs_free. Result layout is four int32 fields followed by four doubles (48 bytes). A context is owned and used serially by one background analysis worker; concurrent calls on the same context are not supported. Gray buffers must contain height rows with the supplied stride. Status 0 is valid, 1 is a normal rejected frame, and negative codes are system errors. Invalid readings/absent points are NaN, never a fabricated zero.

Parameters use RSR4BIN1, version 1, explicit little-endian scalar data. The header fixes the calibrated 1280x1080 image geometry; model shape, finite values, ranges, reserved bytes and exact file length are validated. The Windows caller validates model/DLL SHA256 against its versioned manifest before loading. Development export tools live in the Windows Luckfox debug-tools repository, not in the installed application.

The right branch uses 2x2 INTER_AREA, a learned white-band mask, 3x3 opening, 8-connected area selection and rightmost-edge interpolation. The left branch uses area-resized 96x112 normalized shadow features, float32 pairwise RMS and two-reference weighted interpolation. It preserves the reference rejection gates. Fixed resize weights are precomputed during model loading; morphology uses equivalent branch-free interior operations.

Windows PNG decoding uses system WIC RGB8 only. Each call initializes/releases COM in the calling thread, never in DllMain and never sharing cached COM objects between Dart isolate calls. Gray conversion is (9797*R+19234*G+3737*B)>>15, matching the frozen OpenCV/libpng reference on all 62 source PNGs. No Python/OpenCV/NumPy runtime is linked or packaged. Compile with /fp:strict to preserve arithmetic ordering.

Windows build: ScoliosisFlutter/tool/build_signed_reader.ps1 reads these authoritative WSL files directly and records source/binary hashes. Linux memory checks: make asan, then build/test_signed_asan <exported-model.bin>. Windows-only PNG input has no Linux fallback decoder.

Validation evidence: Windows Luckfox/调试/记录/ruler-native-r4-20261016. Full-model self matches and leave-one-video-out accuracy are separate. Migration is required to match all 124 reference predictions with angle tolerance 0.001 degree and exact status/reason/branch; the model itself is not independently validated across instruments.

## Native v2 best-effort boundary readings (2026-10-17)

Reason 10 with status 0 is an estimated reading, never a trusted measured sample. An interior right-edge point beyond the final calibration anchor uses the final distinct-anchor slope. A component clipped at x>=1096 or y<=82 must not be extrapolated: on prior range reason 4 it may try the existing local reference matcher with a finite 2x distance cap and the unchanged 1-degree neighbor agreement gate. Other mismatch/brightness checks remain. This 2x cap is a display recovery heuristic, not a new accuracy validation. A severely dissimilar frame still returns no angle.

The Flutter caller preserves the estimate provenance in the input-image metadata, persists raw/beautified data separately, excludes estimates from recognized coverage and leaves estimate-only screening decisions pending review. The model is unchanged. The user-reported possible global +1-degree label offset is recorded as pending and is not applied. No board code was deployed or flashed.

## Native v3 frozen round-four policy (2026-10-20)

User explicitly selected the finalized fourth-round model directory. Restore the a48517a C decisions (equivalent to frozen Python), including original range rejection and unchanged local-neighbor gate. Remove v2 right-edge extrapolation and doubled-distance border recovery. Keep v2 history and packages. Windows runtime is ruler-signed-r4-native-v3; model parameters are byte-identical to the frozen round-four export. No firmware behavior changes in this policy update.
