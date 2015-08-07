/***
 * The WaterwayCollector is collecting the waterway relations and ways and
 * insert them into the sqlite tables.
 */

#ifndef WATERWAY_HPP_
#define WATERWAY_HPP_

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
        public osmium::relations::Collector<WaterwayCollector,
                                            false, true, false> {

    typedef typename osmium::relations::Collector<WaterwayCollector,
                                              false, true, false>
        collector_type;
    

    //osmium::memory::Buffer m_output_buffer;
    location_handler_type &location_handler;

    static constexpr size_t initial_output_buffer_size = 1024 * 1024;
    static constexpr size_t max_buffer_size_for_flush = 100 * 1024;

    DataStorage &ds;
    osmium::geom::GEOSFactory<> osmium_geos_factory;

    OGRGeometry *geos2ogr(const geos::geom::Geometry *g)
    {
        OGRGeometry *out;

        geos::io::WKBWriter wkbWriter;
        wkbWriter.setOutputDimension(g->getCoordinateDimension());
        ostringstream ss;
        wkbWriter.write(*g, ss);
        string wkb = ss.str();
        if (OGRGeometryFactory::createFromWkb((unsigned char *) wkb.c_str(),
                                              nullptr, &out, wkb.size())
            != OGRERR_NONE ) {
            out = nullptr;
            assert(false);
        }
        return out;
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
    void detect_name_error(vector<const char*> &names, ErrorSum *sum) {
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
    void detect_flow_errors(vector<char> &category_in,
                            vector<char> &category_out,
                            ErrorSum *sum) {
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
                cerr << "node without location: " << node_id << endl;
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
                     vector<geos::geom::Geometry *> *linestrings) {
        
        for (auto& member : relation.members()) {
            if (member_is_valid(member)) {
                const osmium::Way& way = way_from(member);
                linestring_type *linestr = nullptr;
                try {
                    linestr = osmium_geos_factory.create_linestring(way,
                            osmium::geom::use_nodes::unique,
                            osmium::geom::direction::forward).release();
                } catch (osmium::geometry_error) {
                    insert_way_error(way);
                    continue;
                } catch (...) {
                    cerr << "Error at way: " << way.id() << endl;
                    cerr << "  Unexpected error" << endl;
                    continue;
                }
                if (linestr) {
                    linestrings->push_back(linestr);
                } else {
                    continue;
                }

                if (TagCheck::has_waterway_tag(way)) {
                    contains_nowaterway_ways = true;
                }

                OGRGeometry *ogr_linestring = nullptr;
                ogr_linestring = geos2ogr(linestr);

                try {
                    ds.insert_way_feature(ogr_linestring, way, relation_id);
                } catch (osmium::geometry_error&) {
                    cerr << "Inserting to table failed for way: "
                         << way.id() << endl;
                } catch (...) {
                    cerr << "Inserting to table failed for way: "
                         << way.id() << endl;;
                    cerr << "  Unexpected error" << endl;
                }
                OGRGeometryFactory::destroyGeometry(ogr_linestring);
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
                         vector<geos::geom::Geometry *> *linestrings) {

        if (!(linestrings->size())) {
            return;
        }
        const geos::geom::GeometryFactory geom_factory =
                geos::geom::GeometryFactory();
        geos::geom::GeometryCollection *geom_collection = nullptr;
        try {
            geom_collection = geom_factory.createGeometryCollection(
                              linestrings);
        } catch (...) {
            cerr << "Failed to create geometry collection at relation: "
                 << relation_id << endl;
            delete linestrings;
            return;
        }
        geos::geom::Geometry *geos_geom = nullptr;
        try {
            geos_geom = geom_collection->Union().release();
        } catch (...) {
            cerr << "Failed to union linestrings at relation: "
                 << relation_id << endl;
            delete geom_collection;
            return;
        }
        OGRGeometry *ogr_multilinestring = nullptr;
        ogr_multilinestring = geos2ogr(geos_geom);
        if (!strcmp(ogr_multilinestring->getGeometryName(),"LINESTRING")) {
            try {
                ogr_multilinestring =
                        OGRGeometryFactory::forceToMultiLineString(
                        ogr_multilinestring);
            } catch (...) {
                delete geom_collection;
                delete geos_geom;
                return;
            }
        }
        try {
            ds.insert_relation_feature(ogr_multilinestring, relation,
                                       contains_nowaterway_ways);
        } catch (osmium::geometry_error&) {
            OGRGeometryFactory::destroyGeometry(ogr_multilinestring);
            cerr << "Inserting to table failed for relation: "
                 << relation_id << endl;
        } catch (...) {
            OGRGeometryFactory::destroyGeometry(ogr_multilinestring);
            cerr << "Inserting to table failed for relation: "
                 << relation_id << endl;
            cerr << "  Unexpected error" << endl;
        }
        delete geom_collection;
        delete geos_geom;
        OGRGeometryFactory::destroyGeometry(ogr_multilinestring);
    }

    void handle_relation(const osmium::relations::RelationMeta& relation_meta) {
        const osmium::Relation& relation = this->get_relation(relation_meta);
        const osmium::object_id_type relation_id = relation.id();
        vector<geos::geom::Geometry *> *linestrings;
        linestrings = new vector<geos::geom::Geometry *>();
        bool contains_nowaterway_ways = false;
        
        create_ways(relation, relation_id, contains_nowaterway_ways,
                    linestrings);

        create_relation(relation, relation_id, contains_nowaterway_ways,
                         linestrings);
    }

    void create_single_way(const osmium::Way &way) {
        osmium::geom::OGRFactory<> ogr_factory;
        OGRLineString *linestring = nullptr;
        try {
            linestring = ogr_factory.create_linestring(way,
                         osmium::geom::use_nodes::unique,
                         osmium::geom::direction::forward).release();
        } catch (osmium::geometry_error) {
            insert_way_error(way);
            return;
        } catch (...) {
            cerr << "Error at way: " << way.id() << endl;
            cerr << "  Unexpected error" << endl;
            return;
        }

        try {
            ds.insert_way_feature(linestring, way, 0);
        } catch (osmium::geometry_error&) {
            cerr << "Inserting to table failed for way: "
                 << way.id() << endl;
        } catch (...) {
            cerr << "Inserting to table failed for way: "
                 << way.id() << endl;
            cerr << "  Unexpected error" << endl;
        }
        delete linestring;
    }

public:

    explicit WaterwayCollector(location_handler_type &location_handler,
                               DataStorage &data_storage) :
        collector_type(),
        location_handler(location_handler),
        ds(data_storage) {
    }

    ~WaterwayCollector() {
    }

    bool keep_relation(const osmium::Relation& relation) const {
        bool is_relation = true;
        return TagCheck::is_waterway(relation, is_relation);
    }

    bool way_is_valid(const osmium::Way& way) {
        bool is_relation = false;
        return TagCheck::is_waterway(way, is_relation);
    }

    bool keep_member(const osmium::relations::RelationMeta&,
                     const osmium::RelationMember& member) const {
        return member.type() == osmium::item_type::way;
    }

    bool member_is_valid(const osmium::RelationMember& member) {
        return member.type() == osmium::item_type::way;
    }

    const osmium::Way& way_from(const osmium::RelationMember& member) {
        size_t temp = this->get_offset(member.type(), member.ref());
        osmium::memory::Buffer& mb = this->members_buffer();
        return mb.get<const osmium::Way>(temp);
    }

    /***
     * For the found relations, insert multilinestings into table relations and
     * linestrings into table ways.
     */
    void complete_relation(const osmium::relations::RelationMeta& relation_meta) {
        handle_relation(relation_meta);
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
        clean_assembled_relations();
        for (auto relation_meta : relations()) {
            handle_relation(relation_meta);
        }
    }

    /***
     * Iterate over node_map, where first_nodes and last_nodes
     * are mapped with the names and categories of the connected
     * ways to detect errors.
     */
    void analyse_nodes() {
        int count_first_node, count_last_node;
        long fid = 1;
        vector<const char*> names;
        vector<char> category_in;
        vector<char> category_out;
        for (auto node : ds.node_map) {
            ErrorSum *sum = new ErrorSum();
            osmium::object_id_type node_id = node.first;

            count_first_node = 0; count_last_node = 0;
            names.clear(); category_in.clear(); category_out.clear();
            for (auto wway : node.second) {
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
