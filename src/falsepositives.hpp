/***
 * IndicateFalsePositives checks all possible node errors found in pass 2
 * through analyse_nodes().
 *   - iterates in pass 3 over all relevant ways
 *   - iterates in pass 4 over all areas
 */

#ifndef FALSEPOSITIVES_HPP_
#define FALSEPOSITIVES_HPP_

#include <iostream>
#include <osmium/geom/geos.hpp>
#include <osmium_geos_factory/geos_factory.hpp>
#include <geos/geom/MultiPolygon.h>
#include <geos/geom/Point.h>
#include <osmium/handler.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <geos/index/strtree/STRtree.h>
#include <geos/geom/prep/PreparedPolygon.h>


typedef osmium::handler::NodeLocationsForWays<index_pos_type,
                                              index_neg_type>
        location_handler_type;
typedef geos::geom::prep::PreparedPolygon prepared_polygon_type;

class IndicateFalsePositives: public osmium::handler::Handler {

    DataStorage &ds;
    location_handler_type &location_handler;
    osmium_geos_factory::GEOSFactory<> geos_factory;

    bool is_valid(const osmium::OSMObject& osm_object) {
        return TagCheck::is_way_to_analyse(osm_object);
    }

    bool check_all_nodes(const osmium::Way& way) {
        return TagCheck::is_riverbank_or_coastline(way);
    }

    void errormsg(const osmium::Area &area) {
        cerr << "IndicateFalsePositives: Error at ";
        if (area.from_way())
            cerr << "way: ";
        else
            cerr << "relation: ";
        cerr << area.orig_id() << endl;
    }

    /***
     * Search given node in the error_map. Traced nodes are either flagged as
     * mouth or deleted from the map and inserted as normal node.
     */
    void check_node(const osmium::NodeRef& node) {
        osmium::object_id_type node_id = node.ref();
        auto error_node = ds.error_map.find(node_id);
        if (error_node != ds.error_map.end()) {
            ErrorSum *sum = error_node->second;
            delete_error_node(node_id, sum);
        }
    }

    void delete_error_node(osmium::object_id_type node_id, ErrorSum *sum) {
        if (sum->is_poss_rivermouth()) {
            sum->set_rivermouth();
        } else if (sum->is_poss_outflow()) {
            sum->set_outflow();
        } else {
            sum->set_to_normal();
            ds.insert_node_feature(
                    location_handler.get_node_location(node_id),
                    node_id, sum);
            ds.error_map.erase(node_id);
            delete sum;
        }
    }

public:

    explicit IndicateFalsePositives(DataStorage &data_storage,
            location_handler_type &location_handler) :
            ds(data_storage), location_handler(location_handler) {
    }

    /***
     * Iterate through all nodes of waterways in pass 3 if way is coastline
     * or riverbank. Otherwise iterate just through the nodes between
     * firstnode and lastnode.
     */
    void way(const osmium::Way& way) {
        if (is_valid(way)) {
            if (check_all_nodes(way)) {
                for (auto node : way.nodes()) {
                    check_node(node);
                }
            } else {
                if (way.nodes().size() > 2) {
                    for (auto node = way.nodes().begin() + 1;
                            node != way.nodes().end() - 1; ++node) {
                        check_node(*node);
                    }
                }
            }
        }
    }

    /***
     * Check all waterpolygons in pass 4: Iterate over error map and search
     * the node in the polygon tree by bounding box.
     * If found do geos contains with the polygon to make sure, the node is
     * containing in the polygon.
     * If the poylgon contains the error node the error is detected as a false
     * possitive and is either a normal node or a river mouth.
     */
    void check_area() {
        for (auto node : ds.error_map) {
            osmium::Location location;
            osmium::object_id_type node_id = node.first;
            const geos::geom::Point *point = nullptr;
            try {
                location = location_handler.get_node_location(node_id);
                point = geos_factory.create_point(location).release();
            } catch (...) {
                cerr << "Error at node: " << node_id
                     << " - not able to create point of location." << endl;
                continue;
            }
            vector<void *> results;
            ds.polygon_tree.query(point->getEnvelopeInternal(), results);
            if (results.size()) {
                for (auto result : results) {
                    prepared_polygon_type *geos_polygon;
                    geos_polygon = static_cast<prepared_polygon_type*> (result);
                    if (geos_polygon->contains(point)) {
                        ErrorSum *sum = node.second;
                        delete_error_node(node_id, sum);
                        break;
                    }
                }
            }
            delete point;
        }
    }
};



#endif /* FALSEPOSITIVES_HPP_ */
