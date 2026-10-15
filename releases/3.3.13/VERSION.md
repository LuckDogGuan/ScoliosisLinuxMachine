# Instrument 3.3.13 — Wi-Fi camera path and diagnostics

Source: 96528cabe077b9cd46310cbb388972e405de8043. The immutable archive SHA256 is unchanged by this release note.

RKiPC storage.mount_path is normalized to / to match /video0 and the capture spool filesystem.
Detailed diagnostics advertise diagnostics_health_v1, include stage/error/file/RPC/HTTP evidence and round/boot/build identity, and have a bounded worker.
Pairing, Wi-Fi configuration, capture spool and previous recordings are preserved.

Live Wi-Fi acceptance on CAM-211ec7045815bf02: 30 seconds/30 decoded preview frames, five completed/uploaded/fully decoded captures (5103/5355/6327/7647/9686 ms); after software reboot normal startup preview and one additional 4661 ms capture passed.
Automatic capture triggers used the existing test input file. Normal physical GPIO input was restored afterwards. Physical key/speaker and power-loss cold boot are not manually accepted.

Windows application 3.3.17 source f9587766db7c280cbbdc4284c83aac08affc64f6.
Detailed logs, rollback backups and exact hashes: Windows Luckfox record wifi-camera-diagnostics-20261015.
