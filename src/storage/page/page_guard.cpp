#include "storage/page/page_guard.h"
#include "buffer/buffer_pool_manager.h"

namespace bustub {

BasicPageGuard::BasicPageGuard(BasicPageGuard &&that) noexcept 
{
    this->bpm_ = that.bpm_;
    this->page_= that.page_;
    this->is_dirty_ = that.is_dirty_;

    that.bpm_=nullptr;
    that.page_=nullptr;
}

void BasicPageGuard::Drop() 
{
    if(this->bpm_!=nullptr && this->page_!=nullptr){
        this->bpm_->UnpinPage(page_->GetPageId(),is_dirty_);
    }
    this->bpm_=nullptr;
    this->page_=nullptr;
}

auto BasicPageGuard::operator=(BasicPageGuard &&that) noexcept -> BasicPageGuard & 
{ 
    //do nothing
    if(this==&that)
        return *this;

    Drop();
    this->bpm_=that.bpm_;
    this->page_=that.page_;
    this->is_dirty_=that.is_dirty_;

    that.bpm_=nullptr;
    that.page_=nullptr; 
    return *this;
}

BasicPageGuard::~BasicPageGuard(){
    Drop();
};  // NOLINT

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept = default;

auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & { return *this; }

void ReadPageGuard::Drop() {}

ReadPageGuard::~ReadPageGuard() {}  // NOLINT

WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept = default;

auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & { return *this; }

void WritePageGuard::Drop() {}

WritePageGuard::~WritePageGuard() {}  // NOLINT

}  // namespace bustub
