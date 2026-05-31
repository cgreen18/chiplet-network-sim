#include "buffer.h"

#include "packet.h"

extern Parameters* param;

VCInfo::VCInfo(Buffer* buffer_, int vc_, NodeID id_) {
  buffer = buffer_;
  vcb = vc_;
  if (buffer_ != nullptr)
    id = buffer->node_->id_;
  else
    id = id_;
}

Packet* VCInfo::head_packet() const { return buffer->head_packet(vcb); }

Buffer::Buffer() {
  node_ = nullptr;
  buffer_size_ = 0;
  vc_num_ = 0;
  in_link_used_.store(false);
  sw_link_used_.store(false);
  vc_buffer_ = nullptr;
  vc_queue_ = nullptr;
  vc_head_packet = nullptr;
  pending_credits_ = nullptr;
}

Buffer::Buffer(Node* node, int vc_num, int buffer_size, Channel channel) {
  node_ = node;
  buffer_size_ = buffer_size;
  vc_num_ = vc_num;
  channel_ = channel;
  in_link_used_.store(false);
  sw_link_used_.store(false);
  vc_buffer_ = new std::atomic_int[vc_num_];
  vc_queue_ = new std::queue<Packet*>[vc_num_];
  vc_head_packet = new std::atomic<Packet*>[vc_num_];
  pending_credits_ = new std::deque<PendingCredit>[vc_num_];
  for (int i = 0; i < vc_num_; ++i) {
    vc_buffer_[i].store(buffer_size);
    vc_queue_[i] = std::queue<Packet*>();
    vc_head_packet[i].store(nullptr);
  }
}

Buffer::~Buffer() {
  delete[] vc_buffer_;
  delete[] vc_queue_;
  delete[] vc_head_packet;
  delete[] pending_credits_;
}

int Buffer::router_pipeline_latency() const {
  if (param->router_stages == "FourStage") return 4;
  if (param->router_stages == "ThreeStage") return 3;
  if (param->router_stages == "TwoStage") return 2;
  if (param->router_stages == "OneStage") return 1;
  return 3;
}

int Buffer::credit_return_latency() const {
  if (param->credit_delay <= 0) return 0;
  // Router credit processing delay only; link flight is modeled by deferring
  // release_buffer until releaselink (after channel_.latency cycles).
  return param->credit_delay;
}

bool Buffer::has_buffer(int vcb, int n) const {
  return vc_buffer_[vcb].load() >= n;
}

void Buffer::apply_buffer_credit(int vcb, int n) {
  int buffer = vc_buffer_[vcb].load();
  while (!vc_buffer_[vcb].compare_exchange_weak(buffer, buffer + n))
    ;
  assert(vc_buffer_[vcb].load() <= buffer_size_);
}

bool Buffer::allocate_buffer(int vcb, int n) {
  int buffer = vc_buffer_[vcb].load();
  while (true) {
    if (buffer < n)
      return false;
    else if (vc_buffer_[vcb].compare_exchange_weak(buffer, buffer - n))
      return true;
  }
}

void Buffer::release_buffer(int vcb, int n) {
  const int delay = credit_return_latency();
  if (delay <= 0) {
    apply_buffer_credit(vcb, n);
    return;
  }
  std::lock_guard<std::mutex> lock(credit_mutex_);
  pending_credits_[vcb].push_back(PendingCredit{delay, n});
}

void Buffer::tick_pending_credits() {
  if (credit_return_latency() <= 0) return;
  std::lock_guard<std::mutex> lock(credit_mutex_);
  for (int vcb = 0; vcb < vc_num_; ++vcb) {
    auto& q = pending_credits_[vcb];
    for (auto& pending : q) {
      if (pending.cycles_left > 0) --pending.cycles_left;
    }
    while (!q.empty() && q.front().cycles_left <= 0) {
      apply_buffer_credit(vcb, q.front().amount);
      q.pop_front();
    }
  }
}

bool Buffer::allocate_in_link(Packet& p) {
  int vcb = p.next_vc_.vcb;
  bool link_used_state = in_link_used_.load();
  if (link_used_state)
    return false;
  else if (in_link_used_.compare_exchange_strong(link_used_state, true)) {
    if (node_->id_ != p.destination_) {
      push_pkt(&p, vcb);
    }
    return true;
  } else
    return false;
}

void Buffer::release_in_link(Packet& p) {
  assert(in_link_used_);
  if (p.leaving_vc_.buffer != nullptr) {
    assert(p.leaving_vc_.head_packet() == &p);
    p.leaving_vc_.buffer->pop_pkt(p.leaving_vc_.vcb);
  }
  in_link_used_.store(false);
}

bool Buffer::allocate_sw_link() {
  bool link_used_state = sw_link_used_.load();
  if (link_used_state)
    return false;
  else if (sw_link_used_.compare_exchange_strong(link_used_state, true)) {
    return true;
  } else
    return false;
}

void Buffer::release_sw_link() {
  assert(sw_link_used_);
  sw_link_used_.store(false);
}

void Buffer::reset() {
  in_link_used_.store(false);
  sw_link_used_.store(false);
  std::lock_guard<std::mutex> lock(credit_mutex_);
  for (int i = 0; i < vc_num_; ++i) {
    vc_buffer_[i].store(buffer_size_);
    while (!vc_queue_[i].empty()) vc_queue_[i].pop();
    vc_head_packet[i].store(nullptr);
    pending_credits_[i].clear();
  }
}
