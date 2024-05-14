#!/bin/bash

REPO_HOME="https://vault.cs.uwaterloo.ca/s/7SEPzsQqybDyCTR"
URL_PREFIX="${REPO_HOME}""/download?path=%2F&files="

TWITTER_ARCHIVE="twitter_compressed.tar.gz"

echo "Downloading twitter"
wget "$URL_PREFIX$TWITTER_ARCHIVE" -O "${TWITTER_ARCHIVE}"

echo "Decompressing twitter"
tar -xzvf "${TWITTER_ARCHIVE}"

echo "Remove compressed archive"
rm -rf "${TWITTER_ARCHIVE}"

echo "Download complete"