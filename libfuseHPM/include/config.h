#ifndef FUSE_CONFIG_H
#define FUSE_CONFIG_H

namespace Fuse {

	namespace Config {

		extern unsigned int fuse_log_level;
		extern bool client_managed_logging;
		extern bool initialized;
		extern unsigned int max_execution_attempts;

	}

}

#endif
