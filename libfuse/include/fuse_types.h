#ifndef FUSE_TYPES_H
#define FUSE_TYPES_H

#include <memory>
#include <string>
#include <vector>

namespace Fuse {

	class Case;
	class Instance;
	class Execution_profile;

	typedef std::string Event;
	typedef std::vector<Event> Event_set;
	struct Sequence_part {
		Event_set overlapping;
		Event_set additional;
	};
	typedef std::vector<Sequence_part> Combination_sequence;
	typedef std::string Symbol;

	typedef std::shared_ptr<::Fuse::Execution_profile> Profile_p;
	typedef std::shared_ptr<::Fuse::Instance> Instance_p;

}

#endif
