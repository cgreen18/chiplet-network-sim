#pragma once
#include <chrono>
#include <fstream>
#include <mutex>

#include "system.h"
extern "C" {
#include "netrace.h"
}

#include "boost/random.hpp"
extern boost::mt19937 gen;

class TrafficManager {
 public:
  TrafficManager();
  ~TrafficManager();
  void reset();
  bool acquire_inflight(NodeID src);
  void release_inflight(NodeID src);
  void genMes(std::vector<Packet*>& packets, uint64_t cyc = 0);
  Packet* uniform_mess();
  Packet* intra_group_uniform_mess();
  Packet* hotspot_mess();
  Packet* bitcomplement_mess();
  Packet* bitreverse_mess();
  Packet* bitshuffle_mess();
  Packet* bittranspose_mess();
  Packet* adversarial_mess();
  Packet* sd_trace_mess();
  void ring_all_reduce_mess(std::vector<Packet*>& packets);
  void ring_all_reduce_bi_mess(std::vector<Packet*>& packets);
  void netrace(std::vector<Packet*>& packets, uint64_t cyc);
  void netrace_packet_arrived(nt_packet_t* trace_packet);
  inline double receiving_rate() const {
    return injection_rate_ * ((double)TM->message_arrived_ / TM->all_message_num_);
  };

  void print_statistics();
  void begin_netrace_region(int region);

  std::fstream trace_;
  nt_context_t* CTX;
  uint64_t netrace_cycle_start_;  // absolute trace cycle of region start
  uint64_t netrace_sim_cycles_;
  uint64_t netrace_sim_packets_;
  std::fstream output_;
  std::fstream log_;

  double injection_rate_;
  inline double message_per_cycle() const {
    return injection_rate_ * traffic_scale_ / param->packet_length;
  };
  std::string traffic_;
  int traffic_scale_;
  int message_length_;

  std::unordered_map<Buffer*, std::atomic_uint64_t> traffic_map_;
  double pkt_for_injection_;
  // atomic statistics, modified by all threds
  std::chrono::system_clock::time_point time_;
  std::atomic_uint64_t all_message_num_;
  std::atomic_uint64_t message_arrived_;
  std::atomic_uint64_t message_timeout_;
  std::atomic_uint64_t total_cycles_;
  std::atomic_uint64_t total_internal_hops_;
  std::atomic_uint64_t total_parallel_hops_;
  std::atomic_uint64_t total_serial_hops_;
  std::atomic_uint64_t total_other_hops_;

  std::vector<int> inflight_per_node_;
  std::mutex inflight_mutex_;

 private:
  void netrace_drain_cleared_packets(std::vector<Packet*>& vecmess);
  // Returns false if trace_packet is held waiting on dependencies (deps enabled only).
  bool netrace_try_inject(nt_packet_t* trace_packet, std::vector<Packet*>& vecmess);
  void netrace_release_trace_packet(nt_packet_t* trace_packet);
};
