#!/bin/sh

install_osx() {
    brew update
    brew install pcre
    brew install libedit
    brew install sdl2
    brew install libpng
    brew install sdl2_ttf
    brew install vde
}

install_linux() {
    sudo apt-get update -yqqm
    sudo apt-get install -ym libegl1-mesa-dev libgles2-mesa-dev
    sudo apt-get install -ym libpcap-dev libvdeplug-dev libpcre3-dev libedit-dev libsdl2-dev libpng-dev libsdl2-ttf-dev
    
}

install_"$1"
