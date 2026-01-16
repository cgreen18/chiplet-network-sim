#include "basic_arbitrary.h"
#include <fstream>
#include <iostream>
#include <sstream>


// TODO allow modifying # ports
// here it is hardcoded to 6 (for TPUv4)
BasicNode::BasicNode(int vc_num, int buffer_size)
    : Node(6, vc_num, buffer_size){
}

void BasicNode::set_node(Chip* chip, NodeID id) {
  chip_ = chip;
  id_ = id;
}

BasicChip::BasicChip(int vc_num, int buffer_size) {
  number_nodes_ = 1;
  number_cores_ = number_nodes_;
  nodes_.reserve(number_nodes_);
  chip_coordinate_.resize(2);
  for (int node_id = 0; node_id < number_nodes_; node_id++) {
    nodes_.push_back(new BasicNode(vc_num, buffer_size));
  }
}

BasicChip::~BasicChip() {
  for (auto node : nodes_) {
    delete node;
  }
  nodes_.clear();
}

void BasicChip::set_chip(System* system, int chip_id) {
  Chip::set_chip(system, chip_id);
  for (int node_id = 0; node_id < number_nodes_; node_id++) {
    BasicNode* node = get_node(node_id);
  }
}

BasicArbitrary::BasicArbitrary() {
  // topology parameters
  read_config();
  for (int chip_id = 0; chip_id < num_chips_; chip_id++) {
    chips_.push_back(new BasicChip(param->vc_number, param->buffer_size));
    get_chip(chip_id)->set_chip(this, chip_id);
  }

  next_buf_id.resize(num_nodes_, 0);

  connect_chiplets();
}

BasicArbitrary::~BasicArbitrary() {
  for (auto chiplet : chips_) delete chiplet;
  chips_.clear();
}

bool BasicArbitrary::parse_tuple(const std::string& s, int& i, int& j, int& k, int& v) {
  std::istringstream iss(s);
  char ch;
  if (!(iss >> std::ws >> ch) || ch != '(') return false;
  if (!(iss >> i >> std::ws >> ch) || ch != ',') return false;
  if (!(iss >> j >> std::ws >> ch) || ch != ',') return false;
  if (!(iss >> k >> std::ws >> ch) || ch != ',') return false;
  if (!(iss >> v >> std::ws >> ch) || ch != ')') return false;
  return true;
}

void BasicArbitrary::read_config() {
  num_nodes_ = param->params_ptree.get<int>("Network.n_nodes", 1);
  algorithm_ = param->params_ptree.get<std::string>("Network.routing_algorithm", "XY");

  num_total_vcs_ = param->params_ptree.get<int>("Network.vc_number", 2);
  num_escape_vcs_ = param->params_ptree.get<int>("Network.num_escape_vcs", 2);
  d2d_IF_ = param->params_ptree.get<std::string>("Network.d2d_IF", "off_chip_serial");

  printf("Basic Arbitrary of %d nodes\n", num_nodes_);
  num_chips_ = num_nodes_;
  num_nodes_ = num_nodes_;
  num_cores_ = num_nodes_;

  auto adjacency_matrix_filename = param->params_ptree.get<std::string>("Network.adjaceny_matrix_filename", "example.txt");
  std::cout << "TOPO filename = " << adjacency_matrix_filename << std::endl;
  load_adjacency_matrix(adjacency_matrix_filename);

  auto nrl_filename = param->params_ptree.get<std::string>("Network.nrl_filename", "");
  std::cout << "NRL filename = " << nrl_filename << std::endl;
  uses_nrl2 = false;
  nrl_version = param->params_ptree.get<std::string>("Network.nrl_version", "1");
  if (nrl_version == "2") uses_nrl2 = true;
  if (uses_nrl2) {
    std::cout << "Using NRL version 2 (sparse format)" << std::endl;
    load_tupled_routing_table(nrl_filename);
  } else {
    std::cout << "Using NRL version 1 (dense format)" << std::endl;
    load_3d_routing_table(nrl_filename);
  }

  uses_datelines = false;
  vc_type = param->params_ptree.get<std::string>("Network.vc_type", "flow");
  if (vc_type == "dateline") uses_datelines = true;

  auto vc_filename = param->params_ptree.get<std::string>("Network.vc_filename", "");
  std::cout << "VC filename = " << vc_filename << std::endl;
  
  uses_vcmat2 = false;
  vc_version = param->params_ptree.get<std::string>("Network.vc_version", "1");
  if (vc_version == "2") uses_vcmat2 = true;

  if (!uses_datelines) {
    std::cout << "   Using per-flow (2D) deadlock avoidance" << std::endl;
    load_vc_matrix(vc_filename);
  } else if (uses_vcmat2) {
    std::cout << "   Using datelining (3D) deadlock avoidance with VC version 2 (sparse format)" << std::endl;
    load_tupled_vc_matrix(vc_filename);
  } else {
    std::cout << "   Using datelining (3D) deadlock avoidance with VC version 1 (dense format)" << std::endl;
    load_3d_vc_matrix(vc_filename);
  }
}

void BasicArbitrary::load_adjacency_matrix(const std::string& filename) {
  std::ifstream file(filename);
  if (!file) {
    std::cerr << "Error: Could not open file " << filename << "\n";
    return;
  }

  adjacency_matrix_.resize(num_nodes_, std::vector<int>(num_nodes_, 0));

  for (int i = 0; i < num_nodes_; ++i) {
    for (int j = 0; j < num_nodes_; ++j) {
      file >> adjacency_matrix_[i][j];
    }
  }

  file.close();
}

void BasicArbitrary::load_vc_matrix(const std::string& filename) {
  std::ifstream file(filename);
  if (!file) {
    std::cerr << "Error: Could not open file " << filename << "\n";
    return;
  }

  src_dst_vc_table_.resize(num_nodes_, std::vector<int>(num_nodes_, 0));

  int max_vc_value = -1;
  for (int i = 0; i < num_nodes_; ++i) {
    for (int j = 0; j < num_nodes_; ++j) {
      file >> src_dst_vc_table_[i][j];
      if (src_dst_vc_table_[i][j] > max_vc_value) {
        max_vc_value = src_dst_vc_table_[i][j];
      }
    }
  }

  file.close();
  
  // Assert that num_escape_vcs_ is greater than all VC matrix values
  if (max_vc_value >= 0) {
    assert(max_vc_value < num_escape_vcs_ && 
           "ERROR: num_escape_vcs must be greater than all VC matrix values");
  }
  
  return;
}

void BasicArbitrary::load_3d_routing_table(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
      std::cerr << "Error: Could not open file " << filename << "\n";
      return;
  }

  std::cout << "reading from file " << filename << "\n";

  std::string line;
  int nodes = num_nodes_;

  // Skip the first line
  std::getline(file, line);


  for (int i = 0; i < nodes; ++i) {
      std::vector<std::vector<int>> matrix;
      for (int j = 0; j < nodes; ++j) {
          std::getline(file, line);
          line = line.substr(1, line.size() - 2); // Remove square brackets
          std::stringstream ss(line);
          std::vector<int> row;
          int value;
          while (ss >> value) {
              row.push_back(value);
              if (ss.peek() == ',') ss.ignore(); // Skip comma
          }
          matrix.push_back(row);
      }
      cur_src_dst_routing_table_.push_back(matrix);
  }

  file.close();
  return;
}

void BasicArbitrary::load_tupled_routing_table(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
      std::cerr << "Error: Could not open file " << filename << "\n";
      return;
  }

  std::cout << "reading from file " << filename << "\n";

  std::string line;
  src_dst_cur_sparse_routing_table_.assign(num_nodes_, std::vector<std::unordered_map<int,int>>(num_nodes_));

  int n_parsed = 0;
  while (getline(file, line)) {
      // Skip empty lines
      if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
        continue;
      }
      
      int i, j, k, v;
      if (!parse_tuple(line, i, j, k, v)) {
        std::cerr << "ERROR: parsing nrl2 line: " << line << std::endl;
        continue;
      }
      if (i < 0 || j < 0 || k < 0) {
        std::cerr << "ERROR: parsing nrl2. Negative i,j,k: " << i << "," << j << "," << k << std::endl;
        continue;
      }

      src_dst_cur_sparse_routing_table_[i][j][k] = v; // creates or overwrites

      if(n_parsed % 100000 == 0) std::cout << "Completed lines : " << n_parsed << std::endl;
      n_parsed++;
  }
  
  std::cout << "Total NRL entries parsed: " << n_parsed << std::endl;

  std::cout << "Completed parsing routing table" << std::endl;

  file.close();
}


void BasicArbitrary::load_3d_vc_matrix(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
      std::cerr << "Error: Could not open file " << filename << "\n";
      return;
  }

  std::cout << "reading from file " << filename << "\n";

  std::string line;
  int nodes = num_nodes_;

  src_dst_cur_vc_table_.resize(num_nodes_, std::vector<std::vector<int>>(num_nodes_, std::vector<int>(num_nodes_)));

  int max_vc_value = -1;
  for (int block = 0; block < num_nodes_; ++block) {
      for (int row = 0; row < num_nodes_; ++row) {
          for (int col = 0; col < num_nodes_; ++col) {
              file >> src_dst_cur_vc_table_[block][row][col];
              if (src_dst_cur_vc_table_[block][row][col] > max_vc_value) {
                max_vc_value = src_dst_cur_vc_table_[block][row][col];
              }
          }
      }
  }

  file.close();
  
  // Assert that num_escape_vcs_ is greater than all VC matrix values
  if (max_vc_value >= 0) {
    assert(max_vc_value < num_escape_vcs_ && 
           "ERROR: num_escape_vcs must be greater than all VC matrix values");
  }
  
  return;
}

void BasicArbitrary::load_tupled_vc_matrix(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
      std::cerr << "Error: Could not open file " << filename << "\n";
      return;
  }

  std::cout << "reading from file " << filename << "\n";

  std::string line;
  src_dst_cur_sparse_vc_table_.assign(num_nodes_, std::vector<std::unordered_map<int,int>>(num_nodes_));

  int n_parsed = 0;
  int max_vc_value = -1;
  while (getline(file, line)) {
    // Skip empty lines
    if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
      continue;
    }
    
    int i, j, k, v;
    if (!parse_tuple(line, i, j, k, v)) {
      std::cerr << "ERROR: parsing vc2 line: " << line << std::endl;
      continue;
    }
    if (i < 0 || j < 0 || k < 0) {
      std::cerr << "ERROR: parsing vc2. Negative i,j,k: " << i << "," << j << "," << k << std::endl;
      continue;
    }

    src_dst_cur_sparse_vc_table_[i][j][k] = v; // creates or overwrites
    if (v > max_vc_value) {
      max_vc_value = v;
    }

    if(n_parsed % 100000 == 0) std::cout << "Completed lines : " << n_parsed << std::endl;
    n_parsed++;
  }
  
  std::cout << "Total VC entries parsed: " << n_parsed << std::endl;

  file.close();

  // Assert that num_escape_vcs_ is greater than all VC matrix values
  if (max_vc_value >= 0) {
    assert(max_vc_value < num_escape_vcs_ && 
           "ERROR: num_escape_vcs must be greater than all VC matrix values");
  }

  std::cout << "Completed parsing VC matrix" << std::endl;
}

void BasicArbitrary::connect_chiplets() {
  for (int src_chip_id = 0; src_chip_id < num_nodes_; ++src_chip_id) {
    BasicChip* chip = get_chip(src_chip_id);

    for (int dest_chip_id = 0; dest_chip_id < num_nodes_; dest_chip_id++) {
      // Skip if no connection exists
      if (adjacency_matrix_[src_chip_id][dest_chip_id] == 0) continue;

      int src_buf_id = -1;
      int dest_buf_id = -1;

      auto conn_tuple = std::make_tuple(src_chip_id, dest_chip_id);
      auto reverse_conn_tuple = std::make_tuple(dest_chip_id, src_chip_id);

      // Allocate buffer IDs if this connection hasn't been seen before
      if (buf_conn_map.count(conn_tuple) == 0) {
        src_buf_id = next_buf_id[src_chip_id];
        next_buf_id[src_chip_id]++;

        dest_buf_id = next_buf_id[dest_chip_id];
        next_buf_id[dest_chip_id]++;

        buf_conn_map[conn_tuple] = std::make_tuple(src_buf_id, dest_buf_id);
        buf_conn_map[reverse_conn_tuple] = std::make_tuple(dest_buf_id, src_buf_id);
      } else {
        // Reuse existing buffer IDs
        auto buf_id_tuple = buf_conn_map.at(conn_tuple);
        src_buf_id = std::get<0>(buf_id_tuple);
        dest_buf_id = std::get<1>(buf_id_tuple);
      }

      // Connect source node's output buffer to destination node's input buffer
      BasicNode* node = chip->get_node(0);
      node->link_nodes_[src_buf_id] = NodeID(0, dest_chip_id);
      node->link_buffers_[src_buf_id] = get_node(node->link_nodes_[src_buf_id])->in_buffers_[dest_buf_id];

      // Set channel type based on d2d interface
      if (d2d_IF_ == "off_chip_parallel") {
        node->link_buffers_[src_buf_id]->channel_ = off_chip_parallel_channel;
      } else if (d2d_IF_ == "off_chip_serial") {
        node->link_buffers_[src_buf_id]->channel_ = off_chip_serial_channel;
      } else {
        // Default to serial channel
        node->link_buffers_[src_buf_id]->channel_ = off_chip_serial_channel;
      }
    }
  }
}

void BasicArbitrary::routing_algorithm(Packet& s) const {
  if (algorithm_ == "NRL_routing")
    NRL_routing(s);
  else{
    std::cerr << "ERROR : Unknown routing algorithm: " << algorithm_ << std::endl;
    exit(-1);
  }

}

void BasicArbitrary::NRL_routing(Packet& s) const {
  BasicChip* current_chip = get_chip(s.head_trace().id);
  int current_chip_id = current_chip->chip_id_;
  BasicChip* destination_chip = get_chip(s.destination_);
  int destination_chip_id = destination_chip->chip_id_;
  BasicChip* source_chip = get_chip(s.source_);
  int source_chip_id = source_chip->chip_id_;
  BasicNode* current_node = get_node(s.head_trace().id);

  // Determine next chip ID based on routing table version
  int next_chip_id = -1;
  if (uses_nrl2) {
    // Sparse version 2: use unordered_map lookup
    auto it = src_dst_cur_sparse_routing_table_[source_chip_id][destination_chip_id].find(current_chip_id);
    if (it == src_dst_cur_sparse_routing_table_[source_chip_id][destination_chip_id].end()) {
      std::cerr << "ERROR: Missing nrl cur " << current_chip_id
                << ", src " << source_chip_id
                << ", dest " << destination_chip_id << std::endl;
      exit(-1);
    }
    next_chip_id = it->second;
  } else {
    // Dense version 1: use 3D vector lookup
    next_chip_id = cur_src_dst_routing_table_[current_chip_id][source_chip_id][destination_chip_id];
  }

  if (next_chip_id == -1) {
    std::cerr << "ERROR: cannot find next router" << std::endl;
    exit(-1);
  }

  // Determine VC based on VC table version and type
  int vc_allowed = -1;
  if (!uses_datelines) {
    // 2D per-flow VC table: use the table value directly, then apply probability-based selection
    int deadlock_free_vc = src_dst_vc_table_[source_chip_id][destination_chip_id];
    if (deadlock_free_vc == -1) {
      std::cerr << "ERROR: cannot find VC" << std::endl;
      exit(-1);
    }
    assert(deadlock_free_vc < num_escape_vcs_);
    
    vc_allowed = deadlock_free_vc;
    if (num_total_vcs_ > num_escape_vcs_) {
      int num_general_vcs = num_total_vcs_ - num_escape_vcs_;
      // Probability: p(escape) = 1/(g+1) and p(general) = g/(g+1)
      int rand_num = rand() % (num_general_vcs + 1);
      if (rand_num == num_general_vcs) {
        // Use escape VC: vc_allowed in [num_general_vcs, num_total_vcs_)
        vc_allowed = deadlock_free_vc + num_escape_vcs_;
      } else {
        // Use general VC: vc_allowed in [0, num_general_vcs)
        vc_allowed = rand_num;
      }
    }
  } else if (uses_vcmat2) {
    // Sparse version 2 with datelines: get deadlock-free VC from table, then apply probability-based selection
    auto it = src_dst_cur_sparse_vc_table_[source_chip_id][destination_chip_id].find(current_chip_id);
    if (it == src_dst_cur_sparse_vc_table_[source_chip_id][destination_chip_id].end()) {
      std::cerr << "ERROR: Missing vc cur " << current_chip_id
                << ", src " << source_chip_id
                << ", dest " << destination_chip_id << std::endl;
      exit(-1);
    }
    int deadlock_free_vc = it->second;
    assert(deadlock_free_vc < num_escape_vcs_);
    
    vc_allowed = deadlock_free_vc;
    if (num_total_vcs_ > num_escape_vcs_) {
      int num_general_vcs = num_total_vcs_ - num_escape_vcs_;
      // Probability: p(escape) = 1/(g+1) and p(general) = g/(g+1)
      int rand_num = rand() % (num_general_vcs + 1);
      if (rand_num == num_general_vcs) {
        // Use escape VC: vc_allowed in [num_general_vcs, num_total_vcs_)
        vc_allowed = deadlock_free_vc + num_escape_vcs_;
      } else {
        // Use general VC: vc_allowed in [0, num_general_vcs)
        vc_allowed = rand_num;
      }
    }
  } else {
    // Dense version 1 with datelines: get deadlock-free VC from table, then apply probability-based selection
    int deadlock_free_vc = src_dst_cur_vc_table_[source_chip_id][destination_chip_id][current_chip_id];
    if (deadlock_free_vc == -1) {
      std::cerr << "ERROR: cannot find VC" << std::endl;
      exit(-1);
    }
    assert(deadlock_free_vc < num_escape_vcs_);
    
    vc_allowed = deadlock_free_vc;
    if (num_total_vcs_ > num_escape_vcs_) {
      int num_general_vcs = num_total_vcs_ - num_escape_vcs_;
      // Probability: p(escape) = 1/(g+1) and p(general) = g/(g+1)
      int rand_num = rand() % (num_general_vcs + 1);
      if (rand_num == num_general_vcs) {
        // Use escape VC: vc_allowed in [num_general_vcs, num_total_vcs_)
        vc_allowed = deadlock_free_vc + num_escape_vcs_;
      } else {
        // Use general VC: vc_allowed in [0, num_general_vcs)
        vc_allowed = rand_num;
      }
    }
  }

  // Get buffer connection information
  auto conn_tuple = std::make_pair(current_chip_id, next_chip_id);
  auto buf_it = buf_conn_map.find(conn_tuple);
  if (buf_it == buf_conn_map.end()) {
    std::cerr << "ERROR: No buffer connection found for current_chip_id=" << current_chip_id
              << ", next_chip_id=" << next_chip_id << std::endl;
    exit(-1);
  }
  auto buf_id_tuple = buf_it->second;
  auto src_buf_id = std::get<0>(buf_id_tuple);
  auto dest_buf_id = std::get<1>(buf_id_tuple);

  // Add candidate channel
  s.candidate_channels_.push_back(VCInfo(current_node->link_buffers_[src_buf_id], vc_allowed));
}