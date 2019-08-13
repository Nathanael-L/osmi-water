/***
 * The WaterwayCollector is collecting the waterway relations and ways and
 * insert them into the sqlite tables.
 */

#ifndef WATERWAY_HPP_
#define WATERWAY_HPP_

#include <osmium_geos_factory/geos_factory.hpp>
#include <osmium/relations/relations_manager.hpp>

#include "errorsum.hpp"
#include "tagcheck.hpp"
#include "datastorage.hpp"


typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type,
                                  osmium::Location>
        index_neg_type;
        
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type,
                                           osmium::Location>
        index_pos_type;
        
typedef osmium::handler::NodeLocationsForWays<index_pos_type,
                                              index_neg_type>
        location_handler_type;

typedef geos::geom::LineString linestring_type;

class WaterwayCollector :
        public osmium::relations::RelationsManager<WaterwayCollector,
                                            false, true, false> {

    typedef typename osmium::relations::RelationsManager<WaterwayCollector,
                                              false, true, false>
        collector_type;
    

    //osmium::memory::Buffer m_output_buffer;
    location_handler_type &location_handler;

    static constexpr size_t initial_output_buffer_size = 1024 * 1024;
    static constexpr size_t max_buffer_size_for_flush = 100 * 1024;

    DataStorage &ds;
    osmium_geos_factory::GEOSFactory<> osmium_geos_factory;
    geos::geom::GeometryFactory::unique_ptr geom_factory;

    std::unique_ptr<OGRGeometry> geos2ogr(const geos::geom::Geometry *g)
    {
        OGRGeometry *out;

        geos::io::WKBWriter wkbWriter;
        wkbWriter.setOutputDimension(g->getCoordinateDimension());
        std::ostringstream ss;
        wkbWriter.write(*g, ss);
        std::string wkb = ss.str();
        if (OGRGeometryFactory::createFromWkb((unsigned char *) wkb.c_str(),
                                              nullptr, &out, wkb.size())
            != OGRERR_NONE ) {
            assert(false);
            return std::unique_ptr<OGRGeometry>{};
        }
        return std::unique_ptr<OGRGeometry>{out};
    }

    /***
    * direction error: Nodes where every connected way flows in or out.
    */
    void detect_direction_error(int count_first_node, int count_last_node,
                                 ErrorSum *sum) {
        if ((abs(count_first_node - count_last_node) > 1) 
                && ((count_first_node == 0) || (count_last_node == 0))) {
            sum->set_direction_error();
        }
    }

    /***
    * name error: Nodes, that connect two ways with different names.
    */
    void detect_name_error(std::vector<const char*> &names, ErrorSum *sum) {
        if (names.size() == 2) {
            if (strcmp(names[0],names[1])) {
                sum->set_name_error();
            }
        }
    }

    /***
     * Store the highest category of the ways flow in and of the ways
     * flow out.
     * Ignore out flowing canals and other.
     *
     * type error: A river flows in and a only smaller waterways flow
     * out.
     *
     * If only one way flows in or out, flag node as possibly rivermouth
     * or outflow. If its not, detect spring or end error in pass 3/4.
     * Remember if its river or stream. Ignore other types of waterway.
     */
    void detect_flow_errors(std::vector<char> &category_in,
            std::vector<char> &category_out, ErrorSum *sum) {
        char max_in = 0;
        char max_out = 0;
        if (category_in.size())
            max_in = *max_element(category_in.cbegin(), category_in.cend());
        if (category_out.size())
            max_out = *max_element(category_out.cbegin(),
                                   category_out.cend());

        if ((category_out.size()) && (category_in.size())) {
            if ((max_in == 'C') && (max_out < 'C') && (max_out != '?')) {
                sum->set_type_error();
            }
        } else if (category_in.size() == 1) {
            if (category_in[0] == 'C') {
                sum->set_poss_rivermouth();
                sum->set_river();
            } else if (category_in[0] == 'B') {
                sum->set_poss_rivermouth();
                sum->set_stream();
            }
        } else if (category_out.size() == 1) {
            if (category_out[0] == 'C') {
                sum->set_poss_outflow();
                sum->set_river();
            } else if (category_out[0] == 'B'){
                sum->set_poss_outflow();
                sum->set_stream();
            }
        }
    }

    /***
     * If no possibly error or specific is detected, insert node into
     * table nodes.
     */
    bool handle_node(osmium::object_id_type node_id, ErrorSum *sum) {
        osmium::Location location;
        if (sum->is_normal()) {
            try {
                location = location_handler.get_node_location(node_id);
            } catch (...) {
                std::cerr << "node without location: " << node_id << '\n';
                return false;
            }
            ds.insert_node_feature(location, node_id, sum);
            delete sum;
        } else {
            ds.error_map[node_id] = sum;
        }
        return true;
    }

    /***
     * Insert error node into nodes table: way contains of one
     * coordinate.
     */
    void insert_way_error(const osmium::Way &way) {
        ErrorSum *sum = new ErrorSum();
        sum->set_way_error();
        ds.insert_node_feature(way.nodes().begin()->location(),
                               way.nodes().begin()->ref(), sum);
        delete sum;
    }

    /***
     * Iterate through members. Create linestrings of each. First as GEOS
     * linestring to union them later. Then as ORG linestring to insert
     * them into table ways.
     */
    void create_ways(const osmium::Relation &relation,
                     const osmium::object_id_type relation_id,
                     bool &contains_nowaterway_ways,
                     std::vector<geos::geom::Geometry *> *linestrings) {
        
        for (auto& member : relation.members()) {
            if (member_is_valid(member)) {
                const osmium::Way* way = get_member_way(member.ref());
                if (!way) {
                    continue;
                }
                linestring_type *linestr = nullptr;
                try {
                    linestr = osmium_geos_factory.create_linestring(*way,
                            osmium::geom::use_nodes::unique,
                            osmium::geom::direction::forward).release();
                } catch (osmium::geometry_error&) {
                    insert_way_error(*way);
                    continue;
                } catch (...) {
                    std::cerr << "Error at way: " << way->id() << '\n';
                    std::cerr << "  Unexpected error\n";
                    continue;
                }
                if (linestr) {
                    linestrings->push_back(linestr);
                } else {
                    continue;
                }

                if (TagCheck::has_waterway_tag(*way)) {
                    contains_nowaterway_ways = true;
                }
                std::unique_ptr<OGRGeometry> ogr_linestring = geos2ogr(linestr);

                try {
                    ds.insert_way_feature(move(ogr_linestring), *way, relation_id);
                } catch (osmium::geometry_error&) {
                    std::cerr << "Inserting to table failed for way: "
                         << way->id() << '\n';
                } catch (...) {
                    std::cerr << "Inserting to table failed for way: "
                         << way->id() << '\n';
                    std::cerr << "  Unexpected error\n";
                }
            }
        }
    }

    /***
     * Union linestrings to multilinestring and insert them into table
     * relations.
     */
    void create_relation(const osmium::Relation &relation,
                         const osmium::object_id_type relation_id,
                         bool &contains_nowaterway_ways,
                         std::vector<geos::geom::Geometry *> *linestrings) {

        if (!(linestrings->size())) {
            return;
        }
        geos::geom::MultiLineString *multi_line_string = nullptr;
        try {
            multi_line_string = geom_factory->createMultiLineString(
                              linestrings);
        } catch (...) {
            std::cerr << "Failed to create multilinestring at relation: "
                 << relation_id << '\n';
            delete linestrings;
            return;
        }
        std::unique_ptr<OGRGeometry> ogr_multilinestring;
        try {
            ogr_multilinestring = move(geos2ogr(multi_line_string));
        } catch (...) {
            delete multi_line_string;
            return;
        }
        try {
            ds.insert_relation_feature(std::move(ogr_multilinestring), relation,
                                       contains_nowaterway_ways);
        } catch (osmium::geometry_error&) {
            std::cerr << "Inserting to table failed for relation: "
                 << relation_id << '\n';
        } catch (...) {
            std::cerr << "Inserting to table failed for relation: "
                 << relation_id << '\n';
            std::cerr << "  Unexpected error\n";
        }
        delete multi_line_string;
    }

    void handle_relation(const osmium::Relation& relation) {
        const osmium::object_id_type relation_id = relation.id();
        std::vector<geos::geom::Geometry *> *linestrings;
        linestrings = new std::vector<geos::geom::Geometry *>();
        bool contains_nowaterway_ways = false;
        
        create_ways(relation, relation_id, contains_nowaterway_ways,
                    linestrings);

        create_relation(relation, relation_id, contains_nowaterway_ways,
                         linestrings);
    }

    void create_single_way(const osmium::Way &way) {
        osmium::geom::OGRFactory<> ogr_factory;
        std::unique_ptr<OGRGeometry> linestring;
        try {
            linestring = ogr_factory.create_linestring(way,
                         osmium::geom::use_nodes::unique,
                         osmium::geom::direction::forward);
        } catch (osmium::geometry_error&) {
            insert_way_error(way);
            return;
        } catch (...) {
            std::cerr << "Error at way: " << way.id() << '\n';
            std::cerr << "  Unexpected error\n";
            return;
        }

        try {
            ds.insert_way_feature(std::move(linestring), way, 0);
        } catch (osmium::geometry_error&) {
            std::cerr << "Inserting to table failed for way: "
                 << way.id() << '\n';
        } catch (...) {
            std::cerr << "Inserting to table failed for way: "
                 << way.id() << '\n';
            std::cerr << "  Unexpected error\n";
        }
    }

public:

    explicit WaterwayCollector(location_handler_type &location_handler,
                               DataStorage &data_storage) :
        collector_type(),
        location_handler(location_handler),
        ds(data_storage),
        geom_factory(geos::geom::GeometryFactory::create()) {
    }

    ~WaterwayCollector() {
    }

    bool new_relation(const osmium::Relation& relation) const {
        bool is_relation = true;
        return TagCheck::is_waterway(relation, is_relation);
    }

    bool new_member(const osmium::Relation& /*relation*/, const osmium::RelationMember& member, std::size_t /*n*/) const {
        return member.type() == osmium::item_type::way;
    }

    bool way_is_valid(const osmium::Way& way) {
        bool is_relation = false;
        return TagCheck::is_waterway(way, is_relation);
    }

    bool member_is_valid(const osmium::RelationMember& member) {
        return member.type() == osmium::item_type::way;
    }

    /***
     * For the found relations, insert multilinestings into table relations and
     * linestrings into table ways.
     */
    void complete_relation(const osmium::Relation& relation) {
        handle_relation(relation);
    }

    /***
     * Insert waterways not in any relation into table ways.
     */
    void way_not_in_any_relation(const osmium::Way& way) {
        if (way_is_valid(way)) {
            create_single_way(way);
        }
    }
    
    /***
     * Insert waterways and relations of incomplete relations.
     */
    void ways_in_incomplete_relation() {
        this->for_each_incomplete_relation([&](const osmium::relations::RelationHandle& handle){
            const osmium::Relation& relation = *handle;
            handle_relation(relation);
        });
    }

    /***
     * Iterate over node_map, where first_nodes and last_nodes
     * are mapped with the names and categories of the connected
     * ways to detect errors.
     */
    void analyse_nodes() {
        int count_first_node, count_last_node;
        long fid = 1;
        std::vector<const char*> names;
        std::vector<char> category_in;
        std::vector<char> category_out;
        for (auto node : ds.node_map) {
            ErrorSum *sum = new ErrorSum();
            osmium::object_id_type node_id = node.first;

            count_first_node = 0; count_last_node = 0;
            names.clear(); category_in.clear(); category_out.clear();
            for (auto wway_idx : node.second) {
                DataStorage::WaterWay* wway = &(ds.get_waterway(wway_idx));
                if (wway->first_node == node_id) {
                    count_first_node++;
                    names.push_back(wway->name.c_str());
                    category_out.push_back(wway->category);
                }
                if (wway->last_node == node_id) {
                    count_last_node++;
                    names.push_back(wway->name.c_str());
                    category_in.push_back(wway->category);
                }
            }

            detect_direction_error(count_first_node, count_last_node, sum);
            detect_name_error(names, sum);
            detect_flow_errors(category_in, category_out, sum);

            if (!handle_node(node_id, sum)) {
                continue;
            }
            fid++;
        }
    }
};

#endif /* WATERWAY_HPP_ */
