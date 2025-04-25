



# TODO as CLA
build_type="Release"
#build_type="Debug"

# given from original repo
./builds/${build_type}/ChipletNetworkSim ./input/multiple_chip_mesh_4x4.ini

# arbitrary networks
./builds/${build_type}/ChipletNetworkSim ./input/tpuv4_128r_dateline_asc.ini
