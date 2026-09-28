//
//  heap.h
//  Kraken Engine / Mimir
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

#pragma once

#include <cstddef>

#include "region.h"

namespace mimir {

struct TLSFBlock;

// Heap implementation based on Two-Level Segregated Fit (TLSF) memory allocation
class Heap
{
public:
  Heap();
  ~Heap();

  bool init(size_t minSize = 1ULL << 24, size_t maxSize = 1ULL << 32);

  // Allocate `size` bytes
  std::byte* alloc(size_t size);

  // Allocate `size` bytes, aligned to 16 bytes and padded to next 16-byte offset.
  std::byte* allocA16(size_t size);

  // Allocate `size` bytes, aligned to 64 bytes and padded to next 64-byte offset.
  std::byte* allocA64(size_t size);

  // Free the allocation at `address`
  void free(std::byte* address);

  // Reset the heap, freeing all memory and potentially releasing comitted pages.
  void reset();

  // Get the actual used size.
  // This may differ from the sum of allocations due to alignment requirements.
  size_t getUsed() const;

  // Get the actual maximum size.
  // This may be greater than the maxSize passed into init, due to page size alignment.
  size_t getMaxSize() const;

private:
  Region m_region;
  size_t m_minSize;
  size_t m_usedSize;

  // Add a free block to the index
  void insertFreeBlock(TLSFBlock* block);

  // Remove a free block from the index
  void removeFreeBlock(TLSFBlock* block);

  // Find a free block that can hold at least size bytes
  TLSFBlock* findFreeBlock(size_t size) const;

  // Get the usable size of a block
  size_t getBlockUsableSize(const TLSFBlock* block) const;
};

} // namespace mimir
