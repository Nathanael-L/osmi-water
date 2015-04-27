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
#include <gdal/ogr_api.h>
#include <google/sparse_hash_set>
#include <google/sparse_hash_map>

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

    bool is_valid(const osmium::OSMObject& osm_obj) {
        const char *natural = osm_obj.tags().get_value_by_key("natural");

        if (!analyse_ways) {
            const char *waterway = osm_obj.tags().get_value_by_key("waterway");
            if ((waterway)
                    && ((!strcmp(waterway, "riverbank"))
                            || (!strcmp(waterway, "river"))
                            || (!strcmp(waterway, "drain"))
                            || (!strcmp(waterway, "stream"))
                            || (!strcmp(waterway, "canal"))
                            || (!strcmp(waterway, "ditch")))) {
                return false;
            }
            const char *water = osm_obj.tags().get_value_by_key("water");
            if ((water)
                    && ((!strcmp(water, "riverbank"))
                            || (!strcmp(water, "river"))
                            || (!strcmp(water, "drain"))
                            || (!strcmp(water, "stream"))
                            || (!strcmp(water, "canal"))
                            || (!strcmp(water, "ditch")))) {
                return false;
            }
        }
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }
        if (osm_obj.tags().get_value_by_key("waterway")) {
            return true;
        }
        return false;
    }

    void update_db(const string error_type, const osmium::object_id_type node_id) {
        google::sparse_hash_map<osmium::object_id_type, long> *error_map;
        if (error_type == "direction_error") {
            error_map = &(ds->direction_error_map);
        } else if (error_type == "name_error") {
            error_map = &(ds->name_error_map);
        } else {
            cerr << "run update_db with error_type = 'direction_error' OR 'name_error'" << endl;
            exit(1);
        }
        long error_fid;
        error_fid = error_map->find(node_id)->second;
        char error_advice[30 + error_type.length()];
        sprintf(error_advice, "'%s' in node: %ld", error_type.c_str(), node_id);
        ds->change_bool_feature('n', error_fid, error_type.c_str(), "false", error_advice);
        error_map->erase(node_id);
    }

    void check_node(const osmium::NodeRef *node) {
        osmium::object_id_type node_id = node->ref();
        if (ds->direction_error_map.find(node_id) != ds->direction_error_map.end()) {
            update_db("direction_error", node_id);
        }
        if (ds->name_error_map.find(node_id) != ds->name_error_map.end()) {
            update_db("name_error", node_id);
        }
    }

    void check_area(const osmium::Area& area) {
        osmium::geom::GEOSFactory<> geos_factory;
        geos::geom::MultiPolygon *multipolygon = geos_factory.create_multipolygon(area).release();
        vector<void *> results;
        ds->direction_error_tree.query(multipolygon->getEnvelopeInternal(), results);
        if (results.size()) {
            for (auto result : results) {
                osmium::object_id_type node_id = *(static_cast<osmium::object_id_type*>(result));
                const geos::geom::Point *point;
                point = geos_factory.create_point(location_handler.get_node_location(node_id)).release();
                if (multipolygon->contains(point)) {
                    update_db("direction_error", node_id);
                }
            }
        }
        results.clear();
        ds->name_error_tree.query(multipolygon->getEnvelopeInternal(), results);
        if (results.size()) {
            for (auto result : results) {
                osmium::object_id_type node_id = *(static_cast<osmium::object_id_type*>(result));
                const geos::geom::Point *point;
                point = geos_factory.create_point(location_handler.get_node_location(node_id)).release();
                if (multipolygon->contains(point)) {
                    update_db("name_error", node_id);
                }
            }
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

    void way(const osmium::Way& way) {
        if (is_valid(way) && analyse_ways) {
            for (auto node = way.nodes().begin() + 1; node != way.nodes().end() - 1; ++node) {
                check_node(node);
            }
        }
    }

    void area(const osmium::Area& area) {
        if (is_valid(area) && !analyse_ways) {
            check_area(area);
        }
    }
};

/* ================================================== */

class AreaHandler: public osmium::handler::Handler {

    DataStorage *ds;
    osmium::geom::OGRFactory<> ogr_factory;
    osmium::geom::WKTFactory<> wkt_factory;

    bool is_valid(const osmium::Area& area) {
        const char* natural = area.tags().get_value_by_key("natural");
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }

        if (area.tags().get_value_by_key("waterway")) {
            return true;
        }
        return false;
    }

public:

    AreaHandler(DataStorage *datastorage) :
            ds(datastorage) {
    }

    void area(const osmium::Area& area) {
        if (is_valid(area)) {
            const char *white = "";
            OGRMultiPolygon *geom;
            try {
                geom = ogr_factory.create_multipolygon(area).release();
            } catch (...) {
                cerr << "couldn't create multipolygon" << endl;
            }
            osmium::object_id_type way_id;
            osmium::object_id_type relation_id;
            if (area.from_way()) {
                way_id = area.orig_id();
                relation_id = 0;
            } else {
                way_id = 0;
                relation_id = area.orig_id();
            }
            const char *waterway_type = area.get_value_by_key("waterway");
            if (!waterway_type)
                waterway_type = white;
            const char *name = area.get_value_by_key("name");
            if (!name)
                name = white;
            ds->insert_polygon_feature(geom, way_id, relation_id, waterway_type,
                    name, area.timestamp());
        }
    }
};

/* ================================================== */

class DumpHandler: public osmium::handler::Handler {

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
    location_handler_type location_handler_way(index_pos, index_neg);
    location_handler_way.ignore_errors();
    location_handler_type location_handler_area(index_pos, index_neg);
    location_handler_area.ignore_errors();

    /*osmium::area::Assembler::config_type assembler_config;
     assembler_config.enable_debug_output(debug);*/
    osmium::area::Assembler::config_type assembler_config;
    assembler_config.enable_debug_output(debug);
    WaterwayCollector *waterway_collector = new WaterwayCollector(
            location_handler_way, ds);
    WaterpolygonCollector<osmium::area::Assembler> *waterpolygon_collector =
            new WaterpolygonCollector<osmium::area::Assembler>(assembler_config,
                    ds);

    cerr << "Pass 1...\n";
    osmium::io::Reader reader1(input_filename);
    waterway_collector->read_relations(reader1);
    reader1.close();
    cerr << "Pass 1 done\n";

    cerr << "Pass 2...\n";
    osmium::io::Reader reader2(input_filename);
    DumpHandler dumphandler;
    osmium::apply(reader2, location_handler_way,
            waterway_collector->handler(
                    [&dumphandler](const osmium::memory::Buffer& area_buffer) {
                        osmium::apply(area_buffer, dumphandler);
                    }));
    waterway_collector->analyse_nodes();
    reader2.close();
    cerr << "Pass 2 done\n";

    cerr << "Pass 3...\n";
    osmium::io::Reader reader3(input_filename);
    IndicateFalsePositives indicate_fp(ds, location_handler_way);
    osmium::apply(reader3, indicate_fp);
    reader3.close();
    cerr << "Pass 3 done\n";

    cerr << "Pass 4...\n";
    osmium::io::Reader reader4(input_filename);
    waterpolygon_collector->read_relations(reader4);
    reader4.close();
    cerr << "Pass 4 done\n";

    cerr << "Pass 5...\n";
    osmium::io::Reader reader5(input_filename);
    AreaHandler areahandler(ds);
    ds->init_trees(location_handler_way);
    indicate_fp.analyse_polygons();
    osmium::apply(reader5, location_handler_area, waterpolygon_collector->handler(
                    [&areahandler, &indicate_fp](const osmium::memory::Buffer& area_buffer) {
                        osmium::apply(area_buffer, areahandler, indicate_fp);
                    }));
    reader5.close();
    cerr << "Pass 5 done\n";

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
    cout << "fertig" << endl;

    delete waterway_collector;
    google::protobuf::ShutdownProtobufLibrary();
}
