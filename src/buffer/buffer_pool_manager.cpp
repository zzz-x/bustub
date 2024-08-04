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

  // pin the page
  {
    replacer_->SetEvictable(frame_id,false);
    page.pin_count_+=1;
  }

  return &page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id, [[maybe_unused]] AccessType access_type) -> Page * {
  return nullptr;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  return false;
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
