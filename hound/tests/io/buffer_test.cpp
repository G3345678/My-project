#include <gtest/gtest.h>

#include "io/buffer.h"

using namespace hound;

// ── Basic Operations ─────────────────────────────────────────

TEST(BufferTest, EmptyOnCreation) {
    Buffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

TEST(BufferTest, AppendAndPeek) {
    Buffer buf;
    buf.append("hello", 5);

    EXPECT_EQ(buf.size(), 5u);
    EXPECT_FALSE(buf.empty());

    auto view = buf.peek();
    EXPECT_EQ(view, "hello");
}

TEST(BufferTest, AppendMultiplePieces) {
    Buffer buf;
    buf.append("foo", 3);
    buf.append("bar", 3);

    EXPECT_EQ(buf.size(), 6u);

    // peek() only returns the first contiguous block
    auto view = buf.peek();
    EXPECT_EQ(view, "foobar") << "Data should be contiguous for small appends";
}

TEST(BufferTest, Retrieve) {
    Buffer buf;
    buf.append("hello world", 11);

    buf.retrieve(6);  // consume "hello "
    EXPECT_EQ(buf.size(), 5u);

    auto view = buf.peek();
    EXPECT_EQ(view, "world");
}

TEST(BufferTest, RetrieveAll) {
    Buffer buf;
    buf.append("data", 4);
    buf.retrieve_all();

    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

// ── Large Data (Multi-block) ─────────────────────────────────

TEST(BufferTest, HandlesMultiBlockData) {
    Buffer buf;

    // Write 10 KB — should span 3 blocks (4 KB each)
    std::string chunk(1024, 'A');  // 1 KB of 'A'
    for (int i = 0; i < 10; ++i) {
        buf.append(chunk);
    }

    EXPECT_EQ(buf.size(), 10u * 1024u);
    EXPECT_GE(buf.block_count(), 3u);

    // Peek returns first block only
    EXPECT_EQ(buf.peek().size(), Buffer::kBlockSize);

    // But we can read all data
    std::string all = buf.read_n(10 * 1024);
    EXPECT_EQ(all.size(), 10u * 1024u);
    for (char c : all) {
        EXPECT_EQ(c, 'A');
    }
}

TEST(BufferTest, RetrieveAcrossBlocks) {
    Buffer buf;

    // Fill first block completely
    std::string data(Buffer::kBlockSize, 'X');
    buf.append(data);
    buf.append("tail", 4);

    EXPECT_EQ(buf.size(), Buffer::kBlockSize + 4);

    // Retrieve the entire first block
    buf.retrieve(Buffer::kBlockSize);
    EXPECT_EQ(buf.size(), 4u);
    EXPECT_EQ(buf.peek(), "tail");
}

// ── Search ───────────────────────────────────────────────────

TEST(BufferTest, FindChar) {
    Buffer buf;
    buf.append("hello\r\nworld", 12);

    ssize_t pos = buf.find('\r');
    EXPECT_EQ(pos, 5);
}

TEST(BufferTest, FindCharNotFound) {
    Buffer buf;
    buf.append("hello", 5);

    ssize_t pos = buf.find('z');
    EXPECT_EQ(pos, -1);
}

TEST(BufferTest, FindString) {
    Buffer buf;
    buf.append("GET /index.html HTTP/1.1\r\n", 28);

    ssize_t pos = buf.find("\r\n");
    EXPECT_EQ(pos, 26) << "CRLF should be at end of request line";
}

TEST(BufferTest, FindStringAcrossBlocks) {
    Buffer buf;

    // Fill first block, then put "FINDME" at the boundary
    std::string prefix(Buffer::kBlockSize - 3, 'X');
    buf.append(prefix);
    buf.append("FINDME", 6);
    buf.append("suffix", 6);

    ssize_t pos = buf.find("FINDME");
    EXPECT_GE(pos, 0);
}

TEST(BufferTest, PeekByte) {
    Buffer buf;
    buf.append("ABC", 3);

    EXPECT_EQ(buf.peek_byte(), 'A');
    buf.retrieve(1);
    EXPECT_EQ(buf.peek_byte(), 'B');
    buf.retrieve(2);
    EXPECT_EQ(buf.peek_byte(), -1);
}

// ── Write Reserve ────────────────────────────────────────────

TEST(BufferTest, BeginWriteAndCommit) {
    Buffer buf;

    size_t writable = 0;
    char* ptr = buf.begin_write(&writable);
    ASSERT_NE(ptr, nullptr);
    EXPECT_GE(writable, 1u);

    // Write directly into the buffer
    std::memcpy(ptr, "direct", 6);
    buf.has_written(6);

    EXPECT_EQ(buf.size(), 6u);
    EXPECT_EQ(buf.peek(), "direct");
}

TEST(BufferTest, EnsureWritableAllocatesWhenNeeded) {
    Buffer buf;

    // Fill current block
    std::string data(Buffer::kBlockSize, 'Z');
    buf.append(data);

    // Now ensure we have space for more
    buf.ensure_writable(100);

    size_t writable = 0;
    char* ptr = buf.begin_write(&writable);
    EXPECT_GE(writable, 100u);
    EXPECT_NE(ptr, nullptr);
}

// ── Edge Cases ───────────────────────────────────────────────

TEST(BufferTest, AppendEmptyData) {
    Buffer buf;
    buf.append(nullptr, 0);
    buf.append("", 0);
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_TRUE(buf.empty());
}

TEST(BufferTest, RetrieveMoreThanAvailable) {
    Buffer buf;
    buf.append("data", 4);

    // Retrieving more than available should clamp
    buf.retrieve(10);
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_TRUE(buf.empty());
}

TEST(BufferTest, RetrieveFromEmptyBuffer) {
    Buffer buf;
    buf.retrieve(5);  // Should not crash
    EXPECT_EQ(buf.size(), 0u);
}

TEST(BufferTest, ReadNMoreThanAvailable) {
    Buffer buf;
    buf.append("small", 5);

    std::string result = buf.read_n(100);
    EXPECT_TRUE(result.empty());
}

TEST(BufferTest, Capacity) {
    Buffer buf;
    size_t initial_cap = buf.capacity();
    EXPECT_GE(initial_cap, Buffer::kBlockSize);

    // Append enough to force a new block
    std::string data(Buffer::kBlockSize + 1, 'Y');
    buf.append(data);

    EXPECT_GT(buf.capacity(), initial_cap);
    EXPECT_GE(buf.block_count(), 2u);
}
