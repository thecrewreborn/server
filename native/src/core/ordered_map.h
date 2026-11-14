#pragma once

#include <list>
#include <string>
#include <unordered_map>
#include <utility>

namespace tcr {

template <class V>
class OrderedMap {
 public:
  using Entry = std::pair<std::string, V>;
  using List = std::list<Entry>;

  size_t size() const { return list_.size(); }
  bool empty() const { return list_.empty(); }
  typename List::iterator begin() { return list_.begin(); }
  typename List::iterator end() { return list_.end(); }
  typename List::const_iterator begin() const { return list_.begin(); }
  typename List::const_iterator end() const { return list_.end(); }
  Entry& front() { return list_.front(); }

  V* get(const std::string& key) {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &it->second->second;
  }
  const V* get(const std::string& key) const {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &it->second->second;
  }
  bool contains(const std::string& key) const { return index_.count(key) != 0; }
  V& set(const std::string& key, V v) {
    auto it = index_.find(key);
    if (it != index_.end()) {
      it->second->second = std::move(v);
      return it->second->second;
    }
    list_.emplace_back(key, std::move(v));
    index_[key] = std::prev(list_.end());
    return list_.back().second;
  }
  V& setdefault(const std::string& key) {
    auto it = index_.find(key);
    if (it != index_.end()) return it->second->second;
    return set(key, V());
  }
  bool erase(const std::string& key) {
    auto it = index_.find(key);
    if (it == index_.end()) return false;
    list_.erase(it->second);
    index_.erase(it);
    return true;
  }
  typename List::iterator erase(typename List::iterator it) {
    index_.erase(it->first);
    return list_.erase(it);
  }
  void move_to_end(const std::string& key) {
    auto it = index_.find(key);
    if (it != index_.end()) list_.splice(list_.end(), list_, it->second);
  }
  void pop_front() {
    index_.erase(list_.front().first);
    list_.pop_front();
  }
  void clear() {
    list_.clear();
    index_.clear();
  }

 private:
  List list_;
  std::unordered_map<std::string, typename List::iterator> index_;
};

}
