#include <iostream>
#include <getopt.h>
#include <iterator>
#include <vector>

// usually you only need one or two of these
//#include <osmium/index/map/dummy.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>

#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/visitor.hpp>
#include <osmium/geom/factory.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/tags/filter.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/relations/collector.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/geom/ogr.hpp>
#include <osmium/geom/geos.hpp>
#include <osmium/geom/wkt.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <geos/geom/GeometryCollection.h>
#include <geos/geom/PrecisionModel.h>
#include <geos/geom/GeometryFactory.h>
#include <geos/index/strtree/STRtree.h>
#include <geos/io/WKBWriter.h>
#include <gdal/ogr_api.h>
#include <google/sparse_hash_set>
#include <google/sparse_hash_map>

#include "osmium_errorsum.hpp"
#include "osmium_waterdatastorage.hpp"
#include "osmium_waterway.hpp"
#include "osmium_waterpolygon.hpp"

#include <typeinfo>

using namespace std;

typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type,
        osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type,
        osmium::Location> index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type> location_handler_type;
typedef geos::geom::LineString linestring_type;

/* ================================================== */

class IndicateFalsePositives: public osmium::handler::Handler {

    DataStorage *ds;
    location_handler_type &location_handler;
    bool analyse_ways = true;

    /***
     * is_valid is differences if ways or areas are analysed.
     * Ways: has waterway tag or natural=water.
     * Areas: has landuse={reservoir,basin} or natural=water or has waterway tag
     *        but NOT any waterway={iriver,drain,stream,canal,ditch}
     *        ?good idea?
     */
    bool is_valid(const osmium::OSMObject& osm_obj) {
        const char *waterway = osm_obj.tags().get_value_by_key("waterway");
        if (!analyse_ways) {
            const char *landuse = osm_obj.tags().get_value_by_key("landuse");
            if ((landuse) && ((!strcmp(landuse, "reservoir")) || (!strcmp(landuse, "basin")))){
                return true;
            }
            if ((waterway)
                    && ((!strcmp(waterway, "river"))
                     || (!strcmp(waterway, "drain"))
                     || (!strcmp(waterway, "stream"))
                     || (!strcmp(waterway, "canal"))
                     || (!strcmp(waterway, "ditch")))) {
                return false;
            }
            const char *water = osm_obj.tags().get_value_by_key("water");
            if ((water)
                    && ((!strcmp(water, "river"))
                     || (!strcmp(water, "drain"))
                     || (!strcmp(water, "stream"))
                     || (!strcmp(water, "canal"))
                     || (!strcmp(water, "ditch")))) {
                return false;
            }
        }

        const char *natural = osm_obj.tags().get_value_by_key("natural");
        if ((waterway) && (!strcmp(waterway, "riverbank"))) {
            return true;
        }
        if ((natural) && (!strcmp(natural, "coastline"))) {
            return true;
        }
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }
        if (osm_obj.tags().get_value_by_key("waterway")) {
            return true;
        }
        return false;
    }

    bool check_all_nodes(const osmium::Way& way) {
        const char *waterway = way.get_value_by_key("waterway");
        const char *natural = way.get_value_by_key("natural");
        if ((waterway) && (!strcmp(waterway, "riverbank"))) {
            return true;
        }
        if ((natural) && (!strcmp(natural, "coastline"))) {
            return true;
        }
        return false;
    }

    void errormsg(const osmium::Area &area) {
        cerr << "IndicateFalsePositives: Error at ";
        if (area.from_way()) cerr << "way: ";
        else cerr << "relation: ";
        cerr << area.orig_id() << endl;
    }

    /***
     * Search given node in the error_map. Traced nodes are either flagged as mouth
     * or deleted from the map and inserted as normal node.
     */
    void check_node(const osmium::NodeRef& node) {
        osmium::object_id_type node_id = node.ref();
        auto error_node = ds->error_map.find(node_id);
        if (error_node != ds->error_map.end()) {
            ErrorSum *sum = error_node->second;
            if (sum->is_poss_rivermouth()) {
                sum->set_rivermouth();
            } else if (sum->is_poss_outflow()) {
                sum->set_outflow();
            } else {
                sum->set_to_normal();
                ds->insert_node_feature(location_handler.get_node_location(node_id), node_id, sum);
                ds->error_map.erase(node_id);
                delete sum;
            }
        }
    }

    /***
     * Compare given area with the locations in the error_tree. Traced nodes are either flagged as mouth
     * or deleted from the map and inserted as normal node.
     */
    void check_area(const osmium::Area& area) {
        osmium::geom::GEOSFactory<> geos_factory;
        geos::geom::MultiPolygon *multipolygon;
        try {
             multipolygon = geos_factory.create_multipolygon(area).release();
        } catch (osmium::geometry_error) {
            errormsg(area);
            return;
        } catch (...) {
            errormsg(area);
            cerr << "Unexpected error" << endl;
            return;
        }
        if (multipolygon) {
            vector<void *> results;
            ds->error_tree.query(multipolygon->getEnvelopeInternal(), results);
            if (results.size()) {
                for (auto result : results) {
                    osmium::object_id_type node_id;
                    try {
                        node_id = *(static_cast<osmium::object_id_type*>(result));
                    } catch (...) {
                        continue;
                    }
                    osmium::Location location;
                    const geos::geom::Point *point;
                    try {
                        location = location_handler.get_node_location(node_id);
                        point = geos_factory.create_point(location).release();

                    } catch (osmium::geometry_error) {
                        errormsg(area);
                        delete multipolygon;
                        return;
                    } catch (...) {
                        errormsg(area);
                        cerr << "Unexpected error" << endl;
                        delete multipolygon;
                        return;
                    }
                    bool contains;
                    try {
                        contains = multipolygon->contains(point);
                    } catch (...) {
                        errormsg(area);
                        cerr << "and point: " << point->getX() << "," << point->getY() << endl;
                        cerr << "OGR contains error." << endl;
                        contains = false;
                    }
                    if (contains) {
                        auto error_node = ds->error_map.find(node_id);
                        if (error_node != ds->error_map.end()) {
                            ErrorSum *sum = error_node->second;
                            if (sum->is_poss_rivermouth()) {
                                sum->set_rivermouth();
                            } else if (sum->is_poss_outflow()) {
                                sum->set_outflow();
                            } else {
                                if (!(sum->is_normal())) {
                                    sum->set_to_normal();
                                    ds->insert_node_feature(location_handler.get_node_location(node_id), node_id, sum);
                                }
                            }
                        } else {
                            cerr << "Enexpected error: error_tree contains node, but not error_map." << endl;
                            exit(1);
                        }
                    }
                    delete point;
                }
            }
            delete multipolygon;
        }
    }

public:

    explicit IndicateFalsePositives(DataStorage *datastorage,
            location_handler_type &locationhandler) :
            ds(datastorage),
            location_handler(locationhandler) {
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
                    for (auto node = way.nodes().begin() + 1; node != way.nodes().end() - 1; ++node) {
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

/* ================================================== */

class AreaHandler: public osmium::handler::Handler {

    DataStorage *ds;
    osmium::geom::WKTFactory<> wkt_factory;

    bool is_valid(const osmium::Area& area) {
        const char* natural = area.tags().get_value_by_key("natural");
        const char *landuse = area.tags().get_value_by_key("landuse");
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }
        if ((landuse) && ((!strcmp(landuse, "reservoir")) || (!strcmp(landuse, "basin")))){
            return true;
        }
        if (area.tags().get_value_by_key("waterway")) {
            return true;
        }
        return false;
    }

    void errormsg(const osmium::Area &area) {
        cerr << "AreaHandler: Error at ";
        if (area.from_way()) cerr << "way: ";
        else cerr << "relation: ";
        cerr << area.orig_id() << endl;
    }

public:

    AreaHandler(DataStorage *datastorage) :
            ds(datastorage) {
    }

    void area(const osmium::Area& area) {
        osmium::geom::OGRFactory<> ogr_factory;
        if (is_valid(area)) {
            OGRMultiPolygon *geom;
            try {
                geom = ogr_factory.create_multipolygon(area).release();
            } catch(osmium::geometry_error) {
                errormsg(area);
            } catch (...) {
                errormsg(area);
                cerr << "Unexpected error" << endl;
            }
            if (geom) {
                ds->insert_polygon_feature(geom, area);
                OGRGeometryFactory::destroyGeometry(geom);
            }
        }

    }
};

/* ================================================== */
void print_help() {
    cout << "osmium_toogr [OPTIONS] [INFILE [OUTFILE]]\n\n"
            << "If INFILE is not given stdin is assumed.\n"
            << "If OUTFILE is not given 'ogr_out' is used.\n" << "\nOptions:\n"
            << "  -h, --help           This help message\n"
            << "  -d, --debug          Enable debug output\n"
            << "  -f, --format=FORMAT  Output OGR format (Default: 'SQLite')\n";
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = { { "help", no_argument, 0, 'h' }, {
            "debug", no_argument, 0, 'd' }, { 0, 0, 0, 0 } };

    bool debug = false;

    while (true) {
        int c = getopt_long(argc, argv, "hd:", long_options, 0);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            print_help();
            exit(0);
        case 'd':
            debug = true;
            break;
        default:
            exit(1);
        }
    }

    string input_filename;
    int remaining_args = argc - optind;
    if (remaining_args > 2) {
        cerr << "Usage: " << argv[0] << " [OPTIONS] [INFILE]" << endl;
        exit(1);
    } else if (remaining_args == 1) {
        input_filename = argv[optind];
    } else {
        input_filename = "-";
    }

    ////TO REMOVE
    if (system("rm /tmp/waterways.sqlite"))
        cerr << "cannot remove file" << endl;
    ////

    DataStorage *ds = new DataStorage();
    index_pos_type index_pos;
    index_neg_type index_neg;
    location_handler_type location_handler(index_pos, index_neg);
    location_handler.ignore_errors();
    //location_handler_type location_handler_area(index_pos, index_neg);
    //location_handler_area.ignore_errors();

    osmium::area::Assembler::config_type assembler_config;
    assembler_config.enable_debug_output(debug);
    WaterwayCollector *waterway_collector = new WaterwayCollector(
            location_handler, ds);
    WaterpolygonCollector<osmium::area::Assembler> *waterpolygon_collector =
            new WaterpolygonCollector<osmium::area::Assembler>(assembler_config,
                    ds);

    /***
     * Pass 1: waterway_collector and waterpolygon_collector remember the ways
     * according to a relation.
     */
    cerr << "Pass 1...\n";
    osmium::io::Reader reader1(input_filename, osmium::osm_entity_bits::relation);
    while (osmium::memory::Buffer buffer = reader1.read()) {
        waterway_collector->read_relations(buffer.begin(), buffer.end());
        waterpolygon_collector->read_relations(buffer.begin(), buffer.end());
    }
    reader1.close();
    cerr << "Pass 1 done\n";

    /***
     * Pass 2: Collect all waterways in and not in any relation.
     * Insert features to ways and relations table.
     * analyse_nodes is detecting all possibly errors and mouths.
     */
    cerr << "Pass 2...\n";
    osmium::io::Reader reader2(input_filename);
    osmium::apply(reader2, location_handler,
            waterway_collector->handler());/*
                    [&dumphandler](const osmium::memory::Buffer& area_buffer) {
                        osmium::apply(area_buffer, dumphandler);
                    }));*/
    waterway_collector->analyse_nodes();
    reader2.close();
    cerr << "Pass 2 done\n";

    /***
     * Pass 3: Indicate false positives by comparing the error nodes with the way nodes
     * between the firstnode and the lastnode.
     */
    cerr << "Pass 3...\n";
    osmium::io::Reader reader3(input_filename, osmium::osm_entity_bits::way);
    IndicateFalsePositives indicate_fp(ds, location_handler);
    osmium::apply(reader3, indicate_fp);
    reader3.close();
    cerr << "Pass 3 done\n";

    /***
     * Pass 4: Collect all waterpolygons in and not in any relation.
     * Insert features to polygons table.
     * Indicate false positives by comparing the geometry of the error nodes and
     * the polygons.
     */
    cerr << "Pass 4...\n";
    osmium::io::Reader reader4(input_filename, osmium::osm_entity_bits::way |
            osmium::osm_entity_bits::relation);
    AreaHandler areahandler(ds);
    ds->init_tree(location_handler);
    indicate_fp.analyse_polygons();
    osmium::apply(reader4, location_handler, waterpolygon_collector->handler(
                    [&areahandler, &indicate_fp](const osmium::memory::Buffer& area_buffer) {
                        osmium::apply(area_buffer, areahandler, indicate_fp);
                    }));
    reader4.close();
    cerr << "Pass 4 done\n";

    /***
     * Insert the error nodes into the nodes table.
     */
    ds->insert_error_nodes(location_handler);

    vector<const osmium::Relation*> incomplete_relations =
            waterway_collector->get_incomplete_relations();
    if (!incomplete_relations.empty()) {
        cerr << "Warning! Some member ways missing for these multipolygon relations:";
        for (const auto* relation : incomplete_relations) {
            cerr << " " << relation->id();
        }
        cerr << "\n";
    }

    delete ds;
    delete waterway_collector;
    delete waterpolygon_collector;
    google::protobuf::ShutdownProtobufLibrary();
    cout << "ready" << endl;
}
