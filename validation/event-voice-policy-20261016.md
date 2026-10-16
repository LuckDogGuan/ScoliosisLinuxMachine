# Restore existing firmware voice events - 2026-10-16

Base: 242eee0 (firmware 3.3.14). New firmware: 3.3.15. Only canonical WSL
firmware sources/tests/release metadata change; PC application source is unchanged.

Root cause: f94bea2 (3.3.12) globally restricted queue_sound to five capture and
storage prompts. 46ec53f restored four boot/network names but left pairing and
capture_enabled/capture_disabled blocked. PC CapturePage's start-from-position
button calls AppController.startCapture -> capture_enable; runtime receive_udp
already queues capture_enabled after accepting that command.

Remove this global event-name filter, retaining the original eight-entry capacity
guard and platform audio asset validation. Existing callers and WAVs define the
voice events again, including pair_window, pair_success, pair_disconnected,
pair_window_closed, capture_enabled, capture_disabled, uploaded and saved_on_device.
This restores existing triggers; it does not invent missing event triggers or
change network-state transitions, volume, WAVs, capture timing or camera config.
Audio playback remains in its independent worker with existing capture priority
and PC speech leases. Audio failure still cannot prevent an authorized recording.

Regression evidence:
- test_startup_network_prompts.py failed before the fix (boot/assets -6, network
  passed), /tmp/startup-network-prompts-job863kc; after fix all three modes passed,
  /tmp/startup-network-prompts-xgfxk5oy. Inventory covers the 22 manifest assets,
  and queue overflow stays bounded at eight.
- test_pair_capture_prompts.py failed before fix at expect_prompt for successful
  pair_commit, /tmp/pair-capture-prompts-uebeem97; after fix passed,
  /tmp/pair-capture-prompts-bixvnpz_. It sends real UDP requests through receive_udp:
  pair_request/commit, capture_enable, capture_renew, capture_disable, unpair;
  duplicate accepted mutations and unauthorized commands must not add prompts.
  Fixture waits for socket readability because WSL loopback delivery can be
  asynchronous; an initial fixture-readiness failure is not a firmware verdict.
- test_capture_rhythm.py passed, /tmp/capture-rhythm-h4r9kvq4.
- test_boot_audio_isolation.py passed, /tmp/boot-audio-isolation-1mjw5omk.
- test_capture_audio_failure.py and test_speech_focus.py passed; speech evidence
  /tmp/speech-focus-lorimjhw.
- test_pair_lights.py passed: long press opens, second press cancels, commit closes
  without an extra exit prompt, and expiry announces once.

GitNexus: queue_sound upstream HIGH, 10 direct callers / four process groups;
reported before edits. Version macro and edited existing test mains LOW.
Only the shared event filter is removed; dispatch, renewal/idempotence, capture
timing, priority and authorization remain. Canonical WSL Git diff and hashes are
authoritative when reviewing the temporary index.

Deployment, installed build hashes, actual WAV playback and rollback details:
Windows Luckfox/调试/记录/voice-policy-3315-20261016.
Host tests do not establish physical speaker audibility.
