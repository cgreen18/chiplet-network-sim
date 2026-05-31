#include "packet.h"

Packet::Packet(NodeID source, NodeID destination, int length, nt_packet_t* trace_packet) {
    source_ = source;
    destination_ = destination;
    length_ = length;
    trace_packet_ = trace_packet;
    interleaving_tag_ = 0;
    flit_trace_.reserve(length_);
    for (int i = 0; i < length_; i++) {
        flit_trace_.push_back(VCInfo(nullptr, 0, source_));
    }
    process_timer_ = param->processing_time;
    SA_timer_ = 0;
    link_timer_ = 0;
    candidate_channels_.clear();
    next_vc_ = VCInfo();
    switch_allocated_ = false;
    crossbar_allocated_ = false;
    trans_timer_ = 0;
    wait_timer_ = 0;
    internal_hops_ = 0;
    parallel_hops_ = 0;
    serial_hops_ = 0;
    other_hops_ = 0;
    finished_ = false;
    releaselink_ = false;
}
