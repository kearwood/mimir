//
//  arena.cpp
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

#include <cassert>
#include <cstring>
#include <bit>

namespace mimir {

Arena::Arena()
  : m_minSize(0)
  , m_usedSize(0)
{
  for (size_t i = 0; i < kWatermarkLen; i++) {
    m_watermark[i] = 0;
  }
}

Arena::~Arena()
{
}

bool Arena::init(size_t minSize, size_t maxSize)
{
  assert(maxSize >= minSize);
  assert(m_usedSize == 0);

  m_minSize = KRAKEN_MEM_ROUND_UP_PAGE(minSize);
  return m_region.init(maxSize);
}

// Get the actual used size.
// This may differ from the sum of allocations due to alignment requirements.
size_t Arena::getUsed() const
{
  return m_usedSize;
}

// Get the actual maximum size.
// This may be greater than the maxSize passed into init, due to page size alignment.
size_t Arena::getMaxSize() const
{
  return m_region.getSize();
}

std::byte* Arena::alloc(size_t size)
{
  size_t neededSize = m_usedSize + size;
  if (neededSize > m_region.getSize())
  {
    // Increase the size exponentially when we run out
    size_t newSize = std::bit_ceil(neededSize) << 1;
    if (newSize < m_minSize) {
      newSize = m_minSize;
    }
    if (!m_region.resize(newSize)) {
      return nullptr;
    }
  }

  std::byte* ret = m_region.getAddress() + m_usedSize;
  m_usedSize = neededSize;
  return ret;
}

std::byte* Arena::allocA16(size_t size)
{
  size_t nextByte = (m_usedSize + 15) & ~15;
  size_t roundedSize = (size + 15) & ~15;
  if (alloc(roundedSize + nextByte - m_usedSize) == nullptr)     {
    return nullptr;
  }

  return m_region.getAddress() + nextByte;
}

std::byte* Arena::allocA64(size_t size)
{
  size_t nextByte = (m_usedSize + 63) & ~63;
  size_t roundedSize = (size + 63) & ~63;
  if (alloc(roundedSize + nextByte - m_usedSize) == nullptr) {
    return nullptr;
  }

  return m_region.getAddress() + nextByte;
}

void Arena::reset()
{
  size_t highWatermark = m_usedSize;
  for (size_t i = 0; i < kWatermarkLen - 1; i++) {
    m_watermark[i] = m_watermark[i + 1];
    if (m_watermark[i] > highWatermark) {
      highWatermark = m_watermark[i];
    }
  }
  m_watermark[kWatermarkLen - 1] = m_usedSize;

  size_t targetSize = std::bit_ceil(highWatermark) << 1;
  if (targetSize < m_minSize) {
    targetSize = m_minSize;
  }
  size_t thresholdSize = targetSize << 1; // The threshold for shrinking is greater than the target to implement hysteresis
  if (m_region.getSize() > thresholdSize) {
    m_region.resize(targetSize);
  }

  m_usedSize = 0;
}

} // namespace mimir
