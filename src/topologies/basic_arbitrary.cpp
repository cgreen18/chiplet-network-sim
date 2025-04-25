#include "basic_arbitrary.h"


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

void BasicArbitrary::read_config() {
  num_nodes_ = param->params_ptree.get<int>("Network.n_nodes", 1);
  algorithm_ = param->params_ptree.get<std::string>("Network.routing_algorithm", "XY");

  printf("Single Node Arbitrary of %d nodes\n",num_nodes_);
  num_chips_ = num_nodes_;
  num_nodes_ = num_nodes_;
  num_cores_ = num_nodes_;

  auto adjaceny_matrix_filename = param->params_ptree.get<std::string>("Network.adjaceny_matrix_filename", "example.txt");
  std::cout << "TOPO filename = "<<adjaceny_matrix_filename<<std::endl;
  load_adjacency_matrix(adjaceny_matrix_filename);

  auto nrl_filename = param->params_ptree.get<std::string>("Network.nrl_filename", "");
  std::cout << "NRL filename = "<<nrl_filename<<std::endl;
  load_3d_routing_table(nrl_filename);


  uses_datelines = false;
  vc_type = param->params_ptree.get<std::string>("Network.vc_type", "flow");
  if (vc_type == "dateline") uses_datelines = true;

  auto vc_filename = param->params_ptree.get<std::string>("Network.vc_filename", "");
  std::cout << "VC filename = "<<vc_filename<<std::endl;
  if(!uses_datelines){
    std::cout <<"   Using per-flow (2D) deadlock avoidance"<<std::endl;
    load_vc_matrix(vc_filename);
  }
  else{
    std::cout <<"   Using datelining (3D) deadlock avoidance"<<std::endl;
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

  for (int i = 0; i < num_nodes_; ++i) {
    for (int j = 0; j < num_nodes_; ++j) {
      file >> src_dst_vc_table_[i][j];
    }
  }

  file.close();
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


  for (int block = 0; block < num_nodes_; ++block) {
      for (int row = 0; row < num_nodes_; ++row) {
          for (int col = 0; col < num_nodes_; ++col) {
              file >> src_dst_cur_vc_table_[block][row][col];
          }
      }
  }

  file.close();
  return;
}

void BasicArbitrary::connect_chiplets() {

  for (int src_chip_id = 0; src_chip_id < num_nodes_; ++src_chip_id) {
    BasicChip* chip = get_chip(src_chip_id);

    for (int dest_chip_id = 0; dest_chip_id < num_nodes_; dest_chip_id++){

        if(adjacency_matrix_[src_chip_id][dest_chip_id] == 0) continue;

        int src_buf_id = -1;
        int dest_buf_id = -1;

        auto conn_tuple = std::make_tuple(src_chip_id,dest_chip_id);
        auto reverse_conn_tuple = std::make_tuple(dest_chip_id, src_chip_id);
        if(buf_conn_map.count(conn_tuple) == 0){

            src_buf_id = next_buf_id[src_chip_id];
            next_buf_id[src_chip_id]++;

            dest_buf_id = next_buf_id[dest_chip_id];
            next_buf_id[dest_chip_id]++;

            buf_conn_map[conn_tuple] = std::make_tuple(src_buf_id,dest_buf_id);
            buf_conn_map[reverse_conn_tuple] = std::make_tuple(dest_buf_id,src_buf_id);
        }
        else{
            auto buf_id_tuple = buf_conn_map.at(conn_tuple);
            src_buf_id = std::get<0>(buf_id_tuple);
            dest_buf_id = std::get<1>(buf_id_tuple);
        }

        // src
        BasicNode* node = chip->get_node(0);
        node->link_nodes_[src_buf_id] = NodeID(0, dest_chip_id);
        node->link_buffers_[src_buf_id] = get_node(node->link_nodes_[src_buf_id])->in_buffers_[dest_buf_id];

        // off_chip_serial_channel defined in config.h as bw of 2 and latency of 4
        node->link_buffers_[src_buf_id]->channel_ = off_chip_serial_channel;

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
  BasicNode* destination_node = get_node(s.destination_);

  int next_chip_id = cur_src_dst_routing_table_[current_chip_id][source_chip_id][destination_chip_id];

  if(next_chip_id == -1){
    std::cerr << "ERROR : cannot find next router"<<std::endl;
    exit(-1);
  }

  // TODO assert valid router id

  int deadlock_free_vc = -1;
  if(!uses_datelines)
    deadlock_free_vc = src_dst_vc_table_[source_chip_id][destination_chip_id];
  else
    deadlock_free_vc = src_dst_cur_vc_table_[source_chip_id][destination_chip_id][current_chip_id];

  // TODO do something for general versus escape VCs
  // for now only deadlock free escape VCs
  int vc_allowed = deadlock_free_vc;

  if(vc_allowed == -1){
    std::cerr << "ERROR : cannot find VC"<<std::endl;
    exit(-1);
  }
  
  // TODO assert valid VC number

  auto conn_tuple = std::make_pair(current_chip_id,next_chip_id);
  auto buf_id_tuple = buf_conn_map.at(conn_tuple);
  auto src_buf_id = std::get<0>(buf_id_tuple);
  auto dest_buf_id = std::get<1>(buf_id_tuple);


  s.candidate_channels_.push_back(VCInfo(current_node->link_buffers_[src_buf_id], vc_allowed));

}