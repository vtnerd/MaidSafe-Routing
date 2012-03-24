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

#include <utility>
#include "boost/date_time.hpp"
#include "boost/asio/deadline_timer.hpp"
#include "boost/date_time.hpp"
#include "boost/filesystem/v3/fstream.hpp"
#include "maidsafe/common/utils.h"
#include "maidsafe/routing/routing_api.h"
#include "maidsafe/routing/routing.pb.h"
#include "maidsafe/routing/node_id.h"
#include "maidsafe/routing/routing_table.h"
#include "maidsafe/routing/service.h"
#include "maidsafe/routing/rpcs.h"
#include "maidsafe/routing/timer.h"
#include "return_codes.h"

namespace fs = boost::filesystem;
namespace bs2 = boost::signals2;

namespace maidsafe {

namespace routing {

namespace {
const unsigned int kNumChunksToCache(100);
const unsigned int kTimoutInSeconds(5);
}

Message::Message()
    : type(0),
      source_id(),
      destination_id(),
      data(),
      timeout(false),
      cacheable(false),
      direct(false),
      replication(0) {}

Message::Message(const protobuf::Message &protobuf_message)
    : type(protobuf_message.type()),
      source_id(protobuf_message.source_id()),
      destination_id(protobuf_message.destination_id()),
      data(protobuf_message.data()),
      timeout(protobuf_message.routing_failure()),
      cacheable(protobuf_message.cacheable()),
      direct(protobuf_message.direct()),
      replication(protobuf_message.replication()) {}

Routing::Routing(NodeType node_type,
                 const asymm::Keys &keys,
                 bool encryption_required)
    : asio_service_(),
      bootstrap_file_(),
      bootstrap_nodes_(),
      keys_(keys),
      node_local_endpoint_(),
      node_external_endpoint_(),
      transport_(new transport::ManagedConnections()),
      routing_table_(new RoutingTable(keys_, transport_)),
      rpc_ptr_(new Rpcs(routing_table_)),
      service_(),
      timer_(new Timer(asio_service_)),
      message_received_signal_(),
      network_status_signal_(),
      cache_size_hint_(kNumChunksToCache),
      cache_chunks_(),
      waiting_for_response_(),
      client_connections_(),
      joined_(false),
      encryption_required_(encryption_required),
      node_type_(node_type),
      node_validation_functor_() {
  Init();
}

void Routing::setNodeValidationFunctor(NodeValidationFunctor
                                       &node_validation_functor) {
  if (!node_validation_functor) {
    DLOG(ERROR) << "Invalid node_validation_functor passed ";
    return;
  }
  node_validation_functor_ = node_validation_functor;
}

// drop existing routing table and restart
void Routing::BootStrapFromThisEndpoint(const transport::Endpoint
&endpoint) {
  LOG(INFO) << " Entered bootstrap IP address : " << endpoint.ip.to_string();
  LOG(INFO) << " Entered bootstrap Port       : " << endpoint.port;
  for (unsigned int i = 0; i < routing_table_->Size(); ++i) {
    NodeInfo remove_node =
    routing_table_->GetClosestNode(NodeId(routing_table_->kKeys().identity), 0);
    transport_->Remove(remove_node.endpoint);
    routing_table_->DropNode(remove_node.endpoint);
  }
  network_status_signal_(routing_table_->Size());
  bootstrap_nodes_.clear();
  bootstrap_nodes_.push_back(endpoint);
  asio_service_.service().post(std::bind(&Routing::Join, this));
}

int Routing::Send(const Message &message,
                   const ResponseReceivedFunctor &response_functor) {
  if (message.destination_id.empty()) {
    DLOG(ERROR) << "No destination id, aborted send";
    return 1;
  }
  if (message.data.empty()) {
    DLOG(ERROR) << "No data, aborted send";
    return 2;
  }
  if (message.type < 100) {
    DLOG(ERROR) << "Attempt to use Reserved message type (<100), aborted send";
    return 3;
  }
  uint32_t message_unique_id =  timer_->AddTask(kTimoutInSeconds,
                                                response_functor);
  protobuf::Message proto_message;
  proto_message.set_id(message_unique_id);
  proto_message.set_source_id(routing_table_->kKeys().identity);
  proto_message.set_destination_id(message.destination_id);
  proto_message.set_cacheable(message.cacheable);
  proto_message.set_data(message.data);
  proto_message.set_direct(message.direct);
  proto_message.set_replication(message.replication);
  proto_message.set_type(message.type);
  proto_message.set_routing_failure(false);
  routing_table_->SendOn(proto_message);
  return 0;
}

void Routing::Init() {
  if (!node_validation_functor_) {
    DLOG(ERROR) << "Invalid node_validation_functor passed: Aborted start";
    return;
  }
  service_.reset(new Service(node_validation_functor_,
                             routing_table_,
                             transport_));
  asio_service_.Start(5);
  // TODO(dirvine) fill in bootstrap file location and do ReadConfigFile
  node_local_endpoint_ = transport_->GetAvailableEndpoint();
  LOG(INFO) << " Local IP address : " << node_local_endpoint_.ip.to_string();
  LOG(INFO) << " Local Port       : " << node_local_endpoint_.port;
  Join();
}

bool Routing::ReadBootstrapFile() {
  protobuf::ConfigFile protobuf_config;
  protobuf::Bootstrap protobuf_bootstrap;
  fs::path config_file(".");  //TODO(dirvine) get correct location of this

 if (!fs::exists(config_file) || !fs::is_regular_file(config_file)) {
     DLOG(ERROR) << "Cannot read config file " << config_file;
   return false;
 }
 try {
     fs::ifstream config_file_stream(config_file);
   if (!protobuf_config.ParseFromString(config_file.string()))
     return false;

   transport::Endpoint endpoint;
  for (int i = 0; i != protobuf_bootstrap.bootstrap_contacts().size(); ++i) {
    endpoint.ip.from_string(protobuf_bootstrap.bootstrap_contacts(i).endpoint().ip());
    endpoint.port= protobuf_bootstrap.bootstrap_contacts(i).endpoint().port();
    bootstrap_nodes_.push_back(endpoint);
  }
 }
 catch(const std::exception &e) {
     DLOG(ERROR) << "Exception: " << e.what();
   return false;
 }
  return  bootstrap_nodes_.empty() ? false : true;
}

bool Routing::WriteBootstrapFile() const {
  // TODO(dirvine) implement
  return false;
}

bs2::signal<void(int, std::string)> &Routing::RequestReceivedSignal() {
  return message_received_signal_;
}

bs2::signal<void(unsigned int)> &Routing::NetworkStatusSignal() {
  return network_status_signal_;
}

void Routing::Join() {
  if (bootstrap_nodes_.empty()) {
    DLOG(INFO) << "No bootstrap nodes";
    return;
  }

  for (auto it = bootstrap_nodes_.begin();
       it != bootstrap_nodes_.end(); ++it) {
    // TODO(dirvine) send bootstrap requests
  }
}

void Routing::ReceiveMessage(const std::string &message) {
  protobuf::Message protobuf_message;
  protobuf::ConnectRequest connection_request;
  if (protobuf_message.ParseFromString(message))
    ProcessMessage(protobuf_message);
}

void Routing::ProcessMessage(protobuf::Message &message) {
  // TODO(dirvine) if message is from/for a client connected
  // to us replace the source address to me for requests
  // in responses the message will be direct with our address

  // handle cache data
  if (message.has_cacheable() && message.cacheable()) {
    if (message.response()) {
      AddToCache(message);
     } else  {  // request
       if (GetFromCache(message))
         return;// this operation sends back the message
     }
  }
  // is it for us ??
  if (!routing_table_->AmIClosestNode(NodeId(message.destination_id()))) {
    routing_table_->SendOn(message);
    return;
  }   // I am closest
  if (message.type() == 0) {  // ping
    if (message.response()) {
      ProcessPingResponse(message);
      return;  // Job done !!
    } else {
      service_->Ping(message);
    }
  }
   if (message.type() == 1) {  // bootstrap
    if (message.response()) {
        ProcessConnectResponse(message);
      return;
    } else {
        service_->Connect(message);
        return;
    }
  }
  if (message.type() == 2) {  // find_nodes
    if (message.response()) {
      ProcessFindNodeResponse(message);
      return;
    } else {
        service_->FindNodes(message);
        return;
    }
  }


  // if this is set not direct and ID == ME do NOT respond.
  if (!message.direct() &&
    (message.destination_id() != routing_table_->kKeys().identity)) {
    try {
      message_received_signal_(static_cast<int>(message.type()),
                                message.data());
    }
    catch(const std::exception &e) {
      DLOG(ERROR) << e.what();
    }
  } else {  // I am closest and it's direct
    if(message.response()) {
      timer_->ExecuteTaskNow(message);
      return;
    } else { // I am closest and it's a request
      try {
        message_received_signal_(static_cast<int>(message.type()),
                                message.data());
      } catch(const std::exception &e) {
      DLOG(ERROR) << e.what();
      }
    }
  }
  // I am closest so will send to all my replicant nodes
  message.set_direct(true);
  message.set_source_id(routing_table_->kKeys().identity);
  auto close =
        routing_table_->GetClosestNodes(NodeId(message.destination_id()),
                                static_cast<uint16_t>(message.replication()));
  for (auto it = close.begin(); it != close.end(); ++it) {
    message.set_destination_id((*it).String());
    routing_table_->SendOn(message);
  }
}

// always direct !! never pass on
void Routing::ProcessPingResponse(protobuf::Message& message) {
  // TODO , do we need this and where and how can I update the response
  protobuf::PingResponse ping_response;
  if (ping_response.ParseFromString(message.data()) &&
    ping_response.has_pong()) {
    //  do stuff here
    }
}

// the other node agreed to connect - he has accepted our connection
void Routing::ProcessConnectResponse(protobuf::Message& message) {
  protobuf::ConnectResponse connect_response;
  if (!connect_response.ParseFromString(message.data())) {

    DLOG(ERROR) << "Could not parse connect response";
    return;
  }
  if (!connect_response.answer()) {
    return;  // they don't want us
  }
  transport::Endpoint endpoint;
  endpoint.ip.from_string(connect_response.contact().endpoint().ip());
  endpoint.port = connect_response.contact().endpoint().port();
  node_validation_functor_(connect_response.contact().node_id(),
                           endpoint);
}

void Routing::ProcessFindNodeResponse(protobuf::Message& message) {
  protobuf::FindNodesResponse find_nodes;
  if (!find_nodes.ParseFromString(message.data())) {
    DLOG(ERROR) << "Could not parse find node response";
    return;
  }
  if (asymm::CheckSignature(find_nodes.original_request(),
                            find_nodes.original_signature(),
                            routing_table_->kKeys().public_key) != kSuccess) {
    DLOG(ERROR) << " find node request was not signed by us";
    return;  // we never requested this
  }
  for(int i = 0; i < find_nodes.nodes_size() ; ++i) {
    NodeInfo node_to_add;
    node_to_add.node_id = NodeId(find_nodes.nodes(i));
    if (routing_table_->CheckNode(node_to_add)) {
      rpc_ptr_->Connect(NodeId(find_nodes.nodes(i)),
                               transport_->GetAvailableEndpoint(),
                               kClient ? true : false,
                               false);
    }
  }
}

void Routing::ValidateThisNode(const std::string &node_id,
                               const asymm::PublicKey &public_key,
                               const transport::Endpoint &endpoint) {
  NodeInfo node_info;
  node_info.node_id =NodeId(node_id);
  node_info.public_key = public_key;
  node_info.endpoint = endpoint;
  transport_->Add(endpoint, node_id);
  routing_table_->AddNode(node_info);
  if (bootstrap_nodes_.size() > 1000) {
  bootstrap_nodes_.erase(bootstrap_nodes_.begin());
  }
  bootstrap_nodes_.push_back(endpoint);
  WriteBootstrapFile();
}

bool Routing::GetFromCache(protobuf::Message &message) {
  for (auto it = cache_chunks_.begin(); it != cache_chunks_.end(); ++it) {
      if ((*it).first == message.source_id()) {
        message.set_destination_id(message.source_id());
        message.set_cacheable(true);
        message.set_data((*it).second);
        message.set_source_id(routing_table_->kKeys().identity);
        message.set_direct(true);
        message.set_response(false);
        routing_table_->SendOn(message);
        return true;
      }
  }
  return false;
}

void Routing::AddToCache(const protobuf::Message &message) {
  std::pair<std::string, std::string> data;
  try {
    // check data is valid TODO FIXME - ask CAA
    if (crypto::Hash<crypto::SHA512>(message.data()) != message.source_id())
      return;
    data = std::make_pair(message.source_id(), message.data());
    cache_chunks_.push_back(data);
    while (cache_chunks_.size() > cache_size_hint_)
      cache_chunks_.erase(cache_chunks_.begin());
  }
  catch(const std::exception &/*e*/) {
    // oohps reduce cache size quickly
    cache_size_hint_ = cache_size_hint_ / 2;
    while (cache_chunks_.size() > cache_size_hint_)
      cache_chunks_.erase(cache_chunks_.begin()+1);
  }
}

}  // namespace routing

}  // namespace maidsafe