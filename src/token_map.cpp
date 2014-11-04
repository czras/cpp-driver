/*
  Copyright 2014 DataStax

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "token_map.hpp"
#include "md5.hpp"
#include "murmur3.hpp"

#include "boost/algorithm/string/predicate.hpp"
#include "boost/lexical_cast.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace cass {

static const CopyOnWriteHostVec NO_REPLICAS(new HostVec());
static const uint64_t INT64_MAX_PLUS_ONE =  static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;

static void parse_int128(const char* p, size_t n, uint8_t* output) {
  // no sign handling because C* uses [0, 2^127]
  int c;
  const char* s = p;

  for (; n != 0 && isspace(c = *s); ++s, --n) {}

  if (n == 0) {
    memset(output, 0, sizeof(uint64_t) * 2);
    return;
  }

  uint64_t hi = 0;
  uint64_t lo = 0;
  uint64_t hi_tmp;
  uint64_t lo_tmp;
  uint64_t lo_tmp2;
  for (; n != 0  && isdigit(c = *s); ++s, --n) {
    hi_tmp = hi;
    lo_tmp = lo;

    //value *= 10;
    lo = lo_tmp << 1;
    hi = (lo_tmp >> 63) + (hi_tmp << 1);
    lo_tmp2 = lo;
    lo += lo_tmp << 3;
    hi += (lo_tmp >> 61) + (hi_tmp << 3) + (lo < lo_tmp2 ? 1 : 0);

    //value += c - '0';
    lo_tmp = lo;
    lo += c - '0';
    hi += (lo < lo_tmp) ? 1 : 0;
  }

  encode_uint64(output, hi);
  encode_uint64(output + sizeof(uint64_t), lo);
}

void TokenMap::clear() {
  mapped_addresses_.clear();
  token_map_.clear();
  keyspace_replica_map_.clear();
  keyspace_strategy_map_.clear();
  partitioner_.reset();
}

void TokenMap::set_partitioner(const std::string& partitioner_class) {
  if (boost::ends_with(partitioner_class, Murmur3Partitioner::PARTITIONER_CLASS)) {
    partitioner_.reset(new Murmur3Partitioner());
  } else if (boost::ends_with(partitioner_class, RandomPartitioner::PARTITIONER_CLASS)) {
    partitioner_.reset(new RandomPartitioner());
  } else if (boost::ends_with(partitioner_class, ByteOrderedPartitioner::PARTITIONER_CLASS)) {
    partitioner_.reset(new ByteOrderedPartitioner());
  } else {
    // TODO: Global logging
  }
}

void TokenMap::update_host(SharedRefPtr<Host>& host, const TokenStringList& token_strings) {
  if (!partitioner_) return;

  // There's a chance to avoid purging if tokens are the same as existing; deemed
  // not worth the complexity because:
  // 1.) Updates should only happen on "new" host, or "moved"
  // 2.) Moving should only occur on non-vnode clusters, in which case the
  //     token map is relatively small and easy to purge/repopulate
  purge_address(host->address());

  for (TokenStringList::const_iterator i = token_strings.begin();
       i != token_strings.end(); ++i) {
    token_map_[partitioner_->token_from_string_ref(*i)] = host;
  }
  mapped_addresses_.insert(host->address());
  map_replicas();
}

void TokenMap::remove_host(SharedRefPtr<Host>& host) {
  if (!partitioner_) return;

  if (purge_address(host->address())) {
    map_replicas();
  }
}

void TokenMap::update_keyspace(const std::string& ks_name, const KeyspaceMetadata& ks_meta) {
  if (!partitioner_) return;

  SharedRefPtr<ReplicaPlacementStrategy> rps_now(ReplicaPlacementStrategy::from_keyspace_meta(ks_meta));
  KeyspaceStrategyMap::iterator i = keyspace_strategy_map_.find(ks_name);
  if (i == keyspace_strategy_map_.end() ||
      !i->second->equals(*rps_now)) {
    map_keyspace_replicas(ks_name, *rps_now);
    if (i == keyspace_strategy_map_.end()) {
      keyspace_strategy_map_[ks_name] = rps_now;
    } else {
      i->second = rps_now;
    }
  }
}

void TokenMap::drop_keyspace(const std::string& ks_name) {
  if (!partitioner_) return;

  keyspace_replica_map_.erase(ks_name);
  keyspace_strategy_map_.erase(ks_name);
}

const CopyOnWriteHostVec& TokenMap::get_replicas(const std::string& ks_name, const BufferRefs& key_parts) const {
  if (!partitioner_) return NO_REPLICAS;

  KeyspaceReplicaMap::const_iterator i = keyspace_replica_map_.find(ks_name);
  if (i != keyspace_replica_map_.end()) {
    const Token t = partitioner_->hash(key_parts);
    TokenReplicaMap::const_iterator j = i->second.upper_bound(t);
    if (j != i->second.end()) {
      return j->second;
    } else {
      if (!i->second.empty()) {
        return i->second.begin()->second;
      }
    }
  }
  return NO_REPLICAS;
}

void TokenMap::map_replicas(bool force) {
  if (keyspace_replica_map_.empty() && !force) {// do nothing ahead of first build
    return;
  }
  for (KeyspaceStrategyMap::const_iterator i = keyspace_strategy_map_.begin();
       i != keyspace_strategy_map_.end(); ++i) {
    map_keyspace_replicas(i->first, *i->second, force);
  }
}

void TokenMap::map_keyspace_replicas(const std::string& ks_name, const ReplicaPlacementStrategy& rps, bool force) {
  if (keyspace_replica_map_.empty() && !force) {// do nothing ahead of first build
    return;
  }
  rps.tokens_to_replicas(token_map_, &keyspace_replica_map_[ks_name]);
}

bool TokenMap::purge_address(const Address& addr) {
  AddressSet::iterator addr_itr = mapped_addresses_.find(addr);
  if (addr_itr == mapped_addresses_.end()) {
    return false;
  }

  TokenHostMap::iterator i = token_map_.begin();
  while (i != token_map_.end()) {
    if (addr.compare(i->second->address()) == 0) {
      TokenHostMap::iterator to_erase = i++;
      token_map_.erase(to_erase);
    } else {
      ++i;
    }
  }

  mapped_addresses_.erase(addr_itr);
  return true;
}


const std::string Murmur3Partitioner::PARTITIONER_CLASS("Murmur3Partitioner");

Token Murmur3Partitioner::token_from_string_ref(const boost::string_ref& token_string_ref) const {
  Token token(sizeof(int64_t), 0);
  int64_t token_value = boost::lexical_cast<int64_t>(token_string_ref);
  encode_uint64(&token[0], static_cast<uint64_t>(token_value) + INT64_MAX_PLUS_ONE);
  return token;
}

Token Murmur3Partitioner::hash(const BufferRefs& key_parts) const {
  Murmur3 hash;
  for (BufferRefs::const_iterator i = key_parts.begin(); i != key_parts.end(); ++i) {
    hash.update(i->data(), i->size());
  }

  Token token(sizeof(int64_t), 0);
  int64_t token_value;
  hash.final(&token_value, NULL);
  encode_uint64(&token[0], static_cast<uint64_t>(token_value) + INT64_MAX_PLUS_ONE);
  return token;
}

const std::string RandomPartitioner::PARTITIONER_CLASS("RandomPartitioner");

Token RandomPartitioner::token_from_string_ref(const boost::string_ref& token_string_ref) const {
  Token token(sizeof(uint64_t) * 2, 0);
  parse_int128(token_string_ref.data(), token_string_ref.size(), &token[0]);
  return token;
}

Token RandomPartitioner::hash(const BufferRefs& key_parts) const {
  Md5 hash;
  for (BufferRefs::const_iterator i = key_parts.begin(); i != key_parts.end(); ++i) {
    hash.update(i->data(), i->size());
  }

  Token token(sizeof(uint64_t) * 2, 0);
  hash.final(&token[0]);
  return token;
}

const std::string ByteOrderedPartitioner::PARTITIONER_CLASS("ByteOrderedPartitioner");

Token ByteOrderedPartitioner::token_from_string_ref(const boost::string_ref& token_string_ref) const {
  const uint8_t* data = copy_cast<const char*, const uint8_t*>(token_string_ref.data());
  size_t size = token_string_ref.size();
  return Token(data, data + size);
}

Token ByteOrderedPartitioner::hash(const BufferRefs& key_parts) const {
  Token token;
  size_t total_size = 0;
  for (BufferRefs::const_iterator i = key_parts.begin();
       i != key_parts.end(); ++i) {
    total_size += i->size();
  }
  token.reserve(total_size);
  for (BufferRefs::const_iterator i = key_parts.begin();
       i != key_parts.end(); ++i) {
    token.insert(token.end(), i->begin(), i->end());
  }
  return token;
}

}