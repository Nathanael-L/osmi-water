typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type, osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location> index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type> location_handler_type;
typedef geos::geom::LineString linestring_type;

/***
 * The WaterwayCollector is collecting the waterway relations and ways and insert them into the
 * sqlite tables.
 */
class WaterwayCollector : public osmium::relations::Collector<WaterwayCollector, false, true, false> {

    typedef typename osmium::relations::Collector<WaterwayCollector, false, true, false> collector_type;

    //osmium::memory::Buffer m_output_buffer;
    location_handler_type &locationhandler;

    static constexpr size_t initial_output_buffer_size = 1024 * 1024;
    static constexpr size_t max_buffer_size_for_flush = 100 * 1024;

    DataStorage *ds;

    OGRGeometry *geos2ogr(const geos::geom::Geometry *g)
    {
        OGRGeometry *out;

        geos::io::WKBWriter wkbWriter;
        wkbWriter.setOutputDimension(g->getCoordinateDimension());
        ostringstream ss;
        wkbWriter.write(*g, ss);
        string wkb = ss.str();
        if (OGRGeometryFactory::createFromWkb((unsigned char *) wkb.c_str(),
                                               NULL, &out, (int) wkb.size()) != OGRERR_NONE )
        {
            out = NULL;
            assert(false);
        }
        return out;
    }

public:

    explicit WaterwayCollector(location_handler_type &location_handler, DataStorage *datastorage) :
    collector_type(),
    //m_output_buffer(initial_output_buffer_size, osmium::memory::Buffer::auto_grow::yes),
    locationhandler(location_handler),
    ds(datastorage) {
    }

    ~WaterwayCollector() {
    }

    bool keep_relation(const osmium::Relation& relation) const {
        const char* type = relation.get_value_by_key("type");
        const char* natural = relation.get_value_by_key("natural");
        const char* waterway = relation.get_value_by_key("waterway");
        if ((type) && ((!strcmp(type, "multipolygon")) || (!strcmp(type, "boundary")))) {
            return false;
        }
        if ((waterway) && (!strcmp(waterway, "riverbank"))) {
            return false;
        }
        if ((natural) && (!strcmp(natural, "coastline"))) {
            return false;
        }
        if ((type) && (!strcmp(type, "waterway"))) {
            return true;
        }
        if (waterway) {
            return true;
        }
        return false;
    }

    bool keep_member(const osmium::relations::RelationMeta& /*relation_meta*/, const osmium::RelationMember& member) const {
        return member.type() == osmium::item_type::way;
    }

    bool member_is_valid(const osmium::RelationMember& member) {
        return member.type() == osmium::item_type::way; //&&
    }

    bool way_is_valid(const osmium::Way& way) {
        const char* type = way.get_value_by_key("type");
        const char *natural = way.tags().get_value_by_key("natural");
        const char *waterway = way.get_value_by_key("waterway");
        if ((type) && ((!strcmp(type, "multipolygon")) || (!strcmp(type, "boundary")))) {
            return false;
        }
        if ((waterway) && (!strcmp(waterway, "riverbank"))) {
            return false;
        }
        if ((natural) && (!strcmp(natural, "coastline"))) {
            return false;
        }
        if (waterway) {
            return true;
        }
        return false;
    }

    const osmium::Way& way_from(const osmium::RelationMember& member) {
        size_t temp = this->get_offset(member.type(), member.ref());
        osmium::memory::Buffer& mb = this->members_buffer();
        return mb.get<const osmium::Way>(temp);
    }

    /***
     * Iterate over node_map, where firstnodes and lastnodes
     * are mapped with the names and categories of the connected
     * ways to detect errors.
     */
    void analyse_nodes() {
        osmium::Location location;
        int count_fn, count_ln;
        long fid = 1;
        vector<const char*> names;
        vector<char> category_in;
        vector<char> category_out;
        for (auto node = ds->node_map.begin(); node != ds->node_map.end(); ++node) {
            ErrorSum *sum = new ErrorSum();
            osmium::object_id_type node_id = node->first;
            location = locationhandler.get_node_location(node_id);
            count_fn = 0; count_ln = 0;
            names.clear(); category_in.clear(); category_out.clear();
            for (auto wway : node->second) {
                if (wway->firstnode == node_id) {
                    count_fn++;
                    names.push_back(wway->name.c_str());
                    category_out.push_back(wway->category);
                }
                if (wway->lastnode == node_id) {
                    count_ln++;
                    names.push_back(wway->name.c_str());
                    category_in.push_back(wway->category);
                }
            }
            /***
             * direction error: Nodes where every connected way flows in or out.
             */
            if ((abs(count_fn-count_ln) > 1) && ((count_fn == 0) || (count_ln == 0))) {
                sum->set_direction_error();
            }
            /***
             * name error: Nodes, that connect two ways with different names.
             */
            if (names.size() == 2) {
                if (strcmp(names[0],names[1])) {
                    sum->set_name_error();
                }
            }
            /***
             * Store the highest category of the ways flow in and of the ways flow out.
             * ignore out flowing canals and other.
             */
            char max_in, max_out;
            if (category_in.size())
                max_in = *max_element(category_in.cbegin(), category_in.cend());
            if (category_out.size())
                max_out = *max_element(category_out.cbegin(), category_out.cend());
            /***
             * type error: A river flows in and a only smaller waterways flow out.
             */
            if ((category_out.size()) && (category_in.size())) {
                if ((max_in == 'C') && (max_out < 'C') && (max_out != '?')) {
                    sum->set_type_error();
                }
            /***
             * If only one way flows in or out, flag node as possibly rivermouth
             * or outflow. If its not, detect spring or end error in pass 3.
             * Remember if its river or stream. Ignore other types of waterway.
             */
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
            /***
             * If no possibly error or specific is detected, insert node into table
             * nodes.
             */
            if (sum->is_normal()) {
                ds->insert_node_feature(location, node_id, sum);
                delete sum;
            } else {
                ds->error_map[node->first] = sum;
            }
            fid++;
        }
    }

    /***
     * For the found relations, insert multilinestings into table relations and
     * linestrings into table ways.
     */
    void complete_relation(osmium::relations::RelationMeta& relation_meta) {
        const osmium::Relation& relation = this->get_relation(relation_meta);
        const osmium::object_id_type rel_id = relation.id();
        osmium::geom::GEOSFactory<> geos_factory;
        vector<geos::geom::Geometry *> *linestrings = new vector<geos::geom::Geometry *>();
        OGRGeometry *ogr_multilinestring;
        OGRSpatialReference srs;
        srs.SetWellKnownGeogCS("WGS84");
        string wkttmp;
        bool contains_nowaterway_ways = false;
        /***
         * Iterate through members. Create linestrings of each. First as GEOS linestring
         * to union them later. Then as ORG linestring to insert them into table ways.
         */
        for (auto& member : relation.members()) {
            if (member_is_valid(member)){
                const osmium::Way& way = way_from(member);
                linestring_type *linestr;
                try {
                    linestr = geos_factory.create_linestring(way, osmium::geom::use_nodes::unique,
                            osmium::geom::direction::forward).release();
                } catch (osmium::geometry_error) {
                    cerr << "Error at way: " << way.id() << "yo" << endl;
                    continue;
                } catch (...) {
                    cerr << "Error at way: " << way.id() << endl;
                    cerr << "  Unexpected error" << endl;
                    continue;
                }
                linestrings->push_back(linestr);
                if (!way.tags().get_value_by_key("waterway"))
                    contains_nowaterway_ways = true;

                OGRGeometry *ogr_linestring;
                ogr_linestring = geos2ogr(linestr);

                try {
                    ds->insert_way_feature(ogr_linestring, way, rel_id);
                } catch (osmium::geometry_error&) {
                    cerr << "Inserting to table failed for way: " << way.id() << endl;
                } catch (...) {
                    cerr << "Inserting to table failed for way: " << way.id() << endl;;
                    cerr << "  Unexpected error" << endl;
                }
                OGRGeometryFactory::destroyGeometry(ogr_linestring);

            }
        }
        /***
         * Union linestrings to multilinestring and insert them into table relations.
         */
        if (!(linestrings->size())) {
            return;
        }
        const geos::geom::GeometryFactory geom_factory = geos::geom::GeometryFactory();
        geos::geom::GeometryCollection *geom_collection;
        try {
            geom_collection = geom_factory.createGeometryCollection(linestrings);
        } catch (...) {
            cerr << "Failed to create geometry collection at relation: " << relation.id() << endl;
            delete linestrings;
            return;
        }
        geos::geom::Geometry *geos_geom;
        try {
            geos_geom = geom_collection->Union().release();
        } catch (...) {
            cerr << "Failed to union linestrings at relation: " << relation.id() << endl;
            delete geom_collection;
            return;
        }
        /*wkttmp = geos_geom->toString();
        char *wkt_linestring = strdup(wkttmp.c_str());
        delete geom_collection;
        delete geos_geom;
        char *wkt_copy = wkt_linestring;
        if (OGRGeometryFactory::createFromWkt(&wkt_copy,&srs,&ogr_multilinestring) != OGRERR_NONE) {
            cerr << "Failed to create multilinestring from wkt.\n";
            free(wkt_linestring);
            return;
        }
        free(wkt_linestring);*/
        ogr_multilinestring = geos2ogr(geos_geom);
        if (!strcmp(ogr_multilinestring->getGeometryName(),"LINESTRING")) {
            ogr_multilinestring = OGRGeometryFactory::forceToMultiLineString(ogr_multilinestring);
        }
        try {
            ds->insert_relation_feature(ogr_multilinestring, relation, contains_nowaterway_ways);
        } catch (osmium::geometry_error&) {
            OGRGeometryFactory::destroyGeometry(ogr_multilinestring);
            cerr << "Inserting to table failed for relation: " << relation.id() << endl;
        } catch (...) {
            OGRGeometryFactory::destroyGeometry(ogr_multilinestring);
            cerr << "Inserting to table failed for relation: " << relation.id() << endl;
            cerr << "  Unexpected error" << endl;
        }
        delete geom_collection;
        delete geos_geom;
        OGRGeometryFactory::destroyGeometry(ogr_multilinestring);
    }

    /***
     * Insert waterways not in any relation into table ways.
     */
    void way_not_in_any_relation(const osmium::Way& way) {
        if (way_is_valid(way)) {
            osmium::geom::OGRFactory<> ogr_factory;
            OGRLineString *linestring;
            try {
                linestring = ogr_factory.create_linestring(way,osmium::geom::use_nodes::unique,osmium::geom::direction::forward).release();
            } catch (osmium::geometry_error) {
                /***
                 * Insert error node into nodes table: way contains of one coordinate.
                 */
                cerr << "Error at way: " << way.id() << endl;
                ErrorSum *sum = new ErrorSum();
                sum->set_way_error();
                ds->insert_node_feature(way.nodes().begin()->location(), way.nodes().begin()->ref(), sum);
                delete sum;
                return;
            } catch (...) {
                cerr << "Error at way: " << way.id() << endl;
                cerr << "  Unexpected error" << endl;
                return;
            }

            try {
                ds->insert_way_feature(linestring, way, 0);
            } catch (osmium::geometry_error&) {
                cerr << "Inserting to table failed for way: " << way.id() << endl;
            } catch (...) {
                cerr << "Inserting to table failed for way: " << way.id() << endl;
                cerr << "  Unexpected error" << endl;
            }
            delete linestring;
        }
    }
};
