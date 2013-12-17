////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013 The Krell Institute. All Rights Reserved.
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
// details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this library; if not, write to the Free Software Foundation, Inc.,
// 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
////////////////////////////////////////////////////////////////////////////////

/** @file
 *
 * Definition of the Graph class.
 *
 */

#include "KrellInstitute/Core/Assert.hpp"
#include "KrellInstitute/Core/Graph.hpp"

#include <string.h>

using namespace KrellInstitute::Core;

// test if edge exists.
// boost::edge(u,v,g) returns pair<edge_descriptor, bool> where the bool is whether
// the edge exists. So in the case we know it does, use the expression:
// boost::edge(u,v,g).first
// Check if an edge between v0 and v1 exists. It should not at this point.
// std::cout << "Edge exists?" << boost::edge(v0, v1, g).second << std::endl; // false
// Add an edge between v0 and v1.
// std::pair<Graph::edge_descriptor, bool> e0 = boost::add_edge(v0, v1, g);
// Check again if an edge between v0 and v1 exists. It should now.
// std::cout << "Edge exists?" << boost::edge(v0, v1, g).second << std::endl; // true
// A demonstration of the full return type of edge(). At this point, retrievedEdge.first
// should be exactly equal to e0
// std::pair<Graph::edge_descriptor, bool> retrievedEdge = boost::edge(v0, v1, g)
//
// There is also boost::lookup_edge() in boost/graph/lookup_edge.hpp;
// that function dispatches to either edge() or out_edges() and a
// search based on the particular graph type you are using.


/**
 * Default constructor.
 *
 * Constructs an empty Graph whose size is zero and contents is null.
 */
Graph::Graph()
{
}



/**
 * Copy constructor.
 *
 * Constructs a new Graph by copying the specified graph. The compiler provided
 * default is insufficient here because we need a deep, rather than shallow,
 * copy of the contents for correct object destruction.
 *
 * @param other    Graph to be copied.
 */
Graph::Graph(const Graph& other)
{
#if 0
    // Only do an actual copy if the other graph is non-empty
    if((other.dm_size > 0) && (other.dm_contents != NULL)) {
	
	// Make a copy of the graph
	dm_size = other.dm_size;
	dm_contents = new char[dm_size];
	memcpy(dm_contents, other.dm_contents, dm_size);
	
    }
#endif
}



/**
 * Constructor from size and contents.
 *
 * Constructs a new Graph from the specified size and contents. A copy of the
 * contents is made and is automatically release upon object destruction.
 *
 * @param size        Size of the graph (in bytes).
 * @param contents    Pointer to the graph's contents.
 */
#if 0
Graph::Graph(const unsigned& size, const void* contents) :
    dm_size(0),
    dm_contents(NULL)
{
    // Only do initialization if the size and pointer are valid
    if((size > 0) && (contents != NULL)) {
    
	// Make a copy of the graph's contents
	dm_size = size;
	dm_contents = new char[dm_size];
	memcpy(dm_contents, contents, dm_size);

    }
}
#endif


/**
 * Destructor.
 *
 * Destroys the graph's contents if it had any.
 */
Graph::~Graph()
{    
    // Destroy our contents (if any)
}



/**
 * Assignment operator.
 *
 * Operator "=" defined for a Graph object. The compiler provided default is
 * insufficient here because we need a deep, rather than shallow, copy of the
 * contents for correct object destruction.
 *
 * @param other    Graph to be copied.
 */
Graph& Graph::operator=(const Graph& other)
{
#if 0
    // Only do an assignment if the LHS and RHS differ
    if((dm_size != other.dm_size) || (dm_contents != other.dm_contents)) {
	
	// Destroy our current contents (if any)
	delete [] reinterpret_cast<char*>(dm_contents);

	// Only do an actual copy if the RHS is a non-empty graph
	if((other.dm_size > 0) && (other.dm_contents != NULL)) {

	    // Copy the RHS graph
	    dm_size = other.dm_size;
	    dm_contents = new char[dm_size];
	    memcpy(dm_contents, other.dm_contents, dm_size);
	
	}

    }

    // Return ourselves to the caller
    return *this;
#endif
}


/**
 * Test if empty.
 *
 * Returns a boolean value indicating if the graph is empty (has a zero size or
 * null contents).
 *
 * @return    Boolean "true" if the graph is empty, "false" otherwise.
 */
bool Graph::isEmpty() const
{
}


bool GraphaddEdge(Vertex& v1, Vertex& v2) {
};

bool Graph::addEdge(Address& out, Address& in) {
    vertex_t out_v;
    if (addr2vertex.find(out) == addr2vertex.end()) {
	out_v = boost::add_vertex(dm_dg);
	addr2vertex.insert(std::make_pair(out, out_v));
	dm_dg[out_v].a = out;
	std::stringstream output;
	//output << "addr:" << out << std::endl;
	output << "addr:" << out ;
	dm_dg[out_v].name = output.str();
	std::cerr << "ADD vertex for " << out << std::endl;
    } else {
	out_v = addr2vertex[out];
	std::cerr << "FOUND vertex for " << out << std::endl;
    }
    vertex_t in_v;
    if (addr2vertex.find(in) == addr2vertex.end()) {
	in_v = boost::add_vertex(dm_dg);
	addr2vertex.insert(std::make_pair(in, in_v));
	dm_dg[in_v].a = in;
	std::stringstream output;
	//output << "addr:" << in << std::endl;
	output << "addr:" << in ;
	dm_dg[in_v].name = output.str();
	std::cerr << "ADD vertex for " << in << std::endl;
    } else {
	in_v = addr2vertex[in];
	std::cerr << "FOUND vertex for " << in << std::endl;
    }

    edge_t e; bool b;
    if(!boost::edge(out_v, in_v, dm_dg).second) {
        boost::tie(e,b) = boost::add_edge(out_v, in_v, dm_dg);
        dm_dg[e].cost = 1;
    } else {
        // FIXME. crashed with 1.41 on rmzerl.
        //dm_dg[e].cost += 1;
    }
};

void Graph::printGraph() {
    char filename[L_tmpnam];
    std::tmpnam(filename);
    std::ofstream outf(filename);

    boost::dynamic_properties dp;
    dp.property("label", boost::get(&Vertex::name, dm_dg));
    dp.property("node_id", boost::get(boost::vertex_index, dm_dg));
    dp.property("label", boost::get(&Edge::cost, dm_dg));
//  BOOST_VERSION % 100 is the patch level
//  BOOST_VERSION / 100 % 1000 is the minor version
//  BOOST_VERSION / 100000 is the major version
#if (BOOST_VERSION / 100000 == 1) && (BOOST_VERSION / 100 % 1000 == 4) && (BOOST_VERSION % 100 > 3)
    write_graphviz_dp(outf, dm_dg, dp);
#else
    boost::write_graphviz(outf, dm_dg);
#endif

    //boost::write_graphviz(std::cout, dm_dg);
};
