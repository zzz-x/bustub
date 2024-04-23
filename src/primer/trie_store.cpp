#include "primer/trie_store.h"
#include "common/exception.h"

namespace bustub {

// one writer, multiple reader
// 即使有一个进程在写，别的进程可以基于原来的root读
template <class T>
auto TrieStore::Get(std::string_view key) -> std::optional<ValueGuard<T>> {
  // Pseudo-code:
  // (1) Take the root lock, get the root, and release the root lock. Don't lookup the value in the
  //     trie while holding the root lock.
  // (2) Lookup the value in the trie.
  // (3) If the value is found, return a ValueGuard object that holds a reference to the value and the
  //     root. Otherwise, return std::nullopt.

  // 这里加锁是为了是防止拿root的时候有写操作
  root_lock_.lock();
  Trie query_root = root_;
  root_lock_.unlock();

  auto value_ptr = query_root.Get<T>(key);

  if (!value_ptr) {
    return std::nullopt;
  }
  return ValueGuard<T>(query_root, *value_ptr);
}

template <class T>
void TrieStore::Put(std::string_view key, T value) {
  // You will need to ensure there is only one writer at a time. Think of how you can achieve this.
  // The logic should be somehow similar to `TrieStore::Get`.
  this->write_lock_.lock();
  Trie modified_root = root_.Put(key, std::move(value));
  this->root_lock_.lock();
  root_ = modified_root;
  this->root_lock_.unlock();
  this->write_lock_.unlock();
}

void TrieStore::Remove(std::string_view key) {
  // You will need to ensure there is only one writer at a time. Think of how you can achieve this.
  // The logic should be somehow similar to `TrieStore::Get`.
  this->write_lock_.lock();
  Trie modified_root = root_.Remove(key);
  this->root_lock_.lock();
  root_ = modified_root;
  this->root_lock_.unlock();
  this->write_lock_.unlock();
}

// Below are explicit instantiation of template functions.

template auto TrieStore::Get(std::string_view key) -> std::optional<ValueGuard<uint32_t>>;
template void TrieStore::Put(std::string_view key, uint32_t value);

template auto TrieStore::Get(std::string_view key) -> std::optional<ValueGuard<std::string>>;
template void TrieStore::Put(std::string_view key, std::string value);

// If your solution cannot compile for non-copy tests, you can remove the below lines to get partial score.

using Integer = std::unique_ptr<uint32_t>;

template auto TrieStore::Get(std::string_view key) -> std::optional<ValueGuard<Integer>>;
template void TrieStore::Put(std::string_view key, Integer value);

template auto TrieStore::Get(std::string_view key) -> std::optional<ValueGuard<MoveBlocked>>;
template void TrieStore::Put(std::string_view key, MoveBlocked value);

}  // namespace bustub
