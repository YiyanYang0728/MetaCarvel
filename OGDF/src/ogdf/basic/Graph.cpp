/** \file
 * \brief Implementation of Graph class
 *
 * \author Carsten Gutwenger
 *
 * \par License:
 * This file is part of the Open Graph Drawing Framework (OGDF).
 *
 * \par
 * Copyright (C)<br>
 * See README.txt in the root directory of the OGDF installation for details.
 *
 * \par
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 or 3 as published by the Free Software Foundation;
 * see the file LICENSE.txt included in the packaging of this file
 * for details.
 *
 * \par
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * \par
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * \see  http://www.gnu.org/copyleft/gpl.html
 ***************************************************************/

#include <ogdf/basic/Array.h>
#include <ogdf/basic/AdjEntryArray.h>
#include <ogdf/fileformats/GmlParser.h>
#include <ogdf/basic/simple_graph_alg.h>
#include <ogdf/basic/GraphObserver.h>
#include <ogdf/basic/Stack.h>

using std::mutex;

#ifndef OGDF_MEMORY_POOL_NTS
using std::lock_guard;
#endif


#define MIN_NODE_TABLE_SIZE (1 << 4)
#define MIN_EDGE_TABLE_SIZE (1 << 4)


namespace ogdf {

Graph::Graph()
{
	m_nodeIdCount = m_edgeIdCount = 0;
	m_nodeArrayTableSize = MIN_NODE_TABLE_SIZE;
	m_edgeArrayTableSize = MIN_EDGE_TABLE_SIZE;
}


Graph::Graph(const Graph &G)
{
	m_nodeIdCount = m_edgeIdCount = 0;
	copy(G);
	m_nodeArrayTableSize = nextPower2(MIN_NODE_TABLE_SIZE,m_nodeIdCount);
	m_edgeArrayTableSize = nextPower2(MIN_EDGE_TABLE_SIZE,m_edgeIdCount);
}


Graph::~Graph()
{
	restoreAllEdges();

	ListIterator<NodeArrayBase*> itVNext;
	for(ListIterator<NodeArrayBase*> itV = m_regNodeArrays.begin();
		itV.valid(); itV = itVNext)
	{
		itVNext = itV.succ();
		(*itV)->disconnect();
	}

	ListIterator<EdgeArrayBase*> itENext;
	for(ListIterator<EdgeArrayBase*> itE = m_regEdgeArrays.begin();
		itE.valid(); itE = itENext)
	{
		itENext = itE.succ();
		(*itE)->disconnect();
	}

	ListIterator<AdjEntryArrayBase*> itAdjNext;
	for(ListIterator<AdjEntryArrayBase*> itAdj = m_regAdjArrays.begin();
		itAdj.valid(); itAdj = itAdjNext)
	{
		itAdjNext = itAdj.succ();
		(*itAdj)->disconnect();
	}

	for (node v = nodes.head(); v; v = v->succ()) {
		v->adjEdges.~GraphObjectContainer<AdjElement>();
	}
}


Graph &Graph::operator=(const Graph &G)
{
	clear(); copy(G);
	m_nodeArrayTableSize = nextPower2(MIN_NODE_TABLE_SIZE,m_nodeIdCount);
	m_edgeArrayTableSize = nextPower2(MIN_EDGE_TABLE_SIZE,m_edgeIdCount);
	reinitArrays();

	OGDF_ASSERT_IF(dlConsistencyChecks, consistencyCheck());

	return *this;
}


void Graph::assign(const Graph &G, NodeArray<node> &mapNode,
	EdgeArray<edge> &mapEdge)
{
	clear();
	copy(G,mapNode,mapEdge);
	m_nodeArrayTableSize = nextPower2(MIN_NODE_TABLE_SIZE,m_nodeIdCount);
	m_edgeArrayTableSize = nextPower2(MIN_EDGE_TABLE_SIZE,m_edgeIdCount);
	reinitArrays();
}


void Graph::construct(const Graph &G, NodeArray<node> &mapNode,
	EdgeArray<edge> &mapEdge)
{
	copy(G,mapNode,mapEdge);
	m_nodeArrayTableSize = nextPower2(MIN_NODE_TABLE_SIZE,m_nodeIdCount);
	m_edgeArrayTableSize = nextPower2(MIN_EDGE_TABLE_SIZE,m_edgeIdCount);
}


void Graph::copy(const Graph &G, NodeArray<node> &mapNode,
	EdgeArray<edge> &mapEdge)
{
	if (G.nodes.size() == 0) return;

	mapNode.init(G,nullptr);

	for(node vG : G.nodes) {
		node v = mapNode[vG] = pureNewNode();
		v->m_indeg = vG->m_indeg;
		v->m_outdeg = vG->m_outdeg;
	}

	if (G.edges.size() == 0) return;

	mapEdge.init(G,nullptr);

	for(edge e : G.edges) {
		edge eC;
		edges.pushBack(eC = mapEdge[e] =
			OGDF_NEW EdgeElement(
				mapNode[e->source()],mapNode[e->target()],m_edgeIdCount));

		eC->m_adjSrc = OGDF_NEW AdjElement(eC,m_edgeIdCount<<1);
		(eC->m_adjTgt = OGDF_NEW AdjElement(eC,(m_edgeIdCount<<1)|1))
			->m_twin = eC->m_adjSrc;
		eC->m_adjSrc->m_twin = eC->m_adjTgt;
		m_edgeIdCount++;
	}

	EdgeArray<bool> mark(G,false);

	for(node vG : G.nodes) {
		node v = mapNode[vG];
		internal::GraphList<AdjElement> &adjEdges = vG->adjEdges;
		for (AdjElement *adjG = adjEdges.head(); adjG; adjG = adjG->succ()) {
			edge e = adjG->m_edge;
			edge eC = mapEdge[e];

			adjEntry adj;
			if (eC->isSelfLoop()) {
				if (mark[e])
					adj = eC->m_adjTgt;
				else {
					adj = eC->m_adjSrc;
					mark[e] = true;
				}
			} else
				adj = (v == eC->m_src) ? eC->m_adjSrc : eC->m_adjTgt;

			v->adjEdges.pushBack(adj);
			adj->m_node = v;
		}
	}
}


void Graph::copy(const Graph &G)
{
	NodeArray<node> mapNode;
	EdgeArray<edge> mapEdge;
	copy(G,mapNode,mapEdge);

	OGDF_ASSERT_IF(dlConsistencyChecks, consistencyCheck());
}


void Graph::constructInitByCC(
	const CCsInfo &info,
	int cc,
	NodeArray<node> &mapNode,
	EdgeArray<edge> &mapEdge)
{
	// clear
	for (node v = nodes.head(); v; v = v->succ()) {
		v->adjEdges.~GraphObjectContainer<AdjElement>();
	}

	nodes.clear();
	edges.clear();

	m_nodeIdCount = m_edgeIdCount = 0;

	for(int i = info.startNode(cc); i < info.stopNode(cc); ++i)
	{
		node vG = info.v(i);

#ifdef OGDF_DEBUG
		node v = OGDF_NEW NodeElement(this,m_nodeIdCount++);
#else
		node v = OGDF_NEW NodeElement(m_nodeIdCount++);
#endif
		mapNode[vG] = v;
		nodes.pushBack(v);

		v->m_indeg  = vG->m_indeg;
		v->m_outdeg = vG->m_outdeg;
	}

	for(int i = info.startEdge(cc); i < info.stopEdge(cc); ++i)
	{
		edge eG = info.e(i);
		node v = mapNode[eG->source()];
		node w = mapNode[eG->target()];

		edge eC = mapEdge[eG] = OGDF_NEW EdgeElement(v, w, m_edgeIdCount);
		edges.pushBack(eC);

		adjEntry adjSrc = OGDF_NEW AdjElement(eC,  m_edgeIdCount<<1   );
		adjEntry adjTgt = OGDF_NEW AdjElement(eC, (m_edgeIdCount<<1)|1);

		(eC->m_adjSrc = adjSrc)->m_twin = adjTgt;
		(eC->m_adjTgt = adjTgt)->m_twin = adjSrc;

		adjSrc->m_node = v;
		adjTgt->m_node = w;

		++m_edgeIdCount;
	}

	for(int i = info.startNode(cc); i < info.stopNode(cc); ++i)
	{
		node vG = info.v(i);
		node v  = mapNode[vG];

		for(adjEntry adjG : vG->adjEdges) {
			edge eG = adjG->theEdge();
			edge e = mapEdge[eG];

			adjEntry adj = (adjG == eG->adjSource()) ? e->adjSource() : e->adjTarget();
			v->adjEdges.pushBack(adj);
		}
	}

	m_nodeArrayTableSize = nextPower2(MIN_NODE_TABLE_SIZE,m_nodeIdCount);
	m_edgeArrayTableSize = nextPower2(MIN_EDGE_TABLE_SIZE,m_edgeIdCount);
	reinitArrays();

	OGDF_ASSERT_IF(dlConsistencyChecks, consistencyCheck());
}


void Graph::constructInitByNodes(
	const Graph &G,
	const List<node> &nodeList,
	NodeArray<node> &mapNode,
	EdgeArray<edge> &mapEdge)
{
	// clear
	for (node v = nodes.head(); v; v = v->succ()) {
		v->adjEdges.~GraphObjectContainer<AdjElement>();
	}

	nodes.clear();
	edges.clear();

	m_nodeIdCount = m_edgeIdCount = 0;
	m_nodeArrayTableSize = MIN_NODE_TABLE_SIZE;


	// list of edges adjacent to nodes in nodeList
	SListPure<edge> adjEdges;

	// create nodes and assemble list of adjEdges
	for(node vG : nodeList) {
		node v = mapNode[vG] = pureNewNode();

		v->m_indeg = vG->m_indeg;
		v->m_outdeg = vG->m_outdeg;

		for(adjEntry adjG : vG->adjEdges) {
			// corresponding adjacency entries differ by index modulo 2
			// the following conditions makes sure that each edge is
			// added only once to adjEdges
			if ((adjG->m_id & 1) == 0)
				adjEdges.pushBack(adjG->m_edge);
		}
	}

	// create edges
	for (edge eG : adjEdges)
	{
		node v = mapNode[eG->source()];
		node w = mapNode[eG->target()];

		edge eC = mapEdge[eG] = OGDF_NEW EdgeElement(v, w, m_edgeIdCount);
		edges.pushBack(eC);

		eC->m_adjSrc = OGDF_NEW AdjElement(eC, m_edgeIdCount<<1);
		(eC->m_adjTgt = OGDF_NEW AdjElement(eC, (m_edgeIdCount<<1)|1))
			->m_twin = eC->m_adjSrc;
		eC->m_adjSrc->m_twin = eC->m_adjTgt;
		++m_edgeIdCount;
	}

	EdgeArray<bool> mark(G,false);
	for(node vG : nodeList) {
		node v = mapNode[vG];

		internal::GraphList<AdjElement> &adjEdges = vG->adjEdges;
		for (AdjElement *adjG = adjEdges.head(); adjG; adjG = adjG->succ()) {
			edge e = adjG->m_edge;
			edge eC = mapEdge[e];

			adjEntry adj;
			if (eC->isSelfLoop()) {
				if (mark[e])
					adj = eC->m_adjTgt;
				else {
					adj = eC->m_adjSrc;
					mark[e] = true;
				}
			} else
				adj = (v == eC->m_src) ? eC->m_adjSrc : eC->m_adjTgt;

			v->adjEdges.pushBack(adj);
			adj->m_node = v;
		}
	}

	// set size of associated arrays and reinitialize all (we have now a
	// completely new graph)
	m_nodeArrayTableSize = nextPower2(MIN_NODE_TABLE_SIZE,m_nodeIdCount);
	m_edgeArrayTableSize = nextPower2(MIN_EDGE_TABLE_SIZE,m_edgeIdCount);
	reinitArrays();

	OGDF_ASSERT_IF(dlConsistencyChecks, consistencyCheck());
}


//------------------
//mainly a copy of the above code, remerge this again
void Graph::constructInitByActiveNodes(
	const List<node> &nodeList,
	const NodeArray<bool> &activeNodes,
	NodeArray<node> &mapNode,
	EdgeArray<edge> &mapEdge)
{
	// clear
	for (node v = nodes.head(); v; v = v->succ()) {
		v->adjEdges.~GraphObjectContainer<AdjElement>();
	}

	nodes.clear();
	edges.clear();

	m_nodeIdCount = m_edgeIdCount = 0;
	m_nodeArrayTableSize = MIN_NODE_TABLE_SIZE;


	// list of edges adjacent to nodes in nodeList
	SListPure<edge> adjEdges;

	// create nodes and assemble list of adjEdges
	// NOTE: nodeList is a list of ACTIVE nodes
	for(node vG : nodeList) {
		node v = mapNode[vG] = pureNewNode();

		int inCount = 0;
		int outCount = 0;
		for(adjEntry adjG : vG->adjEdges)
		{
			// coresponding adjacency entries differ by index modulo 2
			// the following conditions makes sure that each edge is
			// added only once to adjEdges
			if (activeNodes[adjG->m_edge->opposite(vG)])
			{
				if ((adjG->m_id & 1) == 0)
				{
					adjEdges.pushBack(adjG->m_edge);
				}//if one time
				if (adjG->m_edge->source() == vG) outCount++;
					else inCount++;
			}//if opposite active
		}
		v->m_indeg = inCount;
		v->m_outdeg = outCount;
	}

	// create edges
	for (edge eG : adjEdges)
	{
		node v = mapNode[eG->source()];
		node w = mapNode[eG->target()];

		AdjElement *adjSrc = OGDF_NEW AdjElement(v);

		v->adjEdges.pushBack(adjSrc);

		AdjElement *adjTgt = OGDF_NEW AdjElement(w);

		w->adjEdges.pushBack(adjTgt);

		adjSrc->m_twin = adjTgt;
		adjTgt->m_twin = adjSrc;

		adjTgt->m_id = (adjSrc->m_id = m_edgeIdCount << 1) | 1;
		edge e = OGDF_NEW EdgeElement(v,w,adjSrc,adjTgt,m_edgeIdCount++);

		edges.pushBack(e);

		mapEdge[eG] = adjSrc->m_edge = adjTgt->m_edge = e;
	}

	// set size of associated arrays and reinitialize all (we have now a
	// completely new graph)
	m_nodeArrayTableSize = nextPower2(MIN_NODE_TABLE_SIZE,m_nodeIdCount);
	m_edgeArrayTableSize = nextPower2(MIN_EDGE_TABLE_SIZE,m_edgeIdCount);
	reinitArrays();

	OGDF_ASSERT_IF(dlConsistencyChecks, consistencyCheck());
}//constructinitbyactivenodes

//------------------



node Graph::newNode()
{
	if (m_nodeIdCount == m_nodeArrayTableSize) {
		m_nodeArrayTableSize <<= 1;
		for(NodeArrayBase *nab : m_regNodeArrays)
			nab->enlargeTable(m_nodeArrayTableSize);
	}

#ifdef OGDF_DEBUG
	node v = OGDF_NEW NodeElement(this,m_nodeIdCount++);
#else
	node v = OGDF_NEW NodeElement(m_nodeIdCount++);
#endif

	nodes.pushBack(v);

	// notify all registered observers
	for(GraphObserver *obs : m_regStructures)
		obs->nodeAdded(v);

	return v;
}


//what about negative index numbers?
node Graph::newNode(int index)
{
	if(index >= m_nodeIdCount) {
		m_nodeIdCount = index+1;

		if(index >= m_nodeArrayTableSize) {
			m_nodeArrayTableSize = nextPower2(m_nodeArrayTableSize,index);
			for(NodeArrayBase *nab : m_regNodeArrays)
				nab->enlargeTable(m_nodeArrayTableSize);
		}
	}

#ifdef OGDF_DEBUG
	node v = OGDF_NEW NodeElement(this,index);
#else
	node v = OGDF_NEW NodeElement(index);
#endif

	nodes.pushBack(v);

	// notify all registered observers
	for(GraphObserver *obs : m_regStructures)
		obs->nodeAdded(v);

	return v;
}


node Graph::pureNewNode()
{
#ifdef OGDF_DEBUG
	node v = OGDF_NEW NodeElement(this,m_nodeIdCount++);
#else
	node v = OGDF_NEW NodeElement(m_nodeIdCount++);
#endif

	nodes.pushBack(v);

	// notify all registered observers
	for(GraphObserver *obs : m_regStructures)
		obs->nodeAdded(v);

	return v;
}


// IMPORTANT:
// The indices of the two adjacency entries pointing to an edge differ
// only in the last bit (adjSrc/2 == adjTgt/2)
//
// This can be useful sometimes in order to avoid visiting an edge twice.
edge Graph::createEdgeElement(node v, node w, adjEntry adjSrc, adjEntry adjTgt)
{
	if (m_edgeIdCount == m_edgeArrayTableSize) {
		m_edgeArrayTableSize <<= 1;

		for(EdgeArrayBase *eab : m_regEdgeArrays)
			eab->enlargeTable(m_edgeArrayTableSize);

		for(AdjEntryArrayBase *aab : m_regAdjArrays)
			aab->enlargeTable(m_edgeArrayTableSize << 1);
	}

	adjTgt->m_id = (adjSrc->m_id = m_edgeIdCount << 1) | 1;
	edge e = OGDF_NEW EdgeElement(v,w,adjSrc,adjTgt,m_edgeIdCount++);
	edges.pushBack(e);

	// notify all registered observers
	for(GraphObserver *obs : m_regStructures)
		obs->edgeAdded(e);

	return e;
}


edge Graph::newEdge(node v, node w, int index)
{
	OGDF_ASSERT(v != nullptr);
	OGDF_ASSERT(w != nullptr);
	OGDF_ASSERT(v->graphOf() == this);
	OGDF_ASSERT(w->graphOf() == this);

	AdjElement *adjSrc = OGDF_NEW AdjElement(v);

	v->adjEdges.pushBack(adjSrc);
	v->m_outdeg++;

	AdjElement *adjTgt = OGDF_NEW AdjElement(w);

	w->adjEdges.pushBack(adjTgt);
	w->m_indeg++;

	adjSrc->m_twin = adjTgt;
	adjTgt->m_twin = adjSrc;

	if(index >= m_edgeIdCount) {
		m_edgeIdCount = index+1;

		if(index >= m_edgeArrayTableSize) {
			m_edgeArrayTableSize = nextPower2(m_edgeArrayTableSize,index);

			for(EdgeArrayBase *eab : m_regEdgeArrays)
				eab->enlargeTable(m_edgeArrayTableSize);

			for(AdjEntryArrayBase *aab : m_regAdjArrays)
				aab->enlargeTable(m_edgeArrayTableSize << 1);
		}
	}

	adjTgt->m_id = (adjSrc->m_id = index/*m_edgeIdCount*/ << 1) | 1;
	edge e = OGDF_NEW EdgeElement(v,w,adjSrc,adjTgt,index);
	edges.pushBack(e);

	// notify all registered observers
	for(GraphObserver *obs : m_regStructures)
		obs->edgeAdded(e);

	return adjSrc->m_edge = adjTgt->m_edge = e;
}


edge Graph::newEdge(node v, node w)
{
	OGDF_ASSERT(v != nullptr);
	OGDF_ASSERT(w != nullptr);
	OGDF_ASSERT(v->graphOf() == this);
	OGDF_ASSERT(w->graphOf() == this);

	AdjElement *adjSrc = OGDF_NEW AdjElement(v);

	v->adjEdges.pushBack(adjSrc);
	v->m_outdeg++;

	AdjElement *adjTgt = OGDF_NEW AdjElement(w);

	w->adjEdges.pushBack(adjTgt);
	w->m_indeg++;

	adjSrc->m_twin = adjTgt;
	adjTgt->m_twin = adjSrc;

	edge e = createEdgeElement(v,w,adjSrc,adjTgt);

	return adjSrc->m_edge = adjTgt->m_edge = e;
}


edge Graph::newEdge(adjEntry adjStart, adjEntry adjEnd, Direction dir)
{
	OGDF_ASSERT(adjStart != nullptr);
	OGDF_ASSERT(adjEnd != nullptr)
	OGDF_ASSERT(adjStart->graphOf() == this);
	OGDF_ASSERT(adjEnd->graphOf() == this);

	node v = adjStart->theNode(), w = adjEnd->theNode();

	AdjElement *adjTgt = OGDF_NEW AdjElement(w);
	AdjElement *adjSrc = OGDF_NEW AdjElement(v);

	if(dir == ogdf::after) {
		w->adjEdges.insertAfter(adjTgt,adjEnd);
		v->adjEdges.insertAfter(adjSrc,adjStart);
	} else {
		w->adjEdges.insertBefore(adjTgt,adjEnd);
		v->adjEdges.insertBefore(adjSrc,adjStart);
	}

	w->m_indeg++;
	v->m_outdeg++;

	adjSrc->m_twin = adjTgt;
	adjTgt->m_twin = adjSrc;

	edge e = createEdgeElement(v,w,adjSrc,adjTgt);

	return adjSrc->m_edge = adjTgt->m_edge = e;
}

edge Graph::newEdge(node v, adjEntry adjEnd)
{
	OGDF_ASSERT(v != nullptr);
	OGDF_ASSERT(adjEnd != nullptr)
	OGDF_ASSERT(v->graphOf() == this);
	OGDF_ASSERT(adjEnd->graphOf() == this);

	node w = adjEnd->theNode();

	AdjElement *adjTgt = OGDF_NEW AdjElement(w);

	w->adjEdges.insertAfter(adjTgt,adjEnd);
	w->m_indeg++;

	AdjElement *adjSrc = OGDF_NEW AdjElement(v);

	v->adjEdges.pushBack(adjSrc);
	v->m_outdeg++;

	adjSrc->m_twin = adjTgt;
	adjTgt->m_twin = adjSrc;

	edge e = createEdgeElement(v,w,adjSrc,adjTgt);

	return adjSrc->m_edge = adjTgt->m_edge = e;
}//newedge

//copy of above function with edge ending at v
edge Graph::newEdge(adjEntry adjStart, node v)
{
	OGDF_ASSERT(v != nullptr);
	OGDF_ASSERT(adjStart != nullptr)
	OGDF_ASSERT(v->graphOf() == this);
	OGDF_ASSERT(adjStart->graphOf() == this);

	node w = adjStart->theNode();

	AdjElement *adjSrc = OGDF_NEW AdjElement(w);

	w->adjEdges.insertAfter(adjSrc, adjStart);
	w->m_outdeg++;

	AdjElement *adjTgt = OGDF_NEW AdjElement(v);

	v->adjEdges.pushBack(adjTgt);
	v->m_indeg++;

	adjSrc->m_twin = adjTgt;
	adjTgt->m_twin = adjSrc;

	edge e = createEdgeElement(w,v,adjSrc,adjTgt);

	return adjSrc->m_edge = adjTgt->m_edge = e;
}//newedge


void Graph::move(edge e,
	adjEntry adjSrc,
	Direction dirSrc,
	adjEntry adjTgt,
	Direction dirTgt)
{
	OGDF_ASSERT(e->graphOf() == this);
	OGDF_ASSERT(adjSrc->graphOf() == this);
	OGDF_ASSERT(adjTgt->graphOf() == this);
	OGDF_ASSERT(adjSrc != e->m_adjSrc);
	OGDF_ASSERT(adjSrc != e->m_adjTgt);
	OGDF_ASSERT(adjTgt != e->m_adjSrc);
	OGDF_ASSERT(adjTgt != e->m_adjTgt);

	node v = adjSrc->m_node, w = adjTgt->m_node;
	adjEntry adj1 = e->m_adjSrc, adj2 = e->m_adjTgt;
	e->m_src->adjEdges.move(adj1, v->adjEdges, adjSrc, dirSrc);
	e->m_tgt->adjEdges.move(adj2, w->adjEdges, adjTgt, dirTgt);

	e->m_src->m_outdeg--;
	e->m_tgt->m_indeg--;

	adj1->m_node = e->m_src = v;
	adj2->m_node = e->m_tgt = w;

	v->m_outdeg++;
	w->m_indeg++;
}


void Graph::moveTarget(edge e, node v)
{
	OGDF_ASSERT(e->graphOf() == this);
	OGDF_ASSERT(v->graphOf() == this);

	adjEntry adj = e->m_adjTgt;
	e->m_tgt->adjEdges.move(adj,v->adjEdges);

	e->m_tgt->m_indeg--;
	adj->m_node = e->m_tgt = v;
	v->m_indeg++;
}

void Graph::moveTarget(edge e, adjEntry adjTgt, Direction dir)
{
	node v = adjTgt->theNode();

	OGDF_ASSERT(e->graphOf() == this);
	OGDF_ASSERT(v->graphOf() == this);

	adjEntry adj = e->m_adjTgt;
	e->m_tgt->adjEdges.move(adj, v->adjEdges, adjTgt, dir);

	e->m_tgt->m_indeg--;
	adj->m_node = e->m_tgt = v;
	v->m_indeg++;
}

// By Leipert
void Graph::moveSource(edge e, node v)
{
	OGDF_ASSERT(e->graphOf() == this);
	OGDF_ASSERT(v->graphOf() == this);

	adjEntry adj = e->m_adjSrc;
	e->m_src->adjEdges.move(adj,v->adjEdges);

	e->m_src->m_outdeg--;
	adj->m_node = e->m_src = v;
	v->m_outdeg++;
}

void Graph::moveSource(edge e, adjEntry adjSrc, Direction dir)
{
	node v = adjSrc->theNode();

	OGDF_ASSERT(e->graphOf() == this);
	OGDF_ASSERT(v->graphOf() == this);

	adjEntry adj = e->m_adjSrc;
	e->m_src->adjEdges.move(adj, v->adjEdges, adjSrc, dir);

	e->m_src->m_outdeg--;
	adj->m_node = e->m_src = v;
	v->m_outdeg++;
}

edge Graph::split(edge e)
{
	OGDF_ASSERT(e != nullptr);
	OGDF_ASSERT(e->graphOf() == this);

	node u = newNode();
	u->m_indeg = u->m_outdeg = 1;

	adjEntry adjTgt = OGDF_NEW AdjElement(u);
	adjTgt->m_edge = e;
	adjTgt->m_twin = e->m_adjSrc;
	e->m_adjSrc->m_twin = adjTgt;

	// adapt adjacency entry index to hold invariant
	adjTgt->m_id = e->m_adjTgt->m_id;

	u->adjEdges.pushBack(adjTgt);

	adjEntry adjSrc = OGDF_NEW AdjElement(u);
	adjSrc->m_twin = e->m_adjTgt;
	u->adjEdges.pushBack(adjSrc);

	int oldId = e->m_adjTgt->m_id;
	edge e2 = createEdgeElement(u,e->m_tgt,adjSrc,e->m_adjTgt);
	resetAdjEntryIndex(e->m_adjTgt->m_id,oldId);

	e2->m_adjTgt->m_twin = adjSrc;
	e->m_adjTgt->m_edge = adjSrc->m_edge = e2;

	e->m_tgt = u;
	e->m_adjTgt = adjTgt;
	return e2;
}


void Graph::unsplit(node u)
{
	edge eIn = u->firstAdj()->theEdge();
	edge eOut = u->lastAdj()->theEdge();

	if (eIn->target() != u)
		swap(eIn,eOut);

	unsplit(eIn,eOut);
}



void Graph::unsplit(edge eIn, edge eOut)
{
	node u = eIn->target();

	// u must be a node with exactly one incoming edge eIn and one outgoing
	// edge eOut
	OGDF_ASSERT(u->graphOf() == this);
	OGDF_ASSERT(u->indeg() == 1);
	OGDF_ASSERT(u->outdeg() == 1);
	OGDF_ASSERT(eOut->source() == u);

	// none of them is a self-loop!
	OGDF_ASSERT(!eIn->isSelfLoop());
	OGDF_ASSERT(!eOut->isSelfLoop());

	// we reuse these adjacency entries
	adjEntry adjSrc = eIn ->m_adjSrc;
	adjEntry adjTgt = eOut->m_adjTgt;

	eIn->m_tgt = eOut->m_tgt;

	// adapt adjacency entry index to hold invariant
	resetAdjEntryIndex(eIn->m_adjTgt->m_id,adjTgt->m_id);
	adjTgt->m_id = eIn->m_adjTgt->m_id; // correct id of adjacency entry!

	eIn->m_adjTgt = adjTgt;

	adjSrc->m_twin = adjTgt;
	adjTgt->m_twin = adjSrc;

	adjTgt->m_edge = eIn;

	// notify all registered observers
	for(GraphObserver *obs : m_regStructures)
		obs->edgeDeleted(eOut);
	for(GraphObserver *obs : m_regStructures)
		obs->nodeDeleted(u);

	// remove structures that are no longer used
	edges.del(eOut);
	nodes.del(u);
}


void Graph::delNode(node v)
{
	OGDF_ASSERT(v != nullptr);
	OGDF_ASSERT(v->graphOf() == this);

	// notify all registered observers
	for (GraphObserver *obs : m_regStructures)
		obs->nodeDeleted(v);

	internal::GraphList<AdjElement> &adjEdges = v->adjEdges;
	AdjElement *adj;
	while((adj = adjEdges.head()) != nullptr)
		Graph::delEdge(adj->m_edge);

	nodes.del(v);
}


void Graph::delEdge(edge e)
{
	OGDF_ASSERT(e != nullptr);
	OGDF_ASSERT(e->graphOf() == this);

	// notify all registered observers
	for(GraphObserver *obs : m_regStructures)
		obs->edgeDeleted(e);

	node src = e->m_src, tgt = e->m_tgt;

	src->adjEdges.del(e->m_adjSrc);
	src->m_outdeg--;
	tgt->adjEdges.del(e->m_adjTgt);
	tgt->m_indeg--;

	edges.del(e);
}


void Graph::clear()
{
	// tell all structures to clear their graph-initialized data
	for(GraphObserver *obs : m_regStructures)
		obs->cleared();

	for (node v = nodes.head(); v; v = v->succ()) {
		v->adjEdges.~GraphObjectContainer<AdjElement>();
	}

	nodes.clear();
	edges.clear();

	m_nodeIdCount = m_edgeIdCount = 0;
	m_nodeArrayTableSize = MIN_NODE_TABLE_SIZE;
	reinitArrays();

	OGDF_ASSERT_IF(dlConsistencyChecks, consistencyCheck());
}


void Graph::reverseEdge(edge e)
{
	OGDF_ASSERT(e != nullptr);
	OGDF_ASSERT(e->graphOf() == this)
	node &src = e->m_src, &tgt = e->m_tgt;

	swap(src,tgt);
	swap(e->m_adjSrc,e->m_adjTgt);
	src->m_outdeg++; src->m_indeg--;
	tgt->m_outdeg--; tgt->m_indeg++;
}


void Graph::reverseAllEdges()
{
	for (edge e = edges.head(); e; e = e->succ())
		reverseEdge(e);

	OGDF_ASSERT_IF(dlConsistencyChecks, consistencyCheck());
}


void Graph::reverseAdjEdges()
{
	for(node v : nodes)
		reverseAdjEdges(v);
}


node Graph::chooseNode() const
{
	if (nodes.empty()) return nullptr;
	int k = ogdf::randomNumber(0, numberOfNodes()-1);
	node v = firstNode();
	while(k--) v = v->succ();
	return v;
}


edge Graph::chooseEdge() const
{
	if (edges.size() == 0) return nullptr;
	int k = ogdf::randomNumber(0, numberOfEdges()-1);
	edge e = firstEdge();
	while(k--) e = e->succ();
	return e;
}


edge Graph::searchEdge(node v, node w) const
{
	OGDF_ASSERT(v != nullptr);
	OGDF_ASSERT(v->graphOf() == this)
	OGDF_ASSERT(w != nullptr);
	OGDF_ASSERT(w->graphOf() == this)
	if (w->degree() < v->degree()) {
		swap(v,w);
	}

	for(adjEntry adj : v->adjEdges) {
		if (adj->twinNode() == w) {
			return adj->theEdge();
		}
	}
	return nullptr;
}


void Graph::hideEdge(edge e)
{
	OGDF_ASSERT(e != nullptr);
	OGDF_ASSERT(e->graphOf() == this)

	node src = e->m_src, tgt = e->m_tgt;

	src->adjEdges.delPure(e->m_adjSrc);
	src->m_outdeg--;
	tgt->adjEdges.delPure(e->m_adjTgt);
	tgt->m_indeg--;

	edges.move(e, m_hiddenEdges);
}


void Graph::restoreEdge(edge e)
{
	node v = e->m_src;
	v->adjEdges.pushBack(e->m_adjSrc);
	++v->m_outdeg;

	node w = e->m_tgt;
	w->adjEdges.pushBack(e->m_adjTgt);
	++w->m_indeg;

	m_hiddenEdges.move(e, edges);
}


void Graph::restoreAllEdges()
{
	edge e, ePrev;
	for(e = m_hiddenEdges.tail(); e != nullptr; e = ePrev) {
		ePrev = e->pred();
		restoreEdge(e);
	}
}


int Graph::genus() const
{
	if (empty()) return 0;

	int nIsolated = 0;
	for(node v : nodes)
		if (v->degree() == 0) ++nIsolated;

	NodeArray<int> component(*this);
	int nCC = connectedComponents(*this,component);

	AdjEntryArray<bool> visited(*this,false);
	int nFaceCycles = 0;

	for(node v : nodes) {
		for(adjEntry adj1 : v->adjEdges) {
			if (visited[adj1]) continue;

			adjEntry adj = adj1;
			do {
				visited[adj] = true;
				adj = adj->faceCycleSucc();
			} while (adj != adj1);

			++nFaceCycles;
		}
	}

	return (numberOfEdges() - numberOfNodes() - nIsolated - nFaceCycles + 2*nCC) / 2;
}


ListIterator<NodeArrayBase*> Graph::registerArray(
	NodeArrayBase *pNodeArray) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	return m_regNodeArrays.pushBack(pNodeArray);
}


ListIterator<EdgeArrayBase*> Graph::registerArray(
	EdgeArrayBase *pEdgeArray) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	return m_regEdgeArrays.pushBack(pEdgeArray);
}


ListIterator<AdjEntryArrayBase*> Graph::registerArray(
	AdjEntryArrayBase *pAdjArray) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	return m_regAdjArrays.pushBack(pAdjArray);
}

ListIterator<GraphObserver*> Graph::registerStructure(
	GraphObserver *pStructure) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	return m_regStructures.pushBack(pStructure);
}


void Graph::unregisterArray(ListIterator<NodeArrayBase*> it) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	m_regNodeArrays.del(it);
}


void Graph::unregisterArray(ListIterator<EdgeArrayBase*> it) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	m_regEdgeArrays.del(it);
}


void Graph::unregisterArray(ListIterator<AdjEntryArrayBase*> it) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	m_regAdjArrays.del(it);
}

void Graph::unregisterStructure(ListIterator<GraphObserver*> it) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	m_regStructures.del(it);
}


void Graph::moveRegisterArray(ListIterator<NodeArrayBase*> it, NodeArrayBase *pNodeArray) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	*it = pNodeArray;
}

void Graph::moveRegisterArray(ListIterator<EdgeArrayBase*> it, EdgeArrayBase *pEdgeArray) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	*it = pEdgeArray;
}

void Graph::moveRegisterArray(ListIterator<AdjEntryArrayBase*> it, AdjEntryArrayBase *pAdjArray) const
{
#ifndef OGDF_MEMORY_POOL_NTS
	lock_guard<mutex> guard(m_mutexRegArrays);
#endif
	*it = pAdjArray;
}


void Graph::reinitArrays()
{
	for(NodeArrayBase *nab : m_regNodeArrays)
		nab->reinit(m_nodeArrayTableSize);

	for(EdgeArrayBase *eab : m_regEdgeArrays)
		eab->reinit(m_edgeArrayTableSize);

	for(AdjEntryArrayBase *aab : m_regAdjArrays)
		aab->reinit(m_edgeArrayTableSize << 1);
}


void Graph::reinitStructures()
{
	// is there a challenge?
	for(GraphObserver *obs : m_regStructures)
		obs->reInit();
}


void Graph::resetAdjEntryIndex(int newIndex, int oldIndex)
{
	for(AdjEntryArrayBase *aab : m_regAdjArrays)
		aab->resetIndex(newIndex,oldIndex);
}


int Graph::nextPower2(int start, int idCount)
{
	while (start <= idCount)
		start <<= 1;

	return start;
}


bool Graph::consistencyCheck() const
{
	int n = 0;
	for(node v : nodes) {
#ifdef OGDF_DEBUG
		if (v->graphOf() != this)
			return false;
#endif

		n++;
		int in = 0, out = 0;

		for(adjEntry adj : v->adjEdges) {
			edge e = adj->m_edge;
			if (adj->m_twin->m_edge != e)
				return false;

			if (e->m_adjSrc == adj)
				out++;
			else if (e->m_adjTgt == adj)
				in++;
			else
				return false;

			if (adj->m_node != v)
				return false;

#ifdef OGDF_DEBUG
			if (adj->graphOf() != this)
				return false;
#endif
		}

		if (v->m_indeg != in)
			return false;

		if (v->m_outdeg != out)
			return false;
	}

	if (n != nodes.size())
		return false;

	int m = 0;
	for(edge e : edges) {
#ifdef OGDF_DEBUG
		if (e->graphOf() != this)
			return false;
#endif

		m++;
		if (e->m_adjSrc == e->m_adjTgt)
			return false;

		if (e->m_adjSrc->m_edge != e)
			return false;

		if (e->m_adjTgt->m_edge != e)
			return false;

		if (e->m_adjSrc->m_node != e->m_src)
			return false;

		if (e->m_adjTgt->m_node != e->m_tgt)
			return false;
	}

	if (m != edges.size())
		return false;

	return true;
}


void Graph::resetEdgeIdCount(int maxId)
{
	m_edgeIdCount = maxId+1;

#ifdef OGDF_DEBUG
	if (ogdf::debugLevel >= int(ogdf::dlConsistencyChecks)) {
		for(edge e : edges)
		{
			// if there is an edge with higer index than maxId, we cannot
			// set the edge id count to maxId+1
			if (e->index() > maxId)
				OGDF_ASSERT(false);
		}
	}
#endif
}


node Graph::splitNode(adjEntry adjStartLeft, adjEntry adjStartRight)
{
	OGDF_ASSERT(adjStartLeft != nullptr);
	OGDF_ASSERT(adjStartRight != nullptr);
	OGDF_ASSERT(adjStartLeft->graphOf() == this);
	OGDF_ASSERT(adjStartRight->graphOf() == this);
	OGDF_ASSERT(adjStartLeft->theNode() == adjStartRight->theNode());

	node w = newNode();

	adjEntry adj, adjSucc;
	for(adj = adjStartRight; adj != adjStartLeft; adj = adjSucc) {
		adjSucc = adj->cyclicSucc();
		moveAdj(adj,w);
	}

	newEdge(adjStartLeft, adjStartRight, ogdf::before);

	return w;
}


node Graph::contract(edge e)
{
	adjEntry adjSrc = e->adjSource();
	adjEntry adjTgt = e->adjTarget();
	node v = e->source();
	node w = e->target();

	adjEntry adjNext;
	for (adjEntry adj = adjTgt->cyclicSucc(); adj != adjTgt; adj = adjNext) {
		adjNext = adj->cyclicSucc();
		if (adj->twinNode() == v) {
			continue;
		}

		edge eAdj = adj->theEdge();
		if (w == eAdj->source()) {
			moveSource(eAdj, adjSrc, before);
		} else {
			moveTarget(eAdj, adjSrc, before);
		}
	}

	delNode(adjTgt->theNode());

	return v;
}


void Graph::moveAdj(adjEntry adj, node w)
{
	node v = adj->m_node;

	v->adjEdges.move(adj,w->adjEdges);
	adj->m_node = w;

	edge e = adj->m_edge;
	if(v == e->m_src) {
		--v->m_outdeg;
		e->m_src = w;
		++w->m_outdeg;
	} else {
		--v->m_indeg;
		e->m_tgt = w;
		++w->m_indeg;
	}
}


ostream &operator<<(ostream &os, ogdf::node v)
{
	if (v) os << v->index(); else os << "nil";
	return os;
}

ostream &operator<<(ostream &os, ogdf::edge e)
{
	if (e) os << "(" << e->source() << "," << e->target() << ")";
	else os << "nil";
	return os;
}

ostream &operator<<(ostream &os, ogdf::adjEntry adj)
{
	if (adj) {
		ogdf::edge e = adj->theEdge();
		if (adj == e->adjSource())
			os << e->source() << "->" << e->target();
		else
			os << e->target() << "->" << e->source();
	} else os << "nil";
	return os;
}


Graph::CCsInfo::CCsInfo(const Graph& G)
	: m_graph(&G), m_nodes(G.numberOfNodes()), m_edges(G.numberOfEdges())
{
	NodeArray<int> component(G,-1);

	StackPure<node> S;
	SListPure<int> startNodes;
	SListPure<int> startEdges;
	int nComponent = 0, n = 0, m = 0;

	for(node v : G.nodes) {
		if (component[v] != -1) continue;

		S.push(v);
		component[v] = nComponent;

		while(!S.empty()) {
			node w = S.pop();
			m_nodes[n++] = w;

			for(adjEntry adj : w->adjEdges) {
				if((adj->index() & 1) == 0)
					m_edges[m++] = adj->theEdge();
				node x = adj->twinNode();
				if (component[x] == -1) {
					component[x] = nComponent;
					S.push(x);
				}
			}
		}

		++nComponent;
		startNodes.pushBack(n);
		startEdges.pushBack(m);
	}

	m_startNode.init(nComponent+1);
	m_startNode[0] = 0;

	int i = 1;
	for (int j : startNodes)
		m_startNode[i++] = j;


	m_startEdge.init(nComponent+1);
	m_startEdge[0] = 0;

	i = 1;
	for(int j : startEdges)
		m_startEdge[i++] = j;

	m_numCC = nComponent;
}


} // end namespace ogdf
