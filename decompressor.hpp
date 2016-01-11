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

struct DecompTrajState {
  Time t0, dt;
  int x0, dx;

  template<typename Real>
  Real get(Time t1, Real quantum) {
    return dt 
      ?                   quant2real<Real>(x0, quantum)
        + Real(t1 - t0) * quant2real<Real>(dx, quantum) / dt
      : quant2real<Real>(x0, quantum);
  }
};

template<typename Real>
struct DecompressorState {
  TId numTraj;
  Real quantum;

  DecompTrajState *trajState;
  priority_queue<STP, priority_queue<STP>::container_type, std::greater<STP>> expectedSegment;
  Time curTime;

	SplitSVIBuffer buf;
  uint64_t chunkSz, chunkCur;

  function<ChunkSize(char*)> chunkSrc;
  EncodingPtr decoder;

  // statistic helpers
#ifdef HACKY_STATS
  map<int, uint> stat_key_x, stat_dx, stat_dt;
#endif

  DecompressorState(TId numTraj, Real quantum,
		    uint64_t maxChunkSize, EncodingPtr decoder,
		    function<ChunkSize(char*)> chunkSrc)
  : numTraj(numTraj),
    quantum(quantum),
    trajState(new DecompTrajState[numTraj]),
    curTime(0),
    buf(decoder, maxChunkSize),
    chunkSz(0),
    chunkCur(0),
    chunkSrc(chunkSrc),
    decoder(decoder)
  {}

  bool readFrame(Real *trajDst) {
    if (!curTime)
      if (!readKeyFrame()) return false;
    while ((curTime == expectedSegment.top().time) && (chunkCur < chunkSz))
      readSegment();
    if (expectedSegment.top().time <= curTime)
      return false;
    // push data to trajDst
    if (trajDst) {
      for (int i=0; i<numTraj; i++)
	trajDst[i] = trajState[i].get<Real>(curTime, quantum);
    }
    curTime++;
    return true;
  }

  bool readKeyFrame() {
    // init expected segements
    uint8_t *raw_iv = new uint8_t[numTraj * sizeof(Real)];
    ChunkSize sz = chunkSrc((char*) raw_iv);
    if (!sz.raw) return false;
    uint bit_count = sz.raw / numTraj;
    assert(bit_count * numTraj == sz.raw);
    dynamic_bitset<uint8_t> iv(raw_iv, raw_iv + sz.compressed);
    delete[] raw_iv;

    for (int i=0; i<numTraj; i++) {
      uint32_t x_quant = 0;
      for (uint j=0; j<bit_count; j++)
	    x_quant |= decltype(x_quant)(iv[i * bit_count + j]) << j;

      STP stp;
      stp.id = i;
      stp.time = 1;
      expectedSegment.push(stp);
      
      DecompTrajState &traj = trajState[i];
      traj.t0 = 0;
      traj.x0 = unsigned2signed(x_quant);
      traj.dt = 0;
      traj.dx = 0;

#ifdef HACKY_STATS
      stat_key_x[traj.x0]++;
#endif
    }
    loadNextChunk();
    return true;
  }
  
  void readSegment() {
    assert(chunkCur < chunkSz);
    
    // update mentioned traj
    TId id = expectedSegment.top().id;
    SVI svi = buf.get(chunkCur);
    DecompTrajState &traj = trajState[id];
    traj.x0 += traj.dx;
    traj.t0 += traj.dt; assert(traj.t0 == curTime-1);
    traj.dt  = svi.dt + 1;
    traj.dx  = unsigned2signed(svi.v);

    // gather histogram data
#ifdef HACKY_STATS
    stat_dx[traj.dx]++;
    stat_dt[traj.dt]++;
#endif
    
    // add next expected point
    STP stp;
    stp.id = id;
    stp.time = curTime + traj.dt;
    expectedSegment.pop();
    expectedSegment.push(stp);
    
    chunkCur++;
    
    // fetch new trajectory data
    if (chunkCur == chunkSz)
      loadNextChunk();
  }

  void loadNextChunk() {
	  ChunkSize sz = chunkSrc((char*) buf.compressed); 
    chunkCur = 0;
    chunkSz = sz.raw / 2 / 4;
    if (chunkSz) {
	    buf.decode(chunkSz, sz.compressed / 4);
    }
    // NOTE: chunkCur == chunkSz is used to signal a failed load
  }

  ~DecompressorState() {
    delete[] trajState;

#ifdef HACKY_STATS
		for (auto stat : {make_tuple(stat_key_x, "stat_key_x"),
					make_tuple(stat_dx, "stat_dx"),
					make_tuple(stat_dt, "stat_dt")})
			for (auto i : get<0>(stat))
				cout << get<1>(stat) << " " << i.first << " " << i.second << endl;
#endif
  }
};
