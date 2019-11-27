#include "config.h"

unsigned int Fuse::Config::fuse_log_level = 1;
bool Fuse::Config::client_managed_logging = false;
bool Fuse::Config::initialized = false;

unsigned int Fuse::Config::max_execution_attempts = 5;
bool Fuse::Config::lazy_load_references = false;
unsigned int Fuse::Config::tmd_bin_count = 10;
bool Fuse::Config::calculate_per_workfunction_tmds = true;
