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

#include <sys/types.h>
#include <unistd.h>

bool readAll(int fd, char *buf, size_t size) {
  size_t cur = 0;
  while (cur < size) {
    auto ret = read(fd, buf+cur, size-cur);
    assert(ret >= 0); // catch FS errors
    if (ret ==0) // EOF
      return false;
    cur += ret;
  }
  return true;
}

template<typename Real>
/* read binary format data file, which contains <numberOfTrajectories> trajectories followed by <numberOfTrajectories> velocities and ??? */
bool readHubin(Real* targetBuffer, uint64_t numberOfTrajectories, int sourceFileHandle) {
  uint32_t size = numberOfTrajectories * sizeof(Real);
  static Real *trash = new Real[2*size];

  // read payload (coordinates)
  return (readAll(sourceFileHandle, (char*) targetBuffer, size)
          && read(sourceFileHandle, trash, 2*size));
}

template<typename Real>
bool readTSV(Real* targetBuffer, uint64_t numberOfTrjectories, int sourceFileHandle) {
  const int bufSz = 65536;
  static char *buf = (char*) malloc(bufSz);
  static int bufPos = 0, bufMax = 0;

  char numBuf[32], c;
  int numPos(0);
  int ret;
  uint64_t traj(0);
  for (;;) {
    // refill buffers if empty
    if (bufPos == bufMax) {
      ret = read(sourceFileHandle, buf, bufSz);
      if (ret == 0) return false; // end of stream
      if (ret < 0) {
	    perror("while reading from input stream: ");
	    exit(EXIT_FAILURE);
      }
      bufPos = 0;
      bufMax = ret;
    }
    // read one char, interpret it
    c = *(buf + bufPos++);
    switch (c) {
    case '\n':
    case '\t': // completed reading number
      numBuf[numPos] = 0;
      char *endp;
      targetBuffer[traj] = strtod((char*) numBuf, &endp); // convert number digits to atcual number
      assert(*endp == 0);
      numPos = 0;
      if (++traj == numberOfTrjectories) return c == '\n'; // after reading <numberOfTrjectories> values, we should find a line break. otherwise something is weird.
      break;
    default: // append digit to number
      numBuf[numPos] = c;
      assert(++numPos < 32);
    }
  }
  return false;
}

template<typename Real>
function<bool(float*, TId, int)> readTest(uint blockSize) {
  return [=] (Real* dstBuf, uint64_t numTraj, int) -> bool {
    static Real* srcBuf = nullptr;
    static int cur = 0;
    static int total = 0;

    if (!srcBuf) {
	    srcBuf = (Real*) malloc(sizeof(Real) * numTraj * blockSize);
      assert(srcBuf);
      for (size_t i = 0; i<numTraj*blockSize; i++)
	      srcBuf[i] = cos(double(i) / 3724);
    }
    memcpy(dstBuf, srcBuf + cur * numTraj, numTraj * sizeof(Real));

    cur = (cur + 1) % blockSize;
    total++;

    return (total <= 1000000);
  };
}
