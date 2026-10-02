# v3.3.0 capture deletion validation, 2026-10-02

Source/build/Git authority: Luckfox-Ubuntu-22.04:/home/guanjx/luckfox/Scoliosis.
Baseline source commit: 3a3ac12. Existing uncommitted v3.2.1/v3.2.2 record deletion work is preserved in this release.

make all and make check-host pass with -Wall -Wextra -Werror. Host deletion tests cover proof mismatch, current recording busy, duplicate proof, persistent startup cleanup, filtering discarded pending entries, killing matching uploader while gate stays enabled. Existing upload and physical button state-machine tests pass.

CAM-000123 live test on 192.168.101.3 passes: blocked 16 MiB upload discarded with exact SHA-256/size; uploading child killed; MP4/metadata remain absent; capture gate survives. Real camera capture uploads to Flutter and computes first fixed-ruler-image result; deletion during computation suppresses late writes and preserves gate. Automated button input only; physical hearing/buttons/cold boot/optical timing are not validated. Audio busy log remains a field-acceptance item.

Board original application kept at /opt/instrument/backups/capture-results-20261002/instrument_app and local validation/capture-results-20261002/board-before.instrument_app (ignored binary). Current version 3.3.0 deployed without changing configuration/state/pending inventory; test teardown restarted normal physical-button mode and unpaired the dedicated test host.

Immutable package: image/v3.3.0-20261002-results, tag instrument-v3.3.0-20261002-results. Source commit recorded by package BUILD.txt; SHA256SUMS validates contents. Roll back application/adapter only after stopping recording/upload; preserve config, pairing, pending media/events, durable discard receipts, and PC workspace associations. Do not restore test state over production state.

GitNexus reads real WSL Git through scripts/gitnexus_wsl_review.mjs, while the exact source-hash-matched read-only index cache lives on Windows. Run this bridge before commits; no Windows Git writes are used for firmware.
