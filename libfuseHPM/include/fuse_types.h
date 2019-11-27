#ifndef FUSE_TYPES_H
#define FUSE_TYPES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Fuse {

	class Target;
	class Instance;
	class Execution_profile;
	class Statistics;

	enum Runtime {
		OPENSTREAM,
		OPENMP
	};

	enum Strategy {
		RANDOM,
		RANDOM_MINIMAL,
		RANDOM_TT,
		RANDOM_TT_MINIMAL,
		CTC,
		CTC_MINIMAL,
		LGL,
		LGL_MINIMAL,
		BC,
		HEM
	};

	typedef std::string Event;
	typedef std::vector<Event> Event_set;
	struct Sequence_part {
		unsigned int part_idx;
		Event_set overlapping;
		Event_set unique;
	};
	typedef std::vector<Sequence_part> Combination_sequence;
	typedef std::string Symbol;

	typedef std::shared_ptr<Fuse::Execution_profile> Profile_p;
	typedef std::shared_ptr<Fuse::Instance> Instance_p;
	typedef std::shared_ptr<Fuse::Statistics> Statistics_p;

	/* Functions */

	Strategy convert_string_to_strategy(std::string strategy_string, bool minimal);
	Strategy convert_string_to_strategy(std::string strategy_string);
	std::string convert_strategy_to_string(Strategy strategy);

}

#endif
