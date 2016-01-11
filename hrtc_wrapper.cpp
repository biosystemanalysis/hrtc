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


extern "C" {
	#include <tng/tng_io.h>
}

	template<typename T>
	tng_function_status typed_hrtc_compress(const tng_trajectory_t tng_data,
							const int64_t n_frames,
							const int64_t n_particles,
							char **data,
							int64_t *new_len) {
		int64_t dimensions;
		double bound = 0,error = 0,quantum = 0;

		{// this block: read number of dimensions + read box shape to calculate bound
	     // this is done here, because tng_data_block_write does not know about
		 // these things and thus cannot pass it
			char type;
			union data_values **box_data = 0;
			int64_t dummy,current_dimension;

			tng_function_status stat=tng_data_get(tng_data,TNG_TRAJ_BOX_SHAPE,&box_data,&dummy,&dimensions,&type);
			assert(stat == TNG_SUCCESS);
			assert(dimensions>0);

			double val = 0; // current dimension value in loop
			for (current_dimension=0; current_dimension<dimensions;current_dimension++){
				val=(type==TNG_DOUBLE_DATA)?box_data[0][current_dimension].d:box_data[0][current_dimension].f;
				assert(val>0);
				bound = max(val,bound);
			}
		}

		{ // this block: set error and quantum using precision from tng_environment
			double precision;
			tng_function_status stat = tng_compression_precision_get(tng_data,&precision);
			assert(precision!=0);
			quantum=1/precision;
			assert(stat == TNG_SUCCESS);
			error       = quantum/2;
			assert(error>0);
			assert(quantum>0);
		}

		char* result = nullptr;
		size_t result_size=sizeof(double); // for quantum
		result=(char*) malloc(result_size); // allocate memory for one double
		memcpy(result,&quantum,result_size); // copy double to result data

		int integerEncoder = 5; // pareto optimal / good space-time tradeoff
		int numberOfTrajectories=n_particles*dimensions;

		CompressorState<T> compressor(numberOfTrajectories,
									  error,
									  bound,
									  quantum,
									  1024 /* chunk size */,
									  integer_encoding::EncodingFactory::create(integerEncoder),
									  [&](char* buf, ChunkSize chunkSize) {
											auto offset=result_size;
										  	result_size+= sizeof(chunkSize) + chunkSize.compressed;
										  	result = (char*) realloc(result,result_size);
//										  	printf("chunksize: %d (raw) => %d (compressed), offset: %d\n",chunkSize.raw,chunkSize.compressed,offset);
										  	memcpy((void*)(result+offset),(void*)&chunkSize,sizeof(chunkSize));
										  	offset+=sizeof(chunkSize);
										  	memcpy((void*)(result+offset),(void*)buf,chunkSize.compressed);
									  });

		auto traj_data=(T*) *data;
		for (int64_t frame_number=0;frame_number<n_frames;frame_number++){ // loop through frames
			compressor.addFrame(traj_data+frame_number*numberOfTrajectories);
		}
		compressor.finish();

	    free(*data);
	    *data = (char *)result;
	    *new_len = result_size;

		return TNG_SUCCESS;
	}

	template<typename T>
	tng_function_status typed_hrtc_uncompress(const tng_trajectory_t tng_data,
            char **data) {


		int64_t dimensions,number_of_frames,number_of_particles;
		double bound = 0,error = 0,quantum = 0;

		int integerEncoder = 5; // pareto optimal / good space-time tradeoff

		{// this block: read number of dimensions + number of frames + box shape to calculate bound
			char type;
			union data_values **box_data = 0;
			int64_t current_dimension;

			tng_function_status stat=tng_data_get(tng_data,TNG_TRAJ_BOX_SHAPE,&box_data,&number_of_frames,&dimensions,&type);
			assert(stat == TNG_SUCCESS);
			assert(dimensions>0);
			assert(number_of_frames>0);

			double val = 0; // current dimension value in loop
			for (current_dimension=0; current_dimension<dimensions;current_dimension++){
				val=(type==TNG_DOUBLE_DATA)?box_data[0][current_dimension].d:box_data[0][current_dimension].f;
				assert(val>0);
				bound = max(val,bound);
			}
		}

		{ // this block: determine number of particles
			tng_function_status stat = tng_num_molecules_get(tng_data, &number_of_particles);
			assert(stat == TNG_SUCCESS);
			assert(number_of_particles>0);
		}

		{ // this block: determine number of frames per frameset
			tng_function_status stat = tng_num_frames_per_frame_set_get(tng_data, &number_of_frames);
			assert(stat == TNG_SUCCESS);
			assert(number_of_frames>0);
		}


		int number_of_trajectories=number_of_particles*dimensions;
		auto src_buf = *data;


		{ // this block: read quantum from data blob, set error
			quantum = *((double*) src_buf); // reading quantum from tip of data blob
			src_buf+=sizeof(double); // moving pointer ahead
			error       = quantum/2;
			assert(error>0);
			assert(quantum>0);
		}

		DecompressorState<T> decompressor(number_of_trajectories,
				quantum,
				1024 /* chunk size */,
				integer_encoding::EncodingFactory::create(integerEncoder),
				[&](char* buf) -> ChunkSize {
					ChunkSize chunkSize = *((ChunkSize*) src_buf);
					src_buf += sizeof(chunkSize);
//				  	printf("chunksize: %d (compressed) => %d (raw)\n",chunkSize.compressed,chunkSize.raw);
//				  	printf("src_buf: %d\n",src_buf-*data);
					memcpy(buf,src_buf,chunkSize.compressed);
					src_buf += chunkSize.compressed;

					return chunkSize;
		       	  });

		auto result = (T*) malloc(number_of_frames * number_of_trajectories * sizeof(T));
		auto result_pos = result;

		for (int i=0; i<number_of_frames;i++){
			assert(decompressor.readFrame(result_pos)); // should not be activated unless all frames have been read.
			result_pos += number_of_trajectories;
		}
		assert(!decompressor.readFrame(result_pos)); // if we have not read all frames, something went wrong

	    free(*data);

	    *data = (char *)result;

		return TNG_SUCCESS;
	}

extern "C" {
	tng_function_status hrtc_compress(const tng_trajectory_t tng_data,
			const int64_t n_frames,
			const int64_t n_particles,
			const char type,
			char **data,
			int64_t *new_len) {
		switch (type){
			case TNG_DOUBLE_DATA:
				return typed_hrtc_compress<double>(tng_data,n_frames,n_particles,data,new_len);
			case TNG_FLOAT_DATA:
				return typed_hrtc_compress<float>(tng_data,n_frames,n_particles,data,new_len);
			default:
				return TNG_FAILURE;
		}
	}

	/* decompression */
	tng_function_status hrtc_uncompress(const tng_trajectory_t tng_data,
            const char type,
            char **data) {
		switch (type){
			case TNG_DOUBLE_DATA:
				return typed_hrtc_uncompress<double>(tng_data,data);
			case TNG_FLOAT_DATA:
				return typed_hrtc_uncompress<float>(tng_data,data);
			default:
				return TNG_FAILURE;
		}
	}

	void hrtc_version(){
		cout << hrtc_version_string() << "\n";
	}
}
