/*******************************************************************************
 *  Copyright 2012 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 ******************************************************************************/

#include "maidsafe/routing/response_handler.h"

#include<memory>
#include<vector>
#include<string>
#include <algorithm>

#include "maidsafe/common/log.h"
#include "maidsafe/common/node_id.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/routing/network_utils.h"
#include "maidsafe/routing/non_routing_table.h"
#include "maidsafe/routing/return_codes.h"
#include "maidsafe/routing/routing_pb.h"
#include "maidsafe/routing/routing_table.h"
#include "maidsafe/routing/rpcs.h"
#include "maidsafe/routing/utils.h"


namespace maidsafe {

namespace routing {

namespace {

typedef boost::asio::ip::udp::endpoint Endpoint;

}  // unnamed namespace

ResponseHandler::ResponseHandler(RoutingTable& routing_table,
                                 NonRoutingTable& non_routing_table,
                                 NetworkUtils& network)
    : routing_table_(routing_table),
      non_routing_table_(non_routing_table),
      network_(network),
      request_public_key_functor_() {}

ResponseHandler::~ResponseHandler() {}

void ResponseHandler::Ping(protobuf::Message& message) {
  // Always direct, never pass on

  // TODO(dirvine): do we need this and where and how can I update the response
  protobuf::PingResponse ping_response;
  if (ping_response.ParseFromString(message.data(0))) {
    // do stuff here
  }
}

void ResponseHandler::Connect(protobuf::Message& message) {
  protobuf::ConnectResponse connect_response;
  protobuf::ConnectRequest connect_request;
  if (!connect_response.ParseFromString(message.data(0))) {
    LOG(kError) << "Could not parse connect response";
    return;
  }

  if (!connect_response.answer()) {
    LOG(kVerbose) << "Peer rejected this node's connection request." << " id: " << message.id();
    return;
  }

  if (!connect_request.ParseFromString(connect_response.original_request())) {
    LOG(kError) << "Could not parse original connect request" << " id: " << message.id();
    return;
  }

  if (!NodeId(connect_response.contact().node_id()).IsValid() ||
      NodeId(connect_response.contact().node_id()).Empty()) {
    LOG(kError) << "Invalid contact details";
    return;
  }

  NodeInfo node_to_add;
  node_to_add.node_id = NodeId(connect_response.contact().node_id());

  if (routing_table_.CheckNode(node_to_add) /*||
      (node_to_add.node_id == network_.bootstrap_connection_id())*/) {
    rudp::EndpointPair peer_endpoint_pair;
    peer_endpoint_pair.external =
        GetEndpointFromProtobuf(connect_response.contact().public_endpoint());
    peer_endpoint_pair.local =
        GetEndpointFromProtobuf(connect_response.contact().private_endpoint());

    if (peer_endpoint_pair.external.address().is_unspecified() &&
        peer_endpoint_pair.local.address().is_unspecified()) {
      LOG(kError) << "Invalid peer endpoint details";
      return;
    }
    // Updating bootstrap only for test local network
    if (!peer_endpoint_pair.external.address().is_unspecified())
      network_.AddToBootstrapFile(peer_endpoint_pair.external);
    else
      network_.AddToBootstrapFile(peer_endpoint_pair.local);

    NodeId peer_node_id(connect_response.contact().node_id());
//    NodeId this_node_seen_connection_id;
//    if (connect_response.has_connection_id())
//      this_node_seen_connection_id = NodeId((connect_response.connection_id()));

    LOG(kVerbose) << "This node [" << DebugId(routing_table_.kNodeId())
                  << "] received connect response from "
                  << DebugId(peer_node_id)
                  << " id: " << message.id();

    AddToRudp(network_, routing_table_.kNodeId(),
              routing_table_.kConnectionId(),
              peer_node_id,
              peer_node_id,
              peer_endpoint_pair,
              true,  // requestor
              routing_table_.client_mode());
  } else {
    LOG(kInfo) << "Already added node";
  }

//  std::vector<std::string> closest_nodes(connect_request.closest_id().begin(),
//                                         connect_request.closest_id().end());
//  closest_nodes.push_back(message.source_id());
//  for (auto node_id : connect_response.closer_id())
//    LOG(kVerbose) << "Connecting to closer id: " << HexSubstr(node_id);

//  ConnectTo(std::vector<std::string>(connect_response.closer_id().begin(),
//                                     connect_response.closer_id().end()),
//            closest_nodes);
}

void ResponseHandler::FindNodes(const protobuf::Message& message) {
  protobuf::FindNodesResponse find_nodes;
  if (!find_nodes.ParseFromString(message.data(0))) {
    LOG(kError) << "Could not parse find node response";
    return;
  }
//  if (asymm::CheckSignature(find_nodes.original_request(),
//                            find_nodes.original_signature(),
//                            routing_table.kKeys().public_key) != kSuccess) {
//    LOG(kError) << " find node request was not signed by us";
//    return;  // we never requested this
//  }

  LOG(kVerbose) << "This node [" << DebugId(routing_table_.kNodeId())
                << "] received FindNodes response from " << HexSubstr(message.source_id())
                << " id: " << message.id();
  std::string find_node_result = "FindNodes from " + HexSubstr(message.source_id()) +
          " returned :\n";
  for (int i = 0; i < find_nodes.nodes_size(); ++i) {
    find_node_result += "[" + HexSubstr(find_nodes.nodes(i)) + "]\t";
  }

  LOG(kVerbose) << find_node_result;

  for (int i = 0; i < find_nodes.nodes_size(); ++i) {
    if (!find_nodes.nodes(i).empty())
      SendConnectRequest(NodeId(find_nodes.nodes(i)));
  }
}

void ResponseHandler::SendConnectRequest(const NodeId peer_node_id) {
  if (network_.bootstrap_connection_id().Empty() && (routing_table_.Size() == 0)) {
      LOG(kWarning) << "Need to re bootstrap !";
    return;
  }

  bool send_to_bootstrap_connection((routing_table_.Size() < Parameters::closest_nodes_size) &&
                                    !network_.bootstrap_connection_id().Empty());
  NodeInfo peer;
  peer.node_id = peer_node_id;
  if (peer.node_id == NodeId(routing_table_.kKeys().identity)) {
    LOG(kWarning) << "Collision detected";
    return;  // TODO(Prakash): FIXME handle collision and return kIdCollision on join()
  }
  if (routing_table_.CheckNode(peer)) {
    LOG(kVerbose) << "CheckNode succeeded for node " << DebugId(peer.node_id);
    rudp::EndpointPair this_endpoint_pair, peer_endpoint_pair;
    rudp::NatType this_nat_type(rudp::NatType::kUnknown);
    int ret_val = network_.GetAvailableEndpoint(peer.node_id,
                                                peer_endpoint_pair,
                                                this_endpoint_pair,
                                                this_nat_type);
    if (kSuccess != ret_val) {
      LOG(kError) << "[" << DebugId(routing_table_.kNodeId()) << "] Response Handler"
                  << "Failed to get available endpoint for new connection to : "
                  << DebugId(peer.node_id)
                  << "peer_endpoint_pair.external = "
                  << peer_endpoint_pair.external
                  << ", peer_endpoint_pair.local = "
                  << peer_endpoint_pair.local
                  << ". Rudp returned :"
                  << ret_val;
      return;
    }
    assert((!this_endpoint_pair.external.address().is_unspecified() ||
            !this_endpoint_pair.local.address().is_unspecified()) &&
           "Unspecified endpoint after GetAvailableEndpoint success.");
    NodeId relay_connection_id;
    bool relay_message(false);
    if (send_to_bootstrap_connection) {
      // Not in any peer's routing table, need a path back through relay IP.
      relay_connection_id = network_.this_node_relay_connection_id();
      relay_message = true;
    }
                                                                 std::vector<std::string> closest_node_ids;
    protobuf::Message connect_rpc(
        rpcs::Connect(peer.node_id,
        this_endpoint_pair,
        routing_table_.kNodeId(),
        routing_table_.kConnectionId(),
        closest_node_ids,
        routing_table_.client_mode(),
        this_nat_type,
        relay_message,
        relay_connection_id));
    LOG(kVerbose) << "Sending Connect RPC to " << DebugId(peer.node_id)
                  << " message id : " << connect_rpc.id();
    if (send_to_bootstrap_connection)
      network_.SendToDirect(connect_rpc, network_.bootstrap_connection_id(),
                            network_.bootstrap_connection_id());
    else
      network_.SendToClosestNode(connect_rpc);
  }
}

//void ResponseHandler::ConnectTo(const std::vector<std::string>& nodes,
//                                const std::vector<std::string>& closest_nodes) {
//  std::vector<std::string> closest_node_ids;
//  auto routing_table_closest_nodes(routing_table_.GetClosestNodes(
//                                   routing_table_.kNodeId(),
//                                   Parameters::max_routing_table_size));

//  for (const NodeId& node_id : routing_table_closest_nodes)
//    closest_node_ids.push_back(node_id.String());

//  for (const std::string& node_string : closest_nodes) {
//    if (NodeId(node_string).IsValid() && !NodeId(node_string).Empty())
//      closest_node_ids.push_back(node_string);
//  }

//  for (const std::string& node_string : nodes) {
//    if (NodeId(node_string).IsValid() && !NodeId(node_string).Empty())
//      closest_node_ids.push_back(node_string);
//  }

//  if (closest_node_ids.size() > 1)
//    std::sort(closest_node_ids.begin(), closest_node_ids.end(),
//            [=](const std::string& lhs, const std::string& rhs)->bool {
//              return NodeId::CloserToTarget(NodeId(lhs), NodeId(rhs),
//                                            NodeId(routing_table_.kKeys().identity));
//            });
//  auto iter(std::unique(closest_node_ids.begin(), closest_node_ids.end()));
//  auto resize = std::min(static_cast<uint16_t>(std::distance(closest_node_ids.begin(), iter)),
//                         Parameters::max_routing_table_size);
//  if (closest_node_ids.size() > resize)
//    closest_node_ids.resize(resize);

//  std::remove_if(closest_node_ids.begin(), closest_node_ids.end(),
//                 [=](const std::string& closet_node_id)->bool {
//                   return (!NodeId(closet_node_id).IsValid() || NodeId(closet_node_id).Empty());
//                 });

//  if (network_.bootstrap_connection_id().Empty() && (routing_table_.Size() == 0)) {
//      LOG(kWarning) << "Need to re bootstrap !";
//    return;
//  }

//  bool send_to_bootstrap_connection((routing_table_.Size() < Parameters::closest_nodes_size) &&
//                                    !network_.bootstrap_connection_id().Empty());
//  for (uint16_t i = 0; i < nodes.size(); ++i) {
//    NodeInfo node_to_add;
//    node_to_add.node_id = NodeId(nodes.at(i));
//    if (node_to_add.node_id == NodeId(routing_table_.kKeys().identity))
//      continue;  // TODO(Prakash): FIXME handle collision and return kIdCollision on join()

//    if (routing_table_.CheckNode(node_to_add)) {
//      LOG(kVerbose) << "CheckNode succeeded for node " << DebugId(node_to_add.node_id);
//      rudp::EndpointPair this_endpoint_pair, peer_endpoint_pair;
//      rudp::NatType this_nat_type(rudp::NatType::kUnknown);
//      int ret_val = network_.GetAvailableEndpoint(NodeId(nodes.at(i)),
//                                                  peer_endpoint_pair,
//                                                  this_endpoint_pair,
//                                                  this_nat_type);
//      if (kSuccess != ret_val) {
//        LOG(kError) << "[" << DebugId(routing_table_.kNodeId()) << "] Response Handler"
//                    << "Failed to get available endpoint for new connection to : "
//                    << HexSubstr(nodes.at(i))
//                    << "peer_endpoint_pair.external = "
//                    << peer_endpoint_pair.external
//                    << ", peer_endpoint_pair.local = "
//                    << peer_endpoint_pair.local
//                    << ". Rudp returned :"
//                    << ret_val;
//        return;
//      }
//      assert((!this_endpoint_pair.external.address().is_unspecified() ||
//              !this_endpoint_pair.local.address().is_unspecified()) &&
//             "Unspecified endpoint after GetAvailableEndpoint success.");
//      NodeId relay_connection_id;
//      bool relay_message(false);
//      if (send_to_bootstrap_connection) {
//        // Not in any peer's routing table, need a path back through relay IP.
//        relay_connection_id = network_.this_node_relay_connection_id();
//        relay_message = true;
//      }

//      protobuf::Message connect_rpc(
//          rpcs::Connect(NodeId(nodes.at(i)),
//          this_endpoint_pair,
//          routing_table_.kNodeId(),
//          routing_table_.kConnectionId(),
//          closest_node_ids,
//          routing_table_.client_mode(),
//          this_nat_type,
//          relay_message,
//          relay_connection_id));
//      LOG(kVerbose) << "Sending Connect RPC to " << HexSubstr(nodes.at(i))
//                    << " message id : " << connect_rpc.id();
//      if (send_to_bootstrap_connection)
//        network_.SendToDirect(connect_rpc, network_.bootstrap_connection_id(),
//                              network_.bootstrap_connection_id());
//      else
//        network_.SendToClosestNode(connect_rpc);
//    }
//  }
//}

void ResponseHandler::ConnectSuccessAcknowledgement(protobuf::Message& message) {
  protobuf::ConnectSuccessAcknowledgement connect_success_ack;

  if (!connect_success_ack.ParseFromString(message.data(0))) {
    LOG(kWarning) << "Unable to parse connect success ack.";
    message.Clear();
    return;
  }

  NodeInfo peer;
  if (!connect_success_ack.node_id().empty())
    peer.node_id = NodeId(connect_success_ack.node_id());
  if (!connect_success_ack.connection_id().empty())
    peer.connection_id = NodeId(connect_success_ack.connection_id());
  if (peer.node_id.Empty() || !peer.node_id.IsValid()) {
    LOG(kWarning) << "Invalid node id provided";
    return;
  }
  if (peer.connection_id.Empty() || !peer.connection_id.IsValid()) {
    LOG(kWarning) << "Invalid peer connection_id provided";
    return;
  }

  bool from_requestor(connect_success_ack.requestor());
  std::vector<NodeId> close_ids;
  for (auto itr = connect_success_ack.close_ids().begin();
           itr != connect_success_ack.close_ids().end(); ++itr) {
    if (!(*itr).empty()) {
      close_ids.push_back(NodeId(*itr));
    }
  }

  std::weak_ptr<ResponseHandler> response_handler_weak_ptr = shared_from_this();
  if (request_public_key_functor_) {
    auto validate_node([=] (const asymm::PublicKey& key) {
                           LOG(kInfo) << "Validation callback called with public key for "
                                      << DebugId(peer.node_id);
                           if (std::shared_ptr<ResponseHandler> response_handler =
                               response_handler_weak_ptr.lock()) {
                             if (ValidateAndAddToRoutingTable(
                                 response_handler->network_,
                                 response_handler->routing_table_,
                                 response_handler->non_routing_table_,
                                 peer.node_id,
                                 peer.connection_id,
                                 key,
                                 message.client_node())) {
                               if (from_requestor) {
                                 response_handler->HandleSuccessAcknowledgementAsReponder(peer);
                               } else {
                                 response_handler->HandleSuccessAcknowledgementAsRequestor(close_ids);
                               }
                             }
                           }
                         });
      request_public_key_functor_(peer.node_id, validate_node);
  }
}
// FIXME life time issue with weak pointers
//  This should be used by responder only
void ResponseHandler::HandleSuccessAcknowledgementAsReponder(NodeInfo peer) {
  LOG(kWarning) << "HandleSuccessAcknowledgementAsReponder";
  if (routing_table_.IsPendingNode(peer)) {
    const std::vector<NodeId> close_ids(
        routing_table_.GetClosestNodes(peer.node_id, Parameters::max_routing_table_size));
//  std::remove_if(close_ids.begin(), close_ids.end(), [=](std::string node_id) {
//                                                         return(peer.node_id.String() == node_id);
//                                                       });
    protobuf::Message connect_success_ack(
        rpcs::ConnectSuccessAcknoledgement(peer.node_id,
                                           routing_table_.kNodeId(),
                                           routing_table_.kConnectionId(),
                                           false,  // this node is requestor
                                           close_ids,
                                           routing_table_.client_mode()));
    network_.SendToDirect(connect_success_ack, peer.node_id, peer.connection_id);
    routing_table_.ClearPendingNode(peer);
  }
}

void ResponseHandler::HandleSuccessAcknowledgementAsRequestor(std::vector<NodeId> close_ids) {
LOG(kWarning) << "HandleSuccessAcknowledgementAsRequestor" << close_ids.size();
  for (auto i : close_ids) {
    if (i.Empty())
      SendConnectRequest(i);
  }
}
//void ResponseHandler::ConnectSuccess(protobuf::Message& message) {
//  protobuf::ConnectSuccess connect_success;

//  if (!connect_success.ParseFromString(message.data(0))) {
//    LOG(kWarning) << "Unable to parse connect success.";
//    message.Clear();
//    return;
//  }

//  NodeId peer_node_id(connect_success.node_id());
//  NodeId peer_connection_id(connect_success.connection_id());
//  if (peer_node_id.Empty() || !peer_node_id.IsValid()) {
//    LOG(kWarning) << "Invalid node id provided";
//    return;
//  }
//  if (peer_connection_id.Empty() || !peer_connection_id.IsValid()) {
//    LOG(kWarning) << "Invalid peer_connection_id provided";
//    return;
//  }

//  std::weak_ptr<ResponseHandler> response_handler_weak_ptr = shared_from_this();
//  if (request_public_key_functor_) {
//    auto validate_node([=] (const asymm::PublicKey& key) {
//                           LOG(kInfo) << "Validation callback called with public key for "
//                                      << DebugId(peer_node_id);
//                           if (std::shared_ptr<ResponseHandler> response_handler =
//                               response_handler_weak_ptr.lock()) {
//                             ValidateAndAddToRoutingTable(
//                                 response_handler->network_,
//                                 response_handler->routing_table_,
//                                 response_handler->non_routing_table_,
//                                 peer_node_id,
//                                 peer_connection_id,
//                                 key,
//                                 message.client_node());
//                           }
//                         });
//      request_public_key_functor_(peer_node_id, validate_node);
//  }
//}

void ResponseHandler::set_request_public_key_functor(RequestPublicKeyFunctor request_public_key) {
  request_public_key_functor_ = request_public_key;
}

RequestPublicKeyFunctor ResponseHandler::request_public_key_functor() const {
  return request_public_key_functor_;
}

}  // namespace routing

}  // namespace maidsafe
