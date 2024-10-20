#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "storage/index/b_plus_tree.h"

namespace bustub {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->FetchPageWrite(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool { return true; }

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPageWithKey(const KeyType &key, page_id_t &leaf_page_id, Context &ctx) const
    -> FindLeafRetType {
  // 先从header_page_id中读取page_id
  page_id_t root_page_id = INVALID_PAGE_ID;
  {
    ReadPageGuard guard = bpm_->FetchPageRead(header_page_id_);
    const BPlusTreeHeaderPage *header_page = guard.As<BPlusTreeHeaderPage>();
    root_page_id = header_page->root_page_id_;
  }
  // 保存root_page_id到context
  ctx.root_page_id_ = root_page_id;
  if (root_page_id == INVALID_PAGE_ID) {
    return FindLeafRetType::EmptyTree;
  }

  auto curr_guard = bpm_->FetchPageRead(root_page_id);
  auto curr_page = curr_guard.As<BPlusTreePage>();
  auto curr_page_id = root_page_id;

  while (!curr_page->IsLeafPage()) {
    const InternalPage *internal_page = curr_guard.As<InternalPage>();
    BUSTUB_ASSERT(internal_page != nullptr, "Page should be internal");
    // 寻找key落在的区间，由于存储的结构，需要从index=1开始遍历
    // 左闭右开
    int key_idx = 1;
    for (key_idx = 1; key_idx < internal_page->GetSize(); ++key_idx) {
      KeyType curr_key = internal_page->KeyAt(key_idx);
      if (comparator_(key, curr_key) < 0) {
        break;
      }
    }
    int val_idx = key_idx;
    if (key_idx != internal_page->GetSize()) {
      val_idx = key_idx - 1;
    }
    ctx.read_set_.push_back(std::move(curr_guard));
    curr_page_id = internal_page->ValueAt(val_idx);
    curr_guard = bpm_->FetchPageRead(curr_page_id);
    curr_page = curr_guard.As<BPlusTreePage>();
  }

  leaf_page_id = curr_page_id;

  return FindLeafRetType::Success;
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *txn) -> bool {
  // Declaration of context instance.
  Context ctx;
  (void)ctx;

  page_id_t leaf_page_id;
  // 先找到key所在的leaf page
  auto flag = FindLeafPageWithKey(key, leaf_page_id, ctx);
  if (flag != FindLeafRetType::Success) {
    return false;
  }

  // 寻找到了对应的叶子结点，遍历匹配即可
  auto guard = bpm_->FetchPageRead(leaf_page_id);
  const LeafPage *leaf = guard.As<LeafPage>();
  bool found = false;
  for (int idx = 0; idx < leaf->GetSize(); ++idx) {
    auto curr_key = leaf->KeyAt(idx);
    if (comparator_(curr_key, key) == 0) {
      result->emplace_back(leaf->ValueAt(idx));
      found = true;
    }
  }

  if (!found) {
    return false;
  }

  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertAndSplitLeaf(const page_id_t leaf_page_id, const KeyType &key, const ValueType &value,
                                        Context &ctx) -> bool {
  // 拿到原始的leaf node
  auto old_leaf_guard = bpm_->FetchPageWrite(leaf_page_id);
  LeafPage *old_leaf_node = old_leaf_guard.AsMut<LeafPage>();

  int leaf_node_size = old_leaf_node->GetSize();
  int leaf_node_max_size = old_leaf_node->GetMaxSize();

  BUSTUB_ENSURE(leaf_node_size == leaf_node_max_size - 1, "When doing split,leaf node must have n-1 values");

  // 临时变量存储 leaf node中的kv对，后续可以优化
  // TODO： 减少此处的拷贝
  std::vector<MappingType> temp;
  {
    temp.reserve(leaf_node_size + 1);
    for (int idx = 0; idx < leaf_node_size; ++idx) {
      temp.emplace_back(old_leaf_node->KeyAt(idx), old_leaf_node->ValueAt(idx));
    }
    Insert2SorrtedList(temp, {key, value}, comparator_);
  }

  page_id_t new_leaf_page_id;
  auto new_leaf_guard = bpm_->NewPageGuarded(&new_leaf_page_id);
  LeafPage *new_leaf_node = new_leaf_guard.AsMut<LeafPage>();

  // 新创建一个叶子结点，并将原来的叶子结点与新创建的结点相连
  new_leaf_node->SetNextPageId(old_leaf_node->GetNextPageId());
  old_leaf_node->SetNextPageId(new_leaf_page_id);

  // 将temp中存储的kv对分配到两个leaf node中
  {
    size_t half_size = (size_t)std::ceil(leaf_node_max_size / 2);
    old_leaf_node->SetSize(0);  // clear old node
    for (size_t idx = 0; idx <= half_size; ++idx) {
      old_leaf_node->PushBack(temp[idx].first, temp[idx].second);
    }
    for (size_t idx = half_size + 1; idx < temp.size(); ++idx) {
      new_leaf_node->PushBack(temp[idx].first, temp[idx].second);
    }
  }

  KeyType new_key = new_leaf_node->KeyAt(0);
  page_id_t left_leaf_node = leaf_page_id;
  page_id_t right_leaf_node = new_leaf_page_id;
  page_id_t parent_leaf_page_id = ctx.read_set_.back().PageId();
  ctx.read_set_.pop_back();  // 退回到父节点处理
  return InsertAndSplitInternal(parent_leaf_page_id, left_leaf_node, new_key, right_leaf_node, ctx);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertAndSplitInternal(const page_id_t internal_page_id, const page_id_t lower_range_id,
                                            const KeyType &key, const page_id_t upper_range_id, Context &ctx) -> bool {
  if (internal_page_id == ctx.root_page_id_) {
    // 如果是root page 也要分裂，则新建一个节点
    page_id_t new_root_page_id;
    auto new_root_guard = bpm_->NewPageGuarded(&new_root_page_id);
    InternalPage* new_root_node = new_root_guard.AsMut<InternalPage>();
    new_root_node->Init();
    new_root_node->SetKeyValueAt(0,key,upper_range_id);
    ctx.root_page_id_ = new_root_page_id;
    return true;
  }

  auto internal_guard = bpm_->FetchPageWrite(internal_page_id);
  InternalPage *internal_node = internal_guard.AsMut<InternalPage>();

  int internal_node_size = internal_node->GetSize();
  int internal_node_max_size = internal_node->GetMaxSize();

  if (internal_node_size < internal_node_max_size) {
    // 不需要split的时候，直接插入
    internal_node->InsertVal(key, upper_range_id, comparator_);
    return true;
  }
  std::vector<std::pair<KeyType, page_id_t>> temp;
  temp.reserve(internal_node_size + 1);
  for (int idx = 0; idx < internal_node_size; ++idx) {
    temp.emplace_back(internal_node->KeyAt(idx), internal_node->ValueAt(idx));
  }
  Insert2SorrtedList(temp, {key, upper_range_id}, comparator_);
  internal_node->SetSize(0);
  page_id_t new_internal_page_id;
  auto new_internal_guard = bpm_->NewPageGuarded(&new_internal_page_id);
  InternalPage *new_internal_node = new_internal_guard.AsMut<InternalPage>();

  size_t split_size = std::ceil((internal_node_max_size + 1) / 2.f);
  for (size_t idx = 0; idx <= split_size; ++idx) {
    internal_node->InsertVal(temp[idx].first, temp[idx].second, comparator_);
  }
  for (size_t idx = split_size + 1; idx < (size_t)internal_node_max_size + 1; ++idx) {
    new_internal_node->InsertVal(temp[idx].first, temp[idx].second, comparator_);
  }

  KeyType new_key = temp[split_size].first;

  page_id_t parent_internal_page_id = ctx.read_set_.back().PageId();
  ctx.read_set_.pop_back();
  return InsertAndSplitInternal(parent_internal_page_id, internal_page_id, new_key, new_internal_page_id, ctx);
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value, Transaction *txn) -> bool {
  // Declaration of context instance.
  Context ctx;
  (void)ctx;

  page_id_t leaf_page_id;
  // 找到Key应该处于的leaf_page，ctx中记录节点
  auto flag = FindLeafPageWithKey(key, leaf_page_id, ctx);

  if (flag == FindLeafRetType::EmptyTree) {
    auto guard = bpm_->NewPageGuarded(&leaf_page_id);
    LeafPage *root_page = guard.AsMut<LeafPage>();
    root_page->Init(this->leaf_max_size_);
    // 将新的root_page_id记录到header_page_id中
    auto header_page_guard = bpm_->FetchPageWrite(header_page_id_);
    auto head_page = header_page_guard.AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = leaf_page_id;
    // 将新的root_page_id记录到ctx中
    BUSTUB_ASSERT(ctx.root_page_id_ == INVALID_PAGE_ID, "Root page id should be invalid in empty tree");
    ctx.root_page_id_ = leaf_page_id;
  } else {
    BUSTUB_ASSERT(flag == FindLeafRetType::Success, "Should always find the leaf page of the key");
  }

  auto guard = bpm_->FetchPageWrite(leaf_page_id);
  LeafPage *leaf_page = guard.AsMut<LeafPage>();

  // 如果不需要split，直接插入到叶子结点中
  if (leaf_page->GetSize() < leaf_page->GetMaxSize() - 1) {
    return leaf_page->Insert(key, value, comparator_);
  }

  guard.Drop();
  return InsertAndSplitLeaf(leaf_page_id, key, value, ctx);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *txn) {
  // Declaration of context instance.
  Context ctx;
  (void)ctx;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  auto guard = bpm_->FetchPageRead(header_page_id_);
  const auto *header_page = guard.As<BPlusTreeHeaderPage>();

  return header_page->root_page_id_;
   return 0; 
}

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string &file_name, Transaction *txn) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;

    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, txn);
  }
}
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string &file_name, Transaction *txn) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, txn);
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Print(BufferPoolManager *bpm) {
  auto root_page_id = GetRootPageId();
  auto guard = bpm->FetchPageBasic(root_page_id);
  PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::PrintTree(page_id_t page_id, const BPlusTreePage *page) {
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<const LeafPage *>(page);
    std::cout << "Leaf Page: " << page_id << "\tNext: " << leaf->GetNextPageId() << std::endl;

    // Print the contents of the leaf page.
    std::cout << "Contents: ";
    for (int i = 0; i < leaf->GetSize(); i++) {
      std::cout << leaf->KeyAt(i);
      if ((i + 1) < leaf->GetSize()) {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;

  } else {
    auto *internal = reinterpret_cast<const InternalPage *>(page);
    std::cout << "Internal Page: " << page_id << std::endl;

    // Print the contents of the internal page.
    std::cout << "Contents: ";
    for (int i = 0; i < internal->GetSize(); i++) {
      std::cout << internal->KeyAt(i) << ": " << internal->ValueAt(i);
      if ((i + 1) < internal->GetSize()) {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      auto guard = bpm_->FetchPageBasic(internal->ValueAt(i));
      PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
    }
  }
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Draw(BufferPoolManager *bpm, const std::string &outf) {
  if (IsEmpty()) {
    LOG_WARN("Drawing an empty tree");
    return;
  }

  std::ofstream out(outf);
  out << "digraph G {" << std::endl;
  auto root_page_id = GetRootPageId();
  auto guard = bpm->FetchPageBasic(root_page_id);
  ToGraph(guard.PageId(), guard.template As<BPlusTreePage>(), out);
  out << "}" << std::endl;
  out.close();
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToGraph(page_id_t page_id, const BPlusTreePage *page, std::ofstream &out) {
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<const LeafPage *>(page);
    // Print node name
    out << leaf_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">P=" << page_id << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">"
        << "max_size=" << leaf->GetMaxSize() << ",min_size=" << leaf->GetMinSize() << ",size=" << leaf->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf->GetSize(); i++) {
      out << "<TD>" << leaf->KeyAt(i) << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
      out << leaf_prefix << page_id << " -> " << leaf_prefix << leaf->GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << page_id << " " << leaf_prefix << leaf->GetNextPageId() << "};\n";
    }
  } else {
    auto *inner = reinterpret_cast<const InternalPage *>(page);
    // Print node name
    out << internal_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">P=" << page_id << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">"
        << "max_size=" << inner->GetMaxSize() << ",min_size=" << inner->GetMinSize() << ",size=" << inner->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < inner->GetSize(); i++) {
      out << "<TD PORT=\"p" << inner->ValueAt(i) << "\">";
      if (i > 0) {
        out << inner->KeyAt(i);
      } else {
        out << " ";
      }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print leaves
    for (int i = 0; i < inner->GetSize(); i++) {
      auto child_guard = bpm_->FetchPageBasic(inner->ValueAt(i));
      auto child_page = child_guard.template As<BPlusTreePage>();
      ToGraph(child_guard.PageId(), child_page, out);
      if (i > 0) {
        auto sibling_guard = bpm_->FetchPageBasic(inner->ValueAt(i - 1));
        auto sibling_page = sibling_guard.template As<BPlusTreePage>();
        if (!sibling_page->IsLeafPage() && !child_page->IsLeafPage()) {
          out << "{rank=same " << internal_prefix << sibling_guard.PageId() << " " << internal_prefix
              << child_guard.PageId() << "};\n";
        }
      }
      out << internal_prefix << page_id << ":p" << child_guard.PageId() << " -> ";
      if (child_page->IsLeafPage()) {
        out << leaf_prefix << child_guard.PageId() << ";\n";
      } else {
        out << internal_prefix << child_guard.PageId() << ";\n";
      }
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::DrawBPlusTree() -> std::string {
  if (IsEmpty()) {
    return "()";
  }

  PrintableBPlusTree p_root = ToPrintableBPlusTree(GetRootPageId());
  std::ostringstream out_buf;
  p_root.Print(out_buf);

  return out_buf.str();
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::ToPrintableBPlusTree(page_id_t root_id) -> PrintableBPlusTree {
  auto root_page_guard = bpm_->FetchPageBasic(root_id);
  auto root_page = root_page_guard.template As<BPlusTreePage>();
  PrintableBPlusTree proot;

  if (root_page->IsLeafPage()) {
    auto leaf_page = root_page_guard.template As<LeafPage>();
    proot.keys_ = leaf_page->ToString();
    proot.size_ = proot.keys_.size() + 4;  // 4 more spaces for indent

    return proot;
  }

  // draw internal page
  auto internal_page = root_page_guard.template As<InternalPage>();
  proot.keys_ = internal_page->ToString();
  proot.size_ = 0;
  for (int i = 0; i < internal_page->GetSize(); i++) {
    page_id_t child_id = internal_page->ValueAt(i);
    PrintableBPlusTree child_node = ToPrintableBPlusTree(child_id);
    proot.size_ += child_node.size_;
    proot.children_.push_back(child_node);
  }

  return proot;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
