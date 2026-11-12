// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#ifndef OPEN_BROADCAST_ENCODER_SOURCE_STATS_PEER_TABLE_H
#define OPEN_BROADCAST_ENCODER_SOURCE_STATS_PEER_TABLE_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Per-peer RIST display state (4c).
//
// librist's sender stats callback fires once per PEER per interval and carries
// the peer's identity (`peer_id`, `cname`), but the encoder used to render each
// sample straight into the panes, so with two or more peers every pane
// alternated between them once a second. This keeps the latest sample per
// identity and derives a display aggregate from that set.
//
// Two rules this module is deliberately NOT allowed to break:
//  - It never feeds the ABR. The bitrate decision keeps librist's own
//  per-sample
//    quality and its window mean (bitrate_scale.cpp); "worst leg" is a DISPLAY
//    rule only.
//  - Row order is by peer_id, so a UI redraw never reorders the legs.
namespace peer_stats
{
struct sample
{
  uint32_t peer_id = 0;
  std::string cname;
  double quality = 0.0;
  uint32_t rtt_ms = 0;
  uint64_t bandwidth = 0;
  uint64_t sent = 0;
  uint64_t retransmitted = 0;
};

struct entry
{
  uint32_t peer_id = 0;
  std::string label;
  double quality = 0.0;
  uint32_t rtt_ms = 0;
  uint64_t bandwidth = 0;
  uint64_t sent = 0;
  uint64_t retransmitted = 0;
  int64_t seen_ms = 0;
};

// One stable figure per display column: sums for the additive counters, the
// WORST leg for quality and RTT (a bond is only as good as its worst link).
struct aggregate
{
  bool any = false;
  std::size_t peers = 0;
  double quality = 0.0;
  uint32_t rtt_ms = 0;
  uint64_t bandwidth = 0;
  uint64_t sent = 0;
  uint64_t retransmitted = 0;
};

// "cable-au" or "peer 3" when librist reported no cname for the leg.
inline auto label_for(uint32_t peer_id, const std::string& cname) -> std::string
{
  if (!cname.empty()) {
    return cname;
  }
  return "peer " + std::to_string(peer_id);
}

class registry
{
public:
  // Record one peer's sample. A sample with peer_id 0 (an older librist that
  // does not identify the peer) is still keyed, under id 0, rather than
  // dropped: one row is the truth for a single-leg session.
  void observe(const sample& s, int64_t now_ms)
  {
    for (entry& e : rows_) {
      if (e.peer_id == s.peer_id) {
        assign(e, s, now_ms);
        return;
      }
    }
    entry e;
    e.peer_id = s.peer_id;
    assign(e, s, now_ms);
    rows_.push_back(std::move(e));
    std::sort(rows_.begin(),
              rows_.end(),
              [](const entry& a, const entry& b)
              { return a.peer_id < b.peer_id; });
  }

  // Peers that stopped reporting are dropped instead of frozen on screen: a
  // muted or dead leg must disappear, not show its last healthy figures.
  void prune(int64_t now_ms, int64_t stale_ms = 5000)
  {
    rows_.erase(std::remove_if(rows_.begin(),
                               rows_.end(),
                               [now_ms, stale_ms](const entry& e)
                               { return now_ms - e.seen_ms > stale_ms; }),
                rows_.end());
  }

  [[nodiscard]] auto peers() const -> const std::vector<entry>&
  {
    return rows_;
  }

  [[nodiscard]] auto total() const -> aggregate
  {
    aggregate a;
    for (const entry& e : rows_) {
      if (!a.any) {
        a.any = true;
        a.quality = e.quality;
        a.rtt_ms = e.rtt_ms;
      } else {
        a.quality = std::min(a.quality, e.quality);
        a.rtt_ms = std::max(a.rtt_ms, e.rtt_ms);
      }
      a.bandwidth += e.bandwidth;
      a.sent += e.sent;
      a.retransmitted += e.retransmitted;
      ++a.peers;
    }
    return a;
  }

  void clear() { rows_.clear(); }

  [[nodiscard]] auto size() const -> std::size_t { return rows_.size(); }

private:
  static void assign(entry& e, const sample& s, int64_t now_ms)
  {
    e.label = label_for(s.peer_id, s.cname);
    e.quality = s.quality;
    e.rtt_ms = s.rtt_ms;
    e.bandwidth = s.bandwidth;
    e.sent = s.sent;
    e.retransmitted = s.retransmitted;
    e.seen_ms = now_ms;
  }

  std::vector<entry> rows_;
};

// One pane line per peer. Fixed field order so a reader can compare legs down
// the column: label, quality, RTT, bandwidth, packets, retransmits. The label
// is padded to a common width because the row is rendered in a monospace font.
inline auto format_row(const entry& e) -> std::string
{
  std::string label = e.label;
  constexpr std::size_t kLabelWidth = 12;
  if (label.size() < kLabelWidth) {
    label.append(kLabelWidth - label.size(), ' ');
  }
  return label + "Q " + std::to_string(static_cast<int>(e.quality)) + "%   "
      + std::to_string(e.rtt_ms) + " ms   " + std::to_string(e.bandwidth / 1000)
      + " Mb   " + std::to_string(e.sent) + " pkt   "
      + std::to_string(e.retransmitted) + " retx";
}
}  // namespace peer_stats

#endif  // OPEN_BROADCAST_ENCODER_SOURCE_STATS_PEER_TABLE_H
