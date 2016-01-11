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

include <iostream>
#include <bitset>
using namespace std;
#include "num_util.hpp"

template<typename Real>
ostream& fail(ostream &o, Real r, string msg) {
	//  BitReal<Real> br(r);
	
  o << "FAILURE " << msg << " for " << r << endl;
    // << "\n\tfor " << real_name<Real>() << " " << r 
    // << "\n\tsign: " << br.sign
    // << "\n\tsignificant:" << br.significant << " == " << bitset<fraction_width<Real>()-1>(br.significant)
    // << "\n\texponent:" << br.exponent
    // << "\t(min: " << mine << ", max: " << maxe << ")\n";
  return o;
}


template<typename Real>
bool test_full() {
  typedef typename int_t<bitsizeof<Real>()>::least Int;
  typedef typename uint_t<bitsizeof<Real>()>::least UInt;
  Real error = 0.04;
  Real quant = error/2;

  auto r2i = [=](Real r) { return quantize(r, quant); };
  auto i2r = [=](Int i) { return quant2real<Real>(i, quant); };

  // loop over all Real values
  union {
    Int i;
    UInt u;
    Real r;
  };
  u = 0;
  Real r2;
  Int q;
  do {
    if (!(i & ((1 << 20)-1))) {
      cerr << "\033[K" << i << "\t/ " << UInt(-1)
    	   << "\t(" << (double(i) / double(UInt(-1)) * 100) << "%)" 
	   << "\r" << flush;
      //      fail(cerr, r, "NOT YET HAPPENED", maxe, mine);

    }

    // ignore invalid values, infinity and nan
    if (!isfinite(r)) goto pass;
    if (fabs(r) > 100) goto pass;

    // ignore values that are larger than max exponent
    // TODO check if neccessary    if (get_exponent(r) > maxe) goto pass;

    q = r2i(r);
    
    if (q != r2i(i2r(q))) {
      fail(cout, r, "requantisation leads to different number")
      	<< "\tquants: " << q << " != " << r2i(i2r(q)) << endl
    	<< "ARG " << i2r(q) << endl;
      fail(cout, i2r(q), "...");
      //return false;
      }

    r2 = i2r(q);
    if (fabs(r-r2) > error) { // TODO: add actual error cond
      cerr << endl;
      fail(cout, r, "error bound exceed")
	<< "\tQuantum = " << q << endl;
      fail(cout, r2, "...");
      cout << "\tDelta = " <<fabs(r-r2) << endl;
      return false;
    }
    
  pass:
    u++;
  } while (i);
  return true;
}


int main() {
  return test_full<float>();
}
