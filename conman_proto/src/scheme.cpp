
#include <conman_proto/scheme.h>

ORO_LIST_COMPONENT_TYPE(conman::Scheme);

using namespace conman;

Scheme::Scheme(std::string name) 
 : RTT::TaskContext(name)
{
  // Add operations
  this->addOperation("addBlock", 
      (bool (Scheme::*)(const std::string&))&Scheme::add_block, this, 
      RTT::OwnThread)
    .doc("Add a conman block into this scheme.");
  
  this->addOperation("getBlocks", 
      &Scheme::get_blocks, this, 
      RTT::OwnThread)
    .doc("Get the list of all blocks.");

  // Block runtime management
  this->addOperation("enableBlock", 
      (bool (Scheme::*)(const std::string&, bool))&Scheme::enable_block, this, 
      RTT::OwnThread)
    .doc("Enable a block in this scheme.");

  this->addOperation("disableBlock", 
      (bool (Scheme::*)(const std::string&))&Scheme::disable_block, this, 
      RTT::OwnThread)
    .doc("Disable a block in this scheme.");

  this->addOperation("switchBlocks", 
      &Scheme::switch_blocks, this, 
      RTT::OwnThread)
    .doc("Simultaneousy enable and disable a list of blocks, any block not in either list will remain in its current state.");

  this->addOperation("setBlocks", 
      &Scheme::set_blocks, this, 
      RTT::OwnThread)
    .doc("Set the list running blocks, any block not on the list will be disabled.");
}


bool Scheme::add_block(const std::string &block_name)
{
  RTT::Logger::In in("Scheme::add_block(string)");

  // Make sure the block exists
  if(!this->hasPeer(block_name)) {
    RTT::TaskContext::PeerList peers = this->getPeerList();

    RTT::Logger::log() << RTT::Logger::Error 
      << "Requested block to add named \""<< block_name << "\" was not found." << std::endl
      << "  Available blocks include:" << std::endl;

    for(RTT::TaskContext::PeerList::iterator it = peers.begin();
        it != peers.end();
        ++it) 
    {
      RTT::Logger::log() << RTT::Logger::Error << "    " << *it << std::endl;
    }

    RTT::Logger::log() << RTT::Logger::Error 
      << RTT::endlog();

    return false;
  }

  // Get the newly loaded block
  RTT::TaskContext *new_block = this->getPeer(block_name);

  // Nulls are bad
  if(new_block == NULL) {
    RTT::Logger::log() << RTT::Logger::Error 
      << "Requested block to add named \""<<block_name
      << "\" was found, but it could not be acquired (getPeer returned NULL)"
      << RTT::endlog();
    return false;
  }


  if(!this->connectPeers(new_block)) {
    RTT::Logger::log() << RTT::Logger::Error << "Could not connect peer: "<< new_block->getName() << RTT::endlog();
  }

  // Add the block to the graphs
  return this->add_block(new_block->getName());
}

bool Scheme::add_block(RTT::TaskContext *new_block)
{
  RTT::Logger::In in("Scheme::add_block(task)");

  // Nulls are bad
  if(new_block == NULL) {
    RTT::Logger::log(RTT::Error) 
      << "Requested block to add is NULL." << RTT::endlog();
    return false;
  }

  // Make sure the block has the conman hook service
  if(!conman::HookService::Present(new_block)) {
    RTT::Logger::log(RTT::Error) 
      << "Requested block to add does not have the conman hook service." << RTT::endlog();
    return false;
  }

  // Get the block name
  const std::string block_name = new_block->getName();

  // Connect the block in the appropriate ports in the estimation and control layers
  if(!add_block_to_graph(new_block, estimation_graph_, estimation_serialization_, "estimation")) {
    RTT::Logger::log() << RTT::Logger::Error << "Could not add TaskContext \""<< block_name <<"\" to scheme estimation layer." << RTT::endlog();
    return false;
  }

  if(!add_block_to_graph(new_block, control_graph_, control_serialization_, "control")) {
    RTT::Logger::log() << RTT::Logger::Error << "Could not add TaskContext \""<< block_name <<"\" to scheme control layer." << RTT::endlog();
    return false;
  }

  // Add this block to the list of block names
  block_names_.push_back(block_name);

  // Print out the ordering
  RTT::Logger::log(RTT::Debug) << "New ordering: [ ";
  for(conman::graph::CausalOrdering::iterator it = control_serialization_.begin();
      it != control_serialization_.end();
      ++it) 
  {
    RTT::Logger::log() << RTT::Logger::Info << control_graph_.graph()[*it].block->getName() << ", ";
  }
  RTT::Logger::log() << RTT::Logger::Info << " ] " << RTT::endlog();

  return true;
}


bool Scheme::add_block_to_graph(
    RTT::TaskContext *new_block,
    conman::graph::CausalGraph &graph,
    conman::graph::CausalOrdering &ordering,
    const std::string &layer)
{
  RTT::Logger::In in("Scheme::add_block_to_graph()");

  // Make sure the block isn't null
  if(new_block == NULL) {
    RTT::Logger::log(RTT::Error) << "TaskContext is NULL." << RTT::endlog();
    return false;
  }

  // Make sure the block has the conman hook service
  if(!conman::HookService::Present(new_block)) {
    RTT::Logger::log(RTT::Error) 
      << "Requested block to add does not have the conman hook service." << RTT::endlog();
    return false;
  }

  // Add this block to the graph
  std::string new_block_name = new_block->getName();

  graph.add_vertex(new_block_name);
  conman::CausalVertex &new_vertex = graph[new_block_name];
  new_vertex.block = new_block;
  new_vertex.hook = new_block->getProvider<conman::HookService>("conman");

  if(!regenerate_graph(graph, ordering, layer)) {
    // Report error (if this block's connections add cycles)
    RTT::Logger::log(RTT::Error) << "Cannot connect block "
      "\""<<new_block_name<<"\" in conman scheme \""<<layer<<"\" layer." <<
      RTT::endlog();

    // Remove the vertex by name
    graph.remove_vertex(new_block_name);

    // Regenerate the graph without the vertex
    return regenerate_graph(graph, ordering, layer);
  }

  return true;
}

bool Scheme::regenerate_graph(
    conman::graph::CausalGraph &graph,
    conman::graph::CausalOrdering &ordering,
    const std::string &layer)
{
  // Iterate over all vertices in this graph layer
  for(std::pair<conman::VertexIterator, conman::VertexIterator> vert_it = boost::vertices(graph);
      vert_it.first != vert_it.second;
      ++vert_it.first) 
  {
    // Temporary variable for readability
    conman::graph::VertexProperties &block_vertex = estimation_graph_.graph()[*vert_it];

    // Get the registered output ports for a given layer
    std::vector<rtt::base::PortInterface*> layer_ports;
    block_vertex.hook->getOutputPortsOnLayer(layer, layer_ports);

    // Create graph arcs for each port between blocks
    for(std::vector<rtt::base::PortInterface*>::const_iterator port_it = layer_ports.begin();
        port_it != layer_ports.end();
        ++port_it)
    {
      // Get the port, for readability
      RTT::base::PortInterface const * port = *port_it;

      // Get the port connections (to get endpoints)
      std::list<RTT::internal::ConnectionManager::ChannelDescriptor> channels = port->getManager()->getChannels();
      std::list<RTT::internal::ConnectionManager::ChannelDescriptor>::iterator channel_it;

      // Create graph arcs for each connection
      for(channel_it = channels.begin(); channel_it != channels.end(); ++channel_it) {
        // Get the connection descriptor
        RTT::base::ChannelElementBase::shared_ptr connection = channel_it->get<1>();

        // Pointers to the endpoints of this connection
        RTT::base::PortInterface  
          *source_port = connection->getOutputEndPoint()->getPort(), 
          *sink_port = connection->getInputEndPoint()->getPort();

        // Make sure the ports and components are not null
        if( source_port != NULL && source_port->getInterface() != NULL
            && sink_port != NULL && sink_port->getInterface() != NULL) 
        {
          // Get the source and sink names
          std::string 
            source_name = source_port->getInterface()->getOwner()->getName(),
            sink_name = sink_port->getInterface()->getOwner()->getName();

          // Make sure both blocks are in the graph
          if(graph.vertex(source_name) != graph.null_vertex() && graph.vertex(sink_name) != graph.null_vertex()) {
            // Create a new edge representing this connection
            conman::graph::EdgeProperties edge_props = {true, source_port, sink_port};
            // Add the edge to the graph
            boost::add_edge_by_label(source_name, sink_name, edge_props, graph);
          }
        }
      }
    }
  }
  
  // Recompute topological sort (and require that this layer is still a DAG)
  try {
    // Clear the topologically-sorted ordering and recompute the sort
    ordering.clear();
    boost::topological_sort( graph.graph(), std::back_inserter(ordering));
  } catch(std::exception &ex) {
    // Complain
    RTT::Logger::log(RTT::Error)
      << "Cannot regenerate topological ordering in conman scheme "
      "\""<<layer<<"\" layer because: " << ex.what() << RTT::endlog();

    return false;
  }

  return true;
}


bool Scheme::enable_block(const std::string &block_name, const bool force)
{
  // Get the block by name
  return this->enable_block(this->getPeer(block_name), force);
}

bool Scheme::enable_block(RTT::TaskContext *block, const bool force)
{
  RTT::Logger::In in("Scheme::enable_block");

  if(block == NULL) { return false; }

  // Check if conflicting blocks are running
  const std::string &block_name = block->getName();
  std::vector<RTT::TaskContext*> &conflicts = block_conflicts_[block_name];

  for(std::vector<RTT::TaskContext*>::iterator it = conflicts.begin();
      it != conflicts.end();
      ++it)
  {
    // Check if the conflicting block is running
    if((*it)->getTaskState() == RTT::TaskContext::Running) {
      // If force is selected, disable the conflicting block
      if(force) {
        RTT::Logger::log(RTT::Info) << "Force-enabling block \""<< block_name
          << "\" involves disabling block \"" << (*it)->getName() << "\"" <<
          RTT::endlog();
        // Make sure we can actually disable it
        if(this->disable_block(*it) == false) {
          RTT::Logger::log(RTT::Error) << "Could not disable block \"" <<
            (*it)->getName() << "\"" << RTT::endlog();
          return false;
        }
      } else {
        RTT::Logger::logRTT::Error() << "Could not enable block \""<<
          block_name << "\" because it conflicts with block \"" <<
          (*it)->getName() << "\"" << RTT::endlog();
        return false;
      }
    }
  }

  // Make sure the block is configured
  if(!block->isConfigured()) {
    RTT::Logger::log(RTT::Error) << "Could not enable block \""<< block_name <<
      "\" because it has not been confiugre()ed." << RTT::endlog();
    return false;
  }

  // Try to start the block
  if(!block->start()) {
    RTT::Logger::log(RTT::Error) << "Could not enable block \""<< block_name <<
      "\" because it could not be start()ed." << RTT::endlog();
    return false;
  }

  return true;
}

bool Scheme::disable_block(const std::string &block_name)
{
  // Get the block by name
  return this->disable_block(this->getPeer(block_name));
}

bool Scheme::disable_block(RTT::TaskContext* block) 
{
  if(block == NULL) { return false; }

  // Stop a block
  if(block->isRunning()) {
    if(!block->stop()) {
      RTT::Logger::log(RTT::Error) << "Could not disable block \""<<
        block->getName() << "\" because it could not be stop()ed." <<
        RTT::endlog();
      return false;
    }
  }

  return true;
}

bool Scheme::enable_blocks(const std::vector<std::string> &block_names, const bool strict, const bool force)
{
  bool success = true;

  for(std::vector<std::string>::const_iterator it = block_names.begin();
      it != block_names.end();
      ++it)
  {
    // Try to start the block
    success &= this->enable_block(*it,force);

    // Break on failure if strict
    if(!success && strict) { return false; }
  }

  return success;
}

bool Scheme::disable_blocks(const std::vector<std::string> &block_names, const bool strict)
{
  bool success = true;

  for(std::vector<std::string>::const_iterator it = block_names.begin();
      it != block_names.end();
      ++it)
  {
    // Try to disable the block
    success &= this->disable_block(*it);

    // Break on failure if strict
    if(!success && strict) { return false; }
  }

  return success;
}

bool Scheme::switch_blocks(
    const std::vector<std::string> &disable_block_names,
    const std::vector<std::string> &enable_block_names,
    const bool strict,
    const bool force)
{
  // First disable blocks, so that "force" can be used appropriately when
  // enabling blocks. Also note that we used & instead of && in order to prevent
  // short-circuiting.
  return disable_blocks(disable_block_names, strict) & enable_blocks(enable_block_names, strict, force);
}

bool Scheme::set_blocks(
    const std::vector<std::string> &enabled_block_names,
    const bool strict)
{
  return this->switch_blocks(this->block_names_, enabled_block_names, strict, false);
}

bool Scheme::configureHook()
{
  return true;
}

bool Scheme::startHook()
{
  return true;
}

void Scheme::updateHook() 
{
  // What time is it
  RTT::os::TimeService::nsecs now = RTT::os::TimeService::Instance()->getNSecs();
  RTT::os::TimeService::Seconds 
    time = (1E-9)*static_cast<double>(now),
    period = (1E-9)*static_cast<double>(RTT::os::TimeService::Instance()->getNSecs(last_update_time_));
  // Store update time
  // NOTE: We maintain a single update time for all blocks so that any blocks
  // running at the same rate are executed in the same update() cycle
  last_update_time_ = now;

  // Iterate through estimation graph
  for(graph::CausalOrdering::iterator it = estimation_serialization_.begin();
      it != estimation_serialization_.end();
      ++it)
  {
    // Temporary variable for readability
    conman::graph::VertexProperties &block_vertex = estimation_graph_.graph()[*it];

    // Get the state of the task
    RTT::base::TaskCore::TaskState block_state = block_vertex.block->getTaskState();

    // Check if the task is running and needs to be executed
    if( block_state == RTT::base::TaskCore::Running 
        && (now - block_vertex.last_estimation_time) >= block_vertex.get_period())
    { 
      block_vertex.read_hardware(time, now - block_vertex.last_estimation_time);
      block_vertex.compute_estimation(time, now - block_vertex.last_estimation_time);
      block_vertex.last_estimation_time = now;
    }
  }
  
  // Iterate through control graph
  for(graph::CausalOrdering::iterator it = control_serialization_.begin();
      it != control_serialization_.end();
      ++it)
  {
    // Temporary variable for readability
    conman::graph::VertexProperties &block_vertex = control_graph_.graph()[*it];
    
    // Get the state of the task
    RTT::base::TaskCore::TaskState block_state = block_vertex.block->getTaskState();

    // Check if the task is running and needs to be executed
    if( block_state == RTT::base::TaskCore::Running
        && (now - block_vertex.last_control_time) >= block_vertex.get_period())
    {
      block_vertex.compute_control(time, now - block_vertex.last_control_time);
      block_vertex.write_hardware(time, now - block_vertex.last_control_time);
      block_vertex.last_control_time = now;
    }
  }
}
