// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pool.h
 * Pool Implementation
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_POOL_H
#define __DRMDEVICE_POOL_H

#include <memory>
#include <vector>

/**
 * Pool Implementation Template Class
 *
 * A fixed number of default constructed objects, which are owned by the pool.
 *
 * @tparam T    type of the pooled objects
 *
 * @ingroup misc
 */
template <typename T>
class cPool {
protected:
	std::vector<std::unique_ptr<T>> buffer;    ///< the pooled objects

public:
	/** Create a pool with the given number of objects */
	cPool(size_t size) {
		buffer.reserve(size);

		for (size_t i = 0; i < size; ++i) {
				buffer.emplace_back(std::make_unique<T>());
		}
	}
};

#endif
