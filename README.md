## FuseHPM

This repository contains the Fuse HPM tool presented in:

1. [Neill, R., Drebes, A., and Pop, A. Fuse: Accurate multiplexing of hardware performance counters across executions. ACM Trans. Archit. Code Optim. 14, 4 (Dec. 2017), 43:1–43:26](https://dl.acm.org/citation.cfm?id=3148054)
2. [Neill, R., Drebes, A., and Pop, A. Accurate and complete hardware profiling for openmp. In Scaling OpenMP for Exascale Performance and Portability (Cham 2017), B. R. de Supinski, S. L. Olivier, C. Terboven, B. M. Chapman, and M. S. Müller, Eds., Springer International Publishing, pp. 266–280](https://link.springer.com/chapter/10.1007/978-3-319-65578-9_18)

The repository's main functionality:
* Execute profiling runs of [OpenStream](http://openstream.cs.manchester.ac.uk/) or [OpenMP](https://www.openmp.org/) programs
* Combine the different profiling runs according to a selected combination strategy, to produce a complete profile
* Analyse the accuracy of the hardware performance monitoring data in a combined profile, via Execution Profile Dissimilarity

The tool is provided as two components:
- A shared library libFuseHPM in the `libFuseHPM/` directory
    - This contains the core Fuse functionality, with the necessary API included via the `fuse.h` header file
- A runner tool that provides a wrapper for the library, to enable immediate application of Fuse HPM to a target benchmark
    - This is provided in the `src/` directory

### Dependencies

The following external projects are required to build Fuse:
* [Aftermath, Aftermath-OpenMP](https://www.aftermath-tracing.com/)
* [Boost Interval Container Library](https://www.boost.org/doc/libs/1_64_0/libs/icl/doc/html/index.html)

The following external projects are optional:
* [MIToolbox](http://www.cs.man.ac.uk/~pococka4/MIToolbox.html) is required for BC combination sequence generation
    This is built by passing -DMUTUAL_INFORMATION=1 to cmake

The following projects are included as dependencies to the libFuseHPM library:
* [Nlohmann's JSON for Modern C++](https://github.com/nlohmann/json)
* Fast EMD (Ofir Pele, Michael Werman. A Linear Time Histogram Metric for Improved SIFT Matching, ECCV 2008)
* [The spdlog Fast C++ logging library](https://github.com/gabime/spdlog)

The following projects are further included as dependencies for the runner tool:
* [Lightweight C++ command line option parser](https://github.com/jarro2783/cxxopts)

### Build

The project can be built via CMake, for example:

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=RELEASE -DAFTERMATH_INCLUDE_DIR=... -DAFTERMATH_LIB_DIR=... ../
    make

### Using the tool

Fuse is executed with command line options. These options can be viewed via:

    ./fuse_runner --help

This produces the following options:

     Main options:
      -d, --target_dir arg        Target Fuse target directory (containing
                                  fuse.json).
      -e, --execute_sequence arg  Execute the sequence. Argument is number of
                                  repeat sequence executions. Conditioned by
                                  'minimal', 'filter_events'.
      -m, --combine_sequence      Combine the sequence repeats. Conditioned by
                                  'strategies', 'repeat_indexes', 'minimal',
                                  'filter_events'.
      -t, --execute_hem arg       Execute the HEM execution profile. Argument is
                                  number of repeat executions. Conditioned by
                                  'filter_events'.
      -a, --analyse_accuracy      Analyse accuracy of combined execution
                                  profiles. Conditioned by 'strategies', 'repeat_indexes',
                                  'minimal', 'accuracy_metric', 'filter_events'.
      -r, --execute_references    Execute the reference execution profiles.
                                  Conditioned by 'filter_events'.
      -c, --run_calibration       Run EPD calibration on the reference profiles.
                                  Conditioned by 'filter_events'.
    
     Miscellaneous options:
      -h, --help           Print this help.
          --log_level arg  Set minimum logging level. Argument is integer
                           position in {trace, debug, info, warn}. Defaults to info.
                           (default: 2)
    
     Parameter options:
          --strategies arg      Comma-separated list of strategies from
                                {'random','ctc','lgl','bc','hem'}.
          --repeat_indexes arg  Comma-separated list of sequence repeat indexes
                                to operate on, or 'all'. Defaults t all repeat
                                indexes. (default: all)
          --minimal             Use minimal execution profiles (default is
                                non-minimal). Strategies 'bc' and 'hem' cannot use
                                minimal.
          --filter_events       Main options only load and dump data for the
                                events defined in the target JSON (i.e. exclude non
                                HPM events). Default is false.
          --tracefile arg       Argument is the tracefile to load for utility
                                options.
          --benchmark arg       Argument is the benchmark to use when loading
                                tracefile for utility options.
    
     Utility options:
          --dump_instances arg      Dumps an execution profile matrix. Argument
                                    is the output file. Requires 'tracefile',
                                    'benchmark'.
          --dump_dag_adjacency arg  Dumps the data-dependency DAG as a dense
                                    adjacency matrix. Argument is the output file.
                                    Requires 'tracefile', 'benchmark'.
          --dump_dag_dot arg        Dumps the task-creation and data-dependency
                                    DAG as a .dot for visualization. Argument is
                                    the output file. Requires 'tracefile',
                                    'benchmark'.

The main options require a target Fuse folder, which defines the runtime, binary, and target hardware events.
This folder is provided via the 'target_dir' command line option, which must contain a JSON file: `fuse.json`, with mandatory fields as follows:

    {
        "binary": "binary_filename",
        "binary_directory": "binary_dir/",
        "runtime": "openstream",
        "target_events": [
            "PAPI_event_1",
            "PAPI_event_2",
            ...
        ],  
        "references_directory": "references",
        "tracefiles_directory": "tracefiles",
        "combinations_directory": "combinations",
        "papi_directory": "papi_dir/bin/"
    }

Optional fields are:

    {
        "args": "args_for_binary_execution",
        "should_clear_cache": true,
        "bc_sequence": [...],
        "minimal_sequence": [...]
    }

The sequences are specified as an ordered JSON list of hardware event sets, given as JSON objects of the form:

    {
        "overlapping": [
            PAPI_event_1,
            PAPI_event_2,
            ...
        ],
        "unique" : [
            PAPI_event_3,
            PAPI_event_4,
            ...
        ]
    }

### Licence

This project is licensed with GPLv2. Please cite the TACO article if you use this code in your work (https://dl.acm.org/citation.cfm?doid=3154814.3148054)

