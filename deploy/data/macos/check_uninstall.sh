#!/bin/bash
if [ -d "/Applications/FRKN.app" ] || pgrep -x "FRKN-service" >/dev/null; then
  exit 0
fi
exit 1
