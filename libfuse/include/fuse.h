#ifndef FUSE_H
#define FUSE_H

#include <memory>
#include <string>
#include <vector>

#include "fuse_types.h"

#include "instance.h"
#include "case.h"
#include "profile.h"

namespace Fuse {

	/*
	* Global functions
	*/

	void initialize(std::string target_dir, bool debug);

}

#endif
