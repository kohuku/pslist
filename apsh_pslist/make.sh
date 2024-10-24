#!/bin/bash

# ビルドディレクトリの指定
BUILD_DIR="/home/ubuntu/app/"
SOURCE_DIR="/home/ubuntu/src/pslist/apsh_pslist"

# Mesonセットアップの実行
meson setup "$BUILD_DIR" "$SOURCE_DIR" --buildtype=debug

# Ninjaビルドの実行
ninja -C "$BUILD_DIR"
