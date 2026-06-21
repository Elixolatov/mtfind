#include <iostream>
#include "mtfind.h"


int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "Usage: mtfind <file> <mask>\n";
		return 1;
	}
	std::string filename = argv[1];
	std::string mask = argv[2];

	MtFind finder;
	try {
		finder.FindMatches(filename, mask);
		finder.PrintMatches();
	}
	catch (const std::exception& e) {
		std::cerr << e.what();
		return 1;
	};
}
