//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t {
  BUSTUB_ASSERT(next_page_id_ != INVALID_PAGE_ID, "next page id should be valid");
  return next_page_id_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // replace with your own code
  return array_[index].first;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType { return array_[index].second; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::PushBack(const KeyType &key, const ValueType &value) {
  array_[GetSize()] = {key, value};
  IncreaseSize(1);
  return;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::InsertBefore(const KeyType &key, const ValueType &value, int idx) -> bool {
  int curr_size = GetSize();
  int max_size = GetMaxSize();
  if (curr_size >= max_size - 1 || idx < 0 || idx >= curr_size) {
    return false;
  }

  for (int i = curr_size + 1; i >= idx; --i) {
    array_[i] = array_[i - 1];
  }
  array_[idx] = {key, value};
  IncreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAfter(const KeyType &key, const ValueType &value, int idx) -> bool {
  int curr_size = GetSize();
  int max_size = GetMaxSize();
  if (curr_size >= max_size - 1 || idx < 0 || idx >= curr_size) {
    return false;
  }

  for (int i = curr_size + 1; i > idx; --i) {
    array_[i] = array_[i - 1];
  }
  array_[idx + 1] = {key, value};
  IncreaseSize(1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value, const KeyComparator &comp) -> bool {
  int curr_size = GetSize();
  int max_size = GetMaxSize();

  BUSTUB_ENSURE(GetSize() != 0, "Leaf Page must have keys");

  if (curr_size >= max_size - 1) {
    return false;
  }

  int idx = 0;
  int key_size = GetSize();
  for (idx = 0; idx < key_size; ++idx) {
    if (comp(key, KeyAt(idx)) < 0) {
      break;
    }
  }
  return this->InsertBefore(key, value, idx);
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
