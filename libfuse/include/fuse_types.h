#ifndef FUSE_TYPES_H
#define FUSE_TYPES_H

#include <memory>
#include <string>
#include <vector>

namespace Fuse {

	class Target;
	class Instance;
	class Execution_profile;

	enum Runtime {
		OPENSTREAM,
		OPENMP
	};

	enum Strategy {
		RANDOM,
		RANDOM_TT,
		CTC,
		LGL,
		BC,
		HEM
	};

	typedef std::string Event;
	typedef std::vector<Event> Event_set;
	struct Sequence_part {
		Event_set overlapping;
		Event_set unique;
	};
	typedef std::vector<Sequence_part> Combination_sequence;
	typedef std::string Symbol;

	typedef std::shared_ptr<Fuse::Execution_profile> Profile_p;
	typedef std::shared_ptr<Fuse::Instance> Instance_p;

	/* Functions */

	Strategy convert_string_to_strategy(std::string strategy_string);
	std::string convert_strategy_to_string(Strategy strategy);

}

#endif
