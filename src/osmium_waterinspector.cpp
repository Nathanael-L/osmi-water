#include <iostream>
#include <getopt.h>
#include <iterator>
#include <vector>

// usually you only need one or two of these
//#include <osmium/index/map/dummy.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>

#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/visitor.hpp>
//#include <osmium/area/multipolygon_collector.hpp>
//#include <osmium/area/assembler.hpp>
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
#include <gdal/ogr_api.h>
#include <google/sparse_hash_set>
#include <google/sparse_hash_map>

#include "osmium_waterdatastorage.hpp"
#include "osmium_waterway.hpp"
#include "osmium_waterpolygon.hpp"

#include <typeinfo>

using namespace std;

typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type, osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location> index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type> location_handler_type;
typedef geos::geom::LineString linestring_type;

/* ================================================== */

class IndicateFalsePositives : public osmium::handler::Handler {

    DataStorage *ds;
    bool analyse_ways = true;

    bool is_valid(const osmium::OSMObject& osm_obj) {
        const char* natural = osm_obj.tags().get_value_by_key("natural");
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }

        if (osm_obj.tags().get_value_by_key("waterway")) {
            return true;
        }
        return false;
    }

    void check_node(const osmium::NodeRef *node) {
        osmium::object_id_type node_id = node->ref();
        long error_fid;
        if (ds->direction_error_map.find(node_id) != ds->direction_error_map.end()) {
            error_fid = ds->direction_error_map.find(node_id)->second;
            char error_advice[39];
            sprintf(error_advice, "'direction_error' in node: %ld", node_id);
            ds->change_bool_feature('n', error_fid, "direction_error", "false", error_advice);
            ds->direction_error_map.erase(node_id);
        }
        if (ds->name_error_map.find(node_id) != ds->name_error_map.end()) {
            error_fid = ds->name_error_map.find(node_id)->second;
            char error_advice[34];
            sprintf(error_advice, "'name_error' in node: %ld", node_id);
            ds->change_bool_feature('n', error_fid, "name_error", "false", error_advice);
            ds->name_error_map.erase(node_id);
        }
    }

public:

    explicit IndicateFalsePositives(DataStorage *datastorage) :
        ds(datastorage)
        {
    }

    void analyse_polygons() {
        analyse_ways = false;
    }

    void way(const osmium::Way& way) {
        if (is_valid(way) && analyse_ways) {
            for (auto node = way.nodes().begin()+1; node != way.nodes().end()-1; ++node) {
                check_node(node);
            }
        }
    }
///bycoordinate
    /*void area(const osmium::Area& area) {
        if (is_valid(area) && !analyse_ways) {
            for (auto& way : area) {
                const osmium::OuterRing& nodelist = static_cast<const osmium::OuterRing&>(way);
                for (auto node : nodelist) {
                    cout << node.positive_ref() << endl;
                }
            }
        }
    }*/
};

/* ================================================== */

class AreaHandler : public osmium::handler::Handler {

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
            if (!waterway_type) waterway_type = white;
            const char *name = area.get_value_by_key("name");
            if (!name) name = white;
            ds->insert_polygon_feature(geom, way_id, relation_id, waterway_type, name, area.timestamp());
        }
    }
};

/* ================================================== */

class DumpHandler : public osmium::handler::Handler {

};

/* ================================================== */
void print_help() {
    cout << "osmium_toogr [OPTIONS] [INFILE [OUTFILE]]\n\n" \
              << "If INFILE is not given stdin is assumed.\n" \
              << "If OUTFILE is not given 'ogr_out' is used.\n" \
              << "\nOptions:\n" \
              << "  -h, --help           This help message\n" \
              << "  -d, --debug          Enable debug output\n" \
              << "  -f, --format=FORMAT  Output OGR format (Default: 'SQLite')\n";
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"help",   no_argument, 0, 'h'},
        {"debug",  no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };

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
        input_filename =  argv[optind];
    } else {
        input_filename = "-";
    }

    ////TO REMOVE
    if (system("rm /tmp/waterways.sqlite")) cerr << "cannot remove file" << endl;
    ////

    DataStorage *ds = new DataStorage();
    index_pos_type index_pos;
    index_neg_type index_neg;
    location_handler_type location_handler(index_pos, index_neg);
    location_handler.ignore_errors();

    /*osmium::area::Assembler::config_type assembler_config;
    assembler_config.enable_debug_output(debug);*/
    osmium::area::Assembler::config_type assembler_config;
    assembler_config.enable_debug_output(debug);
    WaterwayCollector *waterway_collector = new WaterwayCollector(location_handler, ds);
    WaterpolygonCollector<osmium::area::Assembler> *waterpolygon_collector = new WaterpolygonCollector<osmium::area::Assembler>(assembler_config, ds);

    cerr << "Pass 1...\n";
    osmium::io::Reader reader1(input_filename);
    waterway_collector->read_relations(reader1);
    reader1.close();
    cerr << "Pass 1 done\n";

    cerr << "Pass 2...\n";
    osmium::io::Reader reader2(input_filename);
    DumpHandler dumphandler;
    osmium::apply(reader2, location_handler, waterway_collector->handler([&dumphandler](const osmium::memory::Buffer& area_buffer) {
        osmium::apply(area_buffer, dumphandler);
    }));
    waterway_collector->analyse_nodes();
    reader2.close();
    cerr << "Pass 2 done\n";

    cerr << "Pass 3...\n";
    osmium::io::Reader reader3(input_filename);
    IndicateFalsePositives indicate_fp(ds);
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
    indicate_fp.analyse_polygons();
    osmium::apply(reader5, location_handler, waterpolygon_collector->handler([&areahandler, &indicate_fp](const osmium::memory::Buffer& area_buffer) {
        osmium::apply(area_buffer, areahandler, indicate_fp);
    }));
    reader5.close();
    cerr << "Pass 5 done\n";

    vector<const osmium::Relation*> incomplete_relations = waterway_collector->get_incomplete_relations();
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
