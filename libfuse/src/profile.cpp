#include "profile.h"

#include "easylogging++.h"

extern "C" {
	// linux-kernel/list uses 'new' as a variable name, so must be redefined to avoid conflict with c++ keyword
	#define new extern_new
	#include "aftermath/core/multi_event_set.h"
	#include "aftermath/core/debug.h"
	#undef new
}

::Fuse::Execution_profile::Execution_profile(std::string tracefile, std::string benchmark)
	: tracefile(tracefile)
	, benchmark(benchmark){

}
::Fuse::Execution_profile::~Execution_profile(){
}

void ::Fuse::Execution_profile::load_from_tracefile(bool load_communication_matrix){

	// load the instances from the tracefile into the map, using the aftermath calls
	LOG(DEBUG) << "Loading tracefile: " << this->tracefile;
	
  struct multi_event_set* mes = new multi_event_set;
  multi_event_set_init(mes);

	off_t bytes_read = 0;
	
	if(read_trace_sample_file(mes, this->tracefile.c_str(), &bytes_read) != 0) {
			LOG(ERROR) << "There was an error reading the tracefile '" << this->tracefile << "'.";
			return;
	}

	if(debug_read_task_symbols(this->benchmark.c_str(),mes) != 0){
			LOG(ERROR) << "There was an error reading symbols from the binary '" << this->benchmark << "'.";
	}
	
	this->parse_instances_from_mes(mes, load_communication_matrix);

	// We no longer need the MES, so free the memory
	multi_event_set_destroy(mes);
	delete mes;

}

void ::Fuse::Execution_profile::parse_instances_from_mes(struct multi_event_set* mes, bool load_communication_matrix){
	this->parse_openstream_instances(mes, load_communication_matrix);
	this->parse_openmp_instances(mes);
}

void ::Fuse::Execution_profile::parse_openstream_instances(struct multi_event_set* mes, bool load_communication_matrix){

}

void ::Fuse::Execution_profile::parse_openmp_instances(struct multi_event_set* mes){

}


