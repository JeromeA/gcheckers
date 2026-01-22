#!/bin/sh

set +x

sudo apt update
sudo apt install -y libgtk-4-bin libgtk-4-dev chromium

command -v gtk4-broadwayd
command -v chromium
