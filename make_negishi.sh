#!/bin/bash

# in CMakePresets.json : change to "generator": "Unix Makefiles",
# only needs to be done once
# sed -i 's/"generator"[[:space:]]*:[[:space:]]*"Ninja"/"generator": "Unix Makefiles"/' CMakePresets.json
sed -i 's/"generator"[[:space:]]*:[[:space:]]*"[^"]*"/"generator": "Unix Makefiles"/' CMakePresets.json

echo "Using Unix Makefiles"

ml boost/1.80.0

echo "Modules loaded"

# TODO as CLA
build_type="Release"
#build_type="Debug"

cmake --preset ${build_type}

echo "Configuring ${build_type} build"

cd builds/${build_type}

cmake --build .

echo "Done"
