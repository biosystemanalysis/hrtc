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
#include <boost/integer.hpp>
using boost::int_t;
using boost::uint_t;

#include "common.hpp"

/// quantization

template<typename Real>
typename int_t<bitsizeof<Real>()>::least
quantize(Real v, Real quantum) {
  return round(v / quantum);
}

template<typename Real>
Real quant2real(typename int_t<bitsizeof<Real>()>::least i, Real quantum) {
  return i * quantum;
}

// Convert a signed integer into an unsigned by storing the sign in
// the least significant bit. This allows to efficiently store small
// negative quantities using the integer_encoding_library (which only
// deals well with small unsigned values).
uint32_t signed2unsigned(int32_t v) {
  return (v >= 0) ? ((uint32_t(    v ) << 1) | 0)
                  : ((uint32_t(abs(v)) << 1) | 1);
}

int32_t unsigned2signed(uint32_t v) {
  return (v & 1) ? -(v >> 1)
                 :  (v >> 1);
}
