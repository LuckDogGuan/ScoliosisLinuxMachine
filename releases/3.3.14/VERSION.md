# Firmware 3.3.14 - essential device prompts

Release date: 2026-10-16 (Asia/Shanghai).
Source commit: dff5e1e54e9478f059f2c33bf9f22710216a0b6c.
Includes voice fix 46ec53f.

Restores existing boot-ready, first-network-connected, network-disconnected and
network-restored WAV prompts without changing volume or blocking capture on
audio failures. One disconnect per outage; PC unpair remains independent of Wi-Fi.
Retains the 3.3.13 camera path normalization and detailed diagnostic protocol.

Host startup/network transition, boot audio isolation, capture audio failure and
speech focus regressions passed. ARM build and bundle integrity checks passed.
Board installation and audible/physical acceptance are recorded separately in
Windows Luckfox/调试/记录/startup-network-voice-20261016.

Install using the one-click application's USB/SSH installer with identity, idle,
hash and rollback checks. Preserve configuration, pairing, state and recordings.
Rollback uses the installer-generated board backup after confirming idle and
stopping application/camera services; never overwrite business state or Wi-Fi.
