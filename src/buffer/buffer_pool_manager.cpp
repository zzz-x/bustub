//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"

#include "common/exception.h"
#include "common/macros.h"
#include "storage/page/page_guard.h"

namespace bustub {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // TODO(students): remove this line after you have implemented the buffer pool manager
  throw NotImplementedException(
      "BufferPoolManager is not implemented yet. If you have finished implementing BPM, please remove the throw "
      "exception line in `buffer_pool_manager.cpp`.");

  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page *
{ 
  //TODO： whether record this as an access to frame
  if(free_list_.empty() || replacer_->Size()==0)
    return nullptr;

  frame_id_t frame_id;
  //search a valid frame
  if(!free_list_.empty()){
    // first search in free list
    frame_id=free_list_.front();
    free_list_.pop_front();
  }
  else{
    // search evictable frames
    bool evict_flag=replacer_->Evict(&frame_id);
    if(!evict_flag)
      throw std::invalid_argument("no evictable frames");
    Page& replaced_page = pages_[frame_id];
    // write back if dirty
    if(replaced_page.IsDirty()){
      disk_manager_->WritePage(replaced_page.GetPageId(),replaced_page.GetData());
      replaced_page.is_dirty_=false;
    }
    // erase the record in page_table
    page_table_.erase(replaced_page.GetPageId());
  }

  // allocate a page id and record it in page_table
  *page_id=AllocatePage();
  page_table_.emplace(*page_id,frame_id);

  // reset the pages data
  Page& page=pages_[frame_id];
  page.ResetMemory();
  page.page_id_=*page_id;
  page.pin_count_ = 0;

  // pin and record the page
  replacer_->RecordAccess(frame_id,AccessType::Unknown);
  replacer_->SetEvictable(frame_id,false);
  page.pin_count_+=1;

  return &page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id, [[maybe_unused]] AccessType access_type) -> Page * {
  
  auto frame_iter=this->page_table_.find(page_id);
  frame_id_t frame_id;
  // search from buffer pool first
  if(frame_iter!=this->page_table_.end()){
    frame_id=frame_iter->second;
    //disable evict and record access
    if(access_type!=AccessType::Scan){
      replacer_->RecordAccess(frame_id,access_type);
    }
    replacer_->SetEvictable(frame_id,false);
    pages_[frame_id].pin_count_+=1;
    return &pages_[frame_id];
  }

  if(free_list_.empty() && replacer_->Size()==0)
    return nullptr;
  
  if(!free_list_.empty()){
    frame_id=free_list_.front();
    free_list_.pop_front();
  }
  else{
    bool evict_flag=replacer_->Evict(&frame_id);
    if(!evict_flag)
      throw std::invalid_argument("no evictable frames");
    Page& replaced_page=pages_[frame_id];
    //write back first if dirty
    if(replaced_page.IsDirty()){
      disk_manager_->WritePage(replaced_page.GetPageId(),replaced_page.GetData());
      replaced_page.is_dirty_=false;
    }
    page_table_.erase(replaced_page.GetPageId());
  }

  page_table_.emplace(page_id,frame_id);
  Page& page=pages_[frame_id];
  //TODO: reset operation may only do when evictable
  page.ResetMemory();
  page.page_id_=page_id;
  page.pin_count_=0;

  replacer_->RecordAccess(frame_id,AccessType::Unknown);
  replacer_->SetEvictable(frame_id,false);
  page.pin_count_+=1;

}   

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  auto iter=page_table_.find(page_id);
  // return false if page_id not in buffer or its pin count already been zero
  if(iter==page_table_.end() || pages_[iter->second].GetPinCount()<=0)
    return false;
  
  frame_id_t frame_id=iter->second;
  Page& page=pages_[frame_id];

  page.pin_count_-=1;

  if(page.GetPinCount()==0){
    replacer_->SetEvictable(frame_id,true);
  }
  
  page.is_dirty_=is_dirty;
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool { return false; }

void BufferPoolManager::FlushAllPages() {}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool { return false; }

auto BufferPoolManager::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManager::FetchPageBasic(page_id_t page_id) -> BasicPageGuard { return {this, nullptr}; }

auto BufferPoolManager::FetchPageRead(page_id_t page_id) -> ReadPageGuard { return {this, nullptr}; }

auto BufferPoolManager::FetchPageWrite(page_id_t page_id) -> WritePageGuard { return {this, nullptr}; }

auto BufferPoolManager::NewPageGuarded(page_id_t *page_id) -> BasicPageGuard { return {this, nullptr}; }

}  // namespace bustub
