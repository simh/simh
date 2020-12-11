#!/bin/sh

install_osx() {
    brew update
    brew install sdl2
    brew install sdl2_ttf
    brew install vde
}

install_linux() {
    sudo apt-get update -yqqm
    sudo apt-get install -ym libegl1-mesa-dev libgles2-mesa-dev
    sudo apt-get install -ym libsdl2-dev libpcap-dev libvdeplug-dev
    sudo apt-get install -ym libsdl2-ttf-dev
}

install_"$1"
