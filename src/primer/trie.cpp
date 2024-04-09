#include "primer/trie.h"
#include <string_view>
#include <stack>
#include "common/exception.h"

namespace bustub {

template <class T>
auto Trie::Get(std::string_view key) const -> const T * {
  throw NotImplementedException("Trie::Get is not implemented.");
  if(!root_)
    return nullptr;
  std::shared_ptr<const TrieNode> p = root_;
  for(const auto & ch : key){
    if(p->children_.find(ch)==p->children_.end())
        return nullptr;     // can't find the key
      p = p->children_.at(ch);
  }
  if(!p->is_value_node_)
    return nullptr;     // do not have value

  const TrieNodeWithValue<T>* node_with_value = dynamic_cast<const TrieNodeWithValue<T>*>(p.get());
  if(!node_with_value)
    return nullptr;

  return static_cast<const T*>(node_with_value->value_.get());
  // You should walk through the trie to find the node corresponding to the key. If the node doesn't exist, return
  // nullptr. After you find the node, you should use `dynamic_cast` to cast it to `const TrieNodeWithValue<T> *`. If
  // dynamic_cast returns `nullptr`, it means the type of the value is mismatched, and you should return nullptr.
  // Otherwise, return the value.
}

template <class T>
auto Trie::Put(std::string_view key, T value) const -> Trie {
  // You should walk through the trie and create new nodes if necessary. If the node corresponding to the key already
  // exists, you should create a new `TrieNodeWithValue`.
  if(!root_){
    std::shared_ptr<TrieNode> new_root = std::make_shared<TrieNode>();
    std::shared_ptr<TrieNode> prev_node = new_root;

    for(size_t idx=0;idx<key.size();++idx){
      std::shared_ptr<TrieNode> new_child;
      if(idx==key.size()-1){
        new_child= std::make_shared<TrieNodeWithValue<T>>(std::make_shared<T>(std::move(value)));
        prev_node->children_[key[idx]]= new_child;
      }
      else{
        new_child = std::make_shared<TrieNode>();
        prev_node->children_[key[idx]]= new_child;
      }
      prev_node=new_child;
    }
    return Trie(new_root);
  }

  std::stack<std::unique_ptr<TrieNode>> visited_nodes;
  std::stack<char> visited_chars;

  std::shared_ptr<const TrieNode> p = root_;
  size_t idx;
  for(idx=0;idx<key.size();++idx){
    const char ch = key.at(idx);
    if(p->children_.find(ch) == p->children_.end()) //can't find the key
      break;
    else{
      visited_nodes.emplace(p->Clone());
      visited_chars.emplace(ch);
      p=p->children_.at(ch);
    }
  }

  std::shared_ptr<TrieNode> backtrace_node;
  if(idx == key.size()){
    // find the key, but we don't know whether the node is value_node
    backtrace_node =std::move(std::make_unique<TrieNodeWithValue<T>>(p->children_ ,std::make_shared<T>(std::move(value))));
    backtrace_node->is_value_node_ = true;
  }
  else{
    // we can't find the key
    backtrace_node = std::move(std::make_unique<TrieNodeWithValue<T>>(p->children_,std::make_shared<T>(std::move(value))));
    backtrace_node->is_value_node_ = true;

    std::shared_ptr<TrieNode> prev_node=backtrace_node;

    for(size_t i = idx ; i< key.size() ; ++i){
      std::shared_ptr<TrieNode> new_child = std::make_shared<TrieNode>();
      prev_node->children_[key[i]]= new_child;
      prev_node=new_child;
    }
  }

  // backtrace
  while(!visited_nodes.empty()){
    std::unique_ptr<TrieNode> top_node =std::move(visited_nodes.top());
    char ch =visited_chars.top();
    top_node->children_[ch]=backtrace_node;
    backtrace_node=std::move(top_node);
  }
  return Trie(backtrace_node);
}

auto Trie::Remove(std::string_view key) const -> Trie {
  throw NotImplementedException("Trie::Remove is not implemented.");

  // You should walk through the trie and remove nodes if necessary. If the node doesn't contain a value any more,
  // you should convert it to `TrieNode`. If a node doesn't have children any more, you should remove it.
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
