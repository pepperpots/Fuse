#include "instance.h"

#include "fuse_types.h"

Fuse::Instance::Instance(){

}

Fuse::Instance::~Instance(){

}

void Fuse::Instance::append_event_value(::Fuse::Event event, int64_t value, bool additive){

	if(additive){

		auto event_iter = this->event_values.find(event);
		if(event_iter == this->event_values.end())
			this->event_values.insert(std::make_pair(event, value));
		else
			event_iter->second += value;

	} else
		this->event_values[event] = value;

}

void Fuse::Instance::append_max_event_value(::Fuse::Event event, int64_t value){

	auto event_iter = this->event_values.find(event);
	if(event_iter == this->event_values.end())
		this->event_values.insert(std::make_pair(event, value));
	else if(event_iter->second < value)
		event_iter->second = value;

	// do nothing if the current value is equal or creator than the new value

}

int64_t Fuse::Instance::get_event_value(::Fuse::Event event, bool& error){

	auto event_iter = this->event_values.find(event);
	if(event_iter == this->event_values.end()){
		error = true;
		return 0;
	} 
	
	error = false;
	return event_iter->second;

}
