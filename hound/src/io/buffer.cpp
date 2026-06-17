#include "io/buffer.h"
#include "util/logger.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <new>

namespace hound {

// ─────────────────────────────────────────────────────────────
//  Construction / Destruction
// ─────────────────────────────────────────────────────────────

Buffer::Buffer()
    : read_block_(&head_)
    , write_block_(&head_)
{
    // head_ is a sentinel block — it participates in the linked list
    // but its data array is used like any other block.
    head_.reset();
}

Buffer::~Buffer() {
    // Free all blocks except the sentinel (head_ is not heap-allocated).
    Block* block = head_.next;
    while (block) {
        Block* next = block->next;
        delete block;
        block = next;
    }
}

Buffer::Block* Buffer::alloc_block() {
    Block* block = new (std::nothrow) Block();
    if (!block) {
        LOG_FATAL("Buffer: out of memory allocating block");
    }
    block->reset();
    return block;
}

// ─────────────────────────────────────────────────────────────
//  Writing
// ─────────────────────────────────────────────────────────────

void Buffer::append(const char* data, size_t len) {
    if (len == 0) return;

    size_t remaining = len;
    const char* src = data;

    while (remaining > 0) {
        size_t writable = write_block_->writable();

        if (writable == 0) {
            // Current block is full — allocate a new one.
            if (!write_block_->next) {
                write_block_->next = alloc_block();
            }
            write_block_ = write_block_->next;
            continue;
        }

        size_t to_copy = std::min(remaining, writable);
        std::memcpy(write_block_->data + write_block_->write_idx,
                    src, to_copy);

        write_block_->write_idx += to_copy;
        src += to_copy;
        remaining -= to_copy;
        size_ += to_copy;
    }
}

char* Buffer::begin_write(size_t* writable) {
    // If the current write block is full, allocate a new one.
    if (write_block_->writable() == 0) {
        if (!write_block_->next) {
            write_block_->next = alloc_block();
        }
        write_block_ = write_block_->next;
    }

    *writable = write_block_->writable();
    return write_block_->data + write_block_->write_idx;
}

void Buffer::has_written(size_t len) {
    if (len > write_block_->writable()) {
        LOG_ERROR("Buffer::has_written: %zu exceeds writable bytes (%zu)",
                  len, write_block_->writable());
        len = write_block_->writable();
    }
    write_block_->write_idx += len;
    size_ += len;
}

void Buffer::ensure_writable(size_t len) {
    if (write_block_->writable() >= len) return;

    // Allocate a new block. We don't compact — simplicity over memory.
    if (!write_block_->next) {
        write_block_->next = alloc_block();
    }
    write_block_ = write_block_->next;
}

// ─────────────────────────────────────────────────────────────
//  Reading
// ─────────────────────────────────────────────────────────────

std::string_view Buffer::peek() const {
    if (size_ == 0) return {};

    return std::string_view(
        read_block_->data + read_block_->read_idx,
        read_block_->readable());
}

const char* Buffer::peek_data() const {
    if (size_ == 0) return nullptr;
    return read_block_->data + read_block_->read_idx;
}

size_t Buffer::peek_size() const {
    if (size_ == 0) return 0;
    return read_block_->readable();
}

int Buffer::peek_byte() const {
    if (size_ == 0) return -1;
    return static_cast<unsigned char>(
        read_block_->data[read_block_->read_idx]);
}

void Buffer::retrieve(size_t len) {
    if (len == 0) return;
    if (len > size_) {
        LOG_WARN("Buffer::retrieve: %zu > size %zu — clamping", len, size_);
        len = size_;
    }

    size_t remaining = len;

    while (remaining > 0 && read_block_ != write_block_) {
        size_t readable = read_block_->readable();
        size_t to_consume = std::min(remaining, readable);

        read_block_->read_idx += to_consume;
        remaining -= to_consume;
        size_ -= to_consume;

        if (read_block_->read_idx == read_block_->write_idx) {
            // This block is fully consumed — advance to next.
            read_block_ = read_block_->next;
            // NOTE: We do NOT free the block. head_ is a sentinel,
            // and intermediate blocks are kept for potential reuse.
            // Blocks are freed on destruction only.
        }
    }

    // Handle the case where read_block_ == write_block_
    if (remaining > 0) {
        size_t readable = read_block_->readable();
        size_t to_consume = std::min(remaining, readable);
        read_block_->read_idx += to_consume;
        size_ -= to_consume;
        remaining -= to_consume;
    }

    // Compact: if read_block_ has advanced past head_, shift data back
    // into head_ to avoid an ever-growing chain of depleted blocks.
    if (read_block_ != &head_ && size_ > 0) {
        // Move remaining data to head_
        size_t saved = size_;
        std::string data = read_n(saved);
        retrieve_all();
        append(data);
        (void)saved; // suppress unused warning
    }
}

void Buffer::retrieve_all() {
    // Reset all blocks
    Block* block = head_.next;
    while (block) {
        block->reset();
        block = block->next;
    }
    head_.reset();

    read_block_ = &head_;
    write_block_ = &head_;
    size_ = 0;
}

// ─────────────────────────────────────────────────────────────
//  Search
// ─────────────────────────────────────────────────────────────

ssize_t Buffer::find(const std::string_view& needle) const {
    if (needle.empty() || size_ < needle.size()) return -1;

    // Boyer-Moore would be overkill. Linear scan with two pointers
    // across the chain is simple and correct.
    //
    // Optimization: we scan one byte at a time. For small needles
    // (the common case: "\r\n", ":", " "), this is faster than
    // building a skip table.
    size_t offset = 0;
    const Block* block = read_block_;

    while (block) {
        size_t readable = block->readable();
        for (size_t i = 0; i < readable && offset + i + needle.size() <= size_; ++i) {
            // Check if needle matches starting at this position
            bool match = true;
            const Block* search_block = block;
            size_t search_idx = i;
            size_t matched = 0;

            while (matched < needle.size()) {
                size_t search_readable = search_block->readable();
                size_t available = search_readable - search_idx;
                size_t to_check = std::min(needle.size() - matched, available);

                if (std::memcmp(search_block->data + search_block->read_idx + search_idx,
                                needle.data() + matched, to_check) != 0) {
                    match = false;
                    break;
                }

                matched += to_check;
                if (matched >= needle.size()) break;

                search_block = search_block->next;
                search_idx = 0;
                if (!search_block) break;
            }

            if (match) return static_cast<ssize_t>(offset + i);
        }

        offset += readable;
        block = block->next;
    }

    return -1;
}

ssize_t Buffer::find(char c) const {
    if (size_ == 0) return -1;

    size_t offset = 0;
    const Block* block = read_block_;

    while (block) {
        size_t readable = block->readable();
        const char* start = block->data + block->read_idx;
        const char* pos = static_cast<const char*>(
            std::memchr(start, c, readable));

        if (pos) {
            return static_cast<ssize_t>(offset + (pos - start));
        }

        offset += readable;
        block = block->next;
    }

    return -1;
}

std::string_view Buffer::read_until(const std::string_view& delim) {
    ssize_t pos = find(delim);
    if (pos < 0) return {};  // Not found

    // Peek up to and including the delimiter.
    // Since the delimiter might span blocks, we need to copy into a
    // temporary. Return the first block's view for the common case.
    size_t total = static_cast<size_t>(pos) + delim.size();

    // Common case: all in the first block
    if (total <= read_block_->readable()) {
        return std::string_view(
            read_block_->data + read_block_->read_idx, total);
    }

    // Rare case: spans blocks — return empty and let caller use read_n
    return {};
}

std::string Buffer::read_n(size_t len) {
    if (len > size_) return {};

    std::string result;
    result.reserve(len);

    Block* block = read_block_;
    size_t remaining = len;

    while (remaining > 0 && block) {
        size_t readable = block->readable();
        size_t to_copy = std::min(remaining, readable);
        result.append(block->data + block->read_idx, to_copy);
        remaining -= to_copy;
        block = block->next;
    }

    return result;
}

// ─────────────────────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────────────────────

size_t Buffer::capacity() const {
    size_t total = 0;
    total += head_.writable() + head_.readable();  // sentinel block

    const Block* block = head_.next;
    while (block) {
        total += kBlockSize;
        block = block->next;
    }
    return total;
}

size_t Buffer::block_count() const {
    size_t count = 1;  // head_
    const Block* block = head_.next;
    while (block) {
        ++count;
        block = block->next;
    }
    return count;
}

} // namespace hound
