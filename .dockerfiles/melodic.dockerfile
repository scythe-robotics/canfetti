FROM ubuntu:18.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt update
RUN apt -y install lsb-release
RUN sh -c 'echo "deb http://packages.ros.org/ros/ubuntu $(lsb_release -sc) main" > /etc/apt/sources.list.d/ros-latest.list'
RUN apt -y install curl gnupg
RUN curl -s https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | apt-key add -
RUN apt update
RUN apt install -y ros-melodic-ros-base
ARG CMAKE_VERSION=3.31.0-rc2
RUN curl -o cmake.sh -sL https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-$(uname -m).sh && sh cmake.sh --skip-license --prefix=/usr/local && rm cmake.sh
