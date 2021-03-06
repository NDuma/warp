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

#ifndef __WARP_NGRAM_HPP
#define __WARP_NGRAM_HPP

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>

#include <math.h>

namespace ioremap { namespace warp { namespace ngram {
template <typename S, typename D>
class ngram {
	struct ngram_data {
		D data;
		int pos;

		ngram_data() : pos(0) {}
	};

	struct ngram_index_data {
		size_t data_index;
		int pos;

		bool operator<(const ngram_index_data &other) const {
			return data_index < other.data_index;
		}

		ngram_index_data() : data_index(0), pos(0) {}
	};

	struct ngram_meta {
		std::set<ngram_index_data> data;
		double count;

		ngram_meta() : count(1.0) {}
	};

	public:
		ngram(int n) : m_n(n) {}

		static std::vector<S> split(const S &text, size_t ngram) {
			std::vector<S> ret;

			if (text.size() >= ngram) {
				for (size_t i = 0; i < text.size() - ngram + 1; ++i) {
					S word = text.substr(i, ngram);
					ret.emplace_back(word);
				}
			}

			return ret;
		}

		void load(const S &text, const D &d) {
			std::vector<S> grams = ngram<S, D>::split(text, m_n);
			int position = 0;
			size_t index;

			auto it = m_data_index.find(d);
			if (it == m_data_index.end()) {
				index = m_data.size();
				m_data.push_back(d);

				m_data_index[d] = index;
			} else {
				index = it->second;
			}

			for (auto word = grams.begin(); word != grams.end(); ++word) {
				ngram_index_data data;
				data.data_index = index;
				data.pos = position++;

				auto it = m_map.find(*word);
				if (it == m_map.end()) {
					ngram_meta meta;

					meta.data.insert(data);
					m_map[*word] = meta;
				} else {
					it->second.count++;
					it->second.data.insert(data);
				}
			}
		}

		std::vector<ngram_data> lookup_word(const S &word) const {
			std::vector<ngram_data> ret;

			auto it = m_map.find(word);
			if (it != m_map.end()) {
				for (auto idx = it->second.data.begin(); idx != it->second.data.end(); ++idx) {
					ngram_data data;

					data.data = m_data[idx->data_index];
					data.pos = idx->pos;

					ret.emplace_back(data);
				}
			}

			return ret;
		}

		double lookup(const S &word) const {
			double count = 1.0;

			auto it = m_map.find(word);
			if (it != m_map.end())
				count += it->second.count;

			count /= 2.0 * m_map.size();
			return count;
		}

		size_t num(void) const {
			return m_map.size();
		}

		int n(void) const {
			return m_n;
		}

	private:
		int m_n;
		std::map<S, ngram_meta> m_map;
		std::vector<D> m_data;
		std::map<D, size_t> m_data_index;
};

typedef ngram<std::string, std::string> byte_ngram;

class probability {
	public:
		probability() : m_n2(2), m_n3(3) {}

		bool load_file(const char *filename) {
			std::ifstream in(filename, std::ios::binary);
			std::ostringstream ss;
			ss << in.rdbuf();

			std::string text = ss.str();

			m_n2.load(text, text);
			m_n3.load(text, text);
#if 0
			printf("%s: loaded: %zd bytes, 2-grams: %zd, 3-grams: %zd\n",
					filename, text.size(), m_n2.num(), m_n3.num());
#endif
			return true;
		}

		double detect(const std::string &text) const {
			double p = 0;
			for (size_t i = 3; i < text.size(); ++i) {
				std::string s3 = text.substr(i - 3, 3);
				std::string s2 = text.substr(i - 3, 2);

				p += log(m_n3.lookup(s3) / m_n2.lookup(s2));
			}

			return abs(p);
		}

	private:
		byte_ngram m_n2, m_n3;
};

class detector {
	public:
		detector() {}

		bool load_file(const char *filename, const char *id) {
			probability p;
			bool ret = p.load_file(filename);
			if (ret)
				n_prob[id] = p;

			return ret;
		}

		std::string detect(const std::string &text) const {
			double max_p = 0;
			std::string name = "";

			for (auto it = n_prob.begin(); it != n_prob.end(); ++it) {
				double p = it->second.detect(text);
				if (p > max_p) {
					name = it->first;
					max_p = p;
				}
			}

			return name;
		}

		std::string detect_file(const char *filename) const {
			std::ifstream in(filename, std::ios::binary);
			std::ostringstream ss;
			ss << in.rdbuf();

			std::string text = ss.str();
			return detect(text);
		}

	private:
		std::map<std::string, probability> n_prob;
};

}}} // namespace ioremap::warp::ngram

#endif /* __WARP_NGRAM_HPP */
