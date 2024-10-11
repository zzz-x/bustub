//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.h
//
// Identification: src/include/buffer/buffer_pool_manager.h
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#ifndef BUFFER_POOL_MANAGER_HPP
#define BUFFER_POOL_MANAGER_HPP
#pragma once

#include <condition_variable>  // NOLINT
#include <functional>
#include <future>  // NOLINT
#include <list>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <queue>
#include <thread>  // NOLINT
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "buffer/lru_k_replacer.h"
#include "common/config.h"
#include "recovery/log_manager.h"
#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

class ThreadPool {
 public:
  explicit ThreadPool(size_t pool_size) {
    for (size_t idx = 0; idx < pool_size; ++idx) {
      workers_.emplace_back([&]() {
        while (true) {
          std::function<void()> func;
          {
            std::unique_lock<std::mutex> lock(this->lock_);
            this->cv_.wait(lock, [&]() { return !this->tasks_.empty() || end_; });
            if (end_ && this->tasks_.empty()) {
              return;
            }
            func = std::move(this->tasks_.front());
            this->tasks_.pop();
          }
          func();
        }
      });
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(lock_);
      end_ = true;
    }
    cv_.notify_all();

    for (auto &thread : workers_) {
      thread.join();
    }
  }

  // 添加任务到线程池
  template <class F, class... Args>
  auto Enqueue(F &&func, Args &&... args) -> std::future<decltype(func(std::forward<Args>(args)...))> {
    using ret_t = decltype(func(std::forward<Args>(args)...));
    auto task = std::make_shared<std::packaged_task<ret_t()>>([func, args...]() { func(args...); });
    std::future<ret_t> ret = task->get_future();
    {
      std::unique_lock<std::mutex> lock(lock_);
      tasks_.push([task]() { (*task)(); });
    }

    cv_.notify_one();

    return ret;
  }

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex lock_;
  std::condition_variable cv_;
  bool end_{false};
};

}  // namespace bustub

namespace bustub {

template <class T>
class Channel {
 public:
  Channel() = default;
  ~Channel() = default;

  void Put(const T &element) {
    std::unique_lock<std::mutex> lock(lock_);
    // printf("Put Page: %d  data: %s\n",element->page_id_,element->data_);
    q_.push(element);
    lock.unlock();
    cv_.notify_one();
  }

  auto Get() -> T {
    std::unique_lock<std::mutex> lock(lock_);
    // printf("Get Sleep\n");
    cv_.wait(lock, [&]() { return !q_.empty(); });
    // printf("Get Activated\n");
    T element = std::move(q_.front());
    // printf("Get Page: %d  data: %s\n",element->page_id_,element->data_);
    q_.pop();
    return element;
  }

  auto Size() -> size_t {
    std::unique_lock<std::mutex> lock(lock_);
    return q_.size();
  }

  auto GetLastElem(T &elem) -> bool {
    std::unique_lock<std::mutex> lock(lock_);
    if (q_.empty()) {
      return false;
    }
    elem = q_.back();
    return true;
  }

  auto Empty() -> bool {
    std::unique_lock<std::mutex> lock(lock_);
    return q_.empty();
  }

 private:
  std::mutex lock_;
  std::queue<T> q_;
  std::condition_variable cv_;
};

struct DiskRequest {
  bool is_write_;
  page_id_t page_id_;
  char *data_;

  DiskRequest();
  DiskRequest(bool is_write, page_id_t page_id_, char *data);
  ~DiskRequest();

  auto operator=(const DiskRequest &other) -> DiskRequest &;
  auto operator=(DiskRequest &&other) noexcept -> DiskRequest &;
  DiskRequest(const DiskRequest &other);
  DiskRequest(DiskRequest &&other) noexcept;
};

class PageScheduler {
 public:
  explicit PageScheduler(DiskManager *manager, ThreadPool *worker);
  ~PageScheduler();

  void Schedule(const DiskRequest &request);
  auto GetLastRequest() -> std::optional<DiskRequest>;
  char *cache_data_;
  bool cache_valid_;

 private:
  std::mutex lock_;
  std::queue<DiskRequest> q_;
  DiskManager *disk_manager_;
  ThreadPool *thread_pool_;
};

class DiskManagerProxy {
 public:
  explicit DiskManagerProxy(DiskManager *disk_manager, ThreadPool *worker);

  void WriteToDisk(const DiskRequest &r);
  void ReadFromDisk(page_id_t page_id, char *data);
  void Clear() { request_map_.clear(); }

 private:
  std::unordered_map<page_id_t, std::shared_ptr<PageScheduler>> request_map_;
  DiskManager *disk_manager_;
  ThreadPool *thread_pool_;
  std::mutex lock_;
};

}  // namespace bustub

namespace bustub {

/**
 * BufferPoolManager reads disk pages to and from its internal buffer pool.
 */
class BufferPoolManager {
 public:
  /**
   * @brief Creates a new BufferPoolManager.
   * @param pool_size the size of the buffer pool
   * @param disk_manager the disk manager
   * @param replacer_k the lookback constant k for the LRU-K replacer
   * @param log_manager the log manager (for testing only: nullptr = disable logging). Please ignore this for P1.
   */
  BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k = LRUK_REPLACER_K,
                    LogManager *log_manager = nullptr);

  /**
   * @brief Destroy an existing BufferPoolManager.
   */
  ~BufferPoolManager();

  /** @brief Return the size (number of frames) of the buffer pool. */
  auto GetPoolSize() -> size_t { return pool_size_; }

  /** @brief Return the pointer to all the pages in the buffer pool. */
  auto GetPages() -> Page * { return pages_; }

  /**
   * TODO(P1): Add implementation
   *
   * @brief Create a new page in the buffer pool. Set page_id to the new page's id, or nullptr if all frames
   * are currently in use and not evictable (in another word, pinned).
   *
   * You should pick the replacement frame from either the free list or the replacer (always find from the free list
   * first), and then call the AllocatePage() method to get a new page id. If the replacement frame has a dirty page,
   * you should write it back to the disk first. You also need to reset the memory and metadata for the new page.
   *
   * Remember to "Pin" the frame by calling replacer.SetEvictable(frame_id, false)
   * so that the replacer wouldn't evict the frame before the buffer pool manager "Unpin"s it.
   * Also, remember to record the access history of the frame in the replacer for the lru-k algorithm to work.
   *
   * @param[out] page_id id of created page
   * @return nullptr if no new pages could be created, otherwise pointer to new page
   */
  auto NewPage(page_id_t *page_id) -> Page *;

  /**
   * TODO(P1): Add implementation
   *
   * @brief PageGuard wrapper for NewPage
   *
   * Functionality should be the same as NewPage, except that
   * instead of returning a pointer to a page, you return a
   * BasicPageGuard structure.
   *
   * @param[out] page_id, the id of the new page
   * @return BasicPageGuard holding a new page
   */
  auto NewPageGuarded(page_id_t *page_id) -> BasicPageGuard;

  /**
   * TODO(P1): Add implementation
   *
   * @brief Fetch the requested page from the buffer pool. Return nullptr if page_id needs to be fetched from the disk
   * but all frames are currently in use and not evictable (in another word, pinned).
   *
   * First search for page_id in the buffer pool. If not found, pick a replacement frame from either the free list or
   * the replacer (always find from the free list first), read the page from disk by calling disk_manager_->ReadPage(),
   * and replace the old page in the frame. Similar to NewPage(), if the old page is dirty, you need to write it back
   * to disk and update the metadata of the new page
   *
   * In addition, remember to disable eviction and record the access history of the frame like you did for NewPage().
   *
   * @param page_id id of page to be fetched
   * @param access_type type of access to the page, only needed for leaderboard tests.
   * @return nullptr if page_id cannot be fetched, otherwise pointer to the requested page
   */
  auto FetchPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> Page *;

  /**
   * TODO(P1): Add implementation
   *
   * @brief PageGuard wrappers for FetchPage
   *
   * Functionality should be the same as FetchPage, except
   * that, depending on the function called, a guard is returned.
   * If FetchPageRead or FetchPageWrite is called, it is expected that
   * the returned page already has a read or write latch held, respectively.
   *
   * @param page_id, the id of the page to fetch
   * @return PageGuard holding the fetched page
   */
  auto FetchPageBasic(page_id_t page_id) -> BasicPageGuard;
  auto FetchPageRead(page_id_t page_id) -> ReadPageGuard;
  auto FetchPageWrite(page_id_t page_id) -> WritePageGuard;

  /**
   * TODO(P1): Add implementation
   *
   * @brief Unpin the target page from the buffer pool. If page_id is not in the buffer pool or its pin count is already
   * 0, return false.
   *
   * Decrement the pin count of a page. If the pin count reaches 0, the frame should be evictable by the replacer.
   * Also, set the dirty flag on the page to indicate if the page was modified.
   *
   * @param page_id id of page to be unpinned
   * @param is_dirty true if the page should be marked as dirty, false otherwise
   * @param access_type type of access to the page, only needed for leaderboard tests.
   * @return false if the page is not in the page table or its pin count is <= 0 before this call, true otherwise
   */
  auto UnpinPage(page_id_t page_id, bool is_dirty, AccessType access_type = AccessType::Unknown) -> bool;

  /**
   * TODO(P1): Add implementation
   *
   * @brief Flush the target page to disk.
   *
   * Use the DiskManager::WritePage() method to flush a page to disk, REGARDLESS of the dirty flag.
   * Unset the dirty flag of the page after flushing.
   *
   * @param page_id id of page to be flushed, cannot be INVALID_PAGE_ID
   * @return false if the page could not be found in the page table, true otherwise
   */
  auto FlushPage(page_id_t page_id) -> bool;

  /**
   * TODO(P1): Add implementation
   *
   * @brief Flush all the pages in the buffer pool to disk.
   */
  void FlushAllPages();

  /**
   * TODO(P1): Add implementation
   *
   * @brief Delete a page from the buffer pool. If page_id is not in the buffer pool, do nothing and return true. If the
   * page is pinned and cannot be deleted, return false immediately.
   *
   * After deleting the page from the page table, stop tracking the frame in the replacer and add the frame
   * back to the free list. Also, reset the page's memory and metadata. Finally, you should call DeallocatePage() to
   * imitate freeing the page on the disk.
   *
   * @param page_id id of page to be deleted
   * @return false if the page exists but could not be deleted, true if the page didn't exist or deletion succeeded
   */
  auto DeletePage(page_id_t page_id) -> bool;

 private:
  std::unique_ptr<DiskManagerProxy> disk_proxy_;
  ThreadPool *thread_pool_;

  /** Number of pages in the buffer pool. */
  const size_t pool_size_;
  /** The next page id to be allocated  */
  std::atomic<page_id_t> next_page_id_ = 0;

  /** Array of buffer pool pages. */
  Page *pages_;
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_ __attribute__((__unused__));
  /** Pointer to the log manager. Please ignore this for P1. */
  LogManager *log_manager_ __attribute__((__unused__));
  /** Page table for keeping track of buffer pool pages. */
  std::unordered_map<page_id_t, frame_id_t> page_table_;
  /** Replacer to find unpinned pages for replacement. */
  std::unique_ptr<LRUKReplacer> replacer_;
  /** List of free frames that don't have any pages on them. */
  std::list<frame_id_t> free_list_;
  /** This latch protects shared data structures. We recommend updating this comment to describe what it protects. */
  std::mutex latch_;

  /**
   * @brief Allocate a page on disk. Caller should acquire the latch before calling this function.
   * @return the id of the allocated page
   */
  auto AllocatePage() -> page_id_t;

  /**
   * @brief Deallocate a page on disk. Caller should acquire the latch before calling this function.
   * @param page_id id of the page to deallocate
   */
  void DeallocatePage(__attribute__((unused)) page_id_t page_id) {
    // This is a no-nop right now without a more complex data structure to track deallocated pages
  }

  // TODO(student): You may add additional private members and helper functions
};
}  // namespace bustub

#endif
