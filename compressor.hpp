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

#include "common.hpp"
#include "num_util.hpp"

// state of one trajectory during compression
template<typename Real>
struct TrajState {
  Real x0, x1, vmin, vmax;
  int64_t qx0; // store the quantized x0 as reference so that
	       // numerical error of support vector position does not
	       // accumulate
  uint32_t dt;

  // The first point added initialises the data structure.
  // The quantised integer to be stored is returned
  uint32_t add_first(Real x, Real, Real quantum) {
    qx0 = quantize(x, quantum);
    x0 = quant2real<Real>(qx0, quantum),
    x1 = x,
    vmin = -numeric_limits<Real>::infinity(),
    vmax =  numeric_limits<Real>::infinity(),
    dt = 0;

    return signed2unsigned(quantize(x, quantum));
  }

  optional<SVI> add(Real x, Real e, Real quantum) {
    // compute new error bound
    Real vmin2((x - x0 - e) / (dt + 1)),
         vmax2((x - x0 + e) / (dt + 1));
    vmin2 = max(vmin, vmin2);
    vmax2 = min(vmax, vmax2);

    if (vmin2 > vmax2) {
      // if new point does not fit in the existing error bound, store
      // a linear segment up to the previous point and start a new
      // segment
      SVI res = flush(quantum);
      // qx0 and x0 are set by flush
      x1 = x;
      dt = 1;
      vmin = x1 - x0 - e;
      vmax = x1 - x0 + e;
      return res;
    }else{
      // extend the linear segment by the current point otherwise
      x1 = x;
      vmin = vmin2;
      vmax = vmax2;
      ++dt;
      return optional<SVI>();
    }
  }

  SVI flush(Real quantum) {
    // Compute new support vector: the point sv that is closest to x1
    // while maintaining the derivate bounds vmin/vmax
    Real sv;
    if      (x1 - x0 < vmin * dt) { sv = x0 + vmin * dt; }
    else if (x1 - x0 > vmax * dt) { sv = x0 + vmax * dt; }
    else                          { sv = x1; }

    // create integer support vector (the data struct to VLI-compress)
    SVI svi;
    assert(dt > 0);
    svi.dt = dt - 1;
    svi.v = signed2unsigned(quantize(sv - x0, quantum));

    // start new segment from sv, not from x1
    qx0 = quantize(sv, quantum);
    x0 = quant2real<Real>(qx0, quantum);

    return svi;
  }
};

// state of the compressor
template<typename Real>
struct CompressorState {
  // Input config
  TId numTraj; 
  Real error, bound, quantum;

  // Output config
  int chunkSize; // maximal number of support vectors (SVI)

  // Store the order in which support vectors are expected and in
  // which we know them respectively. Only the later might store more
  // than one support vector for a trajectory
  priority_queue<STP, priority_queue<STP>::container_type, std::greater<STP>> expectedSegment;
  map<STP, SVI> knownSegment;
  Time curTime;

  TrajState<Real> *trajState;

  // Current chunk of support vectors to be written
  int curSV;
	SplitSVIBuffer buf;

  // function which is called with the compressedSV
  function<void(char*, ChunkSize)> sink;

  /// the functions of the compressor in order

  // 0. init compressor
  CompressorState(TId numTraj, Real error, Real bound, Real quantum,
		  int chunkSize, EncodingPtr encoder,
		  function<void(char*, ChunkSize)> sink) 
  : numTraj(numTraj),
    error(error),
    bound(bound),
    quantum(quantum),
    chunkSize(chunkSize),
    curTime(0),
    trajState(new TrajState<Real>[numTraj]),
    curSV(0),
    buf(encoder, chunkSize),
    sink(sink)
  {}

  // 1. add another frame of trajectory data
  void addFrame(Real *trajVal) {
    if (curTime) { addLaterFrame(trajVal); }
    else         { addFirstFrame(trajVal); }
  }

  // Use the first frame for late initialization of TrajState and
  // expected segment queue.
  void addFirstFrame(Real *trajVal) {
    // Instead of compressed support vectors, initial value (x) is
    // stored uncompressed with the minimal number of bits given bound
    // and quantum (+1 for sign)
    uint bit_count = 2 + ceil(log2(bound / quantum));
    dynamic_bitset<uint8_t> iv(bit_count * numTraj);
    for (int traj=0; traj<numTraj; traj++) {
      auto x = trajVal[traj];
      auto x_quant = trajState[traj].add_first(x, error, quantum);
      assert(x_quant < (decltype(x_quant)(1) << (bit_count-1)));
      for (uint i=0; i<bit_count; i++)
	      iv[traj * bit_count + i] = (x_quant >> i) & 1;
    }

    // write data to stream
    // TODO: use buffer of interal representation of dynamic_bitset
    ChunkSize sz;
    sz.raw = bit_count * numTraj;
    sz.compressed = (sz.raw + 7) / 8;
    uint8_t *raw_iv = new uint8_t[sz.compressed];
    to_block_range(iv, raw_iv);
    sink((char*) raw_iv, sz);
    delete[] raw_iv;

    // Add all expected segments
    curTime = 1;
    for (int traj=0; traj<numTraj; traj++) {
      STP stp;
      stp.time = curTime;
      stp.id = traj;
      expectedSegment.push(stp);
    }
  }

  void addLaterFrame(Real *trajVal) {
    for (int traj=0; traj<numTraj; traj++) {
      auto x = trajVal[traj];

      // test new point against particles trajectory
      auto maybePoint = trajState[traj].add(x, error, quantum);
      if (maybePoint) {
	// add point to known support vectors
	STP stp;
	stp.time = curTime - (maybePoint->dt + 1);
	stp.id = traj;
	knownSegment.insert(make_pair(stp, *maybePoint));
	
	// Test if we know the next required support vector. Add it to
	// the raw chunk if so. Push the chunk once it is full.
	while (expectedSegment.size() && (expectedSegment.top() == knownSegment.begin()->first)) {
	  auto segIter = knownSegment.begin();
	  auto expectedTraj = expectedSegment.top().id;

	  // add new expected segemnt
	  STP newSeg;
	  newSeg.time = segIter->first.time + segIter->second.dt + 1;
	  newSeg.id = expectedTraj;
	  expectedSegment.push(newSeg);

	  // write out known segment
	  buf.set(curSV++, segIter->second);
	  knownSegment.erase(segIter);
	  expectedSegment.pop();

	  // write out chunk once full
	  if (curSV >= chunkSize)
	    pushChunk();
	}
      }
    }

    assert(curTime++ < maxTime);
  }

  // X. compress chunk, push it to sink, reset it
  void pushChunk() {
	  uint32_t *cbuf;
    ChunkSize sz;
    sz.raw = curSV * 2 * 4;  // two 4-byte int per support vector
    if (curSV) {
      tie(cbuf, sz.compressed) = buf.encode(curSV);
      sz.compressed *= sizeof(uint32_t);
    }else{
      sz.compressed = 0;
    }
    sink((char*) cbuf, sz);
    curSV = 0;
  }

  void finish() {
    // The data structure must have been initialized.
    assert(curTime);
    // We only have full trajectory (x and v) if at least two frames
    // have been seen. Only then we need to flush the unfinished
    // trajectories.
    if (curTime > 1) {
      for (; expectedSegment.size(); expectedSegment.pop()) {
	      auto es  = expectedSegment.top();
	      auto fks = knownSegment.begin();
	      assert(es.time < curTime);
	      // If we already have a support vector for the point, use it;
	      // otherwise we have to create one by flushing it
	      if ((fks != knownSegment.end()) & (es == fks->first)) {
		      // Construct the next segment to wait for. Note:
		      // - An SVI is only known if it terminates before curTime.
		      // - There may be more than one pending SVI for each
		      //   trajectory.
		      STP newSeg;
		      newSeg.time = fks->first.time + fks->second.dt + 1;
		      newSeg.id   = fks->first.id;
		      assert(newSeg.time < curTime);
		      expectedSegment.push(newSeg);
		      
		      buf.set(curSV++, fks->second);
		      knownSegment.erase(fks);
	      }else{
		      buf.set(curSV++, trajState[es.id].flush(quantum));
	      }
	      if (curSV >= chunkSize) pushChunk();
      }
    }
    // Flush non-empty buffers.
    if (curSV) pushChunk();
    // Add one empty chunk to signal the end of this stream.
    pushChunk();
  }

  ~CompressorState() {
    delete[] trajState;
  }
};
