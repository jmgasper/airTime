#!/bin/sh
# Post-install: let Tracker open films and music with airTime where Haiku's
# MediaPlayer (or nothing) had them; a player the user chose is left alone.
/boot/system/apps/airTime --register-default >/dev/null 2>&1 || true
exit 0
