FROM ubuntu:18.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt update
ARG CMAKE_VERSION=3.31.0-rc2
RUN apt -y install build-essential gdb
RUN apt -y install curl
RUN curl -o cmake.sh -sL https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-$(uname -m).sh && sh cmake.sh --skip-license --prefix=/usr/local && rm cmake.sh
