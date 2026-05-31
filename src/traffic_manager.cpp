#include "traffic_manager.h"

#include <cstdlib>
#include <cstring>

#include "boost/dynamic_bitset.hpp"

namespace {

uint32_t netrace_hash_node(uint8_t trace_node_id) {
  uint32_t x = trace_node_id;
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

int remap_trace_node(uint8_t trace_id, int num_sim_nodes) {
  return static_cast<int>(netrace_hash_node(trace_id) % static_cast<uint32_t>(num_sim_nodes));
}

}  // namespace

TrafficManager::TrafficManager() {
  netrace_cycle_start_ = 0;
  netrace_sim_cycles_ = 0;
  netrace_sim_packets_ = 0;
  injection_rate_ = 0;
  traffic_ = param->traffic;
  traffic_scale_ = param->traffic_scale;
  if (traffic_scale_ == 0) traffic_scale_ = network->num_cores_;
  message_length_ = param->packet_length;
  if (traffic_ == "sd_trace") {
    trace_.open(param->trace_file, std::fstream::in);
    std::cout << "Trace file is read!" << std::endl;
    std::string head;
    std::getline(trace_, head);
  } else if (traffic_ == "netrace") {
    CTX = new nt_context_t();
    memset(CTX, 0, sizeof(nt_context_t));
    nt_open_trfile(CTX, param->netrace_file.c_str());
    if (param->disable_dependencies) {
      nt_disable_dependencies(CTX);
    } else {
      nt_init_cleared_packets_list(CTX);
    }
    nt_print_trheader(CTX);
    if (!param->run_all_regions) {
      if (param->region < 0 ||
          static_cast<unsigned int>(param->region) >= CTX->input_trheader->num_regions) {
        std::cerr << "ERROR: Workload.region=" << param->region
                  << " is out of range [0, " << CTX->input_trheader->num_regions - 1 << "]"
                  << std::endl;
        std::exit(1);
      }
      begin_netrace_region(param->region);
    }
  }
  output_.open(param->output_file, std::fstream::out);
  log_.open(param->log_file, std::fstream::out);

  pkt_for_injection_ = 0;
  inflight_per_node_.assign(network->num_cores_, 0);
  // statistics
  time_ = std::chrono::system_clock::now();
  all_message_num_.store(0);
  message_arrived_.store(0);
  message_timeout_.store(0);
  total_cycles_.store(0);
  total_internal_hops_.store(0);
  total_parallel_hops_.store(0);
  total_serial_hops_.store(0);
  total_other_hops_.store(0);
#ifdef DEBUG
  for (auto& chip : network->chips_) {
    for (auto& node : chip->nodes_) {
      for (auto& buf : node->in_buffers_) {
        traffic_map_[buf].store(0);
      }
    }
  }
#endif  // DEBUG
}

void TrafficManager::begin_netrace_region(int region) {
  if (region < 0 || static_cast<unsigned int>(region) >= CTX->input_trheader->num_regions) {
    std::cerr << "ERROR: netrace region=" << region << " is out of range [0, "
              << CTX->input_trheader->num_regions - 1 << "]" << std::endl;
    std::exit(1);
  }
  if (!param->disable_dependencies) {
    nt_empty_cleared_packets_list(CTX);
    nt_init_cleared_packets_list(CTX);
    nt_delete_all_dependencies(CTX);
  }
  netrace_cycle_start_ = 0;
  for (int r = 0; r < region; ++r) {
    netrace_cycle_start_ += CTX->input_trheader->regions[r].num_cycles;
  }
  nt_regionhead_t* region_head = &CTX->input_trheader->regions[region];
  nt_seek_region(CTX, region_head);
  CTX->latest_active_packet_cycle = netrace_cycle_start_;
  CTX->done_reading = 0;
  netrace_sim_cycles_ = region_head->num_cycles;
  netrace_sim_packets_ = region_head->num_packets;
  reset();
  std::cout << "Netrace region " << region << ": start_cycle=" << netrace_cycle_start_
            << " cycles=" << netrace_sim_cycles_ << " packets=" << netrace_sim_packets_
            << std::endl;
}

TrafficManager::~TrafficManager() {
  if (traffic_ == "sd_trace") {
    trace_.close();
  } else if (traffic_ == "netrace") {
    delete CTX;
  }
  output_.close();
  log_.close();
}

static int node_index(NodeID id) {
  return id.node_id + id.chip_id * network->get_chip(0)->number_cores_;
}

bool TrafficManager::acquire_inflight(NodeID src) {
  if (param->max_inflight_per_node <= 0) return true;
  int idx = node_index(src);
  std::lock_guard<std::mutex> lock(inflight_mutex_);
  if (inflight_per_node_[idx] >= param->max_inflight_per_node) return false;
  ++inflight_per_node_[idx];
  return true;
}

void TrafficManager::release_inflight(NodeID src) {
  if (param->max_inflight_per_node <= 0) return;
  int idx = node_index(src);
  std::lock_guard<std::mutex> lock(inflight_mutex_);
  if (inflight_per_node_[idx] > 0) --inflight_per_node_[idx];
}

void TrafficManager::reset() {
  pkt_for_injection_ = 0;
  time_ = std::chrono::system_clock::now();
  all_message_num_.store(0);
  message_arrived_.store(0);
  message_timeout_.store(0);
  total_cycles_.store(0);
  total_internal_hops_.store(0);
  total_parallel_hops_.store(0);
  total_serial_hops_.store(0);
  total_other_hops_.store(0);
}

void TrafficManager::print_statistics() {
  std::chrono::duration<double> elapsed_seconds = std::chrono::system_clock::now() - time_;
  double average_internal_hops = ((double)TM->total_internal_hops_ / TM->message_arrived_);
  double average_parallel_hops = ((double)TM->total_parallel_hops_ / TM->message_arrived_);
  double average_serial_hops = ((double)TM->total_serial_hops_ / TM->message_arrived_);
  double average_other_hops = ((double)TM->total_other_hops_ / TM->message_arrived_);
  std::cout << std::endl
            << "Time elapsed: " << elapsed_seconds.count() << "s" << std::endl
            << "Injection rate:" << injection_rate_ << " flits/(node*cycle)"
            << "    Injected:" << all_message_num_ << "    Arrived:  " << message_arrived_
            << "    Timeout:  " << message_timeout_ << std::endl
            << "Average latency: " << ((double)TM->total_cycles_ / TM->message_arrived_)
            << "  Average receiving rate: " << receiving_rate() << std::endl
            << "Internal Hops: " << average_internal_hops
            << "   Parallel Hops: " << average_parallel_hops
            << "   Serial Hops: " << average_serial_hops << "   Other Hops: " << average_other_hops
            << std::endl;
  output_ << injection_rate_ << "," << ((double)total_cycles_ / message_arrived_) << ","
          << receiving_rate() << std::endl;
#ifdef DEBUG
  // int max = 0;
  // Buffer* max_buf = nullptr;
  // for (auto& i : traffic_map_) {
  //   if (i.second > max) {
  //     max = i.second;
  //     max_buf = i.first;
  //   }
  // }
  // std::cout << "Max traffic: " << max << " at " << max_buf->node_->id_ << std::endl;
#endif  // DEBUG
}

void TrafficManager::genMes(std::vector<Packet*>& packets, uint64_t cyc) {
  if (traffic_ == "ring_all_reduce") {
    ring_all_reduce_mess(packets);
    return;
  } else if (traffic_ == "ring_all_reduce_bi") {
    ring_all_reduce_bi_mess(packets);
    return;
  } else if (traffic_ == "netrace") {
    netrace(packets, cyc);
    return;
  }
  for (pkt_for_injection_ += message_per_cycle(); pkt_for_injection_ >= 1; pkt_for_injection_--) {
    Packet* mess;
    if (traffic_ == "test")
      mess = new Packet(NodeID(0, 0), NodeID(0, 8), message_length_);
    else if (traffic_ == "uniform")
      mess = uniform_mess();
    else if (traffic_ == "intra_group_uniform")
      mess = intra_group_uniform_mess();
    else if (traffic_ == "hotspot")
      mess = hotspot_mess();
    else if (traffic_ == "bitcomplement")
      mess = bitcomplement_mess();
    else if (traffic_ == "bitreverse")
      mess = bitreverse_mess();
    else if (traffic_ == "bitshuffle")
      mess = bitshuffle_mess();
    else if (traffic_ == "bittranspose")
      mess = bittranspose_mess();
    else if (traffic_ == "adversarial")
      mess = adversarial_mess();
    else if (traffic_ == "sd_traces")
      mess = sd_trace_mess();
    else
      std::cerr << "Unknown traffic pattern!" << std::endl;
    if (!acquire_inflight(mess->source_)) {
      delete mess;
      continue;
    }
    packets.push_back(mess);
    all_message_num_++;
  }
}

Packet* TrafficManager::uniform_mess() {
  int src, dest;
  int core_per_chip = network->chips_[0]->number_cores_;
  while (true) {
    src = gen() % traffic_scale_;
    dest = gen() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::intra_group_uniform_mess() {
  int src, dest;
  int core_per_chip = network->chips_[0]->number_cores_;
  while (true) {
    src = gen() % traffic_scale_;
    dest = gen() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::hotspot_mess() {
  int src, dest;
  int core_per_chip = network->chips_[0]->number_cores_;
  int node_per_WG = traffic_scale_ / 4;
  int WG1, WG2;
  while (true) {
    WG1 = gen() % 4 * 10;
    WG2 = gen() % 4 * 10;
    src = (WG1 * node_per_WG + gen() % node_per_WG) % traffic_scale_;
    dest = (WG2 * node_per_WG + gen() % node_per_WG) % traffic_scale_;
    if (src != dest) break;
  }
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::bitcomplement_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  int core_per_chip = network->chips_[0]->number_cores_;
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    boost::dynamic_bitset<> dest_binary = ~src_binary;
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::bitreverse_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  int core_per_chip = network->chips_[0]->number_cores_;
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    boost::dynamic_bitset<> dest_binary(bits);
    for (int i = 0; i < bits; ++i) {
      dest_binary[i] = src_binary[bits - 1 - i];
    }
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::bitshuffle_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  int core_per_chip = network->chips_[0]->number_cores_;
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    bool last_bit = src_binary[bits - 1];
    boost::dynamic_bitset<> dest_binary = src_binary << 1;
    dest_binary[0] = last_bit;
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::bittranspose_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  int core_per_chip = network->chips_[0]->number_cores_;
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    boost::dynamic_bitset<> dest_binary(bits);
    for (int i = 0; i < bits; ++i) {
      dest_binary[i] = src_binary[(i + bits / 2) % bits];
    }
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::adversarial_mess() {
  int src, dest;
  int core_per_chip = network->chips_[0]->number_cores_;
  int node_per_WG = traffic_scale_ / 41;
  src = gen() % traffic_scale_;
  int src_group = src / node_per_WG;
  dest = ((src_group + 1) * node_per_WG + gen() % node_per_WG) % traffic_scale_;
  assert(src != dest);
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

Packet* TrafficManager::sd_trace_mess() {
  int src, dest;
  int core_number = network->num_cores_;
  // int core_per_chip = (KNode - 2) * (KNode - 2);
  int core_per_chip = network->chips_[0]->number_cores_;
  while (true) {
    std::string word;
    std::getline(trace_, word, ',');
    std::getline(trace_, word, ',');
    src = std::stoi(word) * 4 + gen() % 4;
    std::getline(trace_, word);
    dest = std::stoi(word) * 4 + gen() % 4;
    if (dest != src) break;
  }
  // return new Message(NodeID(src, src / core_per_chip),
  //                    NodeID(dest, dest / core_per_chip));
  return new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                    NodeID(dest % core_per_chip, dest / core_per_chip), message_length_);
}

void TrafficManager::ring_all_reduce_mess(std::vector<Packet*>& packets) {
  int core_per_chip = network->chips_[0]->number_cores_;
  for (pkt_for_injection_ += message_per_cycle(); pkt_for_injection_ > traffic_scale_;
       pkt_for_injection_ -= traffic_scale_) {
    for (int src = 0; src < traffic_scale_; src++) {
      int dest1;
      if (param->topology == "DragonflyChiplet") {
        if (src % 16 == 0 || src % 16 == 1 || src % 16 == 4 || src % 16 == 5)
          dest1 = (src + 2) % traffic_scale_;
        else if (src % 16 == 2 || src % 16 == 3 || src % 16 == 6 || src % 16 == 7)
          dest1 = (src + 8) % traffic_scale_;
        else if (src % 16 == 8 || src % 16 == 9 || src % 16 == 12 || src % 16 == 13)
          dest1 = (src + 8) % traffic_scale_;
        else if (src % 16 == 10 || src % 16 == 11 || src % 16 == 14 || src % 16 == 15)
          dest1 = (src - 2) % traffic_scale_;
      }
      else if (param->topology == "DragonflySW") {
		dest1 = (src + 1) % traffic_scale_;
	  }
      Packet* mess = new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                                NodeID(dest1 % core_per_chip, dest1 / core_per_chip), message_length_);
      packets.push_back(mess);
      all_message_num_ += 1;
    }
  }
}

void TrafficManager::ring_all_reduce_bi_mess(std::vector<Packet*>& packets) {
  int core_per_chip = network->chips_[0]->number_cores_;
  for (pkt_for_injection_ += message_per_cycle(); pkt_for_injection_ > traffic_scale_ * 2;
       pkt_for_injection_ -= traffic_scale_ * 2) {
    for (int src = 0; src < traffic_scale_; src++) {
      int dest1, dest2;
      if (param->topology == "DragonflyChiplet") {
        if (src % 16 == 0 || src % 16 == 1 || src % 16 == 4 || src % 16 == 5) {
          dest1 = (src + 2) % traffic_scale_;
          dest2 = (src - 8 + traffic_scale_) % traffic_scale_;
        } else if (src % 16 == 2 || src % 16 == 3 || src % 16 == 6 || src % 16 == 7) {
          dest1 = (src + 8) % traffic_scale_;
          dest2 = (src - 2) % traffic_scale_;
        } else if (src % 16 == 8 || src % 16 == 9 || src % 16 == 12 || src % 16 == 13) {
          dest1 = (src + 8) % traffic_scale_;
          dest2 = (src + 2) % traffic_scale_;
        } else if (src % 16 == 10 || src % 16 == 11 || src % 16 == 14 || src % 16 == 15) {
          dest1 = (src - 2) % traffic_scale_;
		  dest2 = (src - 8) % traffic_scale_;
        }
      } else if (param->topology == "DragonflySW") {
        dest1 = (src + 1) % traffic_scale_;
        dest2  = (src - 1) % traffic_scale_;
      }
      Packet* mess = new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                                NodeID(dest1 % core_per_chip, dest1 / core_per_chip), message_length_);
      packets.push_back(mess);
      mess = new Packet(NodeID(src % core_per_chip, src / core_per_chip),
                        NodeID(dest2 % core_per_chip, dest2 / core_per_chip), message_length_);
      packets.push_back(mess);
      all_message_num_ += 2;
    }
  }
}

void TrafficManager::netrace_packet_arrived(nt_packet_t* trace_packet) {
  if (trace_packet != nullptr && !param->disable_dependencies) {
    nt_clear_dependencies_free_packet(CTX, trace_packet);
  }
}

void TrafficManager::netrace_release_trace_packet(nt_packet_t* trace_packet) {
  if (trace_packet == nullptr) return;
  if (param->disable_dependencies) {
    nt_packet_free(trace_packet);
  } else {
    nt_clear_dependencies_free_packet(CTX, trace_packet);
  }
}

void TrafficManager::netrace_drain_cleared_packets(std::vector<Packet*>& vecmess) {
  while (CTX->cleared_packets_list != nullptr) {
    nt_packet_list_t* node = CTX->cleared_packets_list;
    CTX->cleared_packets_list = node->next;
    nt_packet_t* trace_packet = node->node_packet;
    free(node);
    netrace_try_inject(trace_packet, vecmess);
  }
  CTX->cleared_packets_list_tail = nullptr;
}

bool TrafficManager::netrace_try_inject(nt_packet_t* trace_packet,
                                      std::vector<Packet*>& vecmess) {
  if (trace_packet == nullptr) return true;

  if (nt_get_packet_size(trace_packet) == -1) {
    netrace_release_trace_packet(trace_packet);
    return true;
  }

  if (!param->disable_dependencies && !nt_dependencies_cleared(CTX, trace_packet)) {
    return false;
  }

  if (all_message_num_ % 100000 == 0) {
    std::cout << "all_message_num_: " << all_message_num_ << std::endl;
    nt_print_packet(trace_packet);
  }

  const int src = trace_packet->src;
  const int dest = trace_packet->dst;
  if (src == dest) {
    netrace_release_trace_packet(trace_packet);
    return true;
  }

  const int packet_length = ceil((double)nt_get_packet_size(trace_packet) / 16);
  bool inject = false;
  int sim_src = src;
  int sim_dest = dest;
  if (param->node_id_remap) {
    sim_src = remap_trace_node(static_cast<uint8_t>(src), network->num_cores_);
    sim_dest = remap_trace_node(static_cast<uint8_t>(dest), network->num_cores_);
    inject = (sim_src != sim_dest);
  } else if (src < network->num_cores_ && dest < network->num_cores_) {
    inject = true;
  }

  if (inject) {
    nt_packet_t* held_trace = param->disable_dependencies ? nullptr : trace_packet;
    Packet* packet =
        new Packet(network->id2nodeid(sim_src), network->id2nodeid(sim_dest), packet_length,
                   held_trace);
    vecmess.push_back(packet);
    all_message_num_++;
    if (param->disable_dependencies) {
      nt_packet_free(trace_packet);
    }
    return true;
  }

  netrace_release_trace_packet(trace_packet);
  return true;
}

void TrafficManager::netrace(std::vector<Packet*>& vecmess, uint64_t cyc) {
  if (cyc > netrace_sim_cycles_) return;

  const uint64_t abs_cyc = netrace_cycle_start_ + cyc;
  if ((cyc + 1) % 100000000 == 0) {
    print_statistics();
  }

  if (!param->disable_dependencies) {
    netrace_drain_cleared_packets(vecmess);
  }

  while (CTX->latest_active_packet_cycle == abs_cyc) {
    nt_packet_t* trace_packet = nt_read_packet(CTX);
    if (trace_packet == nullptr) return;
    if (!netrace_try_inject(trace_packet, vecmess)) {
      continue;
    }
  }
}
