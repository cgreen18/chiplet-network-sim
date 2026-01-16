#!/bin/bash

# TODO as CLA
build_type="Release"
# build_type="Debug"
if [ "$1" == "debug" ]; then
    build_type="Debug"
fi
if [ "$1" == "release" ]; then
    build_type="Release"
fi

if [ -z "$build_type" ]; then
    echo "Error: build_type is not set"
    exit 1
fi

# in CMakePresets.json : change to "generator": "Unix Makefiles",
# only needs to be done once
# sed -i 's/"generator"[[:space:]]*:[[:space:]]*"Ninja"/"generator": "Unix Makefiles"/' CMakePresets.json
sed -i 's/"generator"[[:space:]]*:[[:space:]]*"[^"]*"/"generator": "Unix Makefiles"/' CMakePresets.json

echo "Using Unix Makefiles"

ml boost/1.80.0

echo "Modules loaded"



cmake --preset ${build_type}

echo "Configuring ${build_type} build"

cd builds/${build_type}

cmake --build .

echo "Done"
