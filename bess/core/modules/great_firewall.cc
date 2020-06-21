// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "great_firewall.h"

#include <algorithm>
#include <tuple>

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/http_parser.h"
#include "../utils/ip.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Tcp;
using bess::utils::be16_t;

const uint64_t TIME_OUT_NS = 10ull * 1000 * 1000 * 1000;  // 10 seconds

const Commands GreatFirewall::cmds = {
    {"get_initial_arg", "EmptyArg", MODULE_CMD_FUNC(&GreatFirewall::GetInitialArg),
     Command::THREAD_SAFE},
    {"add", "GreatFirewallArg", MODULE_CMD_FUNC(&GreatFirewall::CommandAdd),
     Command::THREAD_UNSAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&GreatFirewall::CommandClear),
     Command::THREAD_UNSAFE}};

// Template for generating TCP packets without data
struct[[gnu::packed]] PacketTemplate {
  Ethernet eth;
  Ipv4 ip;
  Tcp tcp;

  PacketTemplate() {
    eth.dst_addr = Ethernet::Address();  // TBD
    eth.src_addr = Ethernet::Address();  // TBD
    eth.ether_type = be16_t(Ethernet::Type::kIpv4);
    ip.version = 4;
    ip.header_length = 5;
    ip.type_of_service = 0;
    ip.length = be16_t(40);
    ip.id = be16_t(0);  // TBD
    ip.fragment_offset = be16_t(0);
    ip.ttl = 0x40;
    ip.protocol = Ipv4::Proto::kTcp;
    ip.checksum = 0;           // TBD
    ip.src = be32_t(0);        // TBD
    ip.dst = be32_t(0);        // TBD
    tcp.src_port = be16_t(0);  // TBD
    tcp.dst_port = be16_t(0);  // TBD
    tcp.seq_num = be32_t(0);   // TBD
    tcp.ack_num = be32_t(0);   // TBD
    tcp.reserved = 0;
    tcp.offset = 5;
    tcp.flags = Tcp::Flag::kAck | Tcp::Flag::kRst;
    tcp.window = be16_t(0);
    tcp.checksum = 0;  // TBD
    tcp.urgent_ptr = be16_t(0);
  }
};

static const char HTTP_HEADER_HOST[] = "Host";
static const char HTTP_403_BODY[] =
    "HTTP/1.1 403 母湯喔\r\nConnection: Closed\r\n\r\n";

static PacketTemplate rst_template;

// Generate an HTTP 403 packet
inline static bess::Packet *Generate403Packet(const Ethernet::Address &src_eth,
                                              const Ethernet::Address &dst_eth,
                                              be32_t src_ip, be32_t dst_ip,
                                              be16_t src_port, be16_t dst_port,
                                              be32_t seq, be32_t ack) {
  bess::Packet *pkt = current_worker.packet_pool()->Alloc();
  char *ptr = static_cast<char *>(pkt->buffer()) + SNBUF_HEADROOM;
  pkt->set_data_off(SNBUF_HEADROOM);

  constexpr size_t len = sizeof(HTTP_403_BODY) - 1;
  pkt->set_total_len(sizeof(rst_template) + len);
  pkt->set_data_len(sizeof(rst_template) + len);

  bess::utils::Copy(ptr, &rst_template, sizeof(rst_template));
  bess::utils::Copy(ptr + sizeof(rst_template), HTTP_403_BODY, len);

  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  // We know there is no IP option
  Tcp *tcp = reinterpret_cast<Tcp *>(ip + 1);

  eth->dst_addr = dst_eth;
  eth->src_addr = src_eth;
  ip->id = be16_t(1);  // assumes the SYN packet used ID 0
  ip->src = src_ip;
  ip->dst = dst_ip;
  ip->length = be16_t(40 + len);
  tcp->src_port = src_port;
  tcp->dst_port = dst_port;
  tcp->seq_num = seq;
  tcp->ack_num = ack;
  tcp->flags = Tcp::Flag::kAck;

  tcp->checksum = bess::utils::CalculateIpv4TcpChecksum(*tcp, src_ip, dst_ip,
                                                        sizeof(*tcp) + len);
  ip->checksum = bess::utils::CalculateIpv4NoOptChecksum(*ip);

  return pkt;
}

// Generate a TCP RST packet
inline static bess::Packet *GenerateResetPacket(
    const Ethernet::Address &src_eth, const Ethernet::Address &dst_eth,
    be32_t src_ip, be32_t dst_ip, be16_t src_port, be16_t dst_port, be32_t seq,
    be32_t ack) {
  bess::Packet *pkt = current_worker.packet_pool()->Alloc();
  char *ptr = static_cast<char *>(pkt->buffer()) + SNBUF_HEADROOM;
  pkt->set_data_off(SNBUF_HEADROOM);
  pkt->set_total_len(sizeof(rst_template));
  pkt->set_data_len(sizeof(rst_template));

  bess::utils::Copy(ptr, &rst_template, sizeof(rst_template));

  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  // We know there is no IP option
  Tcp *tcp = reinterpret_cast<Tcp *>(ip + 1);

  eth->dst_addr = dst_eth;
  eth->src_addr = src_eth;
  ip->id = be16_t(2);  // assumes the 403 used ID 1
  ip->src = src_ip;
  ip->dst = dst_ip;
  tcp->src_port = src_port;
  tcp->dst_port = dst_port;
  tcp->seq_num = seq;
  tcp->ack_num = ack;

  tcp->checksum =
      bess::utils::CalculateIpv4TcpChecksum(*tcp, src_ip, dst_ip, sizeof(*tcp));
  ip->checksum = bess::utils::CalculateIpv4NoOptChecksum(*ip);

  return pkt;
}

CommandResponse GreatFirewall::Init(const bess::pb::GreatFirewallArg &arg) {
  for (const auto &word : arg.keywords()) {
    keywords_.insert(word);
  }
  return CommandSuccess();
}

CommandResponse GreatFirewall::CommandAdd(const bess::pb::GreatFirewallArg &arg) {
  Init(arg);
  return CommandSuccess();
}

CommandResponse GreatFirewall::CommandClear(const bess::pb::EmptyArg &) {
  keywords_.clear();
  return CommandSuccess();
}

CommandResponse GreatFirewall::GetInitialArg(const bess::pb::EmptyArg &) {
  bess::pb::GreatFirewallArg resp;
  return CommandSuccess(resp);
}

void GreatFirewall::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  gate_idx_t igate = ctx->current_igate;

  // Pass reverse traffic
  if (igate == 1) {
    RunChooseModule(ctx, 1, batch);
    return;
  }

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

    if (ip->protocol != Ipv4::Proto::kTcp) {
      EmitPacket(ctx, pkt, 0);
      continue;
    }

    int ip_bytes = ip->header_length << 2;
    Tcp *tcp =
        reinterpret_cast<Tcp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    Flow flow;
    flow.src_ip = ip->src;
    flow.dst_ip = ip->dst;
    flow.src_port = tcp->src_port;
    flow.dst_port = tcp->dst_port;

    uint64_t now = ctx->current_ns;

    // Find existing flow, if we have one.
    std::unordered_map<Flow, FlowRecord, FlowHash>::iterator it =
        flow_cache_.find(flow);

    if (it != flow_cache_.end()) {
      if (now >= it->second.ExpiryTime()) {
        // Discard old flow and start over.
        flow_cache_.erase(it);
        it = flow_cache_.end();
      } else if (it->second.IsAnalyzed()) {
        // Once we're finished analyzing, we only record *blocked* flows.
        // Continue blocking this flow for TIME_OUT_NS more ns.
        it->second.SetExpiryTime(now + TIME_OUT_NS);
        DropPacket(ctx, pkt);
        continue;
      }
    }

    if (it == flow_cache_.end()) {
      // Don't have a flow, or threw an aged one out.  If there's no
      // SYN in this packet the reconstruct code will fail.  This is
      // a common case (for any flow that got analyzed and allowed);
      // skip a pointless emplace/erase pair for such packets.
      if (tcp->flags & Tcp::Flag::kSyn) {
        std::tie(it, std::ignore) = flow_cache_.emplace(
            std::piecewise_construct, std::make_tuple(flow), std::make_tuple());
      } else {
        EmitPacket(ctx, pkt, 0);
        continue;
      }
    }

    FlowRecord &record = it->second;
    TcpFlowReconstruct &buffer = record.GetBuffer();

    // If the reconstruct code indicates failure, treat this
    // as a flow to pass.  Note: we only get failure if there is
    // something seriously wrong; we get success if there are holes
    // in the data (in which case the contiguous_len() below is short).
    bool success = buffer.InsertPacket(pkt);
    if (!success) {
      VLOG(1) << "Reconstruction failure";
      flow_cache_.erase(it);
      EmitPacket(ctx, pkt, 0);
      continue;
    }

    // Have something on this flow; keep it alive for a while longer.
    record.SetExpiryTime(now + TIME_OUT_NS);

    // We are by definition still analyzing.  See if we can determine
    // the final disposition of this flow.
    bool matched = false;
    struct phr_header headers[16];
    size_t num_headers = 16, method_len, path_len;
    int minor_version;
    const char *method, *path;
    int parse_result = phr_parse_request(
        buffer.buf(), buffer.contiguous_len(), &method, &method_len, &path,
        &path_len, &minor_version, headers, &num_headers, 0);

    // -2 means incomplete
    if (parse_result > 0 || parse_result == -2) {
      const std::string path_str(path, path_len);

      // Look for the Host header
      for (size_t j = 0; j < num_headers && !matched; ++j) {
        if (strncmp(headers[j].name, HTTP_HEADER_HOST, headers[j].name_len) ==
            0) {
          const std::string host(headers[j].value, headers[j].value_len);
          std::cout << "host: " << host << std::endl;
          std::cout << "path: " << path << std::endl;
        }
      }
      // check if any word matches in keywords_
      for (std::set<std::string>::iterator it=keywords_.begin(); it!=keywords_.end();it++) {
        if (path_str.find(*it) != std::string::npos) matched = true;
      }
    }

    if (!matched) {
      EmitPacket(ctx, pkt, 0);

      // Once FIN is observed, or we've seen all the headers and decided
      // to pass the flow, there is no more need to reconstruct the flow.
      // NOTE: if FIN is lost on its way to destination, this will simply pass
      // the retransmitted packet.
      if (parse_result != -2 || (tcp->flags & Tcp::Flag::kFin)) {
        flow_cache_.erase(it);
      }
    } else {
      // No need to keep reconstructing, just mark it as analyzed
      // (and hence blocked).
      it->second.SetAnalyzed();

      // Inject RST to destination
      EmitPacket(ctx, GenerateResetPacket(eth->src_addr, eth->dst_addr, ip->src,
                                          ip->dst, tcp->src_port, tcp->dst_port,
                                          tcp->seq_num, tcp->ack_num),
                 0);

      // Inject 403 to source. 403 should arrive earlier than RST.
      EmitPacket(ctx, Generate403Packet(eth->dst_addr, eth->src_addr, ip->dst,
                                        ip->src, tcp->dst_port, tcp->src_port,
                                        tcp->ack_num, tcp->seq_num),
                 1);

      // Inject RST to source
      EmitPacket(ctx, GenerateResetPacket(
                          eth->dst_addr, eth->src_addr, ip->dst, ip->src,
                          tcp->dst_port, tcp->src_port,
                          be32_t(tcp->ack_num.value() + strlen(HTTP_403_BODY)),
                          tcp->seq_num),
                 1);

      // Drop the data packet
      DropPacket(ctx, pkt);
    }
  }
}

std::string GreatFirewall::GetDesc() const {
  return bess::utils::Format("%zu hosts", keywords_.size());
}

ADD_MODULE(GreatFirewall, "url-filter", "Filter HTTP connection")
