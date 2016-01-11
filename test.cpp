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

#include <iostream>
#include <bitset>

using namespace std;

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


#include "num_util.hpp"
#include "perftools.hpp"

using namespace std;
int main(int argc, char **argv) {
  // float x = 10;
  // bitset<24> f = get_significant(x);
  // bitset<8> e = get_exponent(x);
  // cerr << f << " x 2^" << e << endl;

  for (float i=1; i<128; i*=2) {
    for (float j=1; j<i; j*=2)
      cout << "\t";
    for (float j=i; j<128; j*=2)
      cout << quantize(float(j), get_exponent(j), get_exponent(i)) << "\t";
    cout << endl;
  }
  return 0;
}
