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

// DiskRequest

DiskRequest::DiskRequest() : is_write_(false), page_id_(-1), data_(nullptr) {}

DiskRequest::DiskRequest(bool is_write, page_id_t page_id, char *data) : is_write_(is_write), page_id_(page_id) {
  data_ = new char[BUSTUB_PAGE_SIZE];
  memcpy(data_, data, sizeof(char) * BUSTUB_PAGE_SIZE);
}

DiskRequest::~DiskRequest() { delete[] data_; }

DiskRequest::DiskRequest(const DiskRequest &other) : is_write_(other.is_write_), page_id_(other.page_id_) {
  data_ = new char[BUSTUB_PAGE_SIZE];
  memcpy(data_, other.data_, sizeof(char) * BUSTUB_PAGE_SIZE);
}

DiskRequest::DiskRequest(DiskRequest &&other) noexcept : is_write_(other.is_write_), page_id_(other.page_id_) {
  data_ = other.data_;
  other.data_ = nullptr;
}

auto DiskRequest::operator=(const DiskRequest &other) -> DiskRequest & {
  if (this == &other) {
    return *this;
  }
  is_write_ = other.is_write_;
  page_id_ = other.page_id_;
  delete[] data_;
  data_ = new char[BUSTUB_PAGE_SIZE];
  memcpy(data_, other.data_, sizeof(char) * BUSTUB_PAGE_SIZE);

  return *this;
}

auto DiskRequest::operator=(DiskRequest &&other) noexcept -> DiskRequest & {
  if (this == &other) {
    return *this;
  }
  is_write_ = other.is_write_;
  page_id_ = other.page_id_;
  delete[] data_;
  data_ = other.data_;
  other.data_ = nullptr;
  return *this;
}

// Page Scheduler
PageScheduler::PageScheduler(DiskManager *manager) : disk_manager_(manager) {
  cache_valid_ = false;
  cache_data_ = new char[sizeof(char) * BUSTUB_PAGE_SIZE];
  background_thread_.emplace([&]() { BackGroundWork(); });
}

PageScheduler::~PageScheduler() {
  if (background_thread_.has_value()) {
    disk_channel_.Put(std::nullopt);
    background_thread_->join();
  }
  cache_valid_ = false;
  delete[] cache_data_;
}

void PageScheduler::Schedule(const DiskRequest &request) { disk_channel_.Put(request); }

void PageScheduler::BackGroundWork() {
  // 消费者
  while (true) {
    auto request = disk_channel_.Get();
    if (request == std::nullopt) {
      break;
    }
    if (request->is_write_) {
      disk_manager_->WritePage(request->page_id_, request->data_);
    } else {
      disk_manager_->ReadPage(request->page_id_, request->data_);
    }
    if (disk_channel_.Size() == 0) {
      memcpy(cache_data_, request->data_, sizeof(char) * BUSTUB_PAGE_SIZE);
      cache_valid_ = true;
    }
  }
}

auto PageScheduler::GetRequestSize() -> size_t { return disk_channel_.Size(); }

auto PageScheduler::GetLastRequest() -> std::optional<DiskRequest> {
  std::optional<DiskRequest> ret;
  disk_channel_.GetLastElem(ret);
  return ret;
}

// DiskManagerProxy
DiskManagerProxy::DiskManagerProxy(DiskManager *disk_manager) : disk_manager_(disk_manager) {}

void DiskManagerProxy::WriteToDisk(const DiskRequest &r) {
  std::shared_ptr<PageScheduler> scheduler;
  {
    std::unique_lock lock(lock_);
    auto iter = request_map_.find(r.page_id_);
    if (iter == request_map_.end()) {
      scheduler = std::make_shared<PageScheduler>(disk_manager_);
      request_map_.emplace(r.page_id_, scheduler);
    } else {
      scheduler = iter->second;
    }
    // 写之后cache就是无效的了
    scheduler->cache_valid_ = false;
  }
  scheduler->Schedule(r);
}

void DiskManagerProxy::ReadFromDisk(page_id_t page_id, char *data) {
  std::unique_lock lock(lock_);
  auto iter = request_map_.find(page_id);
  if (iter == request_map_.end()) {
    disk_manager_->ReadPage(page_id, data);
  } else {
    if (iter->second->cache_valid_) {
      memcpy(data, iter->second->cache_data_, sizeof(char) * BUSTUB_PAGE_SIZE);
      return;
    }
    auto last_request = iter->second->GetLastRequest();
    if (!last_request.has_value()) {
      disk_manager_->ReadPage(page_id, data);
    } else {
      memcpy(data, last_request->data_, BUSTUB_PAGE_SIZE * sizeof(char));
    }
  }
}

}  // namespace bustub

namespace bustub {

// only reset the page when use this page

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);

  disk_proxy_ = std::make_unique<DiskManagerProxy>(disk_manager);
  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() {
  disk_proxy_->Clear();
  delete[] pages_;
}

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);
  if (free_list_.empty() && replacer_->Size() == 0) {
    return nullptr;
  }

  frame_id_t frame_id;
  // search a valid frame
  if (!free_list_.empty()) {
    // first search in free list
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    // search evictable frames
    bool evict_flag = replacer_->Evict(&frame_id);
    if (!evict_flag) {
      throw std::invalid_argument("no evictable frames");
    }
    Page &replaced_page = pages_[frame_id];
    // write back if dirty
    if (replaced_page.IsDirty()) {
      DiskRequest r(true, replaced_page.GetPageId(), replaced_page.GetData());
      disk_proxy_->WriteToDisk(r);
      replaced_page.is_dirty_ = false;
    }
    // erase the record in page_table
    page_table_.erase(replaced_page.GetPageId());
  }

  // allocate a page id and record it in page_table
  *page_id = AllocatePage();
  page_table_.emplace(*page_id, frame_id);

  // reset the pages data
  Page &page = pages_[frame_id];
  page.ResetMemory();
  page.page_id_ = *page_id;
  page.pin_count_ = 0;

  // pin and record the page
  replacer_->RecordAccess(frame_id, AccessType::Unknown);
  replacer_->SetEvictable(frame_id, false);
  page.pin_count_ += 1;

  return &page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id, [[maybe_unused]] AccessType access_type) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);
  auto frame_iter = this->page_table_.find(page_id);
  frame_id_t frame_id;
  // search from buffer pool first
  if (frame_iter != this->page_table_.end()) {
    frame_id = frame_iter->second;
    // disable evict and record access
    if (access_type != AccessType::Scan) {
      replacer_->RecordAccess(frame_id, access_type);
    }
    replacer_->SetEvictable(frame_id, false);
    pages_[frame_id].pin_count_ += 1;
    return &pages_[frame_id];
  }

  if (free_list_.empty() && replacer_->Size() == 0) {
    return nullptr;
  }

  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    bool evict_flag = replacer_->Evict(&frame_id);
    if (!evict_flag) {
      throw std::invalid_argument("no evictable frames");
    }
    Page &replaced_page = pages_[frame_id];
    // write back first if dirty
    if (replaced_page.IsDirty()) {
      DiskRequest r(true, replaced_page.GetPageId(), replaced_page.GetData());
      disk_proxy_->WriteToDisk(r);
      replaced_page.is_dirty_ = false;
    }
    page_table_.erase(replaced_page.GetPageId());
  }

  page_table_.emplace(page_id, frame_id);
  Page &page = pages_[frame_id];
  // TODO(myself): reset operation may only do when evictable
  page.ResetMemory();
  page.page_id_ = page_id;
  page.pin_count_ = 0;

  disk_proxy_->ReadFromDisk(page_id, page.GetData());

  replacer_->RecordAccess(frame_id, AccessType::Unknown);
  replacer_->SetEvictable(frame_id, false);
  page.pin_count_ += 1;

  return &page;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  auto iter = page_table_.find(page_id);
  // return false if page_id not in buffer or its pin count already been zero
  if (iter == page_table_.end() || pages_[iter->second].GetPinCount() <= 0) {
    return false;
  }

  frame_id_t frame_id = iter->second;
  Page &page = pages_[frame_id];

  page.pin_count_ -= 1;

  if (page.GetPinCount() == 0) {
    replacer_->SetEvictable(frame_id, true);
  }

  // 仅当需要设置为dirty时，才需要覆盖
  if (is_dirty) {
    page.is_dirty_ = is_dirty;
  }
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::unique_lock<std::mutex> lock(latch_);
  BUSTUB_ASSERT(page_id != INVALID_PAGE_ID, "page id should be valid");
  auto frame_iter = page_table_.find(page_id);
  if (frame_iter == page_table_.end()) {
    return false;
  }
  frame_id_t frame_id = frame_iter->second;
  Page &page = pages_[frame_id];

  // regradless of the dirty flag, flush the page instantly
  DiskRequest r(true, page.GetPageId(), page.GetData());
  disk_proxy_->WriteToDisk(r);
  // reset the dirty flag
  page.is_dirty_ = false;

  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::unique_lock<std::mutex> lock(latch_);
  for (auto [page_id, frame_id] : page_table_) {
    Page &page = pages_[frame_id];
    if (!page.IsDirty()) {
      continue;
    }
    BUSTUB_ASSERT(page_id == page.GetPageId(), "inconsistent page id");

    DiskRequest r(true, page.GetPageId(), page.GetData());
    disk_proxy_->WriteToDisk(r);
    page.is_dirty_ = false;
  }
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  auto frame_iter = page_table_.find(page_id);
  // page not in buffer, just return true
  if (frame_iter == page_table_.end()) {
    return true;
  }
  frame_id_t frame_id = frame_iter->second;
  Page &page = pages_[frame_id];
  if (page.GetPinCount() != 0) {
    return false;
  }

  page_table_.erase(page_id);
  replacer_->Remove(frame_id);

  free_list_.push_back(frame_id);
  page.ResetMemory();
  page.page_id_ = INVALID_PAGE_ID;
  page.pin_count_ = 0;
  page.is_dirty_ = false;
  DeallocatePage(page_id);

  return true;
}

auto BufferPoolManager::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManager::FetchPageBasic(page_id_t page_id) -> BasicPageGuard {
  auto page = FetchPage(page_id);
  return {this, page};
}

auto BufferPoolManager::FetchPageRead(page_id_t page_id) -> ReadPageGuard {
  auto page = FetchPage(page_id);
  if (page != nullptr) {
    page->RLatch();
  }
  return {this, page};
}

auto BufferPoolManager::FetchPageWrite(page_id_t page_id) -> WritePageGuard {
  auto page = FetchPage(page_id);
  if (page != nullptr) {
    page->WLatch();
  }
  return {this, page};
}

auto BufferPoolManager::NewPageGuarded(page_id_t *page_id) -> BasicPageGuard {
  auto page = this->NewPage(page_id);
  return {this, page};
}

}  // namespace bustub
