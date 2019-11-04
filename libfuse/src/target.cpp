#include "target.h"
#include "config.h"

#include "easylogging++.h"

::Fuse::Target::Target(std::string dir){

	LOG(INFO) << "Setting up Fuse target directory for " << dir;
	

}

::Fuse::Target::~Target(){}
