/* Copyright 2014-2016 Jan Huwald, Stephan Richter

   This file is part of HRTC.

   HRTC is free software: you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   HRTC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with this program (see file LICENSE).  If not, see
   <http://www.gnu.org/licenses/>. */

#pragma once

#include <assert.h>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
using namespace std;

#include <boost/dynamic_bitset.hpp>
#include <boost/optional.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>
using boost::dynamic_bitset;
using boost::optional;
namespace prog_options = boost::program_options;


#include <integer_encoding.hpp>
using integer_encoding::EncodingPtr;


// space time points
typedef uint64_t Time;
const Time maxTime = Time(1) << 48;

typedef uint16_t TId;
const TId maxTId = -1;

union STP {
  uint64_t raw;
  struct {
    Time time : 48;
    TId  id   : 16; // trajectory id
  };
  STP& operator=  (STP const &s) { raw = s.raw; return *this; }
  bool operator== (STP const &s) const { return raw == s.raw; }

};

#define GEN(op)								\
  bool operator op (STP p1, STP p2) {					\
    static_assert(sizeof(p1) == sizeof(p1.raw), "STP not packed");	\
    return (p1.time op p2.time) || ((p1.time == p2.time) && (p1.id op p2.id)); \
  }
GEN(<)
GEN(>)
#undef GEN

// support vector
struct SVI {
  // To save space using the variable length encoding, dt-1 is
  // stored. This works as dt >= 1 is guaranteed.
  uint32_t dt;
  // Signed difference, encoded via signed2unsigned
  uint32_t v;
};

// A buffer that stores SVI structs (x0, v0), ..., (xn, vn) in memory
// as xn, ..., x0, v0, ..., vn and helps with (de)compression. This
// keeps numbers of the same magnitude proximate in memory while
// compressing only one buffer (storing only one size, not copying
// memory arounds)
//
// ATTENTION pointer mangling: uncompressed stores the center of the
// buffer pointed to by uncompressed_full. Elements are appended by
// growing in both directions.
struct SplitSVIBuffer {
	size_t size, compressed_size; // no. of uint32_t, not bytes!
	uint32_t *uncompressed_full,
		*uncompressed,
		*compressed;
	EncodingPtr codec;

	// init with max. number of pairs to store
	SplitSVIBuffer(EncodingPtr codec, size_t size)
		: size(size),
		  compressed_size(codec->require(2 * size)),
		  uncompressed_full(new uint32_t[DECODE_REQUIRE_MEM(2 * size)]),
		  uncompressed(uncompressed_full + size),
		  compressed(new uint32_t[compressed_size]),
		  codec(codec) {
	}

	void set(size_t pos, SVI val) {
		*(uncompressed + pos)     = val.dt;
		*(uncompressed - pos - 1) = val.v;
	}

	SVI get(size_t pos) {
		SVI res;
		res.dt = *(uncompressed + pos);
		res.v  = *(uncompressed - pos - 1);
		return res;
	}

	// return pointer to buf, buf size in uint32_t
	tuple<uint32_t*, size_t> encode(size_t numSVI) {
		auto res_size = compressed_size;
		codec->encodeArray(uncompressed - numSVI, numSVI * 2, compressed, &res_size);
		return make_tuple(compressed, res_size);
	}

	void decode(size_t numSVI, size_t csize) {
		codec->decodeArray(compressed, csize, uncompressed - numSVI, 2 * numSVI);
	}

	~SplitSVIBuffer() {
		delete[] uncompressed_full;
		delete[] compressed;
	}
};


struct ChunkSize {
  uint32_t raw; // uncompressed size in bytes
  uint32_t compressed; //   compressed size in bytes
};


template<typename Src, typename Dst>
Dst bit_convert(Src s) {
  static_assert(sizeof(Src) == sizeof(Dst), "bit size mismatch");
  union {
    Src s;
    Dst d;
  } c;
  c.s = s;
  return c.d;
}

template<typename T>
constexpr int bitsizeof() { return 8 * sizeof(T); }

char* hrtc_version_string() {
#define str(s) #s
#define xstr(s) str(s)
	return (char*) xstr(HRTC_VERSION);
#undef str
}
