#include <iostream>
#include <stdexcept>
#include <sstream>

#include <boost/program_options.hpp>

#include <fann.h>
#include <fann_cpp.h>

#include "feature.hpp"
#include "ann.hpp"

namespace ioremap { namespace warp {

class check {
	public:
		check(const std::string &input, const std::string &input_letters) {
			m_lv = m_nn.load_letters(input_letters);

			m_ann.create_from_file(input.c_str());
			if (m_ann.get_errno()) {
				std::ostringstream ss;
				ss << "FANN creation failed: " << m_ann.get_errstr();
				throw std::runtime_error(ss.str());
			}

			m_input = m_ann.get_num_input();
			m_output = m_ann.get_num_output();
		}

		void run(const std::string &sentence, const std::string &gram) {
			std::vector<std::string> words = m_base.split(sentence);
			std::vector<std::string> grams = m_base.split(gram);

			std::vector<ioremap::warp::token_entity> ents;
			for (auto gr = grams.begin(); gr != grams.end(); ++gr) {
				ioremap::warp::token_entity ent = m_parser.try_parse(*gr);
				if (ent.position != -1)
					ents.emplace_back(ent);
			}

			for (auto word = words.begin(); word != words.end(); ++word) {
				std::vector<fann_type> f = m_nn.convert_word<fann_type>(m_lv, *word, m_input);
				fann_type *output;

				output = m_ann.run(f.data());

				unsigned ending_size = 8;
				std::cout << *word << ": ";
				for (unsigned i = 0; i < m_output; ++i) {
					if (i == ending_size)
						std::cout << "| ";

					std::cout << output[i] << " ";
				}
				std::cout << std::endl;

				std::vector<ioremap::warp::token_entity> matched;
				for (auto ent = ents.begin(); ent != ents.end(); ++ent) {
					if (output[ent->position] > 0.8)
						matched.push_back(*ent);
				}

				if (matched.size()) {
					std::cout << *word << ": ";
					for (auto ent = matched.begin(); ent != matched.end(); ++ent)
						std::cout << ent->position << " ";
					std::cout << std::endl;
				}
			}
			
		}

		~check() {
		}

	private:
		ioremap::warp::ann m_nn;
		class FANN::neural_net m_ann;
		ioremap::warp::zparser m_base;
		std::vector<std::string> m_lv;

		unsigned int m_input, m_output;

		ioremap::warp::parser m_parser;
};

}}	

int main(int argc, char *argv[])
{
	namespace bpo = boost::program_options;
	std::string input, input_letters, grammar;

	bpo::options_description op("ANN options");
	op.add_options()
		("help", "This help message")
		("input", bpo::value<std::string>(&input), "Input FANN file")
		("letters", bpo::value<std::string>(&input_letters), "Letters file")
		("gram", bpo::value<std::string>(&grammar), "Grammar string")
		;

	bpo::variables_map vm;
	try {
		bpo::store(bpo::parse_command_line(argc, argv, op), vm);
		bpo::notify(vm);
	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl << op << std::endl;
		return -1;
	}

	if (!vm.count("input") || !vm.count("letters") || !vm.count("gram")) {
		std::cerr << "You must provide input and letters files as well as grammar string\n" << op << std::endl;
		return -1;
	}

	try {
		ioremap::warp::check ch(input, input_letters);
		std::ifstream in("/dev/stdin");

		if (!in.good()) {
			std::cerr << "Could not open stdin" << std::endl;
			return -1;
		}

		std::string line;
		while (std::getline(in, line)) {
			ch.run(line, grammar);
		}
	} catch (const std::exception &e) {
		std::cerr << "Exception caught during FANN run: " << e.what() << std::endl;
		return -1;
	}

	return 0;
}