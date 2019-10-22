# Fuse

This repository contains experimental work relevant to:

Richard Neill, Andi Drebes, and Antoniu Pop. 2017. Fuse: Accurate Multiplexing of Hardware Performance Counters Across Executions. ACM Trans. Archit. Code Optim. 14, 4, Article 43 (December 2017), 26 pages. DOI: https://doi.org/10.1145/3148054

The repository's main functionality:

* Execute profiling runs of OpenStream or OpenMP programs
* Merge the different profiling runs according to a selected combination strategy, to give a complete profile
* Analyse the accuracy of the hardware performance monitoring data in a merged profile, via Execution Profile Dissimilarity

The repository contains a subset of an experimental codebase, so some code may be unused and/or incomplete.

### Dependencies

The following external projects are required to run Fuse:

* Aftermath, Aftermath-OpenMP (https://www.aftermath-tracing.com/)
* OpenStream (http://openstream.cs.manchester.ac.uk/)
* Boost Interval Container Library (https://www.boost.org/doc/libs/1_64_0/libs/icl/doc/html/index.html)
* MIToolbox - *optionally* built by passing -DMUTUAL_INFORMATION=1 to CMake (http://www.cs.man.ac.uk/~pococka4/MIToolbox.html) - required for experimental merge specification generation

The following projects are included in the repository as dependencies:

* JSON for Modern C++ (https://github.com/nlohmann/json)
* Lightweight C++ command line option parser (https://github.com/jarro2783/cxxopts)
* Fast EMD (Ofir Pele, Michael Werman. A Linear Time Histogram Metric for Improved SIFT Matching, ECCV 2008)
* Easylogging++ (https://github.com/zuhd-org/easyloggingpp)

### Build

The project can be built via CMake, for example:

    mkdir build
    cd build
    cmake -DAFTERMATH_INCLUDE_DIR=... -DAFTERMATH_LIB_DIR=... ../
    make

### Running Fuse

Fuse is executed with command line options. These options can be viewed via:

    ./fuse --help

To run fuse, a fuse specification file named 'fuse.spec' is required. This should be formatted as JSON, with a minimal example as follows:

    {
			"benchmark": "...",
			"args": "...",
			"benchmark_loc": "directory in which executable benchmark exists",
			"cache_cleared": true,
			"runtime": "openstream",
			"reference_distributions_loc": "directory to store the reference event count distributions",
			"tracefiles_loc": "directory to store .ost trace files",
			"papi_executables_dir": "directory containing papi_avail, papi_event_chooser binaries",
			"merged_profile_loc": "directory to store results of the merges"
		}

For combination strategies that require common events, there is no current implemented algorithm to autogenerate the merge process. Therefore for this the JSON file must include something of the form:

		{
			"merge_spec":[
				{
					"merge_index":0,
					"profiled_events":["event1","event2",...], # must be simultaneously compatible
					"linking_events":[], # empty for first merge index
				},
				{
					"merge_index":1,
					"profiled_events":["event1","event3",...], # must be simultaneously compatible
					"linking_events":["event1",...], # events must exist in superset of previous merges
				},
				...
			]
		}

### Licence

This project is licensed with GPLv2. Please cite the paper if you use this code in your work (https://dl.acm.org/citation.cfm?doid=3154814.3148054)

### TODO

There are many TODOs throughout the project, such as:

* Unit testing
* Sanity checking on configuration
* Consolidate configuration in .spec and configuration in common.h,
* Typedef the code to be more readable
* Auto typing to reduce boilerplate

