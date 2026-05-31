#include "system.h"

#include "dragonfly_chiplet.h"
#include "dragonfly_sw.h"
#include "multiple_chip_mesh.h"
#include "multiple_chip_torus.h"
#include "single_chip_mesh.h"
#include "traffic_manager.h"
#include "basic_arbitrary.h"

System::System() {
  num_chips_ = 0;
  num_nodes_ = 0;
  num_cores_ = 0;

  // router parameters
  router_stages_ = param->router_stages;

  // simulation parameters
  timeout_time_ = param->timeout_threshold;
}

System* System::New(const std::string& topology) {
  System* sys_ptr;
  if (topology == "SingleChipMesh")
    sys_ptr = new SingleChipMesh;
  else if (topology == "MultiChipMesh")
    sys_ptr = new MultiChipMesh;
  else if (topology == "MultiChipTorus")
    sys_ptr = new MultiChipTorus;
  else if (topology == "DragonflySW")
    sys_ptr = new DragonflySW;
  else if (topology == "DragonflyChiplet")
    sys_ptr = new DragonflyChiplet;
  else if (topology == "BasicArbitrary")
    sys_ptr = new BasicArbitrary;
  else {
    std::cerr << "No such a topology!" << std::endl;
    return nullptr;
  }
  return sys_ptr;
}

void System::reset() {
  for (auto chip : chips_) {
    chip->reset();
  }
}

void System::process_pending_credits() {
  for (auto chip : chips_) {
    for (int node_id = 0; node_id < chip->number_nodes_; ++node_id) {
      Node* node = chip->get_node(node_id);
      for (auto* buf : node->in_buffers_) {
        buf->tick_pending_credits();
      }
    }
  }
}

void System::onestage(Packet& p) {
  if (p.candidate_channels_.empty()) routing(p);
  if (!p.candidate_channels_.empty() && p.next_vc_.buffer == nullptr)  // VC Allocating Stage
    vc_allocate(p);
  if (p.next_vc_.buffer != nullptr && p.switch_allocated_ == false)  // Switch Allocating Stage
    switch_allocate(p);
}

void System::twostage(Packet& p) {
  if (p.candidate_channels_.empty()) routing(p);
  if (!p.candidate_channels_.empty() && p.next_vc_.buffer == nullptr)  // VC Allocating Stage
    vc_allocate(p);
  else if (p.next_vc_.buffer != nullptr && p.switch_allocated_ == false)  // Switch Allocating Stage
    switch_allocate(p);
}

void System::Threestage(Packet& p) {
  if (p.candidate_channels_.empty())  // Routing Stage
    routing(p);
  else if (!p.candidate_channels_.empty() && p.next_vc_.buffer == nullptr)  // VC Allocating Stage
    vc_allocate(p);
  else if (p.next_vc_.buffer != nullptr && p.switch_allocated_ == false)  // Switch Allocating Stage
    switch_allocate(p);
}

void System::Fourstage(Packet& p) {
  if (p.candidate_channels_.empty())  // Routing Stage
    routing(p);
  else if (!p.candidate_channels_.empty() && p.next_vc_.buffer == nullptr)  // VC Allocating Stage
    vc_allocate(p);
  else if (p.next_vc_.buffer != nullptr && !p.crossbar_allocated_ && !p.switch_allocated_)
    switch_allocate(p);  // Switch Allocating Stage
  else if (p.crossbar_allocated_ && !p.switch_allocated_)  // Crossbar (st_final) Stage
    crossbar_allocate(p);
}

void System::routing(Packet& p) const {
  assert(p.candidate_channels_.empty());
  routing_algorithm(p);
  assert(!p.candidate_channels_.empty());
}

void System::vc_allocate(Packet& p) const {
  VCInfo current_vc = p.head_trace();
  if (current_vc.buffer == nullptr ||
      current_vc.head_packet() == &p) {  // the packet is at the source or at the front of the queue
    for (auto& vc : p.candidate_channels_) {
      if (vc.buffer->is_empty(vc.vcb) && vc.buffer->has_buffer(vc.vcb, p.length_)) {
        p.next_vc_ = vc;
        return;
      }
    }
  }
}

static bool grant_switch_and_link(Packet& p) {
  VCInfo current_vc = p.head_trace();
  if (!p.next_vc_.buffer->allocate_buffer(p.next_vc_.vcb, p.length_)) return false;
  if (current_vc.buffer == nullptr) {
    if (!p.next_vc_.buffer->allocate_in_link(p)) {
      p.next_vc_.buffer->release_buffer(p.next_vc_.vcb, p.length_);
      return false;
    }
    return true;
  }
  if (current_vc.head_packet() != &p) {
    p.next_vc_.buffer->release_buffer(p.next_vc_.vcb, p.length_);
    return false;
  }
  if (!current_vc.buffer->allocate_sw_link()) {
    p.next_vc_.buffer->release_buffer(p.next_vc_.vcb, p.length_);
    return false;
  }
  if (p.next_vc_.buffer->allocate_in_link(p)) return true;
  current_vc.buffer->release_sw_link();
  p.next_vc_.buffer->release_buffer(p.next_vc_.vcb, p.length_);
  return false;
}

void System::switch_allocate(Packet& p) {
  bool granted = false;
  if (router_stages_ == "FourStage") {
    // Switch-alloc delay only (BookSim sw_alloc cycle). No link/switch locks yet.
    VCInfo current_vc = p.head_trace();
    if (current_vc.buffer == nullptr) {
      granted = true;
    } else if (current_vc.head_packet() == &p) {
      granted = true;
    }
    if (granted) p.crossbar_allocated_ = true;
    return;
  }
  if (grant_switch_and_link(p)) p.switch_allocated_ = true;
}

void System::crossbar_allocate(Packet& p) {
  assert(p.crossbar_allocated_);
  // Crossbar stage (BookSim st_final): take switch + link same as ThreeStage, then depart.
  if (grant_switch_and_link(p)) {
    p.switch_allocated_ = true;
  } else {
    p.crossbar_allocated_ = false;
  }
}

void System::update(Packet& p) {
  // A packet cannot be sent to itself
  assert(p.link_timer_ > 0 || p.destination_ != p.tail_trace().id);

  p.trans_timer_++;
  if (p.wait_timer_ == timeout_time_)  // timeout
    TM->message_timeout_++;

  // Processing at source node before transmission (Packetization, injection, etc.)
  if (p.head_trace().id == p.source_ && p.process_timer_ > 0) {
    p.process_timer_--;
    return;
  }

  // Routing -> VC allocating -> Switch allocating -> Transmission
  // switch_allocated_ is the final credit for message forwarding.
  if (p.link_timer_ == 0) {                     // reach the input buffer
    if (p.head_trace().id != p.destination_) {  // not reach destination
      if (router_stages_ == "OneStage") {
        onestage(p);
      } else if (router_stages_ == "TwoStage") {
        twostage(p);
      } else if (router_stages_ == "ThreeStage") {
        Threestage(p);
      } else if (router_stages_ == "FourStage") {
        Fourstage(p);
      } else {
        std::cerr << "No such a microarchitecture!" << std::endl;
      }
      if (!p.switch_allocated_) p.wait_timer_++;
    }
  } else {  // flying in the link
    p.link_timer_--;
  }

  VCInfo temp1, temp2;
  int i = 0;

  if (p.switch_allocated_) {
    temp1 = p.next_vc_;
    p.wait_timer_ = 0;
    p.link_timer_ = p.next_vc_.buffer->channel_.latency;
#ifdef DEBUG
    TM->traffic_map_[temp1.buffer]++;
#endif  // DEBUG
    if (temp1.buffer->channel_ == on_chip_channel)
      p.internal_hops_++;
    else if (temp1.buffer->channel_ == off_chip_parallel_channel)
      p.parallel_hops_++;
    else if (temp1.buffer->channel_ == off_chip_serial_channel)
      p.serial_hops_++;
    else
      p.other_hops_++;
    p.candidate_channels_.clear();
    p.next_vc_ = VCInfo();
    p.switch_allocated_ = false;
    p.crossbar_allocated_ = false;
  } else {
    temp1 = p.head_trace();
    // find the flit that fall behind the head flit
    while (i < p.length_ && p.flit_trace_[i].id == temp1.id) i++;
  }

  if (i < p.length_) {  // there is flits fall behind
    temp2 = p.flit_trace_[i];
    int k = temp1.buffer->channel_.width;  // linkwidth
    int j = 0;
    while (i < p.length_) {
      if (p.flit_trace_[i].id == temp2.id && j < k) {
        assert(p.flit_trace_[i].id != temp1.id);
        p.flit_trace_[i] = temp1;
        j++;
      } else {
        if (p.flit_trace_[i].id != temp2.id) {
          temp1 = temp2;
          temp2 = p.flit_trace_[i];
          k = temp1.buffer->channel_.width;
          j = 0;
          assert(p.flit_trace_[i].id != temp1.id);
          p.flit_trace_[i] = temp1;
          j++;
        } else {
          assert(j == k);
        }
      }
      i++;
    }
    // If last flit shift, realease link
    if (temp2.id != p.tail_trace().id) {
      p.releaselink_ = true;
      p.leaving_vc_ = temp2;
    }
  }
  // If the last flit reach destination, delete message
  if (p.link_timer_ == 0 && p.tail_trace().id == p.destination_) {
    VCInfo dest_vc = p.tail_trace();
    dest_vc.buffer->release_buffer(dest_vc.vcb, p.length_);
    p.finished_ = true;
    if (p.trace_packet_ != nullptr) {
      TM->netrace_packet_arrived(p.trace_packet_);
      p.trace_packet_ = nullptr;
    }
    TM->message_arrived_++;
    TM->total_cycles_ += p.trans_timer_;
    TM->total_parallel_hops_ += p.parallel_hops_;
    TM->total_serial_hops_ += p.serial_hops_;
    TM->total_internal_hops_ += p.internal_hops_;
    TM->total_other_hops_ += p.other_hops_;
    return;
  }
}
