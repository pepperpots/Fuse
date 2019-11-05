#include "fuse.h"
#include "config.h"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <spdlog/sinks/stdout_color_sinks.h>

#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

/* Initialize logging within the library, connected to the same spdlog sinks used by the client application */
std::shared_ptr<spdlog::logger> Fuse::initialize(std::vector<spdlog::sink_ptr> sinks, unsigned int log_level){
	
	std::srand(std::time(0));
		
	std::string logger_name = "fuse";

	// check if we already have a logger in this library
	// If we do, then drop it so we can re-initialize
	auto logger = spdlog::get(logger_name);
	if(logger)
		spdlog::drop_all();

	if(sinks.size() > 0){
		// use the provided sinks in this library

		logger = std::make_shared<spdlog::logger>(logger_name, std::begin(sinks), std::end(sinks));

		switch (log_level) {
			case 0:
				logger->set_level(spdlog::level::off);
				break;
			case 2:
				logger->set_level(spdlog::level::debug);
				break;
			case 3:
				logger->set_level(spdlog::level::trace);
				break;
			default:
				logger->set_level(spdlog::level::info);
				break;
		};

		logger->set_pattern("[%Y-%m-%d %H:%M:%S] [libFuse] %^%l%$ %v");
	
		spdlog::set_default_logger(logger);

	} else {

		std::cerr << "No spdlog sinks were provided to libFuse for logging. Use Fuse::intialize(std::string log_directory,";
		std::cerr << "unsigned int log_level) to have the library generate its own logging." << std::endl;
		exit(1);

	}
	
	::Fuse::Config::fuse_log_level = log_level;

	return logger;
}

/* Initialize logging within the library, disconnected from spdlogging (or lack thereof) in the client application */
void Fuse::initialize(std::string log_directory, unsigned int log_level, bool log_to_file){
	
	std::srand(std::time(0));
		
	std::string logger_name = "fuse";
	
	auto logger = spdlog::get(logger_name);
	if(logger)
		spdlog::drop_all();

	std::vector<spdlog::sink_ptr> sinks;

	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	sinks.push_back(console_sink);

	if(log_to_file){

		// Filename is the current datetime
		auto t = std::time(nullptr);
		auto tm = *std::localtime(&t);

		std::ostringstream oss;
		oss << log_directory << std::put_time(&tm, "/%Y%m%d.%H%M.log");
		auto log_filename = oss.str();
	
		Fuse::Util::check_or_create_directory_from_filename(log_filename);

		auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_filename);
		sinks.push_back(file_sink);

	}

	logger = std::make_shared<spdlog::logger>(logger_name, std::begin(sinks), std::end(sinks));

	switch (log_level) {
		case 0:
			logger->set_level(spdlog::level::warn);
			break;
		case 2:
			logger->set_level(spdlog::level::debug);
			break;
		case 3:
			logger->set_level(spdlog::level::trace);
			break;
		default:
			logger->set_level(spdlog::level::info);
			break;
	};

	logger->set_pattern("[%Y-%m-%d %H:%M:%S] [libFuse] [%^%l%$] %v");

	spdlog::set_default_logger(logger);

}

/*
	el::Configurations conf;
	conf.setGlobally(el::ConfigurationType::Enabled,std::string("true"));
	conf.setGlobally(el::ConfigurationType::ToFile,std::string("true"));
	conf.setGlobally(el::ConfigurationType::ToStandardOutput,std::string("true"));
	conf.setGlobally(el::ConfigurationType::Format,std::string("%datetime{%Y%M%d.%H%m.%s}:[%level]: %msg"));
	conf.setGlobally(el::ConfigurationType::Filename,ss.str());

	
	el::Loggers::reconfigureAllLoggers(conf);
	el::Loggers::addFlag(el::LoggingFlag::FixedTimeFormat);

	LOG(DEBUG) << "Initialized logging to " << ss.str();

}

*/
