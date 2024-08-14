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
  std::scoped_lock<std::mutex> lock(latch_);
  std::optional<size_t> target_value;
  std::optional<frame_id_t> target_id;
  bool has_inf = false;
  for (auto &item : node_store_) {
    auto &node = item.second;
    if (!node->GetEvictable()) {
      continue;
    }

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
  }

  return false;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  std::scoped_lock<std::mutex> lock(latch_);
  this->current_timestamp_ += 1;
  // scan should not disturb lru-k
  if (AccessType::Scan == access_type) {
    return;
  }
  if (AccessType::Get == access_type || AccessType::Unknown == access_type) {
    auto iter = node_store_.find(frame_id);
    if (iter == node_store_.end()) {
      std::shared_ptr<LRUKNode> node = std::make_shared<LRUKNode>(frame_id, k_, false);
      node->RecordAccess(current_timestamp_);
      node_store_.emplace(frame_id, node);
    } else {
      auto node = iter->second;
      size_t history_size=node->GetHistorySize();
      if(history_size<k_-1){
        //如果已经在list_without_k中，则先清除原来的位置
        auto pos_iter=iter_in_list_without_k.find(frame_id);
        if(pos_iter!=iter_in_list_without_k.end()){
          //list_without_k不为空才需要清除
          auto node_iter=pos_iter->second;
          nodes_without_k_.erase(node_iter);
          iter_in_list_without_k.erase(frame_id);
        }
        //移动到尾部，并记录位置
        nodes_without_k_.push_back(node);
        iter_in_list_without_k.emplace(frame_id,nodes_without_k_.end());
        //访问记录
        iter->second->RecordAccess(current_timestamp_);
      }
      else {
        if(history_size==k_-1){
          //如果记录数量为k-1,则需要先从list_without_k中删掉当前node
          auto pos_iter=iter_in_list_without_k.find(frame_id);
          BUSTUB_ENSURE(pos_iter!=iter_in_list_without_k.end(),"");
          auto node_iter=pos_iter->second;
          //删掉node
          nodes_without_k_.erase(node_iter);
          iter_in_list_without_k.erase(frame_id);
          //访问记录，此时记录变为了K
          iter->second->RecordAccess(current_timestamp_);
        }
        //下面要移动到list_with_k
        size_t k_distance = iter->second->GetKDistance(current_timestamp_);
        //如果已经在k_list中，则先删掉，对于从list_without_k移动到list_with_k的情况，不会触发
        auto pos_iter=iter_in_list_with_k.find(frame_id);
        if(pos_iter!=iter_in_list_with_k.end()){
          nodes_with_k_.erase(nodes_with_k_.begin()+pos_iter->second);
          iter_in_list_with_k.erase(frame_id);
        }
        //从大到小排序，找第一个更小的插入位置
        auto insert_iter=std::lower_bound(nodes_with_k_.begin(),nodes_with_k_.end(),[k_distance,this](auto item){
          return item > item->GetKDistance(this->current_timestamp_);
        });
        //更新idx
        iter_in_list_with_k[frame_id]=insert_iter-nodes_with_k_.begin();
        //再插入到排好序的数组中
        nodes_with_k_.insert(insert_iter,iter->second);
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

auto LRUKNode::GetHistorySize() const ->size_t { return history_.size(); }

}  // namespace bustub
