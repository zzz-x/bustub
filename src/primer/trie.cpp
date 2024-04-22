#include "primer/trie.h"
#include <stack>
#include <string_view>
#include "common/exception.h"

namespace bustub {

template <class T>
auto Trie::Get(std::string_view key) const -> const T * {
  if (!root_) {
    return nullptr;
  }
  std::shared_ptr<const TrieNode> p = root_;
  for (const auto &ch : key) {
    if (p->children_.find(ch) == p->children_.end()) {
      return nullptr;
    }  // can't find the key
    p = p->children_.at(ch);
  }
  if (!p->is_value_node_) {
    return nullptr;
  }  // do not have value

  const auto *node_with_value = dynamic_cast<const TrieNodeWithValue<T> *>(p.get());
  if (!node_with_value) {
    return nullptr;
  }

  return static_cast<const T *>(node_with_value->value_.get());
  // You should walk through the trie to find the node corresponding to the key. If the node doesn't exist, return
  // nullptr. After you find the node, you should use `dynamic_cast` to cast it to `const TrieNodeWithValue<T> *`. If
  // dynamic_cast returns `nullptr`, it means the type of the value is mismatched, and you should return nullptr.
  // Otherwise, return the value.
}

template <class T>
auto Trie::Put(std::string_view key, T value) const -> Trie {
  // You should walk through the trie and create new nodes if necessary. If the node corresponding to the key already
  // exists, you should create a new `TrieNodeWithValue`.

  auto value_node = std::make_shared<TrieNodeWithValue<T>>(std::make_shared<T>(std::move(value)));

  if (!root_) {
    std::shared_ptr<TrieNode> new_root = std::make_shared<TrieNode>();
    std::shared_ptr<TrieNode> prev_node = new_root;

    for (size_t idx = 0; idx < key.size(); ++idx) {
      std::shared_ptr<TrieNode> new_child;
      if (idx == key.size() - 1) {
        new_child = value_node;
        prev_node->children_[key.at(idx)] = new_child;
      } else {
        new_child = std::make_shared<TrieNode>();
        prev_node->children_[key.at(idx)] = new_child;
      }
      prev_node = new_child;
    }
    return Trie(new_root);
  }
  std::shared_ptr<const TrieNode> p = root_;
  std::vector<std::unique_ptr<TrieNode>> visited_nodes;

  size_t idx = 0;
  for (idx = 0; idx < key.size(); ++idx) {
    if (p->children_.find(key.at(idx)) == p->children_.end()) {  // can't find the key
      break;
    }
    visited_nodes.emplace_back(p->Clone());
    p = p->children_.at(key.at(idx));
  }

  std::shared_ptr<TrieNode> backtrace_node;
  if (idx == key.size()) {  // find the key
    // set value
    backtrace_node = value_node;
    backtrace_node->children_ = p->children_;
    backtrace_node->is_value_node_ = true;
  } else {
    backtrace_node = p->Clone();
    std::shared_ptr<TrieNode> prev_node = backtrace_node;

    for (size_t j = idx; j < key.size(); ++j) {
      std::shared_ptr<TrieNode> new_child;
      if (j == key.size() - 1) {
        new_child = value_node;
      } else {
        new_child = std::make_shared<TrieNode>();
      }
      prev_node->children_[key.at(j)] = new_child;
      prev_node = new_child;
    }
  }

  for (int j = visited_nodes.size() - 1; j >= 0; --j) {
    visited_nodes[j]->children_[key[j]] = backtrace_node;
    backtrace_node = std::move(visited_nodes[j]);
  }
  return Trie(backtrace_node);
}

auto Trie::Remove(std::string_view key) const -> Trie {
  // You should walk through the trie and remove nodes if necessary. If the node doesn't contain a value any more,
  // you should convert it to `TrieNode`. If a node doesn't have children any more, you should remove it.
  if (!root_) {
    return {};
  }

  std::vector<std::unique_ptr<TrieNode>> visited_nodes;
  std::shared_ptr<const TrieNode> p = root_;
  size_t idx;
  for (idx = 0; idx < key.size(); ++idx) {
    if (p->children_.find(key[idx]) == p->children_.end()) {
      return Trie(root_);
    }

    visited_nodes.emplace_back(p->Clone());
    p = p->children_.at(key[idx]);
  }

  if (!p->is_value_node_) {
    return Trie(root_);
  }

  // convert it to no value node
  std::shared_ptr<TrieNode> no_value_node = std::make_shared<TrieNode>(p->children_);
  // backtrace
  std::shared_ptr<const TrieNode> prev_node = no_value_node;
  for (int i = static_cast<int>(visited_nodes.size() - 1); i >= 0; --i) {
    if (prev_node->children_.empty() && !prev_node->is_value_node_) {  // if the node doesn't have children,remove it
      visited_nodes[i]->children_.erase(key[i]);
    } else {
      visited_nodes[i]->children_[key[i]] = prev_node;
    }
    prev_node = std::move(visited_nodes[i]);
  }
  if (prev_node->children_.empty() && !prev_node->is_value_node_) {
    prev_node = nullptr;
  }
  return Trie(prev_node);
}

// Below are explicit instantiation of template functions.
//
// Generally people would write the implementation of template classes and functions in the header file. However, we
// separate the implementation into a .cpp file to make things clearer. In order to make the compiler know the
// implementation of the template functions, we need to explicitly instantiate them here, so that they can be picked up
// by the linker.

template auto Trie::Put(std::string_view key, uint32_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint32_t *;

template auto Trie::Put(std::string_view key, uint64_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint64_t *;

template auto Trie::Put(std::string_view key, std::string value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const std::string *;

// If your solution cannot compile for non-copy tests, you can remove the below lines to get partial score.

using Integer = std::unique_ptr<uint32_t>;

template auto Trie::Put(std::string_view key, Integer value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const Integer *;

template auto Trie::Put(std::string_view key, MoveBlocked value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const MoveBlocked *;

}  // namespace bustub
