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
#include <optional>
#include "common/exception.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::optional<size_t> target_value;
  std::optional<frame_id_t> target_id;

  bool has_inf = false;
  for (auto &item : node_store_) {
    auto &node = item.second;
    if (!node->GetEvictable()) continue;

    if (has_inf) {
      if (!node->HasKHistory()) {
        size_t estamp = node->GetEarliestStamp();
        if (estamp < target_value) {
          target_value = estamp;
          target_id = node->GetFrameId();
        }
      }
    } else {
      if (!node->HasKHistory()) {
        has_inf = true;
        target_id = node->GetFrameId();
        target_value = node->GetEarliestStamp();
      } else {
        size_t distance = node->GetKDistance(current_timestamp_);
        if (!target_value.has_value() || distance > target_value) {
          target_value = distance;
          target_id = node->GetFrameId();
        }
      }
    }
  }

  if (target_id.has_value()) {
    *frame_id = *target_id;
    node_store_.erase(*frame_id);
    this->curr_size_ -= 1;
    return true;
  } else {
    return false;
  }
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  this->current_timestamp_ += 1;
  // scan should not disturb lru-k
  if (AccessType::Scan == access_type) return;
  if (AccessType::Get == access_type || AccessType::Unknown == access_type ) {
    auto iter = node_store_.find(frame_id);
    if (iter == node_store_.end()) {
      std::shared_ptr<LRUKNode> node = std::make_shared<LRUKNode>(frame_id, k_, false);
      node->RecordAccess(current_timestamp_);
      node_store_.emplace(frame_id, node);
    } else {
      iter->second->RecordAccess(current_timestamp_);
    }
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  auto iter = node_store_.find(frame_id);
  BUSTUB_ASSERT(iter != node_store_.end(), "Must set evictable for a valid existed frame");
  auto node = iter->second;
  if (node->GetEvictable() != set_evictable) {
    if (set_evictable)
      this->curr_size_ += 1;
    else
      this->curr_size_ -= 1;
  }
  node->SetEvictable(set_evictable);
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  auto iter = node_store_.find(frame_id);
  if (iter == node_store_.end()) return;
  if (iter->second->GetEvictable() == false) throw bustub::Exception("Remove a non-evictable frame!");
  curr_size_ -= 1;
  node_store_.erase(frame_id);
}

auto LRUKReplacer::Size() -> size_t { return curr_size_; }

bool LRUKNode::GetEvictable() const { return this->is_evictable_; }

void LRUKNode::SetEvictable(bool is_evictable) { is_evictable_ = is_evictable; }

void LRUKNode::RecordAccess(size_t timestamp) {
  history_.push_front(timestamp);
  if (history_.size() > k_) history_.pop_back();
}

size_t LRUKNode::GetKDistance(size_t curr_timestamp) const {
  if (history_.size() < k_) return INF;

  return curr_timestamp - history_.back();
}

size_t LRUKNode::GetEarliestStamp() const {
  BUSTUB_ASSERT(!this->history_.empty(), "");
  return history_.back();
}

bool LRUKNode::HasKHistory() const { return history_.size() == k_; }

frame_id_t LRUKNode::GetFrameId() const { return this->fid_; }

}  // namespace bustub
