#pragma once
#include "system.h"


class Chip;

class BasicNode : public Node {
 public:
  BasicNode(int vc_num, int buffer_size);

  void set_node(Chip* chip, NodeID id) override;

  // Chip* chip_;  // point to the chip where the node is located
  // int x_, y_;   // coodinate with the chip
  // int k_node_;  // number of nodes in a row/column
};

class BasicChip : public Chip {
 public:
  BasicChip(int vc_num, int buffer_size);
  ~BasicChip();
  void set_chip(System* system, int chip_id) override;
  inline BasicNode* get_node(int node_id) const override {
    return static_cast<BasicNode*>(nodes_[node_id]);
  }
  inline BasicNode* get_node(NodeID id) const override {
    return static_cast<BasicNode*>(nodes_[id.node_id]);
  }

//   int k_node_;
  std::vector<int> chip_coordinate_;
};




class BasicArbitrary : public System {
 public:
  BasicArbitrary();
  ~BasicArbitrary();

  void read_config() override;

  void load_adjacency_matrix(const std::string& filename);
  void load_vc_matrix(const std::string& filename);
  void load_3d_vc_matrix(const std::string& filename);
  void load_3d_routing_table(const std::string& filename);

  // override as simple 1:1
  inline NodeID id2nodeid(int id) const override {
    int node_id = id;
    int chip_id = id;
    return NodeID(node_id, chip_id);
  }

  // TODO are these used?
  inline BasicNode* get_node(NodeID id) const override {
    return dynamic_cast<BasicNode*>(System::get_node(id));
  }
  inline BasicChip* get_chip(int chip_id) const override {
    return dynamic_cast<BasicChip*>(chips_[chip_id]);
  }
  inline BasicChip* get_chip(NodeID id) const override {
    return dynamic_cast<BasicChip*>(chips_[id.chip_id]);
  }
  void connect_chiplets();

  void routing_algorithm(Packet& s) const override;
  void XY_routing(Packet& s) const;
  void NRL_routing(Packet& s) const;

  std::string algorithm_;

  std::vector<std::vector<int>> adjacency_matrix_;
  std::vector<std::vector<std::vector<int>>> cur_src_dst_routing_table_;
  
  std::vector<std::vector<int>> src_dst_vc_table_;
  std::vector< std::vector<std::vector<int>>> src_dst_cur_vc_table_;
  std::string vc_type;
  bool uses_datelines;

  std::map< std::tuple<int,int> , std::tuple<int,int> > buf_conn_map;
  std::vector<int> next_buf_id;


  std::string d2d_IF_;
};