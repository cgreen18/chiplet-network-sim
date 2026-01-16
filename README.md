# Instructions for Simulating Arbitrary Topologies
Conor Green

Targeting TPU v4/5 topologies

# Build

```shell
# [Debug, Release]
BUILD_TYPE="Debug"

# required module
ml boost

cmake --preset $BUILD_TYPE
cd builds/$BUILD_TYPE
cmake --build .
```

Or use script
```shell
./make_negishi.sh
```

# Run

```shell
./builds/Release/ChipletNetworkSim ./input/<config-file>.ini
```

# Configuration File Format

The simulator supports multiple topology types and routing/VC configurations. Configuration files use INI format with sections for `[Network]`, `[Workload]`, and `[Simulation]`.

## Topology Types

The simulator supports the following topology types (set via `topology` in `[Network]` section):

1. **BasicArbitrary** - Arbitrary topology defined by adjacency matrix file
2. **SingleChipMesh** - Single chip mesh topology
3. **MultiChipMesh** - Multi-chip mesh topology
4. **MultiChipTorus** - Multi-chip torus topology
5. **DragonflySW** - Dragonfly switch topology
6. **DragonflyChiplet** - Dragonfly chiplet topology

## BasicArbitrary Topology Configuration

For `BasicArbitrary` topology, the following parameters are available:

### Required Parameters

- `topology = BasicArbitrary`
- `routing_algorithm = NRL_routing` (currently only NRL_routing is supported)
- `n_nodes` - Number of nodes in the network
- `adjaceny_matrix_filename` - Path to adjacency matrix file (N×N matrix of 0s and 1s)
- `nrl_filename` - Path to Next Router List (NRL) file
- `vc_filename` - Path to Virtual Channel (VC) matrix file

### Optional Parameters

- `buffer_size` - Buffer size in flits (default: 16)
- `vc_number` - Total number of virtual channels (default: 2)
- `num_escape_vcs` - Number of escape VCs for deadlock avoidance (default: 2)
- `d2d_IF` - Die-to-die interface type: `off_chip_parallel` or `off_chip_serial` (default: `off_chip_serial`)
- `vc_type` - VC deadlock avoidance type: `flow` (2D per-flow) or `dateline` (3D datelining) (default: `flow`)

### NRL (Next Router List) Versions

The simulator supports two versions of NRL files:

#### Version 1 (Dense Format) - Default
- Format: 3D dense matrix
- File structure: First line is skipped, then for each current node (i), for each source (j), for each destination (k), a line with comma-separated values in brackets `[val1, val2, ...]`
- Set via: `nrl_version = 1` (or omit, as this is the default)

#### Version 2 (Sparse Format)
- Format: Tuple-based sparse representation
- File structure: One tuple per line: `(current_node, source, destination, next_router)`
- Example line: `(0, 1, 2, 3)` means from source 1 to destination 2, at current node 0, next router is 3
- Set via: `nrl_version = 2`

### VC Matrix Versions

The simulator supports two versions of VC matrices (only relevant when `vc_type = dateline`):

#### Version 1 (Dense Format) - Default
- Format: 3D dense matrix (source × destination × current_node)
- File structure: Space-separated values, read as 3 nested loops
- Set via: `vc_version = 1` (or omit, as this is the default)

#### Version 2 (Sparse Format)
- Format: Tuple-based sparse representation
- File structure: One tuple per line: `(source, destination, current_node, vc_index)`
- Example line: `(0, 1, 2, 0)` means from source 0 to destination 1, at current node 2, use VC index 0
- Set via: `vc_version = 2`

### Example Configurations

#### Example 1: Dense NRL and VC (Version 1) with Dateline
```ini
[Network]
topology = BasicArbitrary
routing_algorithm = NRL_routing
n_nodes = 128
buffer_size = 20
vc_number = 2
num_escape_vcs = 2
d2d_IF = off_chip_serial
adjaceny_matrix_filename = topologies_and_routing/topo_maps/pt_2c_128r_6p_8x4x4.map
nrl_filename = topologies_and_routing/nr_lists/pt_2c_128r_6p_8x4x4_dor_dim_tiebreak.nrl
nrl_version = 1
vc_filename = topologies_and_routing/vc_mats/pt_2c_128r_6p_8x4x4_dor_dim_tiebreak_dateline_constr.vcmat
vc_type = dateline
vc_version = 1

[Workload]
traffic = uniform

[Simulation]
injection_increment = 1.0
threads = 32
```

#### Example 2: Sparse NRL and VC (Version 2) with Dateline
```ini
[Network]
topology = BasicArbitrary
routing_algorithm = NRL_routing
n_nodes = 128
buffer_size = 20
vc_number = 4
num_escape_vcs = 2
d2d_IF = off_chip_serial
adjaceny_matrix_filename = topologies_and_routing/topo_maps/pt_2c_128r_6p_8x4x4.map
nrl_filename = topologies_and_routing/nr_lists/pt_2c_128r_6p_8x4x4_dor_dim_tiebreak.nrl2
nrl_version = 2
vc_filename = topologies_and_routing/vc_mats/pt_2c_128r_6p_8x4x4_dor_dim_tiebreak_dateline_constr.vcmat2
vc_type = dateline
vc_version = 2

[Workload]
traffic = uniform

[Simulation]
injection_increment = 1.0
threads = 32
```

#### Example 3: Per-Flow VC (2D) - No Dateline
```ini
[Network]
topology = BasicArbitrary
routing_algorithm = NRL_routing
n_nodes = 64
buffer_size = 16
vc_number = 2
num_escape_vcs = 2
d2d_IF = off_chip_parallel
adjaceny_matrix_filename = topologies_and_routing/topo_maps/example.map
nrl_filename = topologies_and_routing/nr_lists/example.nrl
nrl_version = 1
vc_filename = topologies_and_routing/vc_mats/example_flow.vcmat
vc_type = flow

[Workload]
traffic = uniform

[Simulation]
injection_increment = 0.1
threads = 8
```

## Other Topology Types

For mesh, torus, and dragonfly topologies, refer to the original codebase documentation. These topologies are configured via code rather than file inputs.

## Workload Configuration

- `traffic` - Traffic pattern: `uniform`, `hotspot`, `bitcomplement`, `bittranspose`, `bitreverse`, `bitshuffle`, `adversarial`, `sd_trace`, `netrace`, `ring_all_reduce`, `ring_all_reduce_bi`

## Simulation Configuration

- `injection_increment` - Injection rate increment
- `threads` - Number of simulation threads
- `timeout_threshold` - Timeout threshold (optional)
- `timeout_limit` - Timeout limit (optional)
- `issue_width` - Issue width for packet processing (optional, auto-set based on threads)

## Running Examples

```shell
# Run with dense format (version 1)
./builds/Release/ChipletNetworkSim ./input/test.ini

# Run with sparse format (version 2)
./builds/Release/ChipletNetworkSim ./input/test_sparse.ini

# Run mesh topology
./builds/Release/ChipletNetworkSim ./input/mesh_config.ini
```


# BELOW IS THE ORIGINAL README

# Chiplet Network Sim

Chiplet Network Simulator (CNSim) is a cycle-accurate packet-parallel simulator supporting efficient simulation for large-scale chiplet-based networks.

If you use CNSim in your research, we would appreciate the following citation in any publications to which it has contributed:

Yinxiao Feng, Yuchen Wei, Dong Xiang and Kaisheng Ma. Evaluating Chiplet-based Large-Scale Interconnection Networks via Cycle-Accurate Packet-Parallel Simulation. In 2024 USENIX Annual Technical Conference (ATC), 2024.

### Features
- Hyper-threading
- Cycle-accurate
- Highly configurable and customizable in terms of topology, routing algorithms, and microarchitecture.


### Dependencies
- boost library
- netrace (optional, modified from https://github.com/booksim/netrace)
	- bzip2

### Usage 
- Windows
  - Visual Studio 2022
  - Open the directory as a cmake project
- Linux
  - Enter the directory, build, and run

### Ubuntu 22.04
```
sudo apt install cmake ninja-build build-essential libboost-all-dev libbz2-dev
cd chiplet-network-sim
cmake --preset Release
cd builds/Release/
cmake --build .
./ChipletNetworkSim ../../input/multiple_chip_mesh_4x4.ini
```

### Acknowledgement
 - Yuchen Wei, Tsinghua University
 - Dong Xiang and Kaisheng Ma, Tsinghua University
 - BookSim and Netrace (https://github.com/booksim)