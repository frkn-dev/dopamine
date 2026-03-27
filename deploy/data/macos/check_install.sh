#!/bin/bash
if [ -d "/Applications/FRKN.app" ] || pgrep -x "FRKN-service" >/dev/null; then
  exit 1
fi
exit 0
