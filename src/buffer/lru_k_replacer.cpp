//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include <algorithm>
#include <optional>
#include "common/exception.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  if (!nodes_without_k_.empty()) {
    // nodes without k 不为空，则先从这些node中选择
    for (auto node_iter = nodes_without_k_.begin(); node_iter != nodes_without_k_.end(); ++node_iter) {
      auto &node_item = *node_iter;
      if (!node_item->GetEvictable()) {
        continue;
      }
      *frame_id = node_item->GetFrameId();

      node_store_.erase(*frame_id);
      nodes_without_k_.erase(node_iter);
      iter_in_list_without_k_.erase(*frame_id);
      this->curr_size_ -= 1;
      return true;
    }
  }

  for (auto iter = nodes_with_k_.begin(); iter != nodes_with_k_.end(); ++iter) {
    if (!(*iter)->GetEvictable()) {
      continue;
    }
    *frame_id = (*iter)->GetFrameId();
    nodes_with_k_.erase(iter);
    node_store_.erase(*frame_id);
    this->curr_size_ -= 1;
    return true;
  }
  return false;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  // scan should not disturb lru-k
  if (AccessType::Scan == access_type) {
    return;
  }
  std::scoped_lock<std::mutex> lock(latch_);
  this->current_timestamp_ += 1;
  if (AccessType::Get == access_type || AccessType::Unknown == access_type) {
    auto iter = node_store_.find(frame_id);
    if (iter == node_store_.end()) {
      // 新node，直接移动到list_without_k的尾部，并记录位置
      std::shared_ptr<LRUKNode> node = std::make_shared<LRUKNode>(frame_id, k_, false);
      node->RecordAccess(current_timestamp_);
      node_store_.emplace(frame_id, node);
      // 如果是新node,直接移动到list尾部
      nodes_without_k_.push_back(node);
      iter_in_list_without_k_.emplace(frame_id, std::prev(nodes_without_k_.end(), 1));
    } else {
      auto node = iter->second;
      size_t history_size = node->GetHistorySize();
      if (history_size < k_ - 1) {
        // 如果已经在list_without_k中,访问记录即可，因为决定排序的是其最早被访问的时间
        // 访问记录
        iter->second->RecordAccess(current_timestamp_);
      } else {
        if (history_size == k_ - 1) {
          // 如果记录数量为k-1,则需要先从list_without_k中删掉当前node
          auto pos_iter = iter_in_list_without_k_.find(frame_id);
          BUSTUB_ENSURE(pos_iter != iter_in_list_without_k_.end(), "1");
          auto node_iter = pos_iter->second;
          // 删掉node
          nodes_without_k_.erase(node_iter);
          iter_in_list_without_k_.erase(frame_id);
        }
        {
          if (auto node_iter = nodes_with_k_.find(iter->second); node_iter != nodes_with_k_.end()) {
            nodes_with_k_.erase(node_iter);
          }
          // 访问记录，此时记录长度一定为k
          iter->second->RecordAccess(current_timestamp_);
          nodes_with_k_.emplace(iter->second);
        }
      }
    }
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);
  auto iter = node_store_.find(frame_id);
  BUSTUB_ASSERT(iter != node_store_.end(), "Must set evictable for a valid existed frame");
  auto node = iter->second;
  if (node->GetEvictable() != set_evictable) {
    if (set_evictable) {
      this->curr_size_ += 1;
    } else {
      this->curr_size_ -= 1;
    }
  }
  node->SetEvictable(set_evictable);
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);
  auto iter = node_store_.find(frame_id);
  if (iter == node_store_.end()) {
    return;
  }
  if (!iter->second->GetEvictable()) {
    throw bustub::Exception("Remove a non-evictable frame!");
  }
  size_t history_size = iter->second->GetHistorySize();
  if (history_size < k_) {
    nodes_without_k_.remove(iter->second);
    iter_in_list_without_k_.erase(frame_id);
  } else {
    nodes_with_k_.erase(iter->second);
  }
  curr_size_ -= 1;
  node_store_.erase(frame_id);
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  return curr_size_;
}

auto LRUKNode::GetEvictable() const -> bool { return this->is_evictable_; }

void LRUKNode::SetEvictable(bool is_evictable) { is_evictable_ = is_evictable; }

void LRUKNode::RecordAccess(size_t timestamp) {
  history_.push_front(timestamp);
  if (history_.size() > k_) {
    history_.pop_back();
  }
}

auto LRUKNode::GetKDistance(size_t curr_timestamp) const -> size_t {
  if (history_.size() < k_) {
    return INF;
  }

  return curr_timestamp - history_.back();
}

auto LRUKNode::GetEarliestStamp() const -> size_t {
  BUSTUB_ASSERT(!this->history_.empty(), "");
  return history_.back();
}

auto LRUKNode::HasKHistory() const -> bool { return history_.size() == k_; }

auto LRUKNode::GetFrameId() const -> frame_id_t { return this->fid_; }

auto LRUKNode::GetHistorySize() const -> size_t { return history_.size(); }

}  // namespace bustub
