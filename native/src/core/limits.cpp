#include "core/limits.h"

namespace tcr {

bool RateLimit::allow(const std::string& key, double now) {
  while (!times_.empty()) {
    const std::deque<double>& first = times_.front().second;
    if (!first.empty() && now - first.back() < window_ && times_.size() <= keys_) break;
    times_.pop_front();
  }
  std::deque<double>& times = times_.setdefault(key);
  times_.move_to_end(key);
  while (!times.empty() && now - times.front() >= window_) times.pop_front();
  if (static_cast<int64_t>(times.size()) >= count_) return false;
  times.push_back(now);
  return true;
}

bool LineLimit::allow(const std::string& key, double now, std::string* suppressed) {
  suppressed->clear();
  if (now - start_ >= window_) {
    if (left_out_) {
      *suppressed = label_ + ": " + std::to_string(left_out_) + " line(s) from unverified senders suppressed";
    }
    start_ = now;
    count_ = 0;
    left_out_ = 0;
  }
  while (!said_.empty() && now - said_.front().second >= window_) said_.pop_front();
  if (said_.contains(key) || count_ >= per_window_) {
    left_out_ += 1;
    return false;
  }
  said_.set(key, now);
  count_ += 1;
  return true;
}

}
