# Firmware 3.3.15 - restore existing event voice prompts

Release date: 2026-10-16 (Asia/Shanghai).
Source: f13fdbe42bae (full hash in BUILD.txt).
Removes f94bea2's global prompt-name filter, preserving the original eight-entry
queue capacity. Existing pair-window, pair-success, unpair, window-close,
capture-enable/disable, upload and storage prompts can enter the queue again.
The PC start-from-position command already maps to capture_enable; no PC change.

Retains asynchronous playback, authorized capture despite audio failures,
speech focus, capture prompt priority, duplicate-RPC suppression and existing
network transition detection. WAV assets, volume and camera settings unchanged.

Seven host regression suites, including all 22 asset names and actual UDP
pair/capture session transitions, passed. ARM -Wall -Wextra -Werror and archive
integrity checks passed. Physical audible acceptance is separate.

Install using the standard identity/idle/hash/backup-verified installer.
Preserve configuration, pairing, state and recordings; use the generated
rollback map/archive if rollback is needed, never overwrite business state.
Deployment evidence: Windows Luckfox/调试/记录/voice-policy-3315-20261016.
