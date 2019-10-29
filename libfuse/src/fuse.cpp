#include "fuse.h"
#include "config.h"

#include "easylogging++.h"

#include <string>
#include <sstream>

INITIALIZE_EASYLOGGINGPP

void ::Fuse::initialize(std::string target_dir, bool debug){
	
	std::srand(std::time(0));
		
	std::stringstream ss;
	ss << target_dir << "/logs/libfuse_%datetime{%Y%M%d.%H%m}.log";
	el::Configurations conf;
	conf.setGlobally(el::ConfigurationType::Enabled,std::string("true"));
	conf.setGlobally(el::ConfigurationType::ToFile,std::string("true"));
	conf.setGlobally(el::ConfigurationType::ToStandardOutput,std::string("true"));
	conf.setGlobally(el::ConfigurationType::Format,std::string("%datetime{%Y%M%d.%H%m.%s}:[%level]: %msg"));
	conf.setGlobally(el::ConfigurationType::Filename,ss.str());

	::Fuse::Config::fuse_debug_mode = true;
	if(debug == false){
		conf.set(el::Level::Debug, el::ConfigurationType::Enabled,std::string("false"));
		::Fuse::Config::fuse_debug_mode = false;
	}
	
	el::Loggers::reconfigureAllLoggers(conf);
	el::Loggers::addFlag(el::LoggingFlag::FixedTimeFormat);

	LOG(DEBUG) << "Initialized logging to " << ss.str();
}

