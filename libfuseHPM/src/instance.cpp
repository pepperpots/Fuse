#include "instance.h"

#include "fuse_types.h"

Fuse::Instance::Instance(){

}

Fuse::Instance::~Instance(){

}

void Fuse::Instance::append_event_value(Fuse::Event event, int64_t value, bool additive){

	if(additive){

		auto event_iter = this->event_values.find(event);
		if(event_iter == this->event_values.end())
			this->event_values.insert(std::make_pair(event, value));
		else
			event_iter->second += value;

	} else
		this->event_values[event] = value;

}

void Fuse::Instance::append_max_event_value(Fuse::Event event, int64_t value){

	auto event_iter = this->event_values.find(event);
	if(event_iter == this->event_values.end())
		this->event_values.insert(std::make_pair(event, value));
	else if(event_iter->second < value)
		event_iter->second = value;

	// do nothing if the current value is equal or creator than the new value

}

int64_t Fuse::Instance::get_event_value(Fuse::Event event, bool& error){

	auto event_iter = this->event_values.find(event);
	if(event_iter == this->event_values.end()){
		error = true;
		return 0;
	}

	// Don't change the error to false, so we can detect if at least a single error exists across multiple calls
	// error = false;
	return event_iter->second;

}

bool Fuse::comp_instances_by_label_dfs(Fuse::Instance_p a, Fuse::Instance_p b){

	int a_depth = a->label.size();
	int b_depth = b->label.size();

	for(int i=0; i<a_depth && i<b_depth; i++){
		if(a->label.at(i) < b->label.at(i))
			return true;

		if(b->label.at(i) < a->label.at(i))
			return false;
	}

	return (a_depth < b_depth);
}

Fuse::Event_set Fuse::Instance::get_events(){

	Fuse::Event_set events;
	events.reserve(this->event_values.size());
	for(auto event_iter : this->event_values){
		events.push_back(event_iter.first);
	}

	return events;

}
