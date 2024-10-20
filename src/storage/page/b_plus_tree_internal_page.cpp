//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, and set max page size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  this->SetMaxSize(max_size);
  this->SetPageType(IndexPageType::INTERNAL_PAGE);
  this->SetSize(0);
}
/*
 * Helper method to get/set the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  BUSTUB_ASSERT(index <= GetSize(), "Invalid idx !");
  return array_[index].first;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  BUSTUB_ASSERT(index <= GetSize(), "Invalid idx !");
  array_[index].first = key;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  BUSTUB_ASSERT(index <= GetSize(), "Invalid idx !");
  array_[index].second = value;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyValueAt(int index, const KeyType &key, const ValueType &value) {
  BUSTUB_ASSERT(index <= GetSize(), "InternalPage SetKeyValueAt idx out of bound!");
  array_[index] = std::make_pair(key, value);
}

/*
 * Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  // 检查idx是否越界
  BUSTUB_ASSERT(index <= GetSize(), "Invalid idx !");
  return array_[index].second;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertVal(const KeyType &key, const ValueType &value,
                                               const KeyComparator &comparator) {
  // TODO: check the function here
  int idx = FindKeyIndexLowerBound(key, comparator);  // key[idx]<=value
  int old_size = GetSize();

  // 从idx开始的元素全部向后移动一位
  for (int j = old_size; j > idx; --j) {
    std::swap(array_[j], array_[j - 1]);
  }
  // 插入value
  array_[idx].first = key;
  array_[idx].second = value;
  IncreaseSize(1);
}

INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindKeyIndexLowerBound(const KeyType &key, const KeyComparator &comparator) {
  // 左闭右开
  int left = 1;
  int right = GetSize();

  while (left < right) {
    int mid = left + (right - left) / 2;
    KeyType mid_key = KeyAt(mid);
    if (comparator(mid_key, key)) {
      // mid_key < key
      left = mid + 1;
    } else {
      // mid_key >= key
      right = mid;
    }
  }
  return left;
}

INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindKeyIndexUpperBound(const KeyType &key, const KeyComparator &comparator) {
  // 左闭右开
  int left = 1;
  int right = GetSize();
  while (left < right) {
    int mid = left + (right - left) / 2;
    KeyType mid_key = KeyAt(mid);
    if (comparator(key, mid_key)) {
      // mid_key < key
      left = mid + 1;
    } else {
      // mid_key >=key
    }
  }

  return left;
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
