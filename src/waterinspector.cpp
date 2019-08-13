#include <iostream>
#include <getopt.h>
#include <iterator>
#include <vector>

#include <osmium/index/map/sparse_mem_array.hpp>

#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/visitor.hpp>
#include <osmium/area/multipolygon_manager.hpp>
#include <osmium/area/assembler.hpp>
#include <osmium/geom/factory.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/tags/filter.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/file.hpp>
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

#include "errorsum.hpp"
#include "waterway.hpp"
//#include "waterpolygon.hpp"
#include "tagcheck.hpp"
#include "datastorage.hpp"
#include "falsepositives.hpp"
#include "areahandler.hpp"

typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type,
        osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type,
                                           osmium::Location>
        index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type>
        location_handler_type;
typedef geos::geom::LineString linestring_type;

void print_help() {
    std::cout << "osmi [OPTIONS] INFILE OUTFILE\n\n"
            << "  -h, --help           This help message\n"
            //<< "  -d, --debug          Enable debug output !NOT IN USE\n"
            << std::endl;
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

    std::string input_filename;
    std::string output_filename;
    int remaining_args = argc - optind;
    if ((remaining_args < 2) || (remaining_args > 4)) {
        std::cerr << "Usage: " << argv[0] << " [OPTIONS] INFILE OUTFILE" << '\n';
        std::cerr << remaining_args;
        exit(1);
    } else if (remaining_args == 2) {
        input_filename = argv[optind];
        output_filename = argv[optind + 1];
        std::cout << "in: " << input_filename << " out: " << output_filename << '\n';
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
    assembler_config.debug_level = debug;
    WaterwayCollector waterway_collector(location_handler, ds);
    osmium::area::MultipolygonManager<osmium::area::Assembler> waterpolygon_collector(assembler_config, TagCheck::build_waterpolygon_filter());

    /***
     * Pass 1: waterway_collector and waterpolygon_collector remember the ways
     * according to a relation.
     */
    std::cerr << "Pass 1...\n";
    osmium::relations::read_relations(osmium::io::File(input_filename), waterway_collector, waterpolygon_collector);
    std::cerr << "Pass 1 done\n";;

    /***
     * Pass 2: Collect all waterways in and not in any relation.
     * Insert features to ways and relations table.
     * analyse_nodes is detecting all possibly errors and mouths.
     */
    std::cerr << "Pass 2...\n";
    AreaHandler area_handler(ds);
    osmium::io::Reader reader2(input_filename);
    osmium::apply(reader2, location_handler, waterway_collector.handler(),
                  waterpolygon_collector.handler(
                      [&area_handler]
                      (const osmium::memory::Buffer &area_buffer) {
                          osmium::apply(area_buffer, area_handler);
                  }));
    waterway_collector.ways_in_incomplete_relation();
    waterway_collector.analyse_nodes();
    reader2.close();
    std::cerr << "Pass 2 done\n";

    /***
     * Pass 3: Indicate false positives by comparing the error nodes with the
     * way nodes between the firstnode and the lastnode.
     */
    std::cerr << "Pass 3...\n";
    osmium::io::Reader reader3(input_filename, osmium::osm_entity_bits::way);
    IndicateFalsePositives indicate_false_positives(ds, location_handler);
    osmium::apply(reader3, indicate_false_positives);
    reader3.close();
    area_handler.complete_polygon_tree();
    indicate_false_positives.check_area();
    std::cerr << "Pass 3 done\n";

    /***
     * Insert the error nodes into the nodes table.
     */
    ds.insert_error_nodes(location_handler);

    std::cout << "ready\n";
}
