#include "target.h"
#include "config.h"

#include "nlohmann/json.hpp"

Fuse::Target::Target(std::string target_dir){

	// Load the JSON
	std::string json_filename = target_dir + "/fuse.json";

	
	

}

Fuse::Target::~Target(){}
