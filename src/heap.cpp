//
//  heap.cpp
//  Kraken Engine
//
//  Copyright 2026 Kearwood Gilbert. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are
//  permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice, this list of
//  conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice, this list
//  of conditions and the following disclaimer in the documentation and/or other materials
//  provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY KEARWOOD GILBERT ''AS IS'' AND ANY EXPRESS OR IMPLIED
//  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KEARWOOD GILBERT OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
//  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//  The views and conclusions contained in the software and documentation are those of the
//  authors and should not be interpreted as representing official policies, either expressed
//  or implied, of Kearwood Gilbert.
//

#include "../include/mimir.h"
#include "mimir_impl.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <bit>

namespace mimir {

// Two-Level Segregated Fit (TLSF): http://www.gii.upv.es/tlsf/files/papers/ecrts04_tlsf.pdf

const size_t kMinBlockSize = 16;

struct TLSFBlock
{
  size_t size; // LSB: T, F: T = Last physical block, F = Free block
  TLSFBlock* prevPhys;
  TLSFBlock* prevFree;
  TLSFBlock* nextFree;
};
static_assert(sizeof(TLSFBlock) == 32);

struct TLSFIndex
{
  uint64_t firstLevelFreeBitMap;
  uint8_t secondLevelFreeBitMap[60];
  TLSFBlock* secondLevelFreeBlocks[60][16];
};

// First Level Ranges:
// 0:  0b00000000010000  (    1 ~   16 )
// 1:  0b00000000100000  (   17 ~   32 )
// 2:  0b00000001000000  (   33 ~   64 )
// 3:  0b00000010000000  (   65 ~  128 )
// 4:  0b00000100000000  (  129 ~  256 )
// 5:  0b00001000000000  (  257 ~  512 )
// 6:  0b00010000000000  (  513 ~ 1024 )
// 7:  0b00100000000000  ( 1025 ~ 2048 )
//
// bit_width(15): 4    bit_floor(15): 0b000001000
// bit_width(16): 5    bit_floor(16): 0b000010000
// bit_width(31): 5    bit_floor(31): 0b000010000
// bit_width(32): 6    bit_floor(32): 0b000100000
// bit_width(63): 6    bit_floor(63): 0b000100000
// bit_width(64): 7    bit_floor(64): 0b001000000

/*
uint64_t blockSizeToIndex(uint64_t size)
{
  // MSB 60 bits are the first level index
  // LSB 4 bits is the second level index

  // size              0b0000001101011010000
  // bit_width         13
  // bit_width - 5     8
  // size >> 8         0b0000000000000011010
  //   & 0b1111        0b1010

  size_t firstLevelIndex = std::bit_width(size) - 5;
  size_t secondLevelIndex = (size >> firstLevelIndex) & 0b1111;
  return (firstLevelIndex << 4) | secondLevelIndex;
}
*/

Heap::Heap()
  : m_minSize(0)
  , m_usedSize(0)
{
}

Heap::~Heap()
{
}

bool Heap::init(size_t minSize, size_t maxSize)
{
  assert(maxSize >= minSize);
  assert(m_usedSize == 0);

  m_minSize = sizeof(TLSFIndex) + 16;
  m_minSize = std::max(m_minSize, minSize);
  m_minSize = KRAKEN_MEM_ROUND_UP_PAGE(m_minSize);
  if (!m_region.init(maxSize)) {
    return false;
  }
  if (!m_region.resize(m_minSize)) {
    return false;
  }
  reset();
  return true;
}

// Get the actual maximum size.
// This may be greater than the maxSize passed into init, due to page size alignment.
size_t Heap::getMaxSize() const
{
  return m_region.getSize();
}

// Get the actual used size.
// This may differ from the sum of allocations due to alignment requirements.
size_t Heap::getUsed() const
{
  return m_usedSize;
}

size_t Heap::getBlockUsableSize(const TLSFBlock* block) const
{
  return (block->size & ~0b11) - 16;
}

void Heap::insertFreeBlock(TLSFBlock* block)
{
  // Add the block to the index

  TLSFIndex* index = (TLSFIndex*)m_region.getAddress();

  size_t usableSize = getBlockUsableSize(block);
  size_t firstLevel = std::bit_width(usableSize);
  size_t firstLevelIndex = firstLevel - 5;
  size_t secondLevelIndex = (usableSize >> firstLevelIndex) & 0b1111ULL;

  // Update free bitmaps
  index->firstLevelFreeBitMap |= std::bit_floor(usableSize);
  index->secondLevelFreeBitMap[firstLevelIndex] |= secondLevelIndex;

  TLSFBlock* prevFirstFreeBlock = index->secondLevelFreeBlocks[firstLevelIndex][secondLevelIndex];
  index->secondLevelFreeBlocks[firstLevelIndex][secondLevelIndex] = block;

  block->prevFree = nullptr;
  if (prevFirstFreeBlock) {
    block->nextFree = prevFirstFreeBlock;
    prevFirstFreeBlock->prevFree = block;
  }

  // Mark block as free
  block->size |= 0b01; // T=1: Last Block, F=1: Free Block
}

void Heap::removeFreeBlock(TLSFBlock* block)
{
  // Remove the block from the index
  TLSFIndex* index = (TLSFIndex*)m_region.getAddress();

  size_t usableSize = getBlockUsableSize(block);
  size_t firstLevel = std::bit_width(usableSize);
  size_t firstLevelIndex = firstLevel - 5;
  size_t secondLevelIndex = (usableSize >> (firstLevelIndex)) & 0b1111ULL;

  if (block->prevFree != nullptr) {
    // Link the neighboring free blocks together
    block->prevFree->nextFree = block->nextFree;
    if (block->nextFree) {
      block->nextFree->prevFree = block->prevFree;
    }
  } else {
    // This block was the first for this level
    index->secondLevelFreeBlocks[firstLevelIndex][secondLevelIndex] = block->nextFree;

    if (block->nextFree != nullptr) {
      block->nextFree->prevFree = nullptr;
    } else {
      // We have removed all the free blocks at this level.

      // Update second level bitmask.
      index->secondLevelFreeBitMap[firstLevelIndex] &= ~(1ULL << secondLevelIndex);

      // Check if there are any remaining free blocks within the first level
      if (index->secondLevelFreeBitMap[firstLevelIndex] == 0) {

        // This was the last one.  Clear the first level bit as well.
        index->firstLevelFreeBitMap &= ~(1ULL << firstLevel);
      }
    }
  }

  // Mark block as non-free
  block->size &= ~0b01; // T=1: Last Block, F=1: Free Block
}

// Find a free block that can hold at least size bytes
TLSFBlock* Heap::findFreeBlock(size_t size) const
{
  if (size < kMinBlockSize) {
    size = kMinBlockSize;
  }
  TLSFIndex* index = (TLSFIndex*)m_region.getAddress();

  // size:           0b0000000000010000 (16)
  // bit_ceil(size): 0b0000000000010000
  // - 1 ...         0b0000000000001111
  // ~ ...           0b1111111111110000

  // size:           0b0000001110011101 (925)
  // bit_ceil(size): 0b0000010000000000
  // - 1 ...         0b0000001111111111
  // ~ ...           0b1111110000000000

  uint64_t possibleFirstLevels = ~(std::bit_floor(size) - 1);
  uint64_t freeFirstLevels = index->firstLevelFreeBitMap & possibleFirstLevels;
  uint64_t selectedFirstLevel = std::countr_zero(freeFirstLevels);
  if (selectedFirstLevel == 64) {
    // No first level with free blocks of sufficient size.
    return nullptr;
  }

  uint64_t firstLevelBufferMinSize = 1ULL << selectedFirstLevel;
  uint64_t secondLevelMask = 0b1111ULL;
  if (size > firstLevelBufferMinSize) {
    uint64_t delta = size - firstLevelBufferMinSize;
    secondLevelMask = (delta >> (selectedFirstLevel - 4)) & 0b1111ULL;
  }
  uint64_t possibleSecondLevels = secondLevelMask & index->secondLevelFreeBitMap[selectedFirstLevel - 4];
  uint64_t selectedSecondLevel = std::countr_zero(possibleSecondLevels);
  if (selectedSecondLevel == 64) {
    // No free blocks available at this first level.

    // Select the next highest available first level.
    selectedFirstLevel = std::countr_zero(freeFirstLevels & ~(1ULL << selectedFirstLevel));
    if (selectedFirstLevel == 64) {
      // No other first level with free blocks of sufficient size.
      return nullptr;
    }

    // Any buffer at the second level will fit this allocation. Pick the smallest buffer.
    selectedSecondLevel = std::countr_zero(index->secondLevelFreeBitMap[selectedFirstLevel - 4]);
  }

  // Take the first free block
  TLSFBlock* block = index->secondLevelFreeBlocks[selectedFirstLevel - 4][selectedSecondLevel];

  return block;
}

// Allocate `size` bytes
std::byte* Heap::alloc(size_t size)
{
  if (size < kMinBlockSize) {
    size = kMinBlockSize;
  }

  TLSFBlock* block = findFreeBlock(size);
  if (block == nullptr) {
    // No free block found
    return nullptr;
  }

  removeFreeBlock(block);

  size_t usableSize = getBlockUsableSize(block);

  if (size - usableSize >= sizeof(TLSFBlock)) {
    // Shrink the block to size; allocate a new free block for the remainder

    TLSFBlock* freeBlock = (TLSFBlock*)((std::byte*)block + size + 16ULL);
    freeBlock->size = size - usableSize;
    freeBlock->prevPhys = block;
    freeBlock->prevFree = nullptr;
    freeBlock->nextFree = nullptr;
    if (block->size & 0b10) { // LSB: T, F: T = Last physical block, F = Free block
      // Block was the last physical block
      block->size &= ~0b10;
      freeBlock->size &= 0b10;
    }
    block->size -= freeBlock->size & ~0b10;
    insertFreeBlock(freeBlock);
  }

  m_usedSize += size;
  return (std::byte*)block + 16;
}

// Allocate `size` bytes, aligned to 16 bytes and padded to next 16-byte offset.
std::byte* Heap::allocA16(size_t size)
{
  return nullptr; // not implemented
}

// Allocate `size` bytes, aligned to 64 bytes and padded to next 64-byte offset.
std::byte* Heap::allocA64(size_t size)
{
  return nullptr; // not implemented
}

// Free the allocation at `address`
void Heap::free(std::byte* address)
{
}

// Reset (or initialize) the heap, freeing all memory and potentially releasing comitted pages.
void Heap::reset()
{
  // Clear the index, representing no free blocks
  TLSFIndex* index = (TLSFIndex*)m_region.getAddress();
  memset(index, 0, sizeof(TLSFIndex));

  // Start with one free block, filling the entire Region
  TLSFBlock* block = (TLSFBlock*)(m_region.getAddress() + sizeof(TLSFIndex));
  block->prevPhys = nullptr;
  block->nextFree = nullptr;
  block->prevFree = nullptr;
  block->size = m_region.getMaxSize();
  block->size |= 0b10; // T=1: Last Block, F=1: Free Block

  insertFreeBlock(block);
}

} // namespace mimir
