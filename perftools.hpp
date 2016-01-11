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

#include <sys/time.h>
#include <iostream>

using namespace std;

template<class Title>
void print_throughput(double time, double num, Title &title) {
  cerr << title << ": " << time << "\t" << num / time << "/s\n";
}

struct Timer {
  Timer() {
    gettimeofday(&start,NULL);
  }

  double diff() {
    timeval stop;
    gettimeofday(&stop,NULL);
    return (stop.tv_sec + stop.tv_usec/1000000.0) - (start.tv_sec + start.tv_usec/1000000.0);
  }

  timeval start;
};
