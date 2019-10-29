#include "case.h"
#include "config.h"

#include "easylogging++.h"

::Fuse::Case::Case(std::string dir){

	LOG(INFO) << "Setting up Fuse case directory for " << dir;
	

}

::Fuse::Case::~Case(){}
