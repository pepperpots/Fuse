#ifndef HELPERS_H
#define HELPERS_H

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

namespace Helpers {
		
	template <typename T>
	inline std::string vector_to_string(std::vector<T> vector, std::string delim=","){
		std::stringstream ss;
		for(typename std::vector<T>::iterator it = vector.begin(); it != vector.end(); it++){
			ss << *it;
			if(std::distance(it,vector.end())>1)
				ss << delim;
		}
		return "[" + ss.str()+ "]";
	}

}

#endif
