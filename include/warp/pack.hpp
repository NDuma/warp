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

#ifndef __IOREMAP_WARP_PACK_HPP
#define __IOREMAP_WARP_PACK_HPP

#include <fstream>
#include <iostream>
#include <string>
#include <sstream>

#include <msgpack.hpp>

#include "feature.hpp"

namespace ioremap { namespace warp {

struct feature_ending {
	std::string ending;
	parsed_word::feature_mask features;

	feature_ending() : features(0ULL) {}
	feature_ending(const std::string &end, parsed_word::feature_mask fm) : ending(end), features(fm) {}

	MSGPACK_DEFINE(ending, features);
};

struct entry {
	enum {
		serialization_version = 1
	};

	std::string root;
	std::vector<feature_ending> fe;
};

class packer {
	public:
		packer(const std::string &output) {
			m_out.exceptions(m_out.failbit);
			m_out.open(output.c_str(), std::ios::binary | std::ios::trunc);
		}

		packer(const packer &z) = delete;
		~packer() {
			for (auto root = m_roots.begin(); root != m_roots.end(); ++root) {
				root->second.root = root->first;
				pack(root->second);
			}
		}

		bool zprocess(const std::string &root, const struct parsed_word &rec) {
			feature_ending en(rec.ending, rec.features);

			auto r = m_roots.find(root);
			if (r == m_roots.end()) {
				m_roots[root].fe.emplace_back(en);
			} else {
				r->second.fe.emplace_back(en);
			}
			return true;
		}

	private:
		std::ofstream m_out;
		std::map<std::string, entry> m_roots;

		bool pack(const entry &entry) {
			msgpack::sbuffer buf;
			msgpack::pack(&buf, entry);

			m_out.write(buf.data(), buf.size());
			return true;
		}

};

class unpacker {
	public:
		unpacker(const std::string &input) {
			m_in.exceptions(m_in.failbit);
			m_in.open(input.c_str(), std::ios::binary);
		}

		typedef std::function<bool (const entry &)> unpack_process;
		void unpack(const unpack_process &process) {
			msgpack::unpacker pac;

			try {
				timer t, total;
				long num = 0;
				long chunk = 100000;
				long duration;

				while (true) {
					pac.reserve_buffer(1024 * 1024);
					size_t bytes = m_in.readsome(pac.buffer(), pac.buffer_capacity());

					if (!bytes)
						break;
					pac.buffer_consumed(bytes);

					msgpack::unpacked result;
					while (pac.next(&result)) {
						msgpack::object obj = result.get();

						entry e;
						obj.convert<entry>(&e);

						if (!process(e))
							return;

						if ((++num % chunk) == 0) {
							duration = t.restart();
							std::cout << "Read objects: " << num <<
								", elapsed time: " << total.elapsed() << " msecs" <<
								", speed: " << chunk * 1000 / duration << " objs/sec" <<
								std::endl;
						}
					}
				}

				duration = total.elapsed();
				std::cout << "Read objects: " << num <<
					", elapsed time: " << duration << " msecs" <<
					", speed: " << num * 1000 / duration << " objs/sec" <<
					std::endl;

			} catch (const std::exception &e) {
				std::cerr << "Exception: " << e.what() << std::endl;
			}
		}

	private:
		std::ifstream m_in;
};

}} // namespace ioremap::warp


namespace msgpack {

template <typename Stream>
static inline msgpack::packer<Stream> &operator <<(msgpack::packer<Stream> &o, const ioremap::warp::entry &e)
{
	o.pack_array(3);
	o.pack((int)ioremap::warp::entry::serialization_version);
	o.pack(e.root);
	o.pack(e.fe);

	return o;
}

static inline ioremap::warp::entry &operator >>(msgpack::object o, ioremap::warp::entry &e)
{
	if (o.type != msgpack::type::ARRAY || o.via.array.size < 1) {
		std::ostringstream ss;
		ss << "entry msgpack: type: " << o.type <<
			", must be: " << msgpack::type::ARRAY <<
			", size: " << o.via.array.size;
		throw std::runtime_error(ss.str());
	}

	object *p = o.via.array.ptr;
	const uint32_t size = o.via.array.size;
	uint16_t version = 0;
	p[0].convert(&version);
	switch (version) {
	case 1: {
		if (size != 3) {
			std::ostringstream ss;
			ss << "entry msgpack: array size mismatch: read: " << size << ", must be: 4";
			throw std::runtime_error(ss.str());
		}

		p[1].convert(&e.root);
		p[2].convert(&e.fe);
		break;
	}
	default: {
		std::ostringstream ss;
		ss << "entry msgpack: version mismatch: read: " << version <<
			", must be: <= " << ioremap::warp::entry::serialization_version;
		throw msgpack::type_error();
	}
	}

	return e;
}

} // namespace msgpack


#endif /* __IOREMAP_WARP_PACK_HPP */