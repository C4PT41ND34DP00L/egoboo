//********************************************************************************************
//*
//*    This file is part of Egoboo.
//*
//*    Egoboo is free software: you can redistribute it and/or modify it
//*    under the terms of the GNU General Public License as published by
//*    the Free Software Foundation, either version 3 of the License, or
//*    (at your option) any later version.
//*
//*    Egoboo is distributed in the hope that it will be useful, but
//*    WITHOUT ANY WARRANTY; without even the implied warranty of
//*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//*    General Public License for more details.
//*
//*    You should have received a copy of the GNU General Public License
//*    along with Egoboo.  If not, see <http://www.gnu.org/licenses/>.
//*
//********************************************************************************************

/// @file egolib/AI/AStar.c
/// @brief
/// @details

#include "egolib/AI/AStar.hpp"

#include "egolib/game/renderer_3d.h" // for point debugging
#include "egolib/Script/script.h"  // for waypoint list control
#include "egolib/game/mesh.h"

AStar::AStar() : 
    final_node(nullptr), 
    start_node(nullptr)
{}

void AStar::reset()
{
    /// @author ZF
    /// @details Reset AStar memory.
    final_node = nullptr;
    start_node = nullptr;
}

/// Functor to determine the distance of point (sourceX, sourceY) to point (targetX, targetY).
struct Distance {
    float operator()(int sourceX, int sourceY, int targetX, int targetY) const {
        int distanceX = targetX - sourceX,
            distanceY = targetY - sourceY;
        float distance = std::sqrt(distanceX * distanceX + distanceY * distanceY);
        return distance;
    }
};

bool AStar::find_path(const std::shared_ptr<const ego_mesh_t>& mesh, uint32_t stoppedby, const int src_ix, const int src_iy, int dst_ix, int dst_iy)
{
    /// @author ZF
    /// @details Explores up to MAX_ASTAR_NODES number of nodes to find a path between the source coordinates and destination coordinates.
    //              The result is stored in a node list and can be accessed through AStar_get_path(). Returns false if no path was found.

    float weight;

    // do not start if the initial point is off the mesh
    if (Index1D::Invalid == mesh->getTileIndex(Index2D(src_ix, src_iy)))
    {
#ifdef DEBUG_ASTAR
        Log::get().debug("AStar failed because source position is off the mesh.\n");
#endif
        return false;
    }

    //Is the destination is inside a wall or outside the map?
    if (mesh->tile_has_bits(Index2D(dst_ix, dst_iy), stoppedby) != 0 || Index1D::Invalid == mesh->getTileIndex(Index2D(dst_ix, dst_iy)))
    {
#ifdef DEBUG_ASTAR
        Log::get().debug("AStar failed because goal position is impassable.\n");
#endif
        return false;
    }


    struct Offset
    {
        Offset(int setX, int setY) : x(setX), y(setY) {}
        int x;
        int y;
    };

    //Explore all nearby nodes, not including diagonal ones
    static const std::array<Offset, 4> EXPLORE_NODES = {
        Offset(-1, 0), Offset(0, -1),
        Offset(1, 0), Offset(0, 1)
    };

    //Node sorting algorithm (lowest weight first)
    auto comparator = [](const std::shared_ptr<Node> &first, const std::shared_ptr<Node> &second){
        return first->weight < second->weight;
    };

    //Set of closed nodes
    std::unordered_set<int> closedNodes;
    std::priority_queue<std::shared_ptr<Node>, 
                        std::vector<std::shared_ptr<Node>>,
                        decltype(comparator)> openNodes(comparator);

    // restart the algorithm
    reset();

    // initialize the starting node
    weight = Distance()(src_ix, src_iy, dst_ix, dst_iy);
    start_node = std::make_shared<AStar::Node>(src_ix, src_iy, weight, nullptr);
    openNodes.push(start_node);

    // do the algorithm
    while (!openNodes.empty())
    {
        // list is completely full... we failed
        if (closedNodes.size() >= MAX_ASTAR_NODES) {
#ifdef DEBUG_ASTAR
            Log::get().debug("AStar failed because maximum number of nodes were explored (%lu)\n", MAX_ASTAR_NODES);
#endif
            break;
        }

        //Get the cheapest open node
        std::shared_ptr<Node> currentNode = openNodes.top();
        openNodes.pop();

        // find some child nodes
        for(const auto& offset: EXPLORE_NODES) {

            // do not check diagonals
            if (offset.x != 0 && offset.y != 0) continue;

            //The node to explore
            int tmp_x = currentNode->ix + offset.x;
            int tmp_y = currentNode->iy + offset.y;

            //Do not explore any node more than once
            if(closedNodes.find(tmp_x | tmp_y << 16) != closedNodes.end()) {
                continue;
            }
            closedNodes.insert(tmp_x | tmp_y << 16);

            // check for the simplest case, is this the destination node?
            if (tmp_x == dst_ix && tmp_y == dst_iy)
            {
                weight = Distance()(tmp_x, tmp_y, currentNode->ix, currentNode->iy);
                final_node = std::make_shared<AStar::Node>(tmp_x, tmp_y, weight, currentNode);
                return true;
            }

            // is the test node on the mesh?
            Index1D itile = mesh->getTileIndex(Index2D(tmp_x, tmp_y));
            if (Index1D::Invalid == itile)
            {
                continue;
            }

            //Dont walk into pits
            //@todo: might need to check tile Z level here instead
            const ego_tile_info_t& ptile = mesh->getTileInfo(itile);
            if (ptile.isFanOff())
            {
                // add the invalid tile to the list as a closed tile
                continue;
            }

            // is this a wall or impassable?
            if (mesh->tile_has_bits(Index2D(tmp_x, tmp_y), stoppedby))
            {
                // add the invalid tile to the list as a closed tile
                continue;
            }

            ///
            /// @todo  I need to check for collisions with static objects, like trees

            // OK. determine the weight (F + H)
            weight = Distance()(tmp_x, tmp_y, currentNode->ix, currentNode->iy)
                   + Distance()(tmp_x, tmp_y, dst_ix, dst_iy);
            openNodes.push(std::make_shared<AStar::Node>(tmp_x, tmp_y, weight, currentNode));
        }
    }

    return false;
}

bool AStar::get_path(const int pos_x, const int dst_y, waypoint_list_t& wplst)
{
    /// @author ZF
    /// @details Fills a waypoint list with sensible waypoints. It will return false if it failed to add at least one waypoint.
    //              The function goes through all the AStar_nodes and finds out which one are critical. A critical node is one that
    //              creates a corner. The function automatically prunes away all non-critical nodes. The final waypoint will always be
    //              the destination coordinates.

    int i;
    size_t path_length, waypoint_num;
    //bool diagonal_movement = false;

    Node *current_node, *last_waypoint, *safe_waypoint;
    Node *node_path[MAX_ASTAR_PATH];

    //Fill the waypoint list as much as we can, the final waypoint will always be the destination waypoint
    waypoint_num = 0;
    current_node = final_node.get();
    last_waypoint = start_node.get();

    //Build the local node path tree
    path_length = 0;
    while (path_length < MAX_ASTAR_PATH && current_node != start_node.get())
    {
        // add the node to the end of the path
        node_path[path_length++] = current_node;

        // get next node
        current_node = current_node->parent.get();
    }

    //Begin at the end of the list, which contains the starting node
    safe_waypoint = nullptr;
    for (i = path_length - 1; i >= 0 && waypoint_num < MAXWAY; i--)
    {
        bool change_direction;

        //get current node
        current_node = node_path[i];

        //the first node should be safe
        if (nullptr == safe_waypoint) safe_waypoint = current_node;

        //is there a change in direction?
        change_direction = (last_waypoint->ix != current_node->ix && last_waypoint->iy != current_node->iy);

        //are we moving diagonally? if so, then don't fill the waypoint list with unessecary waypoints
        /*if( i != 0 )
        {
            if( diagonal_movement ) diagonal_movement = (ABS(current_node->ix - safe_waypoint->ix) == 1)  && (ABS(current_node->iy - safe_waypoint->iy) == 1);
            else                    diagonal_movement = (ABS(current_node->ix - last_waypoint->ix) == 1)  && (ABS(current_node->iy - last_waypoint->iy) == 1);

            if( diagonal_movement )
            {
                safe_waypoint = current_node;
                continue;
            }
        }*/

        //If we have a change in direction, we need to add it as a waypoint, always add the last waypoint
        if (i == 0 || change_direction)
        {
            int way_x;
            int way_y;

            //Special exception for final waypoint, use raw integer
            if (i == 0)
            {
                way_x = pos_x;
                way_y = dst_y;
            }
            else
            {
                // translate to raw coordinates
                way_x = safe_waypoint->ix * Info<int>::Grid::Size() + (Info<int>::Grid::Size() / 2);
                way_y = safe_waypoint->iy * Info<int>::Grid::Size() + (Info<int>::Grid::Size() / 2);
            }

#ifdef DEBUG_ASTAR
            // using >> for division only works if you know for certainty that the value
            // you are shifting is not intended to be neative
            Log::get().debug("Waypoint %lu: X: %d, Y: %d \n", waypoint_num, static_cast<int>(way_x / Info<int>::Grid::Size()), static_cast<int>(way_y / Info<int>::Grid::Size()));
            Renderer3D::pointList.add(Vector3f(way_x, way_y, 100.0f), 800);
            Renderer3D::lineSegmentList.add(
                Vector3f(last_waypoint->ix*Info<float>::Grid::Size() + (Info<int>::Grid::Size() / 2), last_waypoint->iy*Info<float>::Grid::Size() + (Info<int>::Grid::Size() / 2), 200.0f),
                Vector3f(way_x, way_y, 100.0f),
                800
            );
#endif

            // add the node to the waypoint list
            last_waypoint = safe_waypoint;
            waypoint_list_t::push(wplst, way_x, way_y);
            waypoint_num++;

            //This one is now safe
            safe_waypoint = current_node;
        }

        //keep track of the last safe node from our previous waypoint
        else
        {
            safe_waypoint = current_node;
        }
    }

#ifdef DEBUG_ASTAR
    if (waypoint_num > 0) {
        Renderer3D::pointList.add(Vector3f(start_node->ix*Info<float>::Grid::Size() + (Info<int>::Grid::Size() / 2), start_node->iy*Info<float>::Grid::Size() + (Info<int>::Grid::Size() / 2), 100.0f), 800);
    }
#endif

    return waypoint_num > 0;
}

AStar g_astar;
