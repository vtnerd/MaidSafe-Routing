/*  Copyright 2014 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_ROUTING_TYPES_H_
#define MAIDSAFE_ROUTING_TYPES_H_

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "boost/asio/ip/udp.hpp"

#include "maidsafe/common/node_id.h"
#include "maidsafe/common/tagged_value.h"

namespace maidsafe {

namespace routing {

static const size_t kGroupSize = 32;
static const size_t kQuorumSize = 29;
using Address = NodeId;
using DestinationAddress = TaggedValue<Address, struct DestinationTag>;
using SourceAddress = TaggedValue<Address, struct SourceTag>;
using MessageId = TaggedValue<uint32_t, struct MessageIdTag>;
using Endpoint = boost::asio::ip::udp::endpoint;
using Connection = boost::asio::ip::udp::endpoint;
using MurmurHash = uint32_t;
using Checksums = std::array<MurmurHash, kGroupSize - 1>;
using SerialisedMessage = std::vector<byte>;
using CloseGroupDifference = std::pair<std::vector<Address>, std::vector<Address>>;

}  // namespace routing

}  // namespace maidsafe

#endif  // MAIDSAFE_ROUTING_TYPES_H_