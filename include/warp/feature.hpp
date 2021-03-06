/*
 * Copyright 2013+ Evgeniy Polyakov <zbr@ioremap.net>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __WARP_FEATURE_HPP
#define __WARP_FEATURE_HPP

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <set>

#include <boost/locale.hpp>

#include "timer.hpp"

namespace ioremap { namespace warp {

namespace lb = boost::locale::boundary;

struct token_entity {
	token_entity() : position(-1) {}
	int		position;

	bool operator() (const token_entity &a, const token_entity &b) {
		return a.position < b.position;
	}
};

struct parser {
	std::map<std::string, token_entity> t2p;
	int unique;

	void push(const std::vector<std::string> &tokens) {
		for (auto it = tokens.begin(); it != tokens.end(); ++it) {
			if (insert(*it, unique))
				++unique;
		}
	}

	void push(const std::string &bool_token) {
		if (insert(bool_token, unique))
			++unique;
	}

	token_entity try_parse(const std::string &token) {
		token_entity tok;

		auto it = t2p.find(token);
		if (it != t2p.end())
			tok = it->second;

		return tok;
	}

	bool insert(const std::string &token, int position) {
		token_entity t;

		auto pos = t2p.find(token);
		if (pos == t2p.end()) {
			t.position = position;

			t2p[token] = t;

			//std::cout << token << ": " << position << std::endl;
		}

		return pos == t2p.end();
	}

	parser() : unique(0) {
		push({ "им", "род", "дат", "вин", "твор", "пр" });
		push({ std::string("ед"), "мн" });
		push({ std::string("неод"), "од" });
		push({ std::string("полн"), "кр" });
		push({ "муж", "жен", "сред", "мж" });
		push("устар");
		push({ std::string("прич"), "деепр" });
		push({ std::string("действ"), "страд" });
		push({ "имя", "отч", "фам" });
		push({ "S", "A", "V", "PART", "PR", "CONJ", "INTJ", "ADV", "PRDK", "SPRO", "COM", "APRO", "ANUM" });

		token_entity ent;
		ent = try_parse("ADV");
		insert("ADVPRO", ent.position);
		ent = try_parse("ANUM");
		insert("NUM", ent.position);

		push({ "наст", "прош", "буд" });
		push({ "1", "2", "3" });
		push({ std::string("сов"), "несов" });
		push({ "сосл", "пов", "изъяв" });
		push("гео");
		push("орг");
		push({ std::string("срав"), "прев" });
		push("инф");

		push("притяж");
		ent = try_parse("притяж");
		insert("AOT_притяж", ent.position);

		push("жарг");

		push("obsclite");
		ent = try_parse("obsclite");
		insert("обсц", ent.position);

		push("weired");
		ent = try_parse("weired");
		for (auto w : { "непрош", "пе", "-", "л", "нп", "reserved", "AOT_разг", "dsbl", "сокр", "парт", "вводн", "местн",
				"редк", "AOT_ФРАЗ", "AOT_безл", "зват", "разг", "AOT_фраз", "AOT_указат", "буфф" }) {
			insert(w, ent.position);
		}
	}

};

struct parsed_word {
	enum {
		serialization_version = 1
	};

	std::string lemma;
	std::string word;

	typedef uint64_t feature_mask;
	feature_mask features;

	int ending_len;

	parsed_word() : features(0ULL), ending_len(0) {}
};

static inline bool default_process(const struct parsed_word &) { return false; }

class zparser {
	public:
		// return false if you want to stop further processing
		typedef std::function<bool (const struct parsed_word &rec)> zparser_process;
		zparser() : m_total(0), m_process(default_process) {
			boost::locale::generator gen;
			m_loc = gen("en_US.UTF8");
		}

		void set_process(const zparser_process &process) {
			m_process = process;
		}

		std::vector<std::string> split(const std::string &sentence) {
			lb::ssegment_index wmap(lb::word, sentence.begin(), sentence.end(), m_loc);
			wmap.rule(lb::word_any);

			std::vector<std::string> ret;
			for (auto it = wmap.begin(), e = wmap.end(); it != e; ++it) {
				ret.push_back(it->str());
			}

			return ret;
		}

		bool parse_dict_string(const std::string &token, const std::string &lemma) {
			//std::string token = boost::locale::to_lower(token_str, m_loc);

			lb::ssegment_index wmap(lb::word, token.begin(), token.end(), m_loc);
			wmap.rule(lb::word_any | lb::word_none);

			std::string root;
			std::string ending;
			parsed_word rec;

			std::vector<std::string> failed;

			for (auto it = wmap.begin(), e = wmap.end(); it != e; ++it) {
				// skip word prefixes
				if ((root.size() == 0) && (it->str() != "["))
					continue;

				if (it->str() == "[") {
					if (++it == e)
						break;
					root = boost::locale::to_lower(it->str(), m_loc);

					if (++it == e)
						break;

					// something is broken, skip this line at all
					if (it->str() != "]")
						break;

					if (++it == e)
						break;

					// ending check
					// we assume here that ending can start with the word token
					if (it->rule() & lb::word_any) {
						do {
							// there is no ending in this word if next token is space (or anything 'similar' if that matters)
							// only not space tokens here mean (parts of) word ending, let's check it
							if (isspace(it->str()[0]))
								break;

							ending += it->str();

							if (++it == e)
								break;
						} while (true);

						if (it == e)
							break;
					}
				}

				// skip this token if it is not word token
				if (!(it->rule() & lb::word_any))
					continue;

				token_entity ent = m_p.try_parse(it->str());
				if (ent.position == -1) {
					failed.push_back(it->str());
				} else {
					if (ent.position < (int)sizeof(parsed_word::feature_mask) * 8)
						rec.features |= (parsed_word::feature_mask)1 << ent.position;
				}
			}

			if (rec.features == 0ULL)
				return true;

			rec.word = root + ending;
			rec.ending_len = ending.size();
			rec.lemma = lemma;
			m_total++;

			return m_process(rec);
		}

		void parse_file(const std::string &input_file) {
			std::ifstream in(input_file.c_str());

			ioremap::warp::timer t;
			ioremap::warp::timer total;

			std::string line, lemma;

			long lines = 0;
			long chunk = 100000;
			long duration;
			bool read_lemma = false;

			while (std::getline(in, line)) {
				if (++lines % chunk == 0) {
					duration = t.restart();
					std::cout << "Read and parsed lines: " << lines <<
						", total words/features found: " << m_total <<
						", elapsed time: " << total.elapsed() << " msecs" <<
						", speed: " << chunk * 1000 / duration << " lines/sec" <<
						std::endl;
				}

				if (line[0] == '@') {
					read_lemma = false;
					continue;
				}

				if (!read_lemma) {
					// next line contains lemma word
					lemma = boost::locale::to_lower(line, m_loc);
					read_lemma = true;
					continue;
				}

				if (!parse_dict_string(line, lemma))
					break;
			}
			duration = total.elapsed();
			std::cout << "Read and parsed " << lines << " lines, elapsed: " << duration <<
				" msecs, speed: " << lines * 1000 / duration << " lines/sec" << std::endl;
		}

		int parser_features_num(void) const {
			return m_p.unique;
		}

		int total_features_num(void) const {
			return m_total;
		}

	private:
		std::locale m_loc;
		parser m_p;
		int m_total;

		zparser_process m_process;
};


}} // namespace ioremap::warp

#endif /* __WARP_FEATURE_HPP */
