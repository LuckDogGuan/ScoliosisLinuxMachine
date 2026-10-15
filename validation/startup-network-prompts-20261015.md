# Essential startup and network prompts - 2026-10-15

User scope: implement source code now; defer all board updates. Canonical source:
Luckfox-Ubuntu-22.04:/home/guanjx/luckfox/Scoliosis.
Base commit: 1f76dcfaff12.

## Cause and behavior

The runtime queue accepted only five capture/storage prompt names, silently dropping
the existing boot_ready and network announcements. wifi_disconnected also had no
runtime trigger. Audio assets and the playback driver did not need changes.

The whitelist now accepts boot_ready, wifi_connected, wifi_disconnected and
wifi_restored. Existing WAVs and volume are retained. The maintenance completion
path enqueues one disconnect on available -> unavailable, first successful
connection as wifi_connected, and subsequent recovery as wifi_restored.
Boot without a reachable gateway does not falsely announce a previous disconnect.
Gateway availability remains the existing network health criterion; this change
does not redefine radio association or recovery policy. PC pairing is independent.

Playback remains asynchronous. Existing capture preemption, speech focus leases,
bounded queue and audio worker timeout remain intact. Normal background upload,
capture-gate and pairing transitions retain their current quiet policy.

## Validation

- tests/test_startup_network_prompts.py failed both boot and network modes against
  the original source (assertion in expect_sound, exit -6); evidence:
  /tmp/startup-network-prompts-mjmzhys5.
- The same test passed after the fix; evidence:
  /tmp/startup-network-prompts-loc94fkq.
  Covers boot prompt, capture/storage prompts, quiet upload, offline startup,
  first connection, repeated polls, disconnect, prolonged outage, recovery and
  PC unpair independent of Wi-Fi.
- tests/test_boot_audio_isolation.py passed:
  /tmp/boot-audio-isolation-k3h1xs6s.
- tests/test_capture_audio_failure.py passed.
- tests/test_speech_focus.py passed:
  /tmp/speech-focus-8h86acj0.
- make -B all passed with ARM compiler, -Wall -Wextra -Werror.

GitNexus impact before edits: queue_sound HIGH, 10 direct callers, 3 affected
process groups; maintenance LOW with partial graph result. Reviewed capture,
completion, network maintenance and startup paths manually. Precommit detection
uses a SHA256-matched temporary index; canonical WSL Git diff is authoritative.
Detection reported MEDIUM, covering queue_sound, maintenance and three execution
flows; temporary auto-generated AGENTS.md sections are index-only artifacts and
are excluded from the canonical commit. New test files are explicitly staged
and inspected because the unstaged GitNexus report omits untracked files.

No board update, service restart, network disruption, package or tag was performed.
Installed firmware is unchanged. Physical audible startup/disconnect/recovery
acceptance remains pending for the later deployment.

Rollback: revert this change's commit in the canonical WSL repository; no board
rollback is needed because it was not deployed.
