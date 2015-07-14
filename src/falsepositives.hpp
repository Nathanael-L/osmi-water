/***
 * IndicateFalsePositives checks all possible node errors found in pass 2 through analyse_nodes().
 *   - iterates in pass 3 over all relevant ways
 *   - iterates in pass 4 over all areas
 */

#ifndef FALSEPOSITIVES_HPP_

#include <iostream>
#include <osmium/geom/geos.hpp>
#include <geos/geom/MultiPolygon.h>
#include <geos/geom/Point.h>
#include <osmium/handler.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <geos/index/strtree/STRtree.h>

#define FALSEPOSITIVES_HPP_

typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type> location_handler_type;

class IndicateFalsePositives: public osmium::handler::Handler {

    DataStorage &ds;
    location_handler_type &location_handler;
    bool analyse_ways = true;
    osmium::geom::GEOSFactory<> geos_factory;

    /***
     * Is_valid is different if ways or areas are analysed.
     * Ways: has waterway tag or natural=water.
     * Areas: has landuse={reservoir,basin} or natural=water or has waterway
     *        tag but NOT any waterway={river,drain,stream,canal,ditch}
     */
    bool is_valid(const osmium::OSMObject& osm_object) {
        if (!analyse_ways) {
            return TagCheck::is_area_to_analyse(osm_object);
        } else {
            return TagCheck::is_way_to_analyse(osm_object);
        }
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
            if (sum->is_poss_rivermouth()) {
                sum->set_rivermouth();
            } else if (sum->is_poss_outflow()) {
                sum->set_outflow();
            } else {
                sum->set_to_normal();
                ds.insert_node_feature(
                        location_handler.get_node_location(node_id), node_id,
                        sum);
                ds.error_map.erase(node_id);
                delete sum;
            }
        }
    }

    /***
     * Compare given area with the locations in the error_tree. Traced nodes are either flagged as mouth
     * or deleted from the map and inserted as normal node.
     */

    geos::geom::MultiPolygon *create_multipolygon(const osmium::Area& area) {
        geos::geom::MultiPolygon *multipolygon = nullptr;
        try {
            multipolygon = geos_factory.create_multipolygon(area).release();
        } catch (osmium::geometry_error) {
            errormsg(area);
            return nullptr;
        } catch (...) {
            errormsg(area);
            cerr << "Unexpected error" << endl;
            return nullptr;
        }
        return multipolygon;
    } 

    bool polygon_contains_result(void *result, const osmium::Area &area,
                                 geos::geom::MultiPolygon *multipolygon,
                                 osmium::object_id_type &node_id) {
        try {
            node_id = *(static_cast<osmium::object_id_type*>(result));
        } catch (...) {
            return false;
        }
        osmium::Location location;
        const geos::geom::Point *point = nullptr;
        try {
            location = location_handler.get_node_location(node_id);
            point = geos_factory.create_point(location).release();
        } catch (osmium::geometry_error) {
            errormsg(area);
            return false;
        } catch (...) {
            errormsg(area);
            cerr << "Unexpected error" << endl;
            return false;
        }
        { //Benchmark
            t_initgeos.stop();
            t_geoscontains.start();
        }
        try {
            bool contains = multipolygon->contains(point);
            delete point;
            return contains;
        } catch (...) {
            errormsg(area);
            cerr << "and point: " << point->getX() << ","
                    << point->getY() << endl;
            cerr << "GEOS contains error." << endl;
            delete point;
            return false;
        }
        { //Benchmark
            t_geoscontains.stop();
        }
    }
   
    void delete_error(osmium::object_id_type node_id) {
        auto error_node = ds.error_map.find(node_id);
        if (error_node != ds.error_map.end()) {
            ErrorSum *sum = error_node->second;
            if (sum->is_poss_rivermouth()) {
                sum->set_rivermouth();
            } else if (sum->is_poss_outflow()) {
                sum->set_outflow();
            } else {
                if (!(sum->is_normal())) {
                    sum->set_to_normal();
                    osmium::Location location;
                    location = location_handler.get_node_location(node_id);
                    ds.insert_node_feature(location, node_id, sum);
                }
            }
        } else {
            cerr
                    << "Enexpected error: error_tree contains node, "
                    << "but not error_map." << endl;
            exit(1);
        }
    }

    void check_area(const osmium::Area& area) {
        geos::geom::MultiPolygon *multipolygon = nullptr;
        multipolygon = create_multipolygon(area);
        if (!multipolygon) return;
        { //Benchmark
            t_treequery.start();
        }
        vector<void *> results;
        ds.error_tree.query(multipolygon->getEnvelopeInternal(), results);
        { //Benchmark
            t_treequery.stop();
        }
        if (results.size()) {
            for (auto result : results) {
                { //Benchmark
                    t_initgeos.start();
                }
                osmium::object_id_type node_id;
                bool contains = polygon_contains_result(result, area,
                                                        multipolygon, node_id);
                if (!contains) {
                    continue;
                }
                delete_error(node_id);
            }
        }
        delete multipolygon;
    }

public:

    explicit IndicateFalsePositives(DataStorage &data_storage,
            location_handler_type &location_handler) :
            ds(data_storage), location_handler(location_handler) {
    }

    void analyse_polygons() {
        analyse_ways = false;
    }

    /***
     * Iterate through all nodes of waterways in pass 3 if way is coastline or riverbank.
     * Otherwise iterate just through the nodes between firstnode and lastnode.
     */
    void way(const osmium::Way& way) {
        if (is_valid(way) && analyse_ways) {
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
     * Check all waterpolygons in pass 5.
     */
    void area(const osmium::Area& area) {
        if (is_valid(area) && !analyse_ways) {
            check_area(area);
        }
    }
};



#endif /* FALSEPOSITIVES_HPP_ */
