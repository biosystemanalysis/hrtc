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

#include "common.hpp"
#include "compressor.hpp"
#include "decompressor.hpp"
#include "format.hpp"

const int chunkSize = 1024;

template<typename Real>
void compressionLoop(function<CompressorState<Real>*(void)> compressorFactory,
	      function<bool(Real*, TId, int)> reader,
	      TId numberOfTrajectories, int sourceFileHandle, int blockSize) {
  Real *trajectoryData = new Real[numberOfTrajectories];
  int block(blockSize);
  CompressorState<Real> *compressor(nullptr);
  while (reader(trajectoryData, numberOfTrajectories, sourceFileHandle)) {
    if (block == blockSize) {
      if (compressor) {
	      compressor->finish();
	      delete compressor;
      }
      compressor = compressorFactory();
      block = 0;
    }
    compressor->addFrame(trajectoryData);
    block++;
  }
  if (block)
    compressor->finish();
  if (compressor)
    delete compressor;
  cerr << "done at " << __LINE__ << endl;
}

double *foo = new double;

template<typename Real>
void decompressionLoop(function<DecompressorState<Real>*(void)> decompressorFactory,
		TId numberOfTrajectories, uint blockSize) {
  Real *trajectoryData = new Real[numberOfTrajectories];
  uint frameInBlock;
  do {
    frameInBlock = 0;
    DecompressorState<Real> *decompressor = decompressorFactory();
    while (decompressor->readFrame(trajectoryData)) {
      frameInBlock++;
      for (int i=0; i<numberOfTrajectories; i++) {
	      *foo = trajectoryData[i];
	      //cout << (i ? "\t" : "") << trajectoryData[i];
      }
      //cout << endl;
    }
    delete decompressor;
  } while (frameInBlock == blockSize);
}

int main(int argc, char **argv) {
  /// parse cmd line options
  prog_options::options_description cmdOpts("Synopsis");
  cmdOpts.add_options()
	  ("compress", "")
	  ("decompress", "")
	  ("src", prog_options::value<std::string>()->default_value("-"),
	   "source file name")
	  ("dst", prog_options::value<std::string>()->default_value("-"),
	   "destination file name")
	  ("format", prog_options::value<std::string>()->default_value("tsvfloat"),
	   "file format: hufloat, hudouble, tsvfloat, tsvdouble...")
	  ("numtraj", prog_options::value<TId>(),
	   "number of trajectories (#particles * #dim)")
	  ("bound", prog_options::value<double>(),
	   "maximal (absolute) value of a trajectory")
	  ("error", prog_options::value<double>(),
	   "maximal deviation from trajectory (quantization + prediction)")
	  ("qp-ratio", prog_options::value<double>()->default_value(0.1),
	   "ratio (0..1) to split the error between quantization and prediction")
	  ("blocksize", prog_options::value<uint>()->default_value(1024),
	   "frames per block")
	  ("integer-encoding", prog_options::value<int>()->default_value(14),
	   "code id used by integer encoding library")
	  ;
  prog_options::variables_map options; // this stores command line options
  try {
    prog_options::store(prog_options::parse_command_line(argc, argv, cmdOpts), options);
  } catch (...) {
    cerr <<  cmdOpts << endl;
    exit(EXIT_FAILURE);
  }
  prog_options::notify(options);
  assert(options.count("compress") + options.count("decompress") <= 1); // at least one of the two options is needed!
  
  auto require = [&](string name) {
    if (!options.count(name)) {
      cerr << "--" << name << " missing\n\n" << cmdOpts << endl;
      exit(EXIT_FAILURE);
    }
    return options[name];
  };

  TId numberOfTrajectories = require("numtraj").as<TId>();

  // open I/O handles
  int sourceFileHandle = 0; // 0 ≙ std in
  { auto name = require("src").as<string>();
    if (name != "-")
      assert((sourceFileHandle = open(name.c_str(), O_RDONLY)) >= 0); }
  int sinkFileHandle = 1; // 1 ≙ std out
  {
	auto name = require("dst").as<string>();
    if (name != "-") {
    	assert((sinkFileHandle = open(name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRGRP | S_IROTH)) >= 0);
    }
  }

  // Split the error between quantization error (quantum/2) and
  // prediction error (error). The total error is error + quantum/2;
  double qpr = require("qp-ratio").as<double>();
  assert((qpr >= 0) && (qpr <= 1));
  double error       = require("error").as<double>() * (1 - qpr);
  double quantum     = require("error").as<double>() * qpr * 2;
  double bound       = require("bound").as<double>();
  int integerEncoder = require("integer-encoding").as<int>();
  
  /// execute (de)compression
  if (options.count("decompress")) {
    auto decompressorFactory = [&]() {
      return new DecompressorState<double> (numberOfTrajectories, quantum, chunkSize, integer_encoding::EncodingFactory::create(integerEncoder), [=](char* buf) -> ChunkSize {
	     ChunkSize chunkSize;
	     if ((read(sourceFileHandle, &chunkSize, sizeof(chunkSize))) == sizeof(chunkSize)) {
	       assert(read(sourceFileHandle, buf, chunkSize.compressed) == chunkSize.compressed);
	     }else{
	       chunkSize.compressed = 0, chunkSize.raw = 0;
	     }
	     return chunkSize;
       });
    };
    decompressionLoop<double>(decompressorFactory, numberOfTrajectories, options["blocksize"].as<uint>());
  }else{
    auto compressorFactory = [&]() {
      return new CompressorState<double>
      (numberOfTrajectories, error, bound, quantum, chunkSize, integer_encoding::EncodingFactory::create(integerEncoder), [&](char* buf, ChunkSize chunkSize) {
	      assert(write(sinkFileHandle, &chunkSize, sizeof(chunkSize)) == sizeof(chunkSize));
	      assert(write(sinkFileHandle, buf, chunkSize.compressed)     == chunkSize.compressed);
      });
    };

    function<bool(double*, TId, int)> format;
    auto fmtString = options["format"].as<string>();
    if      (fmtString == "hudouble") { format = readHubin<double>; }
    else                              { assert(false); }

    compressionLoop<double>(compressorFactory, format, numberOfTrajectories, sourceFileHandle, options["blocksize"].as<uint>());
  }

  return 0;
}
