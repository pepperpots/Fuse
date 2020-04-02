#ifndef FUSE_CONFIG_H
#define FUSE_CONFIG_H

namespace Fuse {

	namespace Config {

		extern unsigned int fuse_log_level;
		extern bool client_managed_logging;
		extern bool initialized;

		extern unsigned int max_execution_attempts;
		extern bool lazy_load_references;
		extern unsigned int tmd_bin_count;
		extern bool calculate_per_workfunction_tmds;
		extern bool weighted_tmd;

	}

}

#endif
