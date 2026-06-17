#pragma once

#include "util/noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>

namespace hound {

/// Chain (linked-list) buffer for socket I/O.
///
/// ## Why not a single contiguous buffer?
///
/// HTTP messages have unpredictable sizes — a GET might be 80 bytes, a POST
/// with a file upload might be megabytes. A single buffer that grows via
/// realloc would copy data on every expansion (O(n²) total). A ring buffer
/// with a fixed size would either truncate (data loss) or waste memory.
///
/// ## Design
///
/// The buffer is a linked list of 4 KB blocks. New data is appended to the
/// current write block; when a block fills, a new one is allocated.
/// Reading advances through blocks, freeing fully-consumed ones.
///
/// ```
///  head_ → [Block 0] → [Block 1] → [Block 2] → nullptr
///            ↑read      ↑write
/// ```
///
/// - `head_` points to the first block (never freed until the whole buffer is
///   destroyed). This simplifies the linked list and avoids O(n) prepend cost.
/// - `read_block_` tracks where the next read comes from.
/// - `write_block_` tracks where the next write goes.
/// - `size_` is the total readable bytes, maintained in O(1).
///
/// ## Performance characteristics
/// - append:  O(1) amortized (new block allocation is rare, 1 per 4 KB)
/// - consume: O(1) amortized (block free is rare)
/// - peek:    O(1) into current block, O(n) across blocks
/// - size:    O(1)
///
/// ## Zero-copy parsing
/// The HTTP parser operates on string_views pointing into the buffer blocks.
/// Since blocks are stable (never moved), these views remain valid until the
/// data is consumed via retrieve().
class Buffer : public NonCopyable {
public:
    static constexpr size_t kBlockSize = 4096;  // One page

    Buffer();
    ~Buffer();

    // ── Writing ──────────────────────────────────────────────
    /// Append data to the end of the buffer.
    void append(const char* data, size_t len);
    void append(const std::string_view& sv) { append(sv.data(), sv.size()); }
    void append(const std::string& s) { append(s.data(), s.size()); }

    /// Reserve space at the end and return a writable pointer.
    /// Returns nullptr if not enough contiguous space in the current block.
    /// Use this for scatter/gather I/O with readv().
    char* begin_write(size_t* writable);

    /// Commit bytes written via begin_write().
    void has_written(size_t len);

    /// Ensure at least `len` bytes of writable contiguous space.
    /// May allocate a new block if the current one is too full.
    /// After this call, begin_write() will succeed for `len` bytes.
    void ensure_writable(size_t len);

    // ── Reading ──────────────────────────────────────────────
    /// Get a string_view of all readable data.
    /// NOTE: This is NOT contiguous — if data spans multiple blocks,
    /// only the first block is returned. Use for scanning, not for
    /// passing to functions that expect the full data.
    std::string_view peek() const;

    /// Peek at data in the first readable block.
    const char* peek_data() const;
    size_t peek_size() const;  // Bytes in the FIRST readable block only

    /// Retrieve (consume) `len` bytes from the front.
    void retrieve(size_t len);

    /// Retrieve all bytes (equivalent to clear).
    void retrieve_all();

    /// Read a single byte without consuming it.
    /// Returns -1 if buffer is empty.
    int peek_byte() const;

    /// Find the first occurrence of `needle` in readable data.
    /// Returns the absolute offset from the read position, or -1 if not found.
    ssize_t find(const std::string_view& needle) const;
    ssize_t find(char c) const;

    /// Read until (and including) `delim`. Returns the slice or empty view.
    /// Does NOT consume — use retrieve(result.size()) to consume after.
    std::string_view read_until(const std::string_view& delim);

    /// Read exactly `len` bytes as a string.
    /// Returns empty string if not enough data.
    std::string read_n(size_t len);

    // ── State ────────────────────────────────────────────────
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    /// Total capacity across all blocks.
    size_t capacity() const;

    /// Total number of allocated blocks.
    size_t block_count() const;

private:
    struct Block {
        char data[kBlockSize];
        size_t read_idx = 0;   // First byte to read
        size_t write_idx = 0;  // First free byte for writing
        Block* next = nullptr;

        size_t readable() const { return write_idx - read_idx; }
        size_t writable() const { return kBlockSize - write_idx; }
        void reset() { read_idx = 0; write_idx = 0; }
    };

    Block* alloc_block();

    Block head_;         // Sentinel: always exists, never freed
    Block* read_block_;  // Where the next read comes from
    Block* write_block_; // Where the next write goes to
    size_t size_ = 0;
};

} // namespace hound
