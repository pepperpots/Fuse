#include "instance.h"

::Fuse::Instance::Instance(){

}

::Fuse::Instance::~Instance(){

}

void ::Fuse::Instance::append_event_value(::Fuse::Event event, int64_t value, bool additive){

	if(additive){

		auto event_iter = this->event_values.find(event);
		if(event_iter == this->event_values.end())
			this->event_values.insert(std::make_pair(event, value));
		else
			event_iter->second += value;

	} else {

		this->event_values[event] = value;

	}

}
