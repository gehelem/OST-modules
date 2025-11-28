#!/bin/bash
#
# CI - build job
# 
apt-get update
apt-get clean
apt-get install -y software-properties-common curl jq unzip wget
apt-add-repository ppa:mutlaqja/ppa
apt-get update
apt-get clean
apt-get install -y autotools-dev build-essential cmake debhelper devscripts dpkg-dev extra-cmake-modules git intltool-debian libavahi-client-dev libavahi-common-dev libcfitsio-dev libeigen3-dev libgsl-dev libindi-dev libkf5config-dev libkf5crash-dev libkf5doctools-dev libkf5guiaddons-dev libkf5i18n-dev libkf5newstuff-dev libkf5notifications-dev libkf5notifyconfig-dev libkf5plotting-dev libkf5xmlgui-dev libnova-dev libopencv-dev libqt5scxml5-dev libqt5svg5-dev libqt5websockets5-dev libraw-dev libsecret-1-dev libstellarsolver-dev lintian npm qtbase5-dev qtdeclarative5-dev qtmultimedia5-dev wcslib-dev zlib1g-dev
./CI/CI-download_ostserver.sh ${CI_API_V4_URL}
apt install -y ./ostserver_latest_amd64.deb
dpkg-buildpackage -us -uc 
cp ../ostmodules_0.0.0_amd64.deb  ./${DEB_FILE_LATEST}
mv ../ostmodules_0.0.0_amd64.deb  ./${DEB_FILE_TAG}
dpkg -i ${DEB_FILE_LATEST}
echo "Quick test..."
mkdir media
export QT_QPA_PLATFORM=offscreen
ostserver --webroot=media > server.log 2>&1 &
SERVER_PID=$!
sleep 7
ps aux | grep ostserver | grep -v grep
node test-ws.js 2>&1 | tee ws.log
echo "Server logs:"
tail -50 server.log
kill $SERVER_PID || true

