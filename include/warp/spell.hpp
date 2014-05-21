/*
 * Copyright 2014+ Evgeniy Polyakov <zbr@ioremap.net>
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

#ifndef __WARP_SPELL_HPP
#define __WARP_SPELL_HPP

#include "warp/distance.hpp"
#include "warp/fuzzy.hpp"
#include "warp/ngram.hpp"
#include "warp/pack.hpp"
#include "warp/timer.hpp"

#include <msgpack.hpp>

namespace ioremap { namespace warp {

class spell {
	public:
		spell(int ngram) : m_fuzzy(ngram), m_words(0), m_lemmas(0) {}

		void feed_dict(const std::vector<std::string> &path) {
			timer tm;

			warp::unpacker(path, 2, std::bind(&spell::unpack_everything, this, std::placeholders::_1));

			printf("spell checker loaded: words: %ld, lemmas: %ld, time: %lld ms\n",
					m_words.load(), m_lemmas.load(), (unsigned long long)tm.elapsed());
		}

		void feed_word(const std::string &word) {
			m_fuzzy.feed_word(lconvert::from_utf8(word), lconvert::from_utf8(word));
			m_words += 1;
			m_lemmas += 1;
		}

		std::vector<lstring> search(const std::string &text) {
			timer tm;
			std::vector<lstring> ret;

			auto precise = m_form2lemma.find(text);

			printf("spell checker lookup: '%s': precise search: total words: %zd, search-time: %lld ms\n",
					text.c_str(), m_form2lemma.size(), (unsigned long long)tm.elapsed());

			if (precise != m_form2lemma.end()) {
				ret.push_back(precise->second);
				return ret;
			}

			lstring t = lconvert::from_utf8(boost::locale::to_lower(text, __fuzzy_locale));
			auto fsearch = m_fuzzy.search(t);

			printf("spell checker lookup: rough search: words: %zd, fuzzy-search-time: %lld ms\n",
					fsearch.size(), (unsigned long long)tm.elapsed());

			ret = search_everything(t, fsearch);

			printf("spell checker lookup: checked: words: %zd, total-search-time: %lld ms:\n",
					ret.size(), (unsigned long long)tm.restart());

			for (auto r = ret.begin(); r != ret.end(); ++r)
				std::cout << *r << std::endl;
			return ret;
		}

	private:
		std::mutex m_lock;
		std::map<std::string, lstring> m_form2lemma;

		fuzzy<lstring> m_fuzzy;
		std::atomic_long m_words, m_lemmas;

		bool unpack_everything(const warp::parsed_word &e) {
			auto lword = lconvert::from_utf8(e.lemma);

			std::unique_lock<std::mutex> guard(m_lock);

			auto it = m_form2lemma.find(e.lemma);
			if (it == m_form2lemma.end()) {
				m_fuzzy.feed_word(lword, lword);
				m_lemmas += 1;
				m_form2lemma[e.lemma] = lword;
			}

			m_form2lemma[e.word] = lword;

			m_words += 1;
			return true;
		}

		std::vector<lstring> search_everything(const lstring &t, const std::vector<lstring> &fsearch) {
			std::vector<lstring> ret;
			int min_dist = 2;

			ret.reserve(fsearch.size());

			for (auto w = fsearch.begin(); w != fsearch.end(); ++w) {
				if (w->size() > t.size() + 2)
					continue;

				if (t.size() > w->size() + 2)
					continue;

				int dist = distance::levenstein<lstring>(t, *w, min_dist);
				if (dist < 0)
					continue;
				std::cout << t << " vs " << *w << " : " << dist << std::endl;

				if (dist < min_dist)
					ret.clear();

				ret.emplace_back(*w);
				min_dist = dist;
			}

			return ret;
		}
};


}} // namespace ioremap::warp

#endif /* __WARP_SPELL_HPP */
