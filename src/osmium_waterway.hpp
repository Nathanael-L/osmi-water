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

public:

    explicit WaterwayCollector(location_handler_type &location_handler, DataStorage *datastorage) :
    collector_type(),
    m_output_buffer(initial_output_buffer_size, osmium::memory::Buffer::auto_grow::yes),
    locationhandler(location_handler),
    ds(datastorage) {
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
        char error_sum = 0;
        vector<const char*> names;
        vector<char> category_in;
        vector<char> category_out;
        for (auto node = ds->node_map.begin(); node != ds->node_map.end(); ++node) {
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
            if ((abs(count_fn-count_ln) > 1) && ((count_fn == 0) || (count_ln == 0))) {
                error_sum += 1;
            } if ()
            if (names.size() == 2) {
                if (strcmp(names[0],names[1])) {
                    error_sum += 2;
                }
            }
            char max_in, max_out;
            if (category_in.size())
                max_in = *max_element(category_in.cbegin(), category_in.cend());
            if (category_out.size())
                max_out = *max_element(category_out.cbegin(), category_out.cend());
            if ((category_out.size()) && (category_in.size())) {
                if ((max_in > max_out) && (max_out > '?')) {
                    error_sum += 4;
                }
            } else if (category_out.size()) {
                if (max_out == 'B') {
                    error_sum += 8;
                } else {
                    error_sum = -8;
                }
            } else if (category_in.size()) {
                if (max_out == 'B') {
                    error_sum += 16;
                } else {
                    delete_node = -4;
                }
            }
            if (!(error_sum)) {
                ds->insert_node_feature(location, node_id, "", false, false, false, false, false);
            } else {
                ds->error_map[node->first] = (int)error_sum;
                error_sum = 0;
            }
            fid++;
        }
    }

    void complete_relation(osmium::relations::RelationMeta& relation_meta) {
        osmium::geom::GEOSFactory<> geos_factory;
        vector<geos::geom::Geometry *> *linestrings = new vector<geos::geom::Geometry *>();
        const osmium::Relation& relation = this->get_relation(relation_meta);
        const osmium::object_id_type rel_id = relation.id();
        bool contains_nowaterway_ways = false;
        OGRGeometry *ogr_multilinestring;
        OGRSpatialReference srs;
        srs.SetWellKnownGeogCS("WGS84");
        string wkttmp;

        for (auto& member : relation.members()) {
            if (member_is_valid(member)){
                const osmium::Way& way = way_from(member);

                //create geos linestring for union to multilinestring
                linestring_type *linestr;
                try {
                    linestr = geos_factory.create_linestring(way,osmium::geom::use_nodes::unique,osmium::geom::direction::forward).release();
                } catch (osmium::geometry_error) {
                    cerr << "Error at way: " << way.id() << endl;
                    continue;
                } catch (...) {
                    cerr << "Error at way: " << way.id() << endl;
                    cerr << "  Unexpected error" << endl;
                    continue;
                }
                linestrings->push_back(linestr);
                if (!way.tags().get_value_by_key("waterway"))
                    contains_nowaterway_ways = true;

                //create ogr linesting for ways-table
                const char *natural = way.tags().get_value_by_key("natural");
                if ((way.tags().get_value_by_key("waterway")) ||
                        ((natural) && (!strcmp(natural, "coastline")))) {
                    OGRGeometry *ogr_linestring;
                    wkttmp = linestr->toString();
                    char *wkt_linestring = strdup(wkttmp.c_str());
                    char *wkt_copy = wkt_linestring;
                    if (OGRGeometryFactory::createFromWkt(&wkt_copy, &srs, &ogr_linestring) != OGRERR_NONE) {
                        cerr << "Failed to create linestring from wkt at way: " << way.id() << endl;
                    }
                    free(wkt_linestring);

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
        }
        if (!(linestrings->size())) return;
        const geos::geom::GeometryFactory geom_factory = geos::geom::GeometryFactory();
        geos::geom::GeometryCollection *geom_collection;
        try {
            geom_collection = geom_factory.createGeometryCollection(linestrings);
        } catch (...) {
            cerr << "Failed to create geometry collection at relation: " << relation.id() << endl;
            return;
        }

        geos::geom::Geometry *geos_geom;
        try {
            geos_geom = geom_collection->Union().release();
        } catch (...) {
            cerr << "Failed to union linestrings at relation: " << relation.id() << endl;
            return;
        }
        wkttmp = geos_geom->toString();
        char *wkt_linestring = strdup(wkttmp.c_str());
        delete geos_geom;
        delete geom_collection;
        char *wkt_copy = wkt_linestring;
        if (OGRGeometryFactory::createFromWkt(&wkt_copy,&srs,&ogr_multilinestring) != OGRERR_NONE) {
            cerr << "Failed to create multilinestring from wkt.\n";
            return;
        }
        free(wkt_linestring);
        if (!strcmp(ogr_multilinestring->getGeometryName(),"LINESTRING")) {
            ogr_multilinestring = OGRGeometryFactory::forceToMultiLineString(ogr_multilinestring);
        }
        try {
            ds->insert_relation_feature(ogr_multilinestring, relation, contains_nowaterway_ways);
        } catch (osmium::geometry_error&) {
            cerr << "Inserting to table failed for relation: " << relation.id() << endl;
        } catch (...) {
            cerr << "Inserting to table failed for relation: " << relation.id() << endl;
            cerr << "  Unexpected error" << endl;
        }

        OGRGeometryFactory::destroyGeometry(ogr_multilinestring);
    }

    void way_not_in_any_relation(const osmium::Way& way) {
        const char *natural = way.tags().get_value_by_key("natural");
        if ((way.tags().get_value_by_key("waterway")) ||
                ((natural) && (!strcmp(natural, "coastline")))) {
            osmium::geom::OGRFactory<> ogr_factory;
            OGRLineString *linestring;
            try {
                linestring = ogr_factory.create_linestring(way,osmium::geom::use_nodes::unique,osmium::geom::direction::forward).release();
            } catch (osmium::geometry_error) {
                cerr << "Error at way: " << way.id() << endl;
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
