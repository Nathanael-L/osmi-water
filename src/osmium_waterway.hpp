typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type, osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location> index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type> location_handler_type;
typedef geos::geom::LineString linestring_type;

class WaterwayCollector : public osmium::relations::Collector<WaterwayCollector, false, true, false> {


    typedef typename osmium::relations::Collector<WaterwayCollector, false, true, false> collector_type;

    osmium::memory::Buffer m_output_buffer;
    location_handler_type &locationhandler;

    static constexpr size_t initial_output_buffer_size = 1024 * 1024;
    static constexpr size_t max_buffer_size_for_flush = 100 * 1024;

    DataStorage *ds;

    /*void flush_output_buffer() {
        if (this->callback()) {
            osmium::memory::Buffer buffer(initial_output_buffer_size);
            swap(buffer, m_output_buffer);
            this->callback()(move(buffer));
        }
    }

    void possibly_flush_output_buffer() {
        if (m_output_buffer.committed() > max_buffer_size_for_flush) {
            flush_output_buffer();
        }
    }*/


public:

    explicit WaterwayCollector(location_handler_type &location_handler, DataStorage *datastorage) :
    locationhandler(location_handler),
    ds(datastorage),
    collector_type(),
        m_output_buffer(initial_output_buffer_size, osmium::memory::Buffer::auto_grow::yes) {
    }

    ~WaterwayCollector() {
    }

    bool keep_relation(const osmium::Relation& relation) const {
        const char* natural = relation.tags().get_value_by_key("natural");
        if ((natural) && (!strcmp(natural,"water"))) {
            return true;
        }
        if (relation.tags().get_value_by_key("waterway")) {
            return true;
        }
        return false;
    }

    bool keep_member(const osmium::relations::RelationMeta& /*relation_meta*/, const osmium::RelationMember& member) const {
        return member.type() == osmium::item_type::way;
    }

    bool member_is_valid(const osmium::RelationMember& member) {
        return member.type() == osmium::item_type::way; //&&
                //way_from(member).tags().get_value_by_key("waterway");
    }

    const osmium::Way& way_from(const osmium::RelationMember& member) {
        size_t temp = this->get_offset(member.type(), member.ref());
        osmium::memory::Buffer& mb = this->members_buffer();
        return mb.get<const osmium::Way>(temp);
    }

    void analyse_nodes() {
        osmium::Location location;
        int count_fn, count_ln;
        long fid = 1;
        bool dir_err, name_err;
        vector<const char*> names;
        vector<string> anomaly;
        for (auto node = ds->node_map.begin(); node != ds->node_map.end(); ++node) {
            location = locationhandler.get_node_location(node->first);
            char node_id[12];
            sprintf(node_id, "%ld", node->first);
            count_fn = 0;
            count_ln = 0;
            dir_err = false;
            name_err = false;
            names.clear();
            for (auto wway : node->second) {
                if (wway->firstnode == node->first) {
                    count_fn++;
                    names.push_back(wway->name.c_str());
                }
                if (wway->lastnode == node->first) {
                    count_ln++;
                    names.push_back(wway->name.c_str());
                }
            }
            if ((abs(count_fn-count_ln) > 1) && ((count_fn == 0) || (count_ln == 0))) {
                dir_err = true;
                ds->direction_error_map[node->first] = fid;
            }
            if (names.size() == 2) {
                if (strcmp(names[0],names[1])) name_err = true;
                ds->name_error_map[node->first] = fid;
            }
            ds->insert_node_feature(location, node_id, dir_err, name_err);
            fid++;
        }
    }

    void complete_relation(osmium::relations::RelationMeta& relation_meta) {
        const char *white = "";
        osmium::geom::GEOSFactory<> geos_factory;
        vector<geos::geom::Geometry *> *linestrings = new vector<geos::geom::Geometry *>();
        const osmium::Relation& relation = this->get_relation(relation_meta);
        const char *waterway_type = relation.get_value_by_key("waterway");
        const char *tag_name = relation.get_value_by_key("name");
        if (!tag_name) tag_name = white;
        const osmium::object_id_type rel_id = relation.id();
        bool contains_nowaterway_ways = false;
        OGRGeometry *ogr_geom;
        OGRSpatialReference srs;
        srs.SetWellKnownGeogCS("WGS84");
        string wkttmp;

        for (auto& member : relation.members()) {
            if (member_is_valid(member)){
                const osmium::Way& way = way_from(member);

                //create geos linestring for union to multilinestring
                linestring_type *linestr;
                linestr = geos_factory.create_linestring(way,osmium::geom::use_nodes::unique,osmium::geom::direction::forward).release();
                linestrings->push_back(linestr);
                if (!way.tags().get_value_by_key("waterway"))
                    contains_nowaterway_ways = true;

                //create ogr linesting for ways-table
                if (way.tags().get_value_by_key("waterway")) {
                    wkttmp = linestr->toString();
                    char *wkt_linestring = strdup(wkttmp.c_str());
                    char *wkt_copy = wkt_linestring;
                    vector<string> anomaly;
                    if (OGRGeometryFactory::createFromWkt(&wkt_copy, &srs, &ogr_geom) != OGRERR_NONE) {
                        cerr << "Failed to create linestring from wkt.\n";
                    }
                    free(wkt_linestring);

                    try {
                        const char *waterway_type = way.get_value_by_key("waterway");
                        if (!waterway_type) waterway_type = white;
                        const char *name = way.get_value_by_key("name");
                        if (!name) name = white;
                        const char *width = (const char*)malloc(sizeof(char)*11);
                        if (way.tags().get_value_by_key("width")) {
                            width = way.get_value_by_key("width");
                        } else if (way.tags().get_value_by_key("est_width")) {
                            width = way.get_value_by_key("est_width");
                        } else {
                            width = white;
                        }

                        ds->insert_way_feature(ogr_geom, way.id(), waterway_type, name,
                                way.nodes().cbegin()->ref(), way.nodes().crbegin()->ref(),
                                rel_id, width, way.timestamp()) ;
                    } catch (osmium::geometry_error&) {
                        cerr << "Inserting to table failed for way: " << way.id() << ".\n";
                    } catch (...) {
                        cerr << "unexpected error" << endl;
                    }
                }
            }
        }
        const geos::geom::GeometryFactory geom_factory = geos::geom::GeometryFactory();
        geos::geom::GeometryCollection *geom_collection = geom_factory.createGeometryCollection(linestrings);

        geos::geom::Geometry *geos_geom;
        geos_geom = geom_collection->Union().release();
        wkttmp = geos_geom->toString();
        char *wkt_linestring = strdup(wkttmp.c_str());
        delete geos_geom;
        delete geom_collection;
        char *wkt_copy = wkt_linestring;
        if (OGRGeometryFactory::createFromWkt(&wkt_copy,&srs,&ogr_geom) != OGRERR_NONE) {
            cerr << "Failed to create multilinestring from wkt.\n";
        }
        free(wkt_linestring);
        if (!strcmp(ogr_geom->getGeometryName(),"LINESTRING")) {
            ogr_geom = OGRGeometryFactory::forceToMultiLineString(ogr_geom);
        }
        try {
            ds->insert_relation_feature(ogr_geom, relation.id(), waterway_type, tag_name, relation.timestamp(), contains_nowaterway_ways);
            //free(waterway_type);
        } catch (osmium::geometry_error&) {
            cerr << "Inserting to table failed for relation: " << relation.id() << ".\n";
        } catch (...) {
            cerr << "unexpected error" << endl;
        }

        OGRGeometryFactory::destroyGeometry(ogr_geom);
    }

    void way_not_in_any_relation(const osmium::Way& way) {
        const char *white = "";
        if (way.tags().get_value_by_key("waterway")) {
            osmium::geom::OGRFactory<> ogr_factory;
            OGRLineString *linestring = ogr_factory.create_linestring(way,osmium::geom::use_nodes::unique,osmium::geom::direction::forward).release();

            try {
                const char *waterway_type = way.get_value_by_key("waterway");
                if (!waterway_type) waterway_type = white;
                const char *name = way.get_value_by_key("name");
                if (!name) name = white;
                const char *width = (const char*)malloc(sizeof(char)*11);
                if (way.tags().get_value_by_key("width")) {
                    width = way.get_value_by_key("width");
                } else if (way.tags().get_value_by_key("est_width")) {
                    width = way.get_value_by_key("est_width");
                } else {
                    width = white;
                }
                ds->insert_way_feature(linestring, way.id(), waterway_type, name,
                        way.nodes().cbegin()->ref(), way.nodes().crbegin()->ref(),
                        0, width, way.timestamp()) ;

            } catch (osmium::geometry_error&) {
                cerr << "Inserting to table failed for way: " << way.id() << ".\n";
            } catch (...) {
                cerr << "unexpected error" << endl;
            }
            delete linestring;
        }
    }
};
