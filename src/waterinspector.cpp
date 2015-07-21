#include <iostream>
#include <getopt.h>
#include <iterator>
#include <vector>

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
#include <google/sparse_hash_set>
#include <google/sparse_hash_map>

/***
 * Benchmark
 */
#include "timer.h"
timer t_total_pass4;
timer t_inittree;
timer t_treequery;
timer t_geoscontains;
timer t_createpolygons;
timer t_indicatefp;

#include "errorsum.hpp"
#include "waterway.hpp"
#include "waterpolygon.hpp"
#include "tagcheck.hpp"
#include "datastorage.hpp"
#include "falsepositives.hpp"
#include "areahandler.hpp"

using namespace std;

typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type,
        osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type,
                                           osmium::Location>
        index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type>
        location_handler_type;
typedef geos::geom::LineString linestring_type;

/***
 * Benchmark
 */
void print_pass4_time() {
    cout << "Pass4 (total):            " << t_total_pass4 << endl;
    cout << "Create polygons:          " << t_createpolygons << endl;
    cout << "   init tree:             " << t_inittree << endl;
    cout << "Indicate false positives: " << t_indicatefp << endl;
    cout << "   tree querry:           " << t_treequery << endl;
    cout << "   geos conatins:         " << t_geoscontains << endl;
}

void print_help() {
    cout << "osmi [OPTIONS] INFILE OUTFILE\n\n"
            << "  -h, --help           This help message\n"
            //<< "  -d, --debug          Enable debug output !NOT IN USE\n"
            << endl;
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
    string output_filename;
    int remaining_args = argc - optind;
    if ((remaining_args < 2) || (remaining_args > 4)) {
        cerr << "Usage: " << argv[0] << " [OPTIONS] INFILE OUTFILE" << endl;
        cerr << remaining_args;
        exit(1);
    } else if (remaining_args == 2) {
        input_filename = argv[optind];
        output_filename = argv[optind + 1];
        cout << "in: " << input_filename << " out: " << output_filename;
    } else {
        input_filename = "-";
    }

    DataStorage ds(output_filename);
    index_pos_type index_pos;
    index_neg_type index_neg;
    location_handler_type location_handler(index_pos, index_neg);
    location_handler.ignore_errors();
    //location_handler_type location_handler_area(index_pos, index_neg);
    //location_handler_area.ignore_errors();

    osmium::area::Assembler::config_type assembler_config;
    assembler_config.enable_debug_output(debug);
    WaterwayCollector waterway_collector(location_handler, ds);
    WaterpolygonCollector<osmium::area::Assembler> 
            waterpolygon_collector(assembler_config);

    /***
     * Pass 1: waterway_collector and waterpolygon_collector remember the ways
     * according to a relation.
     */
    cerr << "Pass 1...\n";
    osmium::io::Reader reader1(input_filename,
            osmium::osm_entity_bits::relation);
    while (osmium::memory::Buffer buffer = reader1.read()) {
        waterway_collector.read_relations(buffer.begin(), buffer.end());
        waterpolygon_collector.read_relations(buffer.begin(), buffer.end());
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
    osmium::apply(reader2, location_handler, waterway_collector.handler());
    waterway_collector.analyse_nodes();
    reader2.close();
    cerr << "Pass 2 done\n";

    /***
     * Pass 3: Indicate false positives by comparing the error nodes with the
     * way nodes between the firstnode and the lastnode.
     */
    cerr << "Pass 3...\n";
    osmium::io::Reader reader3(input_filename, osmium::osm_entity_bits::way);
    IndicateFalsePositives indicate_false_positives(ds, location_handler);
    osmium::apply(reader3, indicate_false_positives);
    reader3.close();
    cerr << "Pass 3 done\n";

    /***
     * Pass 4: Collect all waterpolygons in and not in any relation.
     * Insert features to polygons table.
     * Indicate false positives by comparing the geometry of the error nodes
     * and the polygons.
     */
    cerr << "Pass 4...\n";
    { // Benchmark
        t_total_pass4.start();
        t_createpolygons.start();
    }
    osmium::io::Reader reader4(input_filename,
            osmium::osm_entity_bits::way | osmium::osm_entity_bits::relation);
    AreaHandler area_handler(ds);
    osmium::apply(reader4, location_handler, waterpolygon_collector.handler(
            [&area_handler, &indicate_false_positives]
            (const osmium::memory::Buffer &area_buffer) {
                osmium::apply(area_buffer, area_handler);
            }));
    reader4.close();
    { // Benchmark
        t_createpolygons.stop();
        t_indicatefp.start();
    }
    indicate_false_positives.check_area();
    { // Benchmark
        t_indicatefp.stop();
        t_total_pass4.stop();
        print_pass4_time();
    }
    cerr << "Pass 4 done\n";

    /***
     * Insert the error nodes into the nodes table.
     */
    ds.insert_error_nodes(location_handler);

    vector<const osmium::Relation*> incomplete_relations =
            waterway_collector.get_incomplete_relations();
    if (!incomplete_relations.empty()) {
        cerr
                << "Warning! Some member ways missing for these multipolygon"
                << " relations:";
        for (const auto* relation : incomplete_relations) {
            cerr << " " << relation->id();
        }
        cerr << "\n";
    }

    google::protobuf::ShutdownProtobufLibrary();
    cout << "ready" << endl;
}
