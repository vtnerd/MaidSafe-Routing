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

#ifndef MAIDSAFE_ROUTING_GET_DATA_H_
#define MAIDSAFE_ROUTING_GET_DATA_H_

#include "maidsafe/common/config.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/serialisation/compile_time_mapper.h"

#include "maidsafe/routing/message_header.h"
#include "maidsafe/routing/messages.h"
#include "maidsafe/routing/types.h"

namespace maidsafe {

namespace routing {

struct GetData {
  static const SerialisableTypeTag kSerialisableTypeTag =
      static_cast<SerialisableTypeTag>(MessageTypeTag::kGetData);

  GetData() = default;
  GetData(const GetData&) = delete;
  GetData(GetData&& other) MAIDSAFE_NOEXCEPT : header(std::move(other.header)),
                                               data_name(std::move(other.data_name)) {}
  GetData(DestinationAddress destination, SourceAddress source, Address data_name_in)
      : header(std::move(destination), std::move(source), MessageId(RandomUint32())),
        data_name(std::move(data_name_in)) {}
  ~GetData() = default;
  GetData& operator=(const GetData&) = delete;
  GetData& operator=(GetData&& other) MAIDSAFE_NOEXCEPT {
    header = std::move(other.header);
    data_name = std::move(other.data_name);
    return *this;
  };

  template <typename Archive>
  void serialize(Archive& archive) const {
    archive(header, data_name);
  }

  MessageHeader header;
  Address data_name;
};


}  // namespace routing

}  // namespace maidsafe

#endif  // MAIDSAFE_ROUTING_GET_DATA_H_